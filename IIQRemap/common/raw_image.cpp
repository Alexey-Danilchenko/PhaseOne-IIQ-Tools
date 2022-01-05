/*
    raw_image.cpp - control class that handles display of the raw image and
                    selected defects.

    Copyright 2021 Alexey Danilchenko
    Written by Alexey Danilchenko

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3, or (at your option)
    any later version with ADDITION (see below).

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, 51 Franklin Street - Fifth Floor, Boston,
    MA 02110-1301, USA.
*/
#define NOMINMAX

#include "raw_image.h"

#include <QPainter>
#include <QPaintEvent>

#include <string.h>
#include <math.h>

#include <tbb/tbb.h>

// --------------------------------------------------------
//    static data
// --------------------------------------------------------
// array for compressing 16 to 8 bit
uint8_t from12To8[TOTAL_RAW_VALUES];

// --------------------------------------------------------
//    helper functions
// --------------------------------------------------------
#define SQR(x) ((x)*(x))
// gamma 2.2 lower region calculation variables
static double g[5] = {1.0/2.2, 0, 0, 0, 0};
static double bnd[2] = {0,0};

static void initStaticData()
{
    static bool needInit = true;

    if (needInit)
    {
        // init gamma data
        bnd[g[1] >= 1] = 1;
        if (g[1] && (g[1]-1)*(g[0]-1) <= 0)
        {
            for (int i=0; i < 48; i++)
            {
                g[2] = (bnd[0] + bnd[1])/2;
                if (g[0])
                    bnd[(pow(g[2]/g[1],-g[0]) - 1)/g[0] - 1/g[2] > -1] = g[2];
                else
                    bnd[g[2]/exp(1-1/g[2]) < g[1]] = g[2];
            }
            g[3] = g[2] / g[1];
            if (g[0])
                g[4] = g[2] * (1/g[0] - 1);
        }

        for (int i=0; i<TOTAL_RAW_VALUES; ++i)
        {
            from12To8[i] = roundToInt((double)i*255/(MAX_RAW_VALUE));
        }

        needInit = false;
    }
}

// Contrast curve
//
// Taken from exploits of Guillermo Luijk and Emil (ejmartin) from
// http://www.luminous-landscape.com/forum/index.php?topic=52364.msg430767
//
// The curve is quite tunable and uses a number of parameters
//    f(x) = ((1-s)*x + s*xA*(x/xA)^V)^(log(yA)/log(xA))
//
// Parameters:
//     (xA,yA): turning point
//     s: slope at ends
//     V: contrast strength (1-linear, >1 increasing)
//
// The curve is defined as follows on [0..1] interval
//  y = f(x)       for [0..xA)
//  y = 1 - f(1-x) for [xA..1]
//
inline double f_CC(double x, double s, double V, double xA, double yA)
{
    //  return (1/(1+exp(V*(xA-x)))-1/(1+exp(V)))/(1/(1+exp(V*(xA-1)))-1/(1+exp(V*xA)));
    return pow((1-s)*x + s*xA*pow(x/xA,V), log(yA)/log(xA));
}

// Adjustments calculation for a single point -
// applies black level, gamma, exposure, contrast
inline uint16_t adjustSinglePoint(uint16_t value,
                                  uint16_t blackLevel,
                                  double exposure,
                                  double contrast,
                                  double midpoint,
                                  bool applyGamma,
                                  bool blackLevelsZeroed)
{
    const double s = 0.5;  // larger - more contrast slope

    if (value<=blackLevel)
        return 0;

    double val = blackLevelsZeroed
                                ? double(value)/MAX_RAW_VALUE
                                : double(value-blackLevel)/MAX_RAW_VALUE;

    if (val<1)
    {
		val *= exposure;
        if (val>1)
            val = 1;
        if (val < midpoint)
            val = f_CC(val,s,contrast,midpoint,midpoint);
        else
            val = 1- f_CC(1-val,s,contrast,1-midpoint,1-midpoint);

		if (applyGamma)
            val = val < g[3]
                        ? val*g[1]
                        : (g[0]
                            ? pow(val,g[0])*(1+g[4])-g[4]
                            : log(val)*g[2]+1);
    }
    else
        val = 1;

    return uint16_t(MAX_RAW_VALUE * val);
}

inline void extractChannel(EChannel ch,
                           uint16_t *chValues,
                           IIQFile& raw,
                           uint16_t row,
                           uint16_t col,
                           uint16_t blockSize)
{
    uint16_t lastRow = row+blockSize;
    uint16_t lastCol = col+blockSize;
    for (uint16_t rw=row; rw<lastRow; ++rw)
        for (uint16_t cl=col; cl<lastCol; ++cl)
            if (raw.FC(rw, cl) == ch)
                *chValues++ = raw.getRAW(rw, cl);
}

// --------------------------------------------------------
//    IIQRawImage class
// --------------------------------------------------------
IIQRawImage::IIQRawImage(QWidget *parent)
    : QLabel(parent),
      enableCols_(true), enablePoints_(true),
      defPointsCount_(0), defColsCount_(0),
      applyDefectCorr_(false),
      curDefSetMode_(M_NONE),
	  width_(0), height_(0), topMargin_(0), leftMargin_(0),
      curSensorPlus_(false),
      scale_(1), pX(0), pY(0),
      pauseUpdates_(false), rawData8_(0),
      renderingType_(R_RGB), applyGamma_(true),
      blackLevelsZeroed_(true),
      contrast_(0),
      contrMidpoint_(0.5)
{
    initStaticData();
    chnlEnabled[C_GREEN]  = true;
    chnlEnabled[C_RED]    = true;
    chnlEnabled[C_BLUE]   = true;
    chnlEnabled[C_GREEN2] = true;

    resetAllCorrections();

    setText(tr("Open RAW file with 'Load RAW(s)...'"));
    setAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
}

IIQRawImage::~IIQRawImage()
{
    delete[] rawData8_;
    rawData8_ = 0;
}

void IIQRawImage::generateCurves(EChannel channel)
{
    if (channel == C_ALL)
    {
        double contrast = contrast_*10+1;
        double exposure[4];
        exposure[C_RED]    = exposure_[C_ALL]*exposure_[C_RED];
        exposure[C_GREEN]  = exposure_[C_ALL]*exposure_[C_GREEN];
        exposure[C_BLUE]   = exposure_[C_ALL]*exposure_[C_BLUE];
        exposure[C_GREEN2] = exposure_[C_ALL]*exposure_[C_GREEN2];

        tbb::parallel_for(size_t(0), size_t(TOTAL_RAW_VALUES),
        [&](size_t i)
        {
            chnlCurves_[C_RED][i] = adjustSinglePoint(
                                             i,
                                             blckLevels_[C_RED],
                                             exposure[C_RED],
                                             contrast,
                                             contrMidpoint_,
                                             applyGamma_,
                                             blackLevelsZeroed_);
            chnlCurves_[C_GREEN][i] = adjustSinglePoint(
                                             i,
                                             blckLevels_[C_GREEN],
                                             exposure[C_GREEN],
                                             contrast,
                                             contrMidpoint_,
                                             applyGamma_,
                                             blackLevelsZeroed_);
            chnlCurves_[C_BLUE][i] = adjustSinglePoint(
                                             i,
                                             blckLevels_[C_BLUE],
                                             exposure[C_BLUE],
                                             contrast,
                                             contrMidpoint_,
                                             applyGamma_,
                                             blackLevelsZeroed_);
            chnlCurves_[C_GREEN2][i] = adjustSinglePoint(
                                             i,
                                             blckLevels_[C_GREEN2],
                                             exposure[C_GREEN2],
                                             contrast,
                                             contrMidpoint_,
                                             applyGamma_,
                                             blackLevelsZeroed_);
        });
    }
    else
    {
        uint16_t *chCurve = chnlCurves_[channel];
        double contrast = contrast_*10+1;
        double exposure = exposure_[C_ALL]*exposure_[channel];

        tbb::parallel_for(size_t(0), size_t(TOTAL_RAW_VALUES),
        [&](size_t i)
        {
            chCurve[i] = adjustSinglePoint(i,
                                           blckLevels_[channel],
                                           exposure,
                                           contrast,
                                           contrMidpoint_,
                                           applyGamma_,
                                           blackLevelsZeroed_);
        });
    }
}

QSize IIQRawImage::sizeHint() const
{
    return QSize(width_*scale_, height_*scale_);
}

void IIQRawImage::paintEvent(QPaintEvent *p)
{
    if (!iiqFile_[curSensorPlus_] && !calFile_.valid(curSensorPlus_))
        QLabel::paintEvent(p);
    else
    {
        QPainter painter(this);
        QPen pen(defColour_);

        // set this hint otherwise scaling down raw with only
        // a few channels selected does not work properly
        if (scale_<1.0)
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        painter.scale(scale_, scale_);

        QRect exposedRect = p->rect();
        QRect imageRect = exposedRect;

        // calculate pont offsets
        calcViewpointOffsets();

        // adjust for cases where image is "fit to window"
        exposedRect.adjust(pX,pY,pX,pY);

        // the adjust is to account for half points along edges
        exposedRect = painter.worldTransform().inverted().mapRect(exposedRect).adjusted(-1, -1, 1, 1);
        imageRect   = painter.worldTransform().inverted().mapRect(imageRect).adjusted(-1, -1, 1, 1);

        if (iiqFile_[curSensorPlus_])
            painter.drawPixmap(exposedRect, rawPixmap_, imageRect);

        if (calFile_.valid(curSensorPlus_))
        {
            painter.setPen(pen);
            if (iiqFile_[curSensorPlus_])
                painter.setBackgroundMode(Qt::TransparentMode);
            else
            {
                painter.setBackgroundMode(Qt::OpaqueMode);
                QBrush bgrBrush(Qt::black);
                painter.setBackground(bgrBrush);

            }
            painter.drawPixmap(exposedRect, defBitmap_, imageRect);
        }
    }
}

void IIQRawImage::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);

    // calculate point offsets
    calcViewpointOffsets();
}

void IIQRawImage::mouseMoveEvent(QMouseEvent * e)
{
    if (iiqFile_[curSensorPlus_])
    {
        uint16_t col = uint16_t(double(e->x()-pX)/scale_);
        uint16_t row = uint16_t(double(e->y()-pY)/scale_);

        if (col<width_ && row<height_)
            Q_EMIT imageCursorPosUpdated(row, col);
    }
}

void IIQRawImage::mousePressEvent(QMouseEvent * e)
{
    if (calFile_.valid(curSensorPlus_) && curDefSetMode_ != M_NONE)
    {
        uint16_t col = uint16_t(double(e->x()-pX)/scale_);
        uint16_t row = uint16_t(double(e->y()-pY)/scale_);

        if (col>=width_ || row>=height_)
            return;

        row += topMargin_;
        col += leftMargin_;

        bool updated = false;

        if (enablePoints_ && curDefSetMode_==M_POINT)
        {
            updated = calFile_.isDefPixel(col, row, curSensorPlus_)
                        ? calFile_.removeDefPixel(col, row, curSensorPlus_)
                        : calFile_.addDefPixel(col, row, curSensorPlus_);
        }
        else if (enableCols_ && curDefSetMode_==M_COL)
        {
            updated = calFile_.isDefCol(col, curSensorPlus_)
                        ? calFile_.removeDefCol(col, curSensorPlus_)
                        : calFile_.addDefCol(col, curSensorPlus_);
        }

        if (updated)
        {
            updateDefects();
            Q_EMIT defectsChanged();
        }
    }
}

void IIQRawImage::setScale(double scale)
{
    if (scale)
    {
        scale_ = scale;
        adjustSize();
        if (iiqFile_[curSensorPlus_] || calFile_.valid(curSensorPlus_))
            repaint();
    }
}

void IIQRawImage::setDefectSettingMode(EDefectMode mode)
{
    if (calFile_.valid(curSensorPlus_))
    {
        curDefSetMode_ = mode;

        if (curDefSetMode_==M_NONE)
            setCursor(Qt::ArrowCursor);
        else
            setCursor(Qt::CrossCursor);
    }
    else
        setCursor(Qt::ArrowCursor);
}

void IIQRawImage::setRawRenderingType(ERawRendering renderingType)
{
    if (renderingType_ != renderingType)
    {
        renderingType_ = renderingType;
        updateRaw();
        repaint();
    }
}

void IIQRawImage::enableGammaCorrection(bool enable)
{
    if (applyGamma_ != enable)
    {
        applyGamma_ = enable;
        generateCurves();
        updateRaw();
        repaint();
    }
}

void IIQRawImage::enableBlackLevelZeroed(bool enable)
{
    if (blackLevelsZeroed_ != enable)
    {
        blackLevelsZeroed_ = enable;
        generateCurves();
        updateRaw();
        repaint();
    }
}

// reset for exposure and contrast corrections
void IIQRawImage::resetAllCorrections()
{
    exposure_[C_ALL] = 1;
    exposure_[C_GREEN] = 1;
    exposure_[C_RED] = 1;
    exposure_[C_BLUE] = 1;
    exposure_[C_GREEN2] = 1;
    blckLevels_[C_GREEN] = 0;
    blckLevels_[C_RED] = 0;
    blckLevels_[C_BLUE] = 0;
    blckLevels_[C_GREEN2] = 0;
    contrast_ = 0;
    contrMidpoint_ = 0.5;

    generateCurves();
    updateRaw();
    repaint();
}

// exposure corrections
void IIQRawImage::setExpCorr(double expCorr, EChannel channel)
{
    exposure_[channel] = expCorr;
    generateCurves(channel);
    updateRaw();
    repaint();
}

// contrast corrections
void IIQRawImage::setContrCorr(double contrast)
{
    contrast_ = contrast;
    generateCurves();
    updateRaw();
    repaint();
}

void IIQRawImage::setContrMidpoint(double ctrsMidpoint)
{
    contrMidpoint_ = ctrsMidpoint;
    generateCurves();
    updateRaw();
    repaint();
}

void IIQRawImage::setBlack(int blackLevel, EChannel channel)
{
    if (blackLevel!=blckLevels_[channel] && blackLevel<TOTAL_RAW_VALUES)
    {
        blckLevels_[channel] = (uint16_t)blackLevel;

        generateCurves(channel);
        updateRaw();
        repaint();
    }
}

void IIQRawImage::setWB(double* wb)
{
    exposure_[C_RED] = wb[C_RED];
    exposure_[C_GREEN] = wb[C_GREEN];
    exposure_[C_BLUE] = wb[C_BLUE];
    exposure_[C_GREEN2] = wb[C_GREEN2];

    generateCurves();
    updateRaw();
    repaint();
}

// enable/disable channels
void IIQRawImage::enableChannel(bool enable, EChannel channel)
{
    if (channel!=C_ALL)
    {
        chnlEnabled[channel] = enable;

        updateRaw();
        repaint();
    }
}

void IIQRawImage::pauseUpdates(bool pauseUpdates)
{
    pauseUpdates_ = pauseUpdates;
    if (!pauseUpdates_)
    {
        updateRaw();
        repaint();
    }
}

void IIQRawImage::setSensorPlus(bool sensorPlus, double scale)
{
    if (!iiqFile_[sensorPlus])
        return;

    curSensorPlus_ = sensorPlus;

    width_ = iiqFile_[curSensorPlus_]->imgdata.sizes.width;
    leftMargin_ = iiqFile_[curSensorPlus_]->imgdata.sizes.left_margin;

    height_ = iiqFile_[curSensorPlus_]->imgdata.sizes.height;
    topMargin_ = iiqFile_[curSensorPlus_]->imgdata.sizes.top_margin;

    // setup bitmap
    defBitmap_ = QBitmap(width_, height_);

    iiqFile_[curSensorPlus_]->applyPhaseOneCorr(calFile_, curSensorPlus_, applyDefectCorr_);

    delete[] rawData8_;
    rawData8_ = 0;

    rawData8_ = new uint8_t[height_*width_*3];

    // copy the raw data
    updateRaw();

    if (scale != 0)
        scale_ = scale;

    if (calFile_.valid(curSensorPlus_))
        updateDefects();

    adjustSize();
    repaint();

}

void IIQRawImage::setRawImage(std::unique_ptr<IIQFile>& iiqFile, double scale)
{
    bool sensorPlus = iiqFile->isSensorPlus();
    iiqFile_[sensorPlus] = std::move(iiqFile);
    if (!calFile_.valid() || calFile_.getCalSerial() != iiqFile_[sensorPlus]->getPhaseOneSerial())
    {
        calFile_ = iiqFile_[sensorPlus]->getIIQCalFile();
        if (iiqFile_[!sensorPlus] &&
            iiqFile_[!sensorPlus]->getPhaseOneSerial() != calFile_.getCalSerial())
        {
            iiqFile_[!sensorPlus].reset();
        }
    }
    else if (auto loadedCal = iiqFile_[sensorPlus]->getIIQCalFile(); calFile_.mergable(loadedCal))
    {
        calFile_.merge(loadedCal);
    }

    iiqFile_[sensorPlus]->applyPhaseOneCorr(calFile_, sensorPlus, applyDefectCorr_);

    setSensorPlus(sensorPlus, scale);
}

void IIQRawImage::setDefectCorr(bool applyDefectCorr)
{
    if (calFile_.valid(curSensorPlus_) && applyDefectCorr_ != applyDefectCorr)
    {
        applyDefectCorr_ = applyDefectCorr;
        iiqFile_[curSensorPlus_]->applyPhaseOneCorr(calFile_, curSensorPlus_, applyDefectCorr_);
        updateRaw();
        repaint();
    }
}

void IIQRawImage::clearRawImage()
{
    if (!iiqFile_[0] && !iiqFile_[1])
        return;

    width_ = 0;
    height_ = 0;

    iiqFile_[0] = std::unique_ptr<IIQFile>();
    if (iiqFile_[1])
        iiqFile_[1] = std::unique_ptr<IIQFile>();
    calFile_ = IIQCalFile();
    delete[] rawData8_;
    rawData8_ = 0;
    curSensorPlus_ = false;

    repaint();
}

void IIQRawImage::setDefectColour(QColor &colour)
{
    defColour_ = colour;
    if (calFile_.valid(curSensorPlus_))
        repaint();
}

bool IIQRawImage::setCalFile(IIQCalFile& calFile)
{
    if (!iiqFile_[false] && !iiqFile_[true])
        return false;

    if (!calFile.valid() || calFile.getCalSerial() != getPhaseOneSerial())
        return false;

    if (calFile.fullyValid())
        calFile_.swap(calFile);
    else
        // only load valid part
        calFile_.swap(calFile, calFile.valid(true));

    // update raw
    iiqFile_[curSensorPlus_]->applyPhaseOneCorr(calFile_, curSensorPlus_, applyDefectCorr_);
    updateRaw();

    // update bitmap
    updateDefectsBitmap();

    // reset editing mode
    setDefectSettingMode(M_NONE);

    adjustSize();
    repaint();

    return true;
}

void IIQRawImage::discardChanges()
{
    if (!iiqFile_[curSensorPlus_])
        return;

    calFile_.swap(iiqFile_[curSensorPlus_]->getIIQCalFile(), curSensorPlus_);
    iiqFile_[curSensorPlus_]->applyPhaseOneCorr(calFile_, curSensorPlus_, applyDefectCorr_);

    updateRaw();
    updateDefects();
}

void IIQRawImage::updateDefectsBitmap()
{
    if (pauseUpdates_ || !calFile_.valid(curSensorPlus_))
        return;

    defPointsCount_ = 0;
    defColsCount_ = 0;

    // update bitmap
    defBitmap_.clear();

    QPainter painter(&defBitmap_);
    QPen pen(Qt::color1);
    painter.setPen(pen);

    // paint point defects
    if (enablePoints_)
        for (auto [col, row] : calFile_.getDefectPixels(curSensorPlus_))
        {
            ++defPointsCount_;
            if (row>=topMargin_ && col>=leftMargin_)
                painter.drawPoint(col-leftMargin_, row-topMargin_);
        }

    // paint column defects
    if (enableCols_)
        for (auto col : calFile_.getDefectCols(curSensorPlus_))
        {
            ++defColsCount_;
            if (col>=leftMargin_)
                painter.drawLine(col-leftMargin_, 0, col-leftMargin_, height_);
        }
}

// attempts to autoremap points
bool IIQRawImage::performAvgAutoRemap(double* avgValues, uint16_t* thresholds)
{
    if (!iiqFile_[curSensorPlus_] || !calFile_.valid(curSensorPlus_))
        return false;

    bool remapped = false;
    for (uint16_t row=0; row<height_; ++row)
        for (uint16_t col=0; col<width_; ++col)
        {
            EChannel channel = EChannel(iiqFile_[curSensorPlus_]->FC(row, col));
            uint16_t threshold = thresholds[channel];
            if (threshold>0 &&
                fabs(avgValues[channel]-iiqFile_[curSensorPlus_]->getRAW(row,col))>threshold)
            {
                if (calFile_.addDefPixel(col+leftMargin_, row+topMargin_, curSensorPlus_))
                    remapped = true;
            }
        }

    if (remapped)
    {
        if (applyDefectCorr_)
        {
            iiqFile_[curSensorPlus_]->applyPhaseOneCorr(calFile_, curSensorPlus_, applyDefectCorr_);
            updateRaw();
        }
        updateDefects();
    }

    return remapped;
}

bool IIQRawImage::performAdaptiveAutoRemap(uint16_t* thresholds,
                                           uint16_t blockSize,
                                           bool countOnly,
                                           EChannel ch,
                                           uint32_t *counts)
{
    if (!iiqFile_[curSensorPlus_])
        return false;

    if (!countOnly && !calFile_.valid(curSensorPlus_))
        return false;

    if (countOnly && !counts)
        return false;

    if (counts)
        counts[C_RED]=counts[C_GREEN]=counts[C_BLUE]=counts[C_GREEN2]=0;

    bool remapped = false;

    uint16_t chBlockCount = (blockSize*blockSize)>>2;
    int median[4];

    // loop through blocks claculating median for all channels in a block and
    // then marking the defective pixels as those that exceed thresholds
    // against median
    for (int y=0; y<height_; y+=blockSize)
    {
        uint16_t row = uint16_t(y);
        if (row+blockSize>height_)
            row = height_-blockSize;
        for (int x=0; x<width_; x+=blockSize)
        {
            uint16_t values[(MAX_ADAPTIVE_BLOCK*MAX_ADAPTIVE_BLOCK)/4];
            uint16_t col = uint16_t(x);
            if (col+blockSize>width_)
                col = width_-blockSize;

            if (ch == C_ALL)
            {
                median[C_RED]=median[C_GREEN]=median[C_BLUE]=median[C_GREEN2]=0;
                extractChannel(C_RED, values, *iiqFile_[curSensorPlus_], row, col, blockSize);
                median[C_RED] = calc_median(values, chBlockCount);
                extractChannel(C_GREEN, values, *iiqFile_[curSensorPlus_], row, col, blockSize);
                median[C_GREEN] = calc_median(values, chBlockCount);
                extractChannel(C_BLUE, values, *iiqFile_[curSensorPlus_], row, col, blockSize);
                median[C_BLUE] = calc_median(values, chBlockCount);
                extractChannel(C_GREEN2, values, *iiqFile_[curSensorPlus_], row, col, blockSize);
                median[C_GREEN2] = calc_median(values, chBlockCount);
            }
            else
            {
                median[ch]=0;
                extractChannel(ch, values, *iiqFile_[curSensorPlus_], row, col, blockSize);
                median[ch] = calc_median(values, chBlockCount);
            }

            // walk the block and mark the defects
            uint16_t lastRow = row+blockSize;
            uint16_t lastCol = col+blockSize;
            for (uint16_t rw=row; rw<lastRow; rw++)
                for (uint16_t cl=col; cl<lastCol; cl++)
                {
                    EChannel channel = EChannel(iiqFile_[curSensorPlus_]->FC(rw, cl));
                    if (ch != C_ALL && ch!=channel)
                        continue;

                    uint16_t threshold = thresholds[channel];
                    if (threshold>0 && abs(median[channel]-iiqFile_[curSensorPlus_]->getRAW(rw, cl))>threshold)
                    {
                        if (countOnly)
                            counts[channel]++;
                        else if (calFile_.addDefPixel(cl+leftMargin_, rw+topMargin_, curSensorPlus_))
                            remapped = true;
                    }
                }
        }
    }

    if (remapped)
    {
        if (applyDefectCorr_)
        {
            iiqFile_[curSensorPlus_]->applyPhaseOneCorr(calFile_, curSensorPlus_, applyDefectCorr_);
            updateRaw();
        }
        updateDefects();
    }

    return remapped;
}

void IIQRawImage::enableDefPoints(bool enable)
{
    if (enablePoints_ != enable)
    {
        enablePoints_ = enable;
        updateDefects();
    }
}

void IIQRawImage::enableDefCols(bool enable)
{
    if (enableCols_ != enable)
    {
        enableCols_ = enable;
        updateDefects();
    }
}

// erase enabled defects
void IIQRawImage::eraseEnabledDefects()
{
    if (!calFile_.valid(curSensorPlus_))
        return;

    if (enablePoints_)
        calFile_.removeDefPixel(-1, -1, curSensorPlus_);

    if (enableCols_)
        calFile_.removeDefCol(-1, curSensorPlus_);

    updateDefects();
}

void IIQRawImage::updateRaw()
{
    if (pauseUpdates_ || !iiqFile_[curSensorPlus_] || !rawData8_)
        return;

    #define TO_8_BIT(val) from12To8[(val)]

    if (renderingType_ == R_RGB)
    {
        tbb::parallel_for(size_t(0), size_t((height_+1)>>1),
        [&](size_t i)
        {
            int row = i<<1;
            if (row + 1 == height_)
                --row;
            for (int col=0; col<width_; col+=2)
            {
                if (col + 1 == width_)
                    --col;

                uint16_t pixel[C_ALL] = { 0, 0, 0, 0 };
                pixel[iiqFile_[curSensorPlus_]->FC(row, col)] =
                    getRawDataEnabled(iiqFile_[curSensorPlus_]->FC(row, col),row,col);
                pixel[iiqFile_[curSensorPlus_]->FC(row, col+1)] =
                    getRawDataEnabled(iiqFile_[curSensorPlus_]->FC(row, col+1),row,col+1);
                pixel[iiqFile_[curSensorPlus_]->FC(row+1, col)] =
                    getRawDataEnabled(iiqFile_[curSensorPlus_]->FC(row+1, col),row+1,col);
                pixel[iiqFile_[curSensorPlus_]->FC(row+1, col+1)] =
                    getRawDataEnabled(iiqFile_[curSensorPlus_]->FC(row+1, col+1),row+1,col+1);

                uint8_t* row0 = rawData8_ + (row*width_ + col)*3;
                uint8_t* row1 = row0 + width_*3;

                row0[0]=row0[3]=row1[0]=row1[3]=TO_8_BIT(pixel[C_RED]);
                row0[1]=row0[4]=row1[1]=row1[4]=TO_8_BIT(((uint32_t)pixel[C_GREEN] + pixel[C_GREEN])>>1);
                row0[2]=row0[5]=row1[2]=row1[5]=TO_8_BIT(pixel[C_BLUE]);
            }
        });
    }
    else if (renderingType_ == R_COMPOSITE_COLOUR)
    {
        tbb::parallel_for(size_t(0), size_t(height_),
        [&](size_t row)
        {
            for (uint16_t col=0; col<width_; ++col)
            {
                EChannel channel = EChannel(iiqFile_[curSensorPlus_]->FC(row, col));
                uint8_t* pixel = rawData8_ + (row*width_ + col)*3;
                pixel[0] = pixel[1] = pixel[2] = 0;
                if (chnlEnabled[channel])
                {
                    static int8_t idx[C_ALL] = { 0, 1, 2, 1 };
                    pixel[idx[channel]] = TO_8_BIT(getRawData(channel,row,col));
                }
            }
        });
    }
    else if (renderingType_ == R_COMPOSITE_GRAY)
    {
        tbb::parallel_for(size_t(0), size_t(height_),
        [&](size_t row)
        {
            for (uint16_t col=0; col<width_; ++col)
            {
                EChannel channel = EChannel(iiqFile_[curSensorPlus_]->FC(row, col));
                uint8_t* pixel = rawData8_ + (row*width_ + col)*3;
                if (chnlEnabled[channel])
                {
                    pixel[0] = pixel[1] = pixel[2] = TO_8_BIT(getRawData(channel,row,col));
                }
                else
                    pixel[0] = pixel[1] = pixel[2] = 0;
            }
        });
    }

    // update pixmap
    QImage image(rawData8_, width_, height_, 3*width_, QImage::Format_RGB888);
    rawPixmap_.convertFromImage(image);

    #undef TO_8_BIT
}

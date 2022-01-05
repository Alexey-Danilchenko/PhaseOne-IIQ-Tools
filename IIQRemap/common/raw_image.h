/*
    raw_image.h - control class that handles display of the raw image and
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
#ifndef IIQ_RAW_IMAGE_H
#define IIQ_RAW_IMAGE_H

#include "iiqcal.h"

#include <QBitmap>
#include <QFrame>
#include <QLabel>
#include <QPaintEvent>
#include <QPixmap>
#include <QPoint>
#include <QSize>

#include <memory>

#define MAX_RAW_VALUE     65535
#define TOTAL_RAW_VALUES  MAX_RAW_VALUE+1

#define MAX_ADAPTIVE_BLOCK  64

enum ERawRendering
{
    R_RGB = 0,
    R_COMPOSITE_COLOUR,
    R_COMPOSITE_GRAY
};

enum EChannel
{
    C_RED = 0,
    C_GREEN,
    C_BLUE,
    C_GREEN2,
    C_ALL
};

// constants for various defect setting modes
enum EDefectMode
{
    M_NONE=0,
    M_COL,
    M_POINT
};

inline int roundToInt(double x)
{
   return (int)(x+0.5f);
}

inline int roundToInt(float x)
{
   return (int)(x+0.5);
}

inline uint16_t calc_median(uint16_t *stack, int count)
{
    int middle = count>>1;

    std::nth_element<uint16_t*>(stack, stack+middle, stack+count);

    if (count&1)
        return stack[middle];

    // even number - get the other one and average
    int result = stack[middle];
    std::nth_element<uint16_t*>(stack, stack+middle-1, stack+count);

    return (result+stack[middle-1]+1)>>1;
}

// ------------------------------
//      IIQRawImage class
// ------------------------------
class IIQRawImage : public QLabel
{
    Q_OBJECT

    QPixmap rawPixmap_;
    QBitmap defBitmap_;

    QColor defColour_;

    uint16_t width_;
    uint16_t height_;
    uint16_t topMargin_;
    uint16_t leftMargin_;

    bool curSensorPlus_;
    std::unique_ptr<IIQFile> iiqFile_[2];   // rawData_;
    IIQCalFile calFile_;
    uint8_t* rawData8_;

    ERawRendering renderingType_;

    bool enableCols_;
    bool enablePoints_;
    EDefectMode curDefSetMode_;
    int defPointsCount_;
    int defColsCount_;

    bool applyDefectCorr_;

    bool pauseUpdates_;

    double scale_;

    // displaying options
    int pX, pY;

    // channel curves
    uint16_t chnlCurves_[4][TOTAL_RAW_VALUES];

    // adjustment parameters
    double contrast_;
    double contrMidpoint_;
    double exposure_[5];
    uint16_t blckLevels_[4];
    bool blackLevelsZeroed_;
    bool applyGamma_;

    // per channel enablement
    bool chnlEnabled[4];

public:

    IIQRawImage(QWidget* parent = 0);
    ~IIQRawImage();

    inline bool isDefectPoint(int row, int col)
    {
        return calFile_.valid(curSensorPlus_)
                    ? calFile_.isDefCol(col+leftMargin_, curSensorPlus_) ||
                      calFile_.isDefPixel(col+leftMargin_, row+topMargin_, curSensorPlus_)
                    : false;
    }

    inline uint16_t getRawValue(int row, int col)
    {
        return iiqFile_[curSensorPlus_] ? iiqFile_[curSensorPlus_]->getRAW(row, col) : 0;
    }

    inline EChannel getRawColor(int row, int col)
    {
        return EChannel(iiqFile_[curSensorPlus_] ? iiqFile_[curSensorPlus_]->FC(row, col) : 0);
    }

    // raw image setters
    void setRawImage(std::unique_ptr<IIQFile>& iiqFile, double scale);
    void clearRawImage();
    bool rawLoaded() { return (bool)iiqFile_[curSensorPlus_]; }
    bool rawLoaded(bool sensorPlus) { return (bool)iiqFile_[sensorPlus]; }
    bool hasCalFile() { return calFile_.valid(curSensorPlus_); }
    IIQCalFile& getCalFile() { return calFile_; }
    std::unique_ptr<IIQFile>& getRawImage() { return iiqFile_[curSensorPlus_]; }
    std::unique_ptr<IIQFile>& getRawImage(bool sensorPlus) { return iiqFile_[sensorPlus]; }

    void setSensorPlus(bool sensorPlus, double scale);
    bool getSensorPlus() { return curSensorPlus_; };
    bool supportsSensorPlus() { return calFile_.hasSensorPlus(); }

    bool setCalFile(IIQCalFile& calFile);
    void discardChanges();
    void updateDefects()
    {
        updateDefectsBitmap();
        repaint();
    }
    bool hasUnsavedChanges() { return calFile_.hasUnsavedChanges(); }
    void setDefectColour(QColor &colour);
    void setDefectCorr(bool applyDefectCorr);
    void enableDefPoints(bool enable);
    void enableDefCols(bool enable);
    void setDefectSettingMode(EDefectMode mode);

    // change current rendering
    void setRawRenderingType(ERawRendering renderingType);

    void pauseUpdates(bool pauseUpdates);

    void setScale(double scale);

    void enableGammaCorrection(bool enable);

    void enableBlackLevelZeroed(bool enable);

    // reset for exposure and contrast corrections
    void resetAllCorrections();

    // exposure corrections
    void setExpCorr(double expCorr, EChannel channel = C_ALL);

    // contrast corrections
    void setContrCorr(double contrast);
    void setContrMidpoint(double ctrsMidpoint);

    // black level
    void setBlack(int blackLevel, EChannel channel = C_ALL);

    // enable/disable channels
    void enableChannel(bool enable, EChannel channel = C_ALL);

    // set expo corrections to specified WB
    void setWB(double* wb);

    // attempts to autoremap points
    bool performAvgAutoRemap(double* avgValues, uint16_t* thresholds);
    bool performAdaptiveAutoRemap(uint16_t* thresholds,
                                  uint16_t blockSize,
                                  bool countOnly = false,
                                  EChannel ch = C_ALL,
                                  uint32_t *counts = 0);

    // erase enabled defects
    void eraseEnabledDefects();

    // getters
    uint16_t getRawWidth() { return width_; }
    uint16_t getRawHeight() { return height_; }

    int getDefectPoints() { return defPointsCount_; }
    int getDefectCols() { return defColsCount_; }

    ERawRendering getRawRenderingType() { return renderingType_; }

    // size hint
    QSize sizeHint() const;

Q_SIGNALS:
    void imageCursorPosUpdated(uint16_t row, uint16_t col);
    void defectsChanged();

private:

    inline uint16_t getRawDataEnabled(uint8_t ch, int row, int col)
    {
        return chnlEnabled[ch] ? chnlCurves_[ch][iiqFile_[curSensorPlus_]->getRAW(row, col)] : 0;
    }

    inline const std::string getPhaseOneSerial()
    {
        return iiqFile_[0] ? iiqFile_[0]->getPhaseOneSerial()
                           : (iiqFile_[1] ? iiqFile_[1]->getPhaseOneSerial()
                                          : std::string());
    }

    inline uint16_t getRawData(uint8_t ch, uint16_t row, uint16_t col)
    {
        return chnlCurves_[ch][iiqFile_[curSensorPlus_]->getRAW(row, col)];
    }

    void calcViewpointOffsets()
    {
        pX=pY=0;
        if (width() >= width_*scale_)
            pX = roundToInt((width()-width_*scale_)/2);
        if (height() >= height_*scale_)
            pY = roundToInt((height()-height_*scale_)/2);
    }
    void paintEvent(QPaintEvent *p);
    void resizeEvent(QResizeEvent *event);
    void mouseMoveEvent(QMouseEvent * e);
    void mousePressEvent(QMouseEvent * e);
    void updateRaw();
    void updateDefectsBitmap();
    void generateCurves(EChannel channel = C_ALL);
};

#endif

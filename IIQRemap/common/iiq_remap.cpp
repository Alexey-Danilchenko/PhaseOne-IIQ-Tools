/*
    iiq_remap.cpp - mainform class to deal with QT application

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

#include "iiq_remap.h"
#include "about.h"

#include <QAbstractSlider>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QPalette>
#include <QProxyStyle>
#include <QSettings>
#include <QStyle>
#include <QScrollBar>
#include <QStyleFactory>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <libraw.h>

#include <tbb/tbb.h>

#include <string.h>
#include <math.h>

#include <algorithm>
#include <vector>
#include <memory>

#define APP_VERSION " v1.4"

#define MAIN_TITLE APP_NAME APP_VERSION

#define STATE_SECTION "Saved State"

#define MAX_RAWS  7

#if defined( Q_OS_MACX )
#define BUNDLE_ID CFSTR("IIQRemap")
#if defined(_QT_STATIC_) && QT_VERSION >= 0x050000
#include <QtPlugin>
Q_IMPORT_PLUGIN (QCocoaIntegrationPlugin);
#endif
#endif

#if defined( Q_OS_WIN )
#if defined(_QT_STATIC_) && QT_VERSION >= 0x050000
#include <QtPlugin>
Q_IMPORT_PLUGIN (QWindowsIntegrationPlugin);
#endif
#endif

// --------------------------------------------------------
//    static data
// --------------------------------------------------------

// list of scale levels in percent
static std::vector<int> zoomLevelList = {
    5, 10, 15, 20, 25, 30, 40, 50, 60, 75,
    100, 125, 150, 200, 300, 500, 700, 1000
};

static int zoomLevelListFull = 0;

// --------------------------------------------------------
//    helper functions
// --------------------------------------------------------
static void initStaticData()
{
    static bool needInit = true;

    if (needInit)
    {
        for (int i=0; i<zoomLevelList.size(); ++i)
            if (zoomLevelList[i]==100)
            {
                zoomLevelListFull = i+1;
                break;
            }

        needInit = false;
    }
}

static double fitScale(uint16_t imgWidth, uint16_t imgHeight, QWidget &w)
{
    if (imgWidth==0 || imgHeight==0)
        return 1.0;

    double scale = (double)w.width()/imgWidth;
    double scaleH = (double)w.height()/imgHeight;

    if (scale > scaleH)
        scale = scaleH;

    // find the one with closes match
    int i=0;
    while (i<zoomLevelList.size() && (scale*100) >= zoomLevelList[i])
        ++i;

    if (i>0)
        --i;

    return (double)zoomLevelList[i]/100;
}

static double getScrollBarRelPos(QScrollBar *scrl)
{
    if (!scrl->isVisible())
        return 0.5;
    return ((double)scrl->value() - scrl->minimum()) / (scrl->maximum() - scrl->minimum());
}

static void setScrollBarRelPos(QScrollBar *scrl, double relPos)
{
	scrl->setValue(roundToInt(relPos * (scrl->maximum() - scrl->minimum()) + scrl->minimum()));
}

static void setAdjustedScrollBarPos(QScrollBar *scrl, int value)
{
    if (value<0)
        value = scrl->maximum() + 1 - scrl->pageStep()/2;

    scrl->setValue(value);
}

inline double log2(double x) { return log(x)/log(2.0); }

inline uint16_t blockSize(int index) { return uint16_t((index<<1)+4); }

#if defined(WIN32) || defined(_WIN32)
#define TO_STDSTR(qs)  (qs.toStdWString())
#define TO_QSTR(s)  (QString::fromStdWString(s))
#else
#define TO_STDSTR(qs)  (qs.toStdString())
#define TO_QSTR(s)  (QString::fromStdString(s))
#endif

// --------------------------------------------------------
//    IIQRemap class
// --------------------------------------------------------
IIQRemap::IIQRemap()
    : QMainWindow(), scale(1),
      lockModeChange(false), lockThresChange(false),
      overrideCursorSet(false)
{
    curRawPath = "./";

	// init statics
    initStaticData();

    ui.setupUi(this);
    ui.btnZoomFit->setEnabled(false);

    // load icon
    tickIcon.addFile(QStringLiteral(":/MainForm/images/tick_small.png"));
    tickEmptyIcon.addFile(QStringLiteral(":/MainForm/images/tick_small_empty.png"));

    // integrators
    expControls[C_ALL] = new SpinBoxSliderIntegrator(ui.expAllSpin, ui.expAll);
    expControls[C_RED] = new SpinBoxSliderIntegrator(ui.expRedSpin, ui.expRed);
    expControls[C_GREEN] = new SpinBoxSliderIntegrator(ui.expGreenSpin, ui.expGreen);
    expControls[C_BLUE] = new SpinBoxSliderIntegrator(ui.expBlueSpin, ui.expBlue);
    expControls[C_GREEN2] = new SpinBoxSliderIntegrator(ui.expGreen2Spin, ui.expGreen2);

    // buttons
    connect(ui.btnLoadRaws, SIGNAL(clicked()), this, SLOT(loadRaw()));
    connect(ui.btnLoadCal, SIGNAL(clicked()), this, SLOT(openCalFile()));
    connect(ui.btnSave, SIGNAL(clicked()), this, SLOT(saveCalFile()));
    connect(ui.btnReset, SIGNAL(clicked()), this, SLOT(discardChanges()));
    connect(ui.btnRemoveDefects, SIGNAL(clicked()), this, SLOT(deleteShownDefects()));
    connect(ui.btnAutoRemap, SIGNAL(clicked()), this, SLOT(autoRemap()));
    connect(ui.btnZoom100, SIGNAL(clicked()), this, SLOT(zoomFull()));
    connect(ui.btnZoomFit, SIGNAL(clicked()), this, SLOT(zoomFit()));
    connect(ui.btnZoomIn, SIGNAL(clicked()), this, SLOT(zoomIn()));
    connect(ui.btnZoomOut, SIGNAL(clicked()), this, SLOT(zoomOut()));
    connect(ui.btnResetCorr, SIGNAL(clicked()), this, SLOT(resetAdjustments()));
    connect(ui.btnWB, SIGNAL(clicked()), this, SLOT(setWB()));
    connect(ui.btnDefColour, SIGNAL(clicked()), this, SLOT(changeDefColour()));
    connect(ui.btnDetectFromRaw, SIGNAL(clicked()), this, SLOT(calculateThresholds()));

    // comboboxes
    connect(ui.cboxZoomLevel, SIGNAL(currentIndexChanged(int)), this, SLOT(setZoomLevel(int)));
    connect(ui.cbAdaptiveBlock, SIGNAL(currentIndexChanged(int)), this, SLOT(adjustAdaptiveBlockSize(int)));

    // spinboxes
    connect(ui.spinBlckRed, SIGNAL(valueChanged(int)), this, SLOT(adjustBlack(int)));
    connect(ui.spinBlckGreen, SIGNAL(valueChanged(int)), this, SLOT(adjustBlack(int)));
    connect(ui.spinBlckBlue, SIGNAL(valueChanged(int)), this, SLOT(adjustBlack(int)));
    connect(ui.spinBlckGreen2, SIGNAL(valueChanged(int)), this, SLOT(adjustBlack(int)));
    connect(ui.spbThrRed, SIGNAL(valueChanged(int)), this, SLOT(adjustThreshold(int)));
    connect(ui.spbThrGreen, SIGNAL(valueChanged(int)), this, SLOT(adjustThreshold(int)));
    connect(ui.spbThrBlue, SIGNAL(valueChanged(int)), this, SLOT(adjustThreshold(int)));
    connect(ui.spbThrGreen2, SIGNAL(valueChanged(int)), this, SLOT(adjustThreshold(int)));

    // exposure controls
    connect(expControls[C_ALL], SIGNAL(valueChanged(double)), this, SLOT(adjustExposure(double)));
    connect(expControls[C_RED], SIGNAL(valueChanged(double)), this, SLOT(adjustExposure(double)));
    connect(expControls[C_GREEN], SIGNAL(valueChanged(double)), this, SLOT(adjustExposure(double)));
    connect(expControls[C_BLUE], SIGNAL(valueChanged(double)), this, SLOT(adjustExposure(double)));
    connect(expControls[C_GREEN2], SIGNAL(valueChanged(double)), this, SLOT(adjustExposure(double)));

    // sliders
    connect(ui.sldrContrast, SIGNAL(valueChanged(int)), this, SLOT(adjustContrast(int)));
    connect(ui.sldrContrastPoint, SIGNAL(valueChanged(int)), this, SLOT(adjustContrastMidpoint(int)));

    // checkboxes
    connect(ui.chkApplyDefectCorr, SIGNAL(stateChanged(int)), this, SLOT(applyDefectCorr(int)));
    connect(ui.checkGamma, SIGNAL(stateChanged(int)), this, SLOT(gammaChecked(int)));
    connect(ui.checkBlackZeroed, SIGNAL(stateChanged(int)), this, SLOT(blackLevelZeroed(int)));
    connect(ui.checkR, SIGNAL(stateChanged(int)), this, SLOT(redChecked(int)));
    connect(ui.checkG, SIGNAL(stateChanged(int)), this, SLOT(greenChecked(int)));
    connect(ui.checkB, SIGNAL(stateChanged(int)), this, SLOT(blueChecked(int)));
    connect(ui.checkG2, SIGNAL(stateChanged(int)), this, SLOT(green2Checked(int)));
    connect(ui.chkShowPoints, SIGNAL(stateChanged(int)), this, SLOT(showPointsChecked(int)));
    connect(ui.chkShowCols, SIGNAL(stateChanged(int)), this, SLOT(showColsChecked(int)));
    connect(ui.chkAdaptiveRemap, SIGNAL(stateChanged(int)), this, SLOT(adaptiveRemapModeChecked(int)));

    // toggles
    connect(ui.radioRGB, SIGNAL(toggled(bool)), this, SLOT(rawRenderingChanged(bool)));
    connect(ui.radioComposite, SIGNAL(toggled(bool)), this, SLOT(rawRenderingChanged(bool)));
    connect(ui.radioCompGray, SIGNAL(toggled(bool)), this, SLOT(rawRenderingChanged(bool)));
    connect(ui.btnColMode, SIGNAL(toggled(bool)), this, SLOT(colDefectModeChecked(bool)));
    connect(ui.btnPointMode, SIGNAL(toggled(bool)), this, SLOT(pointDefectModeChecked(bool)));

    // actions
    connect(ui.actionOpen, SIGNAL(triggered()), this, SLOT(openCalFile()));
    connect(ui.actionSave, SIGNAL(triggered()), this, SLOT(saveCalFile()));
    connect(ui.actionDiscard_changes, SIGNAL(triggered()), this, SLOT(discardChanges()));
    connect(ui.actionLoad_raw, SIGNAL(triggered()), this, SLOT(loadRaw()));
    connect(ui.actionAuto_remap, SIGNAL(triggered()), this, SLOT(autoRemap()));
    connect(ui.actionHelp_web, SIGNAL(triggered()), this, SLOT(help()));
    connect(ui.actionAbout, SIGNAL(triggered()), this, SLOT(about()));
    connect(ui.actionQuit, SIGNAL(triggered()), this, SLOT(close()));

    setWindowTitle(MAIN_TITLE);

    // raw image events
    connect(ui.rawImage, SIGNAL(imageCursorPosUpdated(uint16_t, uint16_t)),
            this,        SLOT(updateStatus(uint16_t, uint16_t)));
    connect(ui.rawImage, SIGNAL(defectsChanged()), this, SLOT(defectsChanged()));

    // init data
    init();

    ui.rawImage->setRawRenderingType(R_RGB);

    defectColour = QColor(255, 85, 0, 255);

    // read settings and position the window
    QSettings settings(APP_NAME, STATE_SECTION);
    QPoint pos = settings.value("Position").toPoint();
    QSize size = settings.value("Size").toSize();
    curRawPath = settings.value("Curent IIQ Path").toString();
    curCalPath = settings.value("Curent CAL Path").toString();
    defectColour = settings.value("Defect Colour", defectColour).value<QColor>();
    ui.cbAdaptiveBlock->setCurrentIndex(settings.value("Adaptive Block", 14).toInt());
    ui.chkAdaptiveRemap->setCheckState(Qt::CheckState(settings.value("Adaptive Remap", Qt::Unchecked).toInt()));

    if (!pos.isNull())
        move(pos);

    if (!size.isEmpty())
        resize(size);

    if (settings.value("Maximized").toBool())
        setWindowState(windowState() | Qt::WindowMaximized);

	setZoomLevel(0);
    changeDefColour(&defectColour);

    updateWidgets();
}

IIQRemap::~IIQRemap()
{
    delete expControls[C_ALL];
    delete expControls[C_RED];
    delete expControls[C_GREEN];
    delete expControls[C_BLUE];
    delete expControls[C_GREEN2];
}

bool IIQRemap::checkUnsavedAndSave()
{
    bool okToProceed = true;
    if (ui.rawImage->hasUnsavedChanges())
    {
        int dlgRes = showMessage(
                        tr("Warning"),
                        tr("The remap has been modified!"),
                        tr("Do you want to save your changes?"),
                        QMessageBox::Question,
                        QMessageBox::Save
                            | QMessageBox::Discard
                            | QMessageBox::Cancel);

        if (dlgRes == QMessageBox::Save)
        {
            okToProceed = saveCal();
        }
        else
            okToProceed = dlgRes==QMessageBox::Discard;
    }

    return okToProceed;
}

void IIQRemap::closeEvent(QCloseEvent *event)
{
    QSettings settings(APP_NAME, STATE_SECTION);

    if (!isMaximized())
    {
        settings.setValue("Position", pos());
        settings.setValue("Size", size());
    }

    settings.setValue("Curent IIQ Path", curRawPath);
    settings.setValue("Curent CAL Path", curCalPath);
    settings.setValue("Maximized", isMaximized());
    settings.setValue("Defect Colour", defectColour);
    settings.setValue("Adaptive Remap", ui.chkAdaptiveRemap->checkState());
    settings.setValue("Adaptive Block", ui.cbAdaptiveBlock->currentIndex());

    if (checkUnsavedAndSave())
        event->accept();
    else
        event->ignore();
}

void IIQRemap::init()
{
    // init comboboxes for scale list
    ui.cboxZoomLevel->addItem(tr("Fit to Window"));
    QString numStr;
    for (int i=0; i<zoomLevelList.size(); ++i)
    {
        numStr.setNum(zoomLevelList[i]);
        ui.cboxZoomLevel->addItem(numStr);
    }

    numStr = "%1 x %1";
    for (int i=4; i<=MAX_ADAPTIVE_BLOCK; i+=2)
        ui.cbAdaptiveBlock->addItem(numStr.arg(i));

    ui.cboxZoomLevel->setMaxVisibleItems(10);
    ui.cbAdaptiveBlock->setMaxVisibleItems(20);

    camWB[C_RED] = camWB[C_GREEN] = camWB[C_BLUE] = camWB[C_GREEN2] = 1;
    threshold[C_RED]=threshold[C_GREEN]=threshold[C_BLUE]=threshold[C_GREEN2]=0;
    thrStats[C_RED]=thrStats[C_GREEN]=thrStats[C_BLUE]=thrStats[C_GREEN2]=0;

    maxVal[C_RED] = maxVal[C_GREEN] = maxVal[C_BLUE] = maxVal[C_GREEN2] = 0;
    minVal[C_RED] = minVal[C_GREEN] = minVal[C_BLUE] = minVal[C_GREEN2] = 0;
    avgVal[C_RED] = avgVal[C_GREEN] = avgVal[C_BLUE] = avgVal[C_GREEN2] = 0;
    stdDev[C_RED] = stdDev[C_GREEN] = stdDev[C_BLUE] = stdDev[C_GREEN2] = 0;
}

int IIQRemap::showMessage(const QString& title,
                          const QString& msgText,
                          const QString& informativeText,
                          QMessageBox::Icon icon,
                          QMessageBox::StandardButtons buttons,
                          QMessageBox::StandardButton defButton)
{
    restoreOverrideCursor();

    QMessageBox msgBox(icon,
                       title,
                       msgText,
                       buttons);
    msgBox.setInformativeText(informativeText);
    msgBox.setDefaultButton(defButton);

    return msgBox.exec();
}

void IIQRemap::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    if (ui.cboxZoomLevel->currentIndex()==0)
    {
        scale = fitScale(ui.rawImage->getRawWidth(), ui.rawImage->getRawHeight(), *ui.rawImage);
        ui.rawImage->setScale(scale);
    }
}

void IIQRemap::updateWidgets()
{
    bool hasRaw = ui.rawImage->rawLoaded();
    bool hasCalFile = ui.rawImage->hasCalFile();

    QString title(MAIN_TITLE);

    if (hasRaw)
    {
        QFileInfo info(rawFileName);
        title.append("       IIQ: ").append(info.fileName());
    }
    if (hasCalFile)
    {
        if (auto serial = ui.rawImage->getCalFile().getCalSerial(); !serial.empty())
            title.append(tr("       Serial: ")).append(serial.c_str());
        QFileInfo info(TO_QSTR(ui.rawImage->getCalFile().getCalFileName()));
        title.append("       CAL: ")
             .append(info.fileName().isEmpty() ? "not saved" : info.fileName());
        if (ui.rawImage->hasUnsavedChanges())
            title.append(" * ");
    }

    setWindowTitle(title);

    ui.tabDisplay->setEnabled(hasRaw);
    ui.tabRemap->setEnabled(hasCalFile);

    ui.zoomBar->setEnabled(hasRaw || hasCalFile);
    ui.btnColMode->setEnabled(hasCalFile);
    ui.btnPointMode->setEnabled(hasCalFile);

    ui.btnLoadCal->setEnabled(hasRaw);
    ui.btnSave->setEnabled(hasCalFile);
    ui.btnReset->setEnabled(hasCalFile);

    //ui.actionSave->setEnabled(hasCalFile);
    //ui.actionDiscard_changes->setEnabled(hasCalFile);

    if (hasCalFile)
    {
        ui.grpRemapThr->setEnabled(hasRaw);
        ui.btnAutoRemap->setEnabled(hasRaw);
        ui.btnDetectFromRaw->setEnabled(hasRaw);
    }

    updateAutoRemap();
}

void IIQRemap::updateAutoRemap()
{
    uint32_t matchedCount = thrStats[C_RED]+thrStats[C_GREEN]+thrStats[C_BLUE]+thrStats[C_GREEN2];
    ui.btnAutoRemap->setEnabled(matchedCount>0 && matchedCount<60000);
}

// -------------------------------------------------------------------------
//   Event slots
// -------------------------------------------------------------------------

void IIQRemap::openCalFile()
{
    if (!ui.rawImage->rawLoaded())
        return;

    auto fileName = QFileDialog::getOpenFileName(this,
                                                 tr("Load IIQ .calib file"),
                                                 curCalPath,
                                                 tr("Phase One calibration files (*.calib)"));

    if (!fileName.isEmpty())
    {
        QFileInfo info(fileName);
        curCalPath = info.absolutePath();

        IIQCalFile newCalFile(TO_STDSTR(fileName));

        if (!newCalFile.valid())
        {
            showMessage(tr("Error"), tr("Error opening calibration file\n%1!").arg(fileName));
        }
        else if (newCalFile.getCalSerial() != ui.rawImage->getRawImage()->getPhaseOneSerial())
        {
            showMessage(tr("Error"),
                        tr("Calibration file serial %1 does not match IIQ serial %2\n%3!")
                          .arg(newCalFile.getCalSerial().c_str())
                          .arg(ui.rawImage->getRawImage()->getPhaseOneSerial().c_str())
                          .arg(fileName));
        }
        else if (checkUnsavedAndSave())
        {
            // reset mode
            lockModeChange = true;
            ui.btnPointMode->setChecked(false);
            ui.btnColMode->setChecked(false);
            lockModeChange = false;

            // recalculate fit
            if (ui.cboxZoomLevel->currentIndex()==0)
                scale = fitScale(ui.rawImage->getRawWidth(),
                                 ui.rawImage->getRawHeight(),
                                 *ui.rawImage);

            ui.rawImage->setCalFile(newCalFile);
            if (ui.chkApplyDefectCorr->checkState() == Qt::Checked)
            {
                processRawData();
                updateThresholdStats(C_ALL);
            }
            updateWidgets();
            updateDefectStats();
        }
    }
}

bool IIQRemap::saveCal()
{
    auto& calFile = ui.rawImage->getCalFile();

    if (!calFile.valid())
        return true;

    bool success = false;
    if (calFile.getCalFileName().empty())
    {
        // form the file name
        std::string baseFileName = calFile.getCalSerial() + ".calib";
        auto newCalPath = QFileDialog::getExistingDirectory(
                            this,
                            tr("Save %1 calibration file to").arg(baseFileName.c_str()),
                            curCalPath);
        if (newCalPath.isEmpty())
            return false;

        QFileInfo info(newCalPath, baseFileName.c_str());

        if (info.exists() &&
            showMessage(tr("Warning"),
                        tr("The %1 file already exists!").arg(info.absoluteFilePath()),
                        tr("Do you want to overwrite it?"),
                        QMessageBox::Question,
                        QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
        {
            return false;
        }

        calFile.setCalFileName(TO_STDSTR(info.absoluteFilePath()));
        success = calFile.saveCalFile();
        if (success)
            curCalPath = info.absolutePath();
        else
            // reset the file name as it was not successful
            calFile.setCalFileName(TO_STDSTR(QString()));
    }
    else
        success = calFile.saveCalFile();

    if (!success)
        showMessage(tr("Error"),
                    tr("Error writing calibration file %1!")
                        .arg(calFile.getCalFileName().c_str()));

    return success;
}

void IIQRemap::saveCalFile()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    if (saveCal())
        updateWidgets();

    QApplication::restoreOverrideCursor();
}

void IIQRemap::discardChanges()
{
    // reload
    ui.rawImage->discardChanges();
    if (ui.chkApplyDefectCorr->checkState() == Qt::Checked)
    {
        processRawData();
        updateThresholdStats(C_ALL);
    }
    updateWidgets();
    updateDefectStats();
}

void IIQRemap::loadRaw()
{
    auto fileNames = QFileDialog::getOpenFileNames(
                        this,
                        tr("Load Phase One .IIQ file(s)"),
                        curRawPath,
                        tr("Phase One IIQ (*.iiq *.tif)"));

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    // filter through and leave only files
    auto it = fileNames.begin();
    while (it != fileNames.end())
    {
        QFileInfo info(*it);

        if (!info.isFile())
            it = fileNames.erase(it);
        else
            ++it;
    }

    if (fileNames.size() > MAX_RAWS)
    {
        showMessage(tr("Error"), tr("Cannot load more than %1 IIQ files!").arg(MAX_RAWS));
    }
    else if (fileNames.size() > 0)
    {
        // read the first raw
        QFileInfo info(fileNames.at(0));
		curRawPath = info.absolutePath();

        auto iiqFile = std::make_unique<IIQFile>();
        int ret = LIBRAW_SUCCESS;
        if ((ret = iiqFile->open_file(TO_STDSTR(fileNames.at(0)).c_str())) != LIBRAW_SUCCESS)
        {
            showMessage(tr("Error"), tr("Error opening file\n%1!").arg(fileNames.at(0)));
        }
        else if (!iiqFile->isPhaseOne() || iiqFile->getPhaseOneSerial().empty())
        {
            ret = LIBRAW_FILE_UNSUPPORTED;
            showMessage(tr("Error"), tr("File %1\ndoes not seem to be Phase One IIQ file!").arg(fileNames.at(0)));
        }
        else if ((ret = iiqFile->unpack()) != LIBRAW_SUCCESS)
        {
            showMessage(tr("Error"), tr("Error unpacking IIQ data from file\n%1!").arg(fileNames.at(0)));
        }

        // check if we have multiple files and load up a stack
        if (ret == LIBRAW_SUCCESS && fileNames.size() > 1)
            ret = loadRawStack(*iiqFile, fileNames);

        const auto& calFile = ui.rawImage->getCalFile();
        if (ret == LIBRAW_SUCCESS && ui.rawImage->hasUnsavedChanges() &&
            iiqFile->getPhaseOneSerial() != calFile.getCalSerial())
        {
            if (showMessage(tr("Warning"),
                            tr("IIQ file %1\ndoes not match current calibration with unsaved changed!").arg(fileNames.at(0)),
                            tr("Do you want to load IIQ file anyway?"),
                            QMessageBox::Question,
                            QMessageBox::Yes | QMessageBox::No,
                            QMessageBox::Yes) == QMessageBox::No)
                ret = LIBRAW_UNSPECIFIED_ERROR;
            else
            {
                // reset mode
                lockModeChange = true;
                ui.btnPointMode->setChecked(false);
                ui.btnColMode->setChecked(false);
                lockModeChange = false;
            }
        }

        // actually load the data into control
        if (ret == LIBRAW_SUCCESS)
        {
            // get WB
            float* wb = iiqFile->imgdata.color.cam_mul;

            if (wb[0] <= 0)
                wb = iiqFile->imgdata.color.pre_mul;

            camWB[C_RED]    = wb[C_RED];
            camWB[C_GREEN]  = wb[C_GREEN];
            camWB[C_BLUE]   = wb[C_BLUE];
            camWB[C_GREEN2] = wb[C_GREEN2];

            if (camWB[C_GREEN2] <= 0)
                camWB[C_GREEN2] = camWB[C_GREEN];

            double maxGreen = std::max(camWB[C_GREEN], camWB[C_GREEN2]);
            if (maxGreen == 0.0)
                maxGreen = 1.0;

            // normalise camWB
            camWB[C_RED]    /= maxGreen;
            camWB[C_GREEN]  /= maxGreen;
            camWB[C_BLUE]   /= maxGreen;
            camWB[C_GREEN2] /= maxGreen;

            // recalculate fit
            if (ui.cboxZoomLevel->currentIndex()==0)
                scale = fitScale(iiqFile->imgdata.sizes.raw_width,
                                 iiqFile->imgdata.sizes.raw_height,
                                 *ui.rawImage);
            ui.rawImage->setRawImage(iiqFile, scale);

            // process raw data to gather stats
            processRawData();
            calculateThresholds();
        }

        rawFileName = fileNames.at(0);
        updateWidgets();
        updateDefectStats();
    }

    QApplication::restoreOverrideCursor();
}

// The number of files in a stack passed here needs to be limited by MAX_RAWS
int IIQRemap::loadRawStack(IIQFile& file, QStringList fileNames)
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    int rawCount = fileNames.size()>MAX_RAWS ? MAX_RAWS : fileNames.size();
    auto iiqFiles = std::make_unique<IIQFile[]>(rawCount-1);
    int result = LIBRAW_SUCCESS;

    // attempt to go and open all raw files
    for (int i=1; result==LIBRAW_SUCCESS && i<rawCount; ++i)
    {
        if ((result = iiqFiles[i-1].open_file(TO_STDSTR(fileNames.at(i)).c_str())) != LIBRAW_SUCCESS)
            showMessage(tr("Error"), tr("Error opening file\n%1!").arg(fileNames.at(i)));
        else if (!iiqFiles[i-1].isPhaseOne())
        {
            result = LIBRAW_FILE_UNSUPPORTED;
            showMessage(tr("Error"),
                        tr("File %1\ndoes not seem to be Phase One IIQ file!")
                            .arg(fileNames.at(i)));
        }
        else if (file.getPhaseOneSerial() != iiqFiles[i-1].getPhaseOneSerial())
        {
            result = LIBRAW_FILE_UNSUPPORTED;
            showMessage(tr("Error"),
                        tr("File %1 is not\nfrom the same Phase One camera as the first file!")
                            .arg(fileNames.at(i)));
        }
        else if ((result = iiqFiles[i-1].unpack()) != LIBRAW_SUCCESS)
            showMessage(tr("Error"), tr("Error unpacking IIQ file %1!").arg(fileNames.at(i)));
    }

    // loaded all the raws - calculate the median into first array
    if (result == LIBRAW_SUCCESS)
    {
        uint16_t* data = file.imgdata.rawdata.raw_image;

        tbb::parallel_for(size_t(0), size_t(file.imgdata.sizes.raw_height),
        [&](size_t row)
        {
            int i = row*file.imgdata.sizes.raw_width;
            for (uint16_t col=0; col<file.imgdata.sizes.raw_width; ++col, ++i)
            {
                uint16_t stack[MAX_RAWS];
                stack[0] = data[i];
                for (int cnt=1; cnt<rawCount; cnt++)
                    stack[cnt] = iiqFiles[cnt-1].imgdata.rawdata.raw_image[i];
                data[i] = calc_median(stack, rawCount);
            }
        });
    }

    QApplication::restoreOverrideCursor();

    return result;
}

// walks through raw data and gets the stats
void IIQRemap::processRawData()
{
    uint16_t rawWidth = ui.rawImage->getRawWidth();
    uint16_t rawHeight = ui.rawImage->getRawHeight();
    int nValues = (rawWidth * rawHeight)>>2;

    maxVal[C_RED] = maxVal[C_GREEN] = maxVal[C_BLUE] = maxVal[C_GREEN2] = 0;
    minVal[C_RED] = minVal[C_GREEN] = minVal[C_BLUE] = minVal[C_GREEN2] = 0xFFFF;
    avgVal[C_RED] = avgVal[C_GREEN] = avgVal[C_BLUE] = avgVal[C_GREEN2] = 0;
    stdDev[C_RED] = stdDev[C_GREEN] = stdDev[C_BLUE] = stdDev[C_GREEN2] = 0;

    // calculate mean and stddev
    for (int row=0; row<rawHeight; ++row)
        for (int col=0; col<rawWidth; ++col)
        {
            EChannel channel = ui.rawImage->getRawColor(row,col);
            uint16_t val = ui.rawImage->getRawValue(row,col);
            if (maxVal[channel]<val)
                maxVal[channel] = val;
            if (minVal[channel]>val)
                minVal[channel] = val;
            avgVal[channel] += val;
            stdDev[channel] += double(val)*val;
        }

    stdDev[C_RED]    = sqrt((stdDev[C_RED]-(avgVal[C_RED]*avgVal[C_RED]/nValues))/(nValues-1));
    stdDev[C_GREEN]  = sqrt((stdDev[C_GREEN]-(avgVal[C_GREEN]*avgVal[C_GREEN]/nValues))/(nValues-1));
    stdDev[C_BLUE]   = sqrt((stdDev[C_BLUE]-(avgVal[C_BLUE]*avgVal[C_BLUE]/nValues))/(nValues-1));
    stdDev[C_GREEN2] = sqrt((stdDev[C_GREEN2]-(avgVal[C_GREEN2]*avgVal[C_GREEN2]/nValues))/(nValues-1));

    avgVal[C_RED]    /= nValues;
    avgVal[C_GREEN]  /= nValues;
    avgVal[C_BLUE]   /= nValues;
    avgVal[C_GREEN2] /= nValues;

    updateRawStats();
}

void IIQRemap::updateRawStats()
{
    QString fmtStr("%1");

    // populate UI controls
    ui.lblStatsMinR->setText(fmtStr.arg(minVal[C_RED]));
    ui.lblStatsMinG->setText(fmtStr.arg(minVal[C_GREEN]));
    ui.lblStatsMinB->setText(fmtStr.arg(minVal[C_BLUE]));
    ui.lblStatsMinG2->setText(fmtStr.arg(minVal[C_GREEN2]));
    ui.lblStatsMaxR->setText(fmtStr.arg(maxVal[C_RED]));
    ui.lblStatsMaxG->setText(fmtStr.arg(maxVal[C_GREEN]));
    ui.lblStatsMaxB->setText(fmtStr.arg(maxVal[C_BLUE]));
    ui.lblStatsMaxG2->setText(fmtStr.arg(maxVal[C_GREEN2]));
    ui.lblStatsAvgR->setText(fmtStr.arg(avgVal[C_RED],0,'f',2));
    ui.lblStatsAvgG->setText(fmtStr.arg(avgVal[C_GREEN],0,'f',2));
    ui.lblStatsAvgB->setText(fmtStr.arg(avgVal[C_BLUE],0,'f',2));
    ui.lblStatsAvgG2->setText(fmtStr.arg(avgVal[C_GREEN2],0,'f',2));
}

void IIQRemap::calculateThresholds()
{
    threshold[C_RED]    = stdDev[C_RED]*10;
    threshold[C_GREEN]  = stdDev[C_GREEN]*10;
    threshold[C_BLUE]   = stdDev[C_BLUE]*10;
    threshold[C_GREEN2] = stdDev[C_GREEN2]*10;

    lockThresChange = true;
    ui.spbThrRed->setValue(threshold[C_RED]);
    ui.spbThrGreen->setValue(threshold[C_GREEN]);
    ui.spbThrBlue->setValue(threshold[C_BLUE]);
    ui.spbThrGreen2->setValue(threshold[C_GREEN2]);
    lockThresChange = false;

    // update stats
    updateThresholdStats(C_ALL);
}

void IIQRemap::updateThresholdStats(EChannel channel)
{
    QString fmtStr("%1");

    uint16_t rawWidth = ui.rawImage->getRawWidth();
    uint16_t rawHeight = ui.rawImage->getRawHeight();

    if (!ui.rawImage->rawLoaded())
        return;

    if (ui.chkAdaptiveRemap->checkState()==Qt::Checked)
    {
        ui.rawImage->performAdaptiveAutoRemap(
                    threshold,
                    blockSize(ui.cbAdaptiveBlock->currentIndex()),
                    true,
                    channel,
                    thrStats);
    }
    else if (channel == C_ALL)
    {
        thrStats[C_RED]=thrStats[C_GREEN]=thrStats[C_BLUE]=thrStats[C_GREEN2]=0;
        for (int row=0; row<rawHeight; ++row)
            for (int col=0; col<rawWidth; ++col)
            {
                EChannel channel = ui.rawImage->getRawColor(row,col);
                if (threshold[channel]>0 &&
                    fabs(avgVal[channel]-ui.rawImage->getRawValue(row,col))>threshold[channel])
                    thrStats[channel]++;
            }
    }
    else
    {
        thrStats[channel] = 0;
        int8_t st[][2] = { {0,0}, {0,1}, {1,0}, {1,1} };
        int i=0;
        while (i<4 && channel != ui.rawImage->getRawColor(st[i][0],st[i][1]))
            ++i;
        if (i>3)
            return;
        for (int row=st[i][0]; row<rawHeight; row+=2)
            for (int col=st[i][1]; col<rawWidth; col+=2)
            {
                if (ui.rawImage->isDefectPoint(row,col))
                    continue;

                if (threshold[channel]>0 &&
                    fabs(avgVal[channel]-ui.rawImage->getRawValue(row,col))>threshold[channel])
                    thrStats[channel]++;
            }
    }

    if (channel == C_ALL)
    {
        ui.lblStatsDefR->setText(fmtStr.arg(thrStats[C_RED]));
        ui.lblStatsDefG->setText(fmtStr.arg(thrStats[C_GREEN]));
        ui.lblStatsDefB->setText(fmtStr.arg(thrStats[C_BLUE]));
        ui.lblStatsDefG2->setText(fmtStr.arg(thrStats[C_GREEN2]));
    }
    else if (channel==C_RED)
        ui.lblStatsDefR->setText(fmtStr.arg(thrStats[C_RED]));
    else if (channel==C_GREEN)
        ui.lblStatsDefG->setText(fmtStr.arg(thrStats[C_GREEN]));
    else if (channel==C_BLUE)
        ui.lblStatsDefB->setText(fmtStr.arg(thrStats[C_BLUE]));
    else if (channel==C_GREEN2)
        ui.lblStatsDefG2->setText(fmtStr.arg(thrStats[C_GREEN2]));

    updateAutoRemap();
}

void IIQRemap::autoRemap()
{
    if (ui.chkAdaptiveRemap->checkState()==Qt::Checked)
    {
        if (ui.rawImage->performAdaptiveAutoRemap(
                        threshold,
                        blockSize(ui.cbAdaptiveBlock->currentIndex())))
        {
            processRawData();
            updateThresholdStats(C_ALL);
            updateWidgets();
            updateDefectStats();
        }
    }
    else if (ui.rawImage->performAvgAutoRemap(avgVal, threshold))
    {
        processRawData();
        updateThresholdStats(C_ALL);
        updateWidgets();
        updateDefectStats();
    }
}

void IIQRemap::deleteShownDefects()
{
    if (showMessage(tr("Warning"),
                    tr("This will remove all selected types of remapped\n"
                       "defects for currently loaded calibration file!\n"
                       "You can always go back by pressing \"Reset\"."),
                    tr("Are you sure you want to continue?"),
                    QMessageBox::Question,
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes) == QMessageBox::Yes)
    {
        ui.rawImage->eraseEnabledDefects();
        updateWidgets();
        updateDefectStats();
    }
}

void IIQRemap::help()
{
    QDir dir(QApplication::applicationDirPath());

#ifdef Q_OS_MACX
    dir.cdUp();
#endif

    if (dir.cd("help"))
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir.filePath("help_en.html")));
}

void IIQRemap::about()
{
    About about;

    about.exec();
}

void IIQRemap::zoomFit()
{
    ui.cboxZoomLevel->setCurrentIndex(0);
}

void IIQRemap::zoomFull()
{
    ui.cboxZoomLevel->setCurrentIndex(zoomLevelListFull);
}

void IIQRemap::zoomIn()
{
    int i=0;
    while (i<zoomLevelList.size() && scale*100 >= zoomLevelList[i])
        ++i;

    if (i<zoomLevelList.size())
    {
        ui.cboxZoomLevel->setCurrentIndex(i+1);
    }
}

void IIQRemap::zoomOut()
{
    int i=0;
    while (i<zoomLevelList.size() && scale*100 >= zoomLevelList[i])
        ++i;

    if (--i>0)
    {
        ui.cboxZoomLevel->setCurrentIndex(i);
    }
}

void IIQRemap::setZoomLevel(int cbIndex)
{
    if (cbIndex >= 0)
    {
        double horValue=0, vertValue=0;
        bool resizable = true;

        if (cbIndex == zoomLevelListFull)
            ui.btnZoom100->setEnabled(false);
        else
            ui.btnZoom100->setEnabled(true);

        if (cbIndex == 1)
            ui.btnZoomOut->setEnabled(false);
        else
            ui.btnZoomOut->setEnabled(true);

        if (cbIndex+1 == ui.cboxZoomLevel->count())
            ui.btnZoomIn->setEnabled(false);
        else
            ui.btnZoomIn->setEnabled(true);

        QScrollBar *hBar = ui.rawScrollArea->horizontalScrollBar();
        QScrollBar *vBar = ui.rawScrollArea->verticalScrollBar();

        // disable updates
        ui.rawScrollArea->setUpdatesEnabled(false);

        // check the types of zoom
        if (cbIndex == 0)
        {
            ui.btnZoomFit->setEnabled(false);
	        ui.rawScrollArea->setWidgetResizable(true);
            scale = fitScale(ui.rawImage->getRawWidth(), ui.rawImage->getRawHeight(), *ui.rawImage);
        }
        else
        {
            ui.btnZoomFit->setEnabled(true);
            resizable = false;
            scale = (double)zoomLevelList[cbIndex-1]/100;

            horValue = getScrollBarRelPos(hBar);
            vertValue = getScrollBarRelPos(vBar);

            // setWidgetResizable() updates the scrollbars so it has to
            // setWidgetResizable() updates the scrollbars so it has to
            // be called after we read their values
			ui.rawScrollArea->setWidgetResizable(false);
        }

        ui.rawImage->setScale(scale);

        if (cbIndex != 0)
        {
            // the viewport may not updated yet - it is delayed to layout engine
            QSize max = ui.rawScrollArea->maximumViewportSize();
            QSize vSize = ui.rawScrollArea->viewport()->size();
            QSize wSize = ui.rawImage->sizeHint();

            if (max.width()==vSize.width() && wSize.width()>max.width())
            {
                vSize.rwidth() -= vBar->width();
                hBar->setRange(0, wSize.width() - vSize.width());
                hBar->setPageStep(vSize.width());
            }
            if (max.height()==vSize.height() && wSize.height()>max.height())
            {
                vSize.rheight() -= hBar->height();
                vBar->setRange(0, wSize.height() - vSize.height());
                vBar->setPageStep(vSize.height());
            }

            setScrollBarRelPos(hBar, horValue);
            setScrollBarRelPos(vBar, vertValue);
        }

        ui.rawScrollArea->setUpdatesEnabled(true);

    }
}

void IIQRemap::applyDefectCorr(int state)
{
    ui.rawImage->setDefectCorr(state==Qt::Checked);
    processRawData();
    updateThresholdStats(C_ALL);
    updateWidgets();
    updateDefectStats();
}

void IIQRemap::gammaChecked(int state)
{
    ui.rawImage->enableGammaCorrection(state==Qt::Checked);
}

void IIQRemap::blackLevelZeroed(int state)
{
    ui.rawImage->enableBlackLevelZeroed(state==Qt::Checked);
}

void IIQRemap::redChecked(int state)
{
    ui.rawImage->enableChannel(state==Qt::Checked, C_RED);
}

void IIQRemap::greenChecked(int state)
{
    ui.rawImage->enableChannel(state==Qt::Checked, C_GREEN);
}

void IIQRemap::blueChecked(int state)
{
    ui.rawImage->enableChannel(state==Qt::Checked, C_BLUE);
}

void IIQRemap::rawRenderingChanged(bool checked)
{
    if (checked)
    {
        if (ui.radioRGB->isChecked())
            ui.rawImage->setRawRenderingType(R_RGB);
        else if (ui.radioComposite->isChecked())
            ui.rawImage->setRawRenderingType(R_COMPOSITE_COLOUR);
        else if (ui.radioCompGray->isChecked())
            ui.rawImage->setRawRenderingType(R_COMPOSITE_GRAY);
    }
}

void IIQRemap::adjustContrastMidpoint(int value)
{
    double midpoint = (double)value/ui.sldrContrastPoint->maximum();
    if (midpoint==0.0)
        midpoint=0.01;
    if (midpoint==1.0)
        midpoint=0.99;
    ui.rawImage->setContrMidpoint(midpoint);
}

void IIQRemap::adjustContrast(int value)
{
    // make contrast rise nonlinear - slower initially and faster towards end
    ui.rawImage->setContrCorr(pow((double)value/ui.sldrContrast->maximum(), 1.41));
}

void IIQRemap::adjustExposure(double value)
{
    QObject *sender = QObject::sender();
    double factor = pow(2, value);

    if (sender==expControls[C_ALL])
        ui.rawImage->setExpCorr(factor, C_ALL);
    else if (sender==expControls[C_RED])
        ui.rawImage->setExpCorr(factor, C_RED);
    else if (sender==expControls[C_GREEN])
        ui.rawImage->setExpCorr(factor, C_GREEN);
    else if (sender==expControls[C_BLUE])
        ui.rawImage->setExpCorr(factor, C_BLUE);
    else if (sender==expControls[C_GREEN2])
        ui.rawImage->setExpCorr(factor, C_GREEN2);
}

void IIQRemap::adjustBlack(int value)
{
    QObject *sender = QObject::sender();

    if (sender==ui.spinBlckRed)
        ui.rawImage->setBlack(value, C_RED);
    else if (sender==ui.spinBlckGreen)
        ui.rawImage->setBlack(value, C_GREEN);
    else if (sender==ui.spinBlckBlue)
        ui.rawImage->setBlack(value, C_BLUE);
    else if (sender==ui.spinBlckGreen2)
        ui.rawImage->setBlack(value, C_GREEN2);
}

void IIQRemap::adjustThreshold(int value)
{
    if (lockThresChange)
        return;

    QObject *sender = QObject::sender();
    EChannel channel = C_ALL;
    if (sender==ui.spbThrRed)
        channel = C_RED;
    else if (sender==ui.spbThrGreen)
        channel = C_GREEN;
    else if (sender==ui.spbThrBlue)
        channel = C_BLUE;
    else if (sender==ui.spbThrGreen2)
        channel = C_GREEN2;

    if (channel!=C_ALL)
    {
        threshold[channel] = value;
        updateThresholdStats(channel);
    }
}

void IIQRemap::adaptiveRemapModeChecked(int state)
{
    ui.frmAdaptiveBlock->setEnabled(state==Qt::Checked);
    updateThresholdStats(C_ALL);
}

void IIQRemap::adjustAdaptiveBlockSize(int value)
{
    updateThresholdStats(C_ALL);
}

void IIQRemap::setWB()
{
    ui.rawImage->pauseUpdates(true);

    ui.expRedSpin->setValue(log(camWB[C_RED])/log(2.0));
    ui.expGreenSpin->setValue(log(camWB[C_GREEN])/log(2.0));
    ui.expBlueSpin->setValue(log(camWB[C_BLUE])/log(2.0));
    ui.expGreen2Spin->setValue(log(camWB[C_GREEN2])/log(2.0));

    ui.rawImage->setWB(camWB);

    ui.rawImage->pauseUpdates(false);
}

void IIQRemap::resetAdjustments()
{
    ui.rawImage->pauseUpdates(true);

    ui.sldrContrast->setValue(0);
    ui.sldrContrastPoint->setValue(ui.sldrContrastPoint->maximum()/2);

    ui.expAll->setValue(0);
    ui.expRed->setValue(0);
    ui.expGreen->setValue(0);
    ui.expBlue->setValue(0);
    ui.expGreen2->setValue(0);

    ui.spinBlckRed->setValue(0);
    ui.spinBlckGreen->setValue(0);
    ui.spinBlckBlue->setValue(0);
    ui.spinBlckGreen2->setValue(0);

    ui.rawImage->resetAllCorrections();
    ui.rawImage->pauseUpdates(false);
}

void IIQRemap::updateDefectStats()
{
    QString fmtStr("%1");

    if (!ui.rawImage->hasCalFile())
        return;

    ui.lblStPoints->setText(fmtStr.arg(ui.rawImage->getDefectPoints()));
    ui.lblStCols->setText(fmtStr.arg(ui.rawImage->getDefectCols()));
}

void IIQRemap::updateStatus(uint16_t row, uint16_t col)
{
    QString fmtStr("%1");
    EChannel channel = ui.rawImage->getRawColor(row,col);
    uint16_t value = ui.rawImage->getRawValue(row,col);
    ERawRendering curRendMode = ui.rawImage->getRawRenderingType();

    ui.lblStRow->setText(fmtStr.arg(row, -5));
    ui.lblStCol->setText(fmtStr.arg(col, -5));

    // pattern is
    //    G R
    //    B G
    switch (channel)
    {
        case C_RED:
            ui.lblStR->setText(fmtStr.arg(value, -5));
            if (curRendMode==R_RGB)
            {
                uint16_t green = (ui.rawImage->getRawValue(row,col-1)+
                                ui.rawImage->getRawValue(row+1,col) + 1)>>1;

                ui.lblStG->setText(fmtStr.arg(green, -5));
                ui.lblStB->setText(fmtStr.arg(ui.rawImage->getRawValue(row+1,col-1), -5));
                ui.lblStG2->setText(fmtStr.arg(green, -5));
            }
            else
            {
                ui.lblStG->setText(fmtStr.arg(0, -5));
                ui.lblStB->setText(fmtStr.arg(0, -5));
                ui.lblStG2->setText(fmtStr.arg(0, -5));
            }
            break;
        case C_GREEN:
            if (curRendMode==R_RGB)
            {
                uint16_t green = (value+ui.rawImage->getRawValue(row+1,col+1) + 1)>>1;
                ui.lblStR->setText(fmtStr.arg(ui.rawImage->getRawValue(row,col+1), -5));
                ui.lblStG->setText(fmtStr.arg(green, -5));
                ui.lblStB->setText(fmtStr.arg(ui.rawImage->getRawValue(row+1,col), -5));
                ui.lblStG2->setText(fmtStr.arg(green, -5));
            }
            else
            {
                ui.lblStR->setText(fmtStr.arg(0, -5));
                ui.lblStG->setText(fmtStr.arg(value, -5));
                ui.lblStB->setText(fmtStr.arg(0, -5));
                ui.lblStG2->setText(fmtStr.arg(0, -5));
            }
            break;
        case C_BLUE:
            ui.lblStB->setText(fmtStr.arg(value, -5));
            if (curRendMode==R_RGB)
            {
                uint16_t green = (ui.rawImage->getRawValue(row-1,col)+
                                ui.rawImage->getRawValue(row,col+1) + 1)>>1;
                ui.lblStR->setText(fmtStr.arg(ui.rawImage->getRawValue(row-1,col+1), -5));
                ui.lblStG->setText(fmtStr.arg(green, -5));
                ui.lblStG2->setText(fmtStr.arg(green, -5));
            }
            else
            {
                ui.lblStR->setText(fmtStr.arg(0, -5));
                ui.lblStG->setText(fmtStr.arg(0, -5));
                ui.lblStG2->setText(fmtStr.arg(0, -5));
            }
            break;
        case C_GREEN2:
            if (curRendMode==R_RGB)
            {
                uint16_t green = (value+ui.rawImage->getRawValue(row-1,col-1) + 1)>>1;
                ui.lblStR->setText(fmtStr.arg(ui.rawImage->getRawValue(row-1,col), -5));
                ui.lblStG->setText(fmtStr.arg(green, -5));
                ui.lblStB->setText(fmtStr.arg(ui.rawImage->getRawValue(row,col-1), -5));
                ui.lblStG2->setText(fmtStr.arg(green, -5));
            }
            else
            {
                ui.lblStR->setText(fmtStr.arg(0, -5));
                ui.lblStG->setText(fmtStr.arg(0, -5));
                ui.lblStB->setText(fmtStr.arg(0, -5));
                ui.lblStG2->setText(fmtStr.arg(value, -5));
            }
            break;
        case C_ALL:
            break;
    }
}

void IIQRemap::green2Checked(int state)
{
    ui.rawImage->enableChannel(state==Qt::Checked, C_GREEN2);
}

void IIQRemap::pointDefectModeChecked(bool checked)
{
    if (lockModeChange)
        return;

    if (checked)
    {
        // points remap on
        ui.rawImage->setDefectSettingMode(M_POINT);
        lockModeChange = true;
        ui.btnColMode->setChecked(false);
        lockModeChange = false;

        if (ui.chkShowPoints->checkState()!=Qt::Checked)
            ui.chkShowPoints->setCheckState(Qt::Checked);
    }
    else
        ui.rawImage->setDefectSettingMode(M_NONE);
}

void IIQRemap::colDefectModeChecked(bool checked)
{
    if (lockModeChange)
        return;

    if (checked)
    {
        // cols remap on
        ui.rawImage->setDefectSettingMode(M_COL);
        lockModeChange = true;
        ui.btnPointMode->setChecked(false);
        lockModeChange = false;

        if (ui.chkShowCols->checkState()!=Qt::Checked)
            ui.chkShowCols->setCheckState(Qt::Checked);
    }
    else
        ui.rawImage->setDefectSettingMode(M_NONE);
}

void IIQRemap::changeDefColour(QColor* colour)
{
    if (!colour)
    {
        QColor tmpColour = QColorDialog::getColor(defectColour,
                                                  0,
                                                  tr("Choose colour for displaying defects"));

        if (tmpColour.isValid())
        {
            defectColour = tmpColour;
            colour = &defectColour;
        }
    }

    if (colour)
    {
        ui.rawImage->setDefectColour(*colour);

        QPalette palette = ui.btnDefColour->palette();

        palette.setColor(QPalette::Button, *colour);

        ui.btnDefColour->setPalette(palette);
    }
}

void IIQRemap::showPointsChecked(int state)
{
    ui.rawImage->enableDefPoints(state==Qt::Checked);

    if (state!=Qt::Checked && ui.btnPointMode->isChecked())
        ui.btnPointMode->setChecked(false);

    updateDefectStats();
}

void IIQRemap::showColsChecked(int state)
{
    ui.rawImage->enableDefCols(state==Qt::Checked);

    if (state!=Qt::Checked && ui.btnColMode->isChecked())
        ui.btnColMode->setChecked(false);

    updateDefectStats();
}

void IIQRemap::defectsChanged()
{
    if (!ui.rawImage->hasCalFile())
        return;
    updateDefectStats();
    updateWidgets();
}

// -------------------------------------------------------------------------
//   Fusion proxy style to disable stupid QStyle::SH_ComboBox_Popup
// -------------------------------------------------------------------------
class DCSProxyStyle : public QProxyStyle
{
public:
    DCSProxyStyle(QStyle *style): QProxyStyle(style) {}

    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *returnData) const
    {
        if (hint == QStyle::SH_ComboBox_Popup)
        {
            return 0;
        }
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    void polish (QWidget *w)
    {
#ifdef Q_OS_MACX
        QMenu* mn = qobject_cast<QMenu*>(w);
        if (!mn && !w->testAttribute(Qt::WA_MacNormalSize))
            w->setAttribute(Qt::WA_MacSmallSize);
#endif
    }
};

// -------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

#if QT_VERSION >= 0x050000
	app.setStyle(new DCSProxyStyle(QStyleFactory::create("fusion")));
#endif

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(83,83,83));
    palette.setColor(QPalette::WindowText, Qt::white);
    palette.setColor(QPalette::Base, QColor(63,63,63));
    palette.setColor(QPalette::AlternateBase, QColor(83,83,83));
    palette.setColor(QPalette::ToolTipBase, QColor(94,180,255));
    palette.setColor(QPalette::ToolTipText, Qt::black);
    palette.setColor(QPalette::Text, Qt::white);
    palette.setColor(QPalette::Button, QColor(83,83,83));
    palette.setColor(QPalette::ButtonText, Qt::white);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Highlight, QColor(51,153,255));
    palette.setColor(QPalette::HighlightedText, Qt::black);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, Qt::gray);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::gray);

    app.setPalette(palette);


    IIQRemap remapMain;
    remapMain.show();

    return app.exec();
}

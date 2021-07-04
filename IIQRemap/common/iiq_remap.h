/*
    iiq_remap.h - mainform class to deal with QT application

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
#ifndef IIQ_REMAP_H
#define IIQ_REMAP_H

#include "raw_image.h"

#include <QMainWindow>
#include <QMessageBox>
#include <QLabel>
#include <QPoint>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "ui_iiq_remap.h"

#define APP_NAME "IIQ Remap"

// --------------------------------------------------------
//    Helper integrator class
// --------------------------------------------------------
class SpinBoxSliderIntegrator : public QObject
{
    Q_OBJECT

    QAbstractSlider *slider_;
    QDoubleSpinBox *spinBox_;

    double value_;
    double sliderScale_;
    double initValue_;
    bool lockSetValue_;

    int spinBox2Slide(double spinVal)
    {
        int sliderVal = roundToInt(sliderScale_*spinVal);
        if (sliderVal > slider_->maximum())
            sliderVal -= slider_->maximum() + 1;
        return sliderVal;
    }

    double slide2SpinBox(int sliderVal)
    {
        double spinVal = sliderVal/sliderScale_;
        if (spinVal < spinBox_->minimum())
            spinVal += spinBox_->maximum() + 1;
        return spinVal;
    }

    void valueUpdated()
    {
        if (value_ != spinBox_->value())
        {
            // only emit if value has changed
            value_ = spinBox_->value();
            Q_EMIT valueChanged(value_);
        }
    }

public:

    SpinBoxSliderIntegrator(QDoubleSpinBox *spinBox, QAbstractSlider *slider, QToolButton *resetButton = 0)
    {
        slider_ = slider;
        spinBox_ = spinBox;

        lockSetValue_ = false;

        sliderScale_ = (double)(slider_->maximum() - slider_->minimum())/(spinBox_->maximum() - spinBox_->minimum());

        initValue_ = spinBox_->value();

        int singleStep = roundToInt(spinBox_->singleStep()*sliderScale_);
        singleStep = singleStep ? singleStep : 1;

        slider_->setSingleStep(singleStep);
        slider_->setPageStep(10*singleStep);

        connect(slider_, SIGNAL(valueChanged(int)), this, SLOT(sliderValueChanged(int)));
        connect(slider_, SIGNAL(sliderMoved(int)), this, SLOT(sliderValueChanged(int)));
        connect(spinBox_, SIGNAL(valueChanged(double)), this, SLOT(spinboxValueChanged(double)));

        value_ = spinBox_->value();

        if (resetButton)
            connect(resetButton, SIGNAL(clicked()), this, SLOT(reset()));
    }


    double value()
    {
        return value_;
    }

    void setValue(double v)
    {
        spinBox_->setValue(v);
    }

public Q_SLOTS:

    void spinboxValueChanged(double value)
    {
        if (!lockSetValue_)
        {
            lockSetValue_ = true;
            slider_->setValue(spinBox2Slide(value));
            lockSetValue_ = false;
        }

        // notify updated value if slider was not moving
        if (slider_->value() == slider_->sliderPosition())
            valueUpdated();
    }

    void sliderValueChanged(int value)
    {
        if (!lockSetValue_)
        {
            lockSetValue_ = true;
		    spinBox_->setValue(slide2SpinBox(value));
            lockSetValue_ = false;
        }

        // notify updated value if slider was not moving
        if (slider_->value() == value)
            valueUpdated();
    }

    void reset()
    {
        spinBox_->setValue(initValue_);
    }

Q_SIGNALS:
    void valueChanged(double value);
};


// --------------------------------------------------------
//    IIQRemap class
// --------------------------------------------------------
class IIQRemap : public QMainWindow
{
	Q_OBJECT

    // member variables
    Ui::IIQRemap ui;

    // Defects colour
    QColor defectColour;

    SpinBoxSliderIntegrator* expControls[5];
    SpinBoxSliderIntegrator* wbControls[3];
    SpinBoxSliderIntegrator* wbExpControl;

    double camWB[4];

    // raw per-channel stats
    uint16_t maxVal[4];
    uint16_t minVal[4];
    double stdDev[4];
    double avgVal[4];

    uint16_t threshold[4];
    uint32_t thrStats[4];

    QString curRawPath;
    QString rawFileName;
    QString curCalPath;
    QString calFileName;

    QIcon tickIcon;
    QIcon tickEmptyIcon;

    double scale;

    bool lockModeChange;
    bool lockThresChange;
    bool overrideCursorSet;

public:
	IIQRemap();
	~IIQRemap();

private:

    void closeEvent(QCloseEvent *event);

    int showMessage(const QString& title,
                    const QString& msgText,
                    const QString& informativeText=tr(""),
                    QMessageBox::Icon icon=QMessageBox::Critical,
                    QMessageBox::StandardButtons buttons = QMessageBox::NoButton,
                    QMessageBox::StandardButton defButton = QMessageBox::NoButton);

    void setOverrideCursor(const QCursor& cursor)
    {
        if (!overrideCursorSet)
        {
            overrideCursorSet = true;
            QApplication::setOverrideCursor(cursor);
            QApplication::processEvents();
        }
    }

    void restoreOverrideCursor()
    {
        if (overrideCursorSet)
        {
            overrideCursorSet = false;
            QApplication::restoreOverrideCursor();
        }
    }

    void processRawData();
    void resizeEvent(QResizeEvent *event);
    void updateRawStats();
    void updateDefectStats();
    void updateThresholdStats(EChannel channel);
    void updateAutoRemap();
    void updateWidgets();
    int  loadRawStack(IIQFile& file, QStringList fileNames);
    bool checkUnsavedAndSave();
    bool accepLicense();
    bool saveCal();

    void init();

private Q_SLOTS:

    void openCalFile();
    void saveCalFile();
    void discardChanges();

    void loadRaw();

    void autoRemap();
    void calculateThresholds();
    void deleteShownDefects();

    void changeDefColour(QColor* colour = 0);

    void help();
    void about();

    void zoomFit();
    void zoomFull();
    void zoomIn();
    void zoomOut();
    void setZoomLevel(int cbIndex);

    void applyDefectCorr(int state);

    void gammaChecked(int state);
    void blackLevelZeroed(int state);

    void redChecked(int state);
    void greenChecked(int state);
    void blueChecked(int state);
    void green2Checked(int state);

    void showPointsChecked(int state);
    void showColsChecked(int state);

    void rawRenderingChanged(bool checked);

    void adjustContrast(int value);
    void adjustContrastMidpoint(int value);
    void adjustExposure(double value);
    void adjustBlack(int value);

    void adaptiveRemapModeChecked(int state);
    void adjustAdaptiveBlockSize(int value);

    void adjustThreshold(int value);

    void pointDefectModeChecked(bool checked);
    void colDefectModeChecked(bool checked);

    void updateStatus(uint16_t row, uint16_t col);
    void defectsChanged();

    void setWB();

    void resetAdjustments();
};

#endif

/*
    iiqcal.h - Phase One IIQ and calibration file read/write classes

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

#ifndef IIQ_CAL_H
#define IIQ_CAL_H

#include <libraw.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

// IIQ calibration file class
class IIQCalFile
{
public:
    // datatypes
#if defined(WIN32) || defined(_WIN32)
    using TFileNameType = std::wstring;
#else
    using TFileNameType = std::string;
#endif
    using TDefPixels = std::set<std::pair<int,int>>;
    using TDefCols = std::set<int>;

    // Initialisers/destructors
    IIQCalFile() : convEndian_(false), hasChanges_{false,false}, hasSensorPlus_(false) {};
    IIQCalFile(const uint8_t* data, const size_t size);
    IIQCalFile(const TFileNameType& fileName);
    ~IIQCalFile() = default;

    // Assignment
    IIQCalFile& operator=(const IIQCalFile& from);
    IIQCalFile& operator=(const IIQCalFile&& from);

    // Swapping
    void swap(IIQCalFile& from);
    void swap(IIQCalFile& from, bool sensorPlus);
    void swap(IIQCalFile&& from, bool sensorPlus);
    void merge(IIQCalFile& from) { swap(from, hasSensorPlus_ && !valid(true)); }

    // Cal files are the same when serial matches
    bool operator==(const IIQCalFile &cal) const
        { return calSerial_ == cal.calSerial_; }

    // Files are mergable when they are for the same serial
    // and have opposite parts present
    bool mergable(const IIQCalFile &cal) const
        { return calSerial_ == cal.calSerial_ && hasSensorPlus_ &&
                 valid(false) != cal.valid(false) &&
                 valid(true) != cal.valid(true) &&
                 valid(false) != valid(true); }

    // Save changes back to the file (overrides existing file)
    bool saveCalFile();
    bool saveToData(std::vector<uint8_t>& calData, bool sensorPlus) const
        { return rebuildCalFileData(calData, sensorPlus); }

    // Reset any changes and repopulate defects from last saved
    void reset() { parseCalFileData(false); parseCalFileData(true); }

    // Getters for cal data
    const std::string& getCalSerial() const
        { return calSerial_; }
    const TFileNameType& getCalFileName() const
        { return calFileName_; }
    const TDefPixels& getDefectPixels(bool sensorPlus) const
        { return defPixels_[sensorPlus]; }
    const TDefCols& getDefectCols(bool sensorPlus) const
        { return defCols_[sensorPlus]; }

    // Setting file for saving
    void setCalFileName(const TFileNameType& fileName) { calFileName_ = fileName; }

    // Defect checks
    bool isDefPixel(int col, int row, bool sensorPlus) const
        { return defPixels_[sensorPlus].find({col, row}) != defPixels_[sensorPlus].end(); }
    bool isDefCol(int col, bool sensorPlus) const
        { return defCols_[sensorPlus].find(col) != defCols_[sensorPlus].end(); }

    // Defect modifiers
    bool addDefPixel(int col, int row, bool sensorPlus)
        { return defPixels_[sensorPlus].emplace(col, row).second ? hasChanges_[sensorPlus]=true : false; }
    bool addDefCol(int col, bool sensorPlus)
        { return defCols_[sensorPlus].emplace(col).second ? hasChanges_[sensorPlus]=true : false; }

    // Pixel removal:
    //  - if row is negative, remove all pixels with that col
    //  - if col is negative, clear all pixels
    bool removeDefPixel(int col, int row, bool sensorPlus);

    // Column removal:
    //  - if col is negative, clear all cols
    bool removeDefCol(int col, bool sensorPlus);

    bool hasUnsavedChanges() const { return hasChanges_[0] || hasChanges_[hasSensorPlus_]; }
    bool valid(bool sensorPlus) const { return !calTags_[sensorPlus].empty(); }
    bool valid() const { return valid(false) || valid(hasSensorPlus_); }
    bool fullyValid() const { return valid(false) && valid(hasSensorPlus_); }

    bool hasSensorPlus() const { return hasSensorPlus_; }

    // Access to raw file data
    const std::vector<uint8_t>& getCalFileData(bool sensorPlus) const { return calFileData_[sensorPlus]; };

private:
    // private functions
    void initCalData(const uint8_t* data, const size_t size);
    void parseCalFileData(bool sensorPlus);
    bool rebuildCalFileData(std::vector<uint8_t>& calFileData, bool sensorPlus) const;

    // data members
    TDefPixels defPixels_[2];
    TDefCols defCols_[2];
    std::string calSerial_;
    TFileNameType calFileName_;
    std::vector<uint8_t> calFileData_[2];
    std::set<uint32_t> calTags_[2];
    bool hasChanges_[2];
    bool convEndian_;
    bool hasSensorPlus_;
};

// IIQ raw file class
class IIQFile: public LibRaw
{
public:

    IIQFile(): LibRaw(), calData_(nullptr),
                         calDataEnd_(nullptr),
                         calDataCurPtr_(nullptr),
                         convEndian_(false) {}
    ~IIQFile();

    IIQCalFile getIIQCalFile();

    bool isSensorPlus();

    // Applies Phase One corrections
    void applyPhaseOneCorr(const IIQCalFile& calFile, bool sensorPlus, bool applyDefects);

    const std::string getPhaseOneSerial()
    {
        return is_phaseone_compressed() ? imgdata.shootinginfo.BodySerial : "";
    }

    // public pixel access
    uint16_t getRAW(int row, int col) const
    { return imgdata.rawdata.raw_image[(row+imgdata.sizes.top_margin)*imgdata.sizes.raw_width +
                                        col + imgdata.sizes.left_margin]; }

    uint16_t& RAW(uint16_t row, uint16_t col)
    { return imgdata.rawdata.raw_image[row*imgdata.sizes.raw_width + col]; }

    uint16_t RAW(uint16_t row, uint16_t col) const
    { return imgdata.rawdata.raw_image[row*imgdata.sizes.raw_width + col]; }

    bool isPhaseOne() { return is_phaseone_compressed(); }

private:
    // moved from LibRaw internal ones
    int p1rawc(unsigned row, unsigned col, unsigned& count) const;
    int p1raw(unsigned row, unsigned col) const;
    void phase_one_fix_col_pixel_avg(unsigned row, unsigned col);
    void phase_one_fix_pixel_grad(unsigned row, unsigned col);
    void phase_one_flat_field(int is_float, int nc);
    int phase_one_correct(bool applyDefects = true);

    uint32_t dataGetPos() { return calDataCurPtr_-calData_; }
    void dataSetPos(uint32_t pos, bool fromCur = false);
    void getShorts(uint16_t *shorts, uint32_t count);
    uint16_t get16();
    uint32_t get32();
    float getFloat();

    // members
    const uint8_t* calData_;
    const uint8_t* calDataEnd_;
    const uint8_t* calDataCurPtr_;
    bool convEndian_;
};

#endif

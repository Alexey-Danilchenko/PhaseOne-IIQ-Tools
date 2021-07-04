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
    using TDefPixels = std::set<std::pair<int,int>>;
    using TDefCols = std::set<int>;

    // Initialisers/destructors
    IIQCalFile() : convEndian_(false), hasChanges_(false) {};
    IIQCalFile(const uint8_t* data, const size_t size);
    IIQCalFile(const std::string& fileName);
    ~IIQCalFile() = default;

    // Assignment
    IIQCalFile& operator=(const IIQCalFile& from);
    IIQCalFile& operator=(const IIQCalFile&& from);

    // Swapping
    void swap(IIQCalFile& from);

    // Cal files are the same when serial matches
    bool operator==(const IIQCalFile &cal) const
    { return calSerial_ == cal.calSerial_; }

    // Save changes back to the file (overrides existing file)
    bool saveCalFile();
    bool saveToData(std::vector<uint8_t>& calData) const { return rebuildCalFileData(calData); }

    // Reset any changes and repopulate defects from last saved
    void reset() { parseCalFileData(); }

    // Getters for cal data
    const std::string& getCalSerial() const   { return calSerial_; }
    const std::string& getCalFileName() const { return calFileName_; }
    const TDefPixels& getDefectPixels() const { return defPixels_; }
    const TDefCols& getDefectCols() const     { return defCols_; }

    // Setting file for saving
    void setCalFileName(const std::string& fileName) { calFileName_ = fileName; }

    // Defect checks
    bool isDefPixel(int col, int row) const { return defPixels_.find({col, row}) != defPixels_.end(); }
    bool isDefCol(int col) const { return defCols_.find(col) != defCols_.end(); }

    // Defect modifiers
    bool addDefPixel(int col, int row)
    { return defPixels_.emplace(col, row).second ? hasChanges_=true : false; }
    bool addDefCol(int col)
    { return defCols_.emplace(col).second ? hasChanges_=true : false; }

    // Pixel removal:
    //  - if row is negative, remove all pixels with that col
    //  - if col is negative, clear all pixels
    bool removeDefPixel(int col, int row);

    // Column removal:
    //  - if col is negative, clear all cols
    bool removeDefCol(int col);

    bool hasUnsavedChanges() const { return hasChanges_; }
    bool valid() const { return !calTags_.empty(); }

    // Access to raw file data
    const std::vector<uint8_t>& getCalFileData() const { return calFileData_; };

private:
    // private functions
    void parseCalFileData();
    bool rebuildCalFileData(std::vector<uint8_t>& calFileData) const;

    // data members
    TDefPixels defPixels_;
    TDefCols defCols_;
    std::string calSerial_;
    std::string calFileName_;
    std::vector<uint8_t> calFileData_;
    std::set<uint32_t> calTags_;
    bool convEndian_;
    bool hasChanges_;
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

    // Applies Phase One corrections
    void applyPhaseOneCorr(const IIQCalFile& calFile, bool applyDefects = true);

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

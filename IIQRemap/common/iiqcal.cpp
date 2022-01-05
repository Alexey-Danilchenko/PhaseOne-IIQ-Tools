/*
    iiqcal.cpp - Phase One IIQ and calibration file read/write classes

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

#include "iiqcal.h"

#include <QString>

#include <ctime>
#include <cstdio>
#include <filesystem>

#pragma pack(push)
#pragma pack(1)

// All the structures for the types in binary files go in C style declaration
// to maintain struct packing and avoid C++ alignment/padding

//
// The Phase One calibration file is a kind of TIFF file. They have a modified
// TIFF header with everything 32 bit folowed by tag data and single IFD.
// IFD is non standard and entries are similar to TIFF tag entries but are all
// in 32 bits and do not contain data type (data count is a size in bytes).
//
#define IIQ_BIGENDIAN      0x4d4d4d4d
#define IIQ_LITTLEENDIAN   0x49494949
#define CAL_FOOTER_MAGIC   0x504F4331

struct TIIQHeader
{
    uint32_t iiqMagic;   // Same as TIFF magic but 32bit
    uint32_t version;
    uint32_t dirOffset;
};

struct TSensorPlusFooter
{
    uint32_t calDataOffset;   // always zero
    uint32_t calSize;         // always size of the first or the only calibration
    uint32_t calNumber;       // calibration number (1 for the std, 2 for Sensor+)
    uint32_t totalCals;       // total number of calibrations (1 for the std, 2 for Sensor+)
    uint32_t modTimestamp;    // last modification timestamp
    uint32_t calFooterMagic;  // calibration footer magic 'POC1'
};

struct TSensorPlusTOC
{
    uint32_t calSize[2];  // each calibration size
    uint32_t totalCals;   // total number of calibrations - always
                          //   2 for Sensor+ sensors
};

struct TIiqCalTagEntry
{
    uint32_t tag;           // 0
    uint32_t sizeBytes;     // 4
    uint32_t data;          // 8
};

enum EIIQCalTag
{
    CAL_DefectCorrection       = 0x400,
    CAL_LumaAllColourFlatField = 0x401,
    CAL_TimeCreated            = 0x402,
    CAL_TimeModified           = 0x403,
    CAL_SerialNumber           = 0x407,
    CAL_BlackGain              = 0x408,
    CAL_ChromaRedBlue          = 0x40b,
    CAL_Luma                   = 0x410,
    CAL_XYZCorrection          = 0x412,
    CAL_LumaFlatField2         = 0x416,
    CAL_DualOutputPoly         = 0x419,
    CAL_PolynomialCurve        = 0x41a,
    CAL_KelvinCorrection       = 0x41c,
    CAL_OutputOffsetCorrection = 0x41b,
    CAL_FourTileOutput         = 0x41e,
    CAL_FourTileLinearisation  = 0x41f,
    CAL_OutputCorrectCurve     = 0x423,
    CAL_FourTileTracking       = 0x42C,
    CAL_FourTileGainLUT        = 0x431
};

// defect remap structures
struct TDefectEntry
{
    uint16_t col;
    uint16_t row;
    uint16_t defectType;
    uint16_t extra;
};

enum EDefectType
{
    DEF_PIXEL     = 129,
    DEF_COL       = 131,
    DEF_PIXEL_ROW = 132,
    DEF_PIXEL_ISO = 134,
    DEF_COL_2     = 137,
    DEF_COL_3     = 138,
    DEF_OTHER     = 139,
    DEF_COL_4     = 140
};

#pragma pack(pop)

// endian conversion
uint16_t convEndian16(uint16_t uValue, bool convert)
{
    if (!convert)
        return uValue;

    // convert endianness
    return (uValue << 8) | (uValue >> 8);
}

uint32_t convEndian32(uint32_t uValue, bool convert)
{
    if (!convert)
        return uValue;

    // convert endianness
    return (uValue << 24)             | ((uValue & 0xFF00) << 8) |
           ((uValue & 0xFF0000) >> 8) | (uValue >> 24);
}

uint64_t convEndian64(uint64_t uValue, bool convert)
{
    if (!convert)
        return uValue;

    // convert endianness
    return (uValue << 56)               | ((uValue & 0xFF00) << 40)    |
           ((uValue & 0xFF0000) << 24)  | ((uValue & 0xFF000000) << 8) |
           ((uValue >> 8) & 0xFF000000) | ((uValue >> 24) & 0xFF0000)  |
           ((uValue >> 40) & 0xFF00)    | (uValue >> 56);
}

// constructors and assignements
IIQCalFile::IIQCalFile(const uint8_t* data, const size_t size)
    : hasChanges_{false,false}, convEndian_(false), hasSensorPlus_(false)
{
    initCalData(data, size);
}

IIQCalFile::IIQCalFile(const IIQCalFile::TFileNameType& fileName)
    : calFileName_(fileName), hasChanges_{false,false},
      convEndian_(false), hasSensorPlus_(false)
{
    if (fileName.empty())
        return;

    try
    {
        if (uint32_t size = (uint32_t)std::filesystem::file_size(fileName))
        {
#if defined(WIN32) || defined(_WIN32)
            if (auto *file = _wfopen(calFileName_.c_str(), L"rb"))
#else
            if (auto *file = std::fopen(calFileName_.c_str(), "rb"))
#endif
            {
                std::vector<uint8_t> calFile(size);
                if (std::fread(calFile.data(), 1, size, file) == size)
                    initCalData(calFile.data(), size);
                std::fclose(file);
            }
        }
    }
    catch (...)
    {
        // invalid file
        calFileName_.clear();
    }
}

// Assignment
IIQCalFile& IIQCalFile::operator=(const IIQCalFile& from)
{
    calFileName_ = from.calFileName_;
    convEndian_ = from.convEndian_;
    hasSensorPlus_ = from.hasSensorPlus_;
    calSerial_ = from.calSerial_;
    for (int i=0; i<2; ++i)
    {
        calFileData_[i] = from.calFileData_[i];
        calTags_[i] = from.calTags_[i];
        defPixels_[i] = from.defPixels_[i];
        defCols_[i] = from.defCols_[i];
        hasChanges_[i] = from.hasChanges_[i];
    }

    return *this;
}

IIQCalFile& IIQCalFile::operator=(const IIQCalFile&& from)
{
    calFileName_ = std::move(from.calFileName_);
    convEndian_ = from.convEndian_;
    hasSensorPlus_ = from.hasSensorPlus_;
    calSerial_ = std::move(from.calSerial_);
    for (int i=0; i<2; ++i)
    {
        defPixels_[i] = std::move(from.defPixels_[i]);
        defCols_[i] = std::move(from.defCols_[i]);
        calTags_[i] = std::move(from.calTags_[i]);
        calFileData_[i] = std::move(from.calFileData_[i]);
        hasChanges_[i] = from.hasChanges_[i];
    }

    return *this;
}

void IIQCalFile::swap(IIQCalFile& from)
{
    calFileName_.swap(from.calFileName_);
    calSerial_.swap(from.calSerial_);
    std::swap(convEndian_, from.convEndian_);
    std::swap(hasSensorPlus_, from.hasSensorPlus_);
    swap(from, false);
    swap(from, true);
}

void IIQCalFile::swap(IIQCalFile& from, bool sensorPlus)
{
    calFileData_[sensorPlus].swap(from.calFileData_[sensorPlus]);
    calTags_[sensorPlus].swap(from.calTags_[sensorPlus]);
    defPixels_[sensorPlus].swap(from.defPixels_[sensorPlus]);
    defCols_[sensorPlus].swap(from.defCols_[sensorPlus]);
    std::swap(hasChanges_[sensorPlus], from.hasChanges_[sensorPlus]);
}

void IIQCalFile::swap(IIQCalFile&& from, bool sensorPlus)
{
    calFileData_[sensorPlus].swap(from.calFileData_[sensorPlus]);
    calTags_[sensorPlus].swap(from.calTags_[sensorPlus]);
    defPixels_[sensorPlus].swap(from.defPixels_[sensorPlus]);
    defCols_[sensorPlus].swap(from.defCols_[sensorPlus]);
    std::swap(hasChanges_[sensorPlus], from.hasChanges_[sensorPlus]);
}

// Pixel removal
//  - if row is negative, remove all pixels with that col
//  - if col is negative, clear all pixels
bool IIQCalFile::removeDefPixel(int col, int row, bool sensorPlus)
{
    bool deleted = false;
    if (col < 0)
    {
        defPixels_[sensorPlus].clear();
        deleted = true;
    }
    else if (row < 0)
    {
        auto it = defPixels_[sensorPlus].lower_bound({col, row});
        while (it != defPixels_[sensorPlus].end() && it->first == col)
        {
            deleted = true;
            it = defPixels_[sensorPlus].erase(it);
        }
    }
    else
        deleted = defPixels_[sensorPlus].erase({col, row}) == 1;

    if (deleted)
        hasChanges_[sensorPlus] = true;

    return deleted;
}

// Column removal
//  - if col is negative, clear all cols
bool IIQCalFile::removeDefCol(int col, bool sensorPlus)
{
    bool deleted = false;
    if (col < 0)
    {
        defCols_[sensorPlus].clear();
        deleted = true;
    }
    else
        deleted = defCols_[sensorPlus].erase(col) == 1;

    if (deleted)
        hasChanges_[sensorPlus] = true;

    return deleted;
}

// saving cal file
bool IIQCalFile::saveCalFile()
{
    if (calFileName_.empty() || !valid())
        return false;

    bool success = true;

    for (int i=0; success && i<=(int)hasSensorPlus_; ++i)
    {
        if (hasChanges_[i])
        {
            // first remove duplicate pixels
            for (auto col: defCols_[i])
                removeDefPixel(col, -1, i);

            // merge defects back into binary
            success = success && rebuildCalFileData(calFileData_[i], i);
        }
    }

    if (!success)
        return success;

    // save binary
#if defined(WIN32) || defined(_WIN32)
    if (auto *file = _wfopen(calFileName_.c_str(), L"wb"))
#else
    if (auto *file = std::fopen(calFileName_.c_str(), "wb"))
#endif
    {
        if (!calFileData_[0].empty())
            success = std::fwrite(calFileData_[0].data(), 1, calFileData_[0].size(), file)
                            == calFileData_[0].size();
        if (success && hasSensorPlus_)
        {
            // write sensor plus calibration if any
            if (!calFileData_[1].empty())
                success = std::fwrite(calFileData_[1].data(), 1, calFileData_[1].size(), file)
                                == calFileData_[1].size();

            // build and write footer
            if (success && (!calFileData_[0].empty() || !calFileData_[1].empty()))
            {
                bool hasTOC = false;
                if (!calFileData_[0].empty() && !calFileData_[1].empty())
                {
                    // write TOC
                    TSensorPlusTOC spTOC;
                    spTOC.calSize[0] = convEndian32(calFileData_[0].size(), convEndian_);
                    spTOC.calSize[1] = convEndian32(calFileData_[1].size(), convEndian_);
                    spTOC.totalCals = convEndian32(2, convEndian_);
                    success = std::fwrite(&spTOC, 1, sizeof(spTOC), file) == sizeof(spTOC);
                    hasTOC = true;
                }

                if (success)
                {
                    // write footer
                    TSensorPlusFooter footer;
                    footer.calDataOffset = 0;
                    footer.calSize = convEndian32(calFileData_[calFileData_[0].empty()].size(), convEndian_);
                    footer.calFooterMagic = convEndian32(CAL_FOOTER_MAGIC, convEndian_);
                    footer.calNumber = convEndian32((uint32_t)calFileData_[0].empty() + 1, convEndian_);
                    footer.totalCals = convEndian32(hasTOC ? 2 : 1, convEndian_);
                    footer.modTimestamp = convEndian32((uint32_t)std::time(nullptr), convEndian_);
                    success = std::fwrite(&footer, 1, sizeof(footer), file) == sizeof(footer);
                }
            }
        }

        std::fclose(file);
    }

    if (success)
        hasChanges_[0] = hasChanges_[1] = false;

    return success;
}

void IIQCalFile::initCalData(const uint8_t* data, const size_t size)
{
    calSerial_.clear();
    auto dataSize = size;
    bool sensorPlus = false;

    // check for sensor plus
    if (size > sizeof(TSensorPlusFooter) && size > sizeof(TIIQHeader))
    {
        const TIIQHeader* hdr = (TIIQHeader*)data;
        convEndian_ = (hdr->iiqMagic == IIQ_BIGENDIAN);
        const TSensorPlusFooter* footer =
            (TSensorPlusFooter*)(data+size-sizeof(TSensorPlusFooter));
        if (convEndian32(footer->calFooterMagic, convEndian_) == CAL_FOOTER_MAGIC)
        {
            hasSensorPlus_ = true;
            size_t dataSize0 = convEndian32(footer->calSize, convEndian_);
            if (convEndian32(footer->totalCals, convEndian_) == 2 &&
                size > sizeof(TSensorPlusFooter)+sizeof(TSensorPlusTOC))
            {
                TSensorPlusTOC* spTOC = (TSensorPlusTOC*)((uint8_t*)footer-sizeof(TSensorPlusTOC));
                // process sensor+ data
                dataSize0 = convEndian32(spTOC->calSize[0], convEndian_);
                size_t dataSize1 = convEndian32(spTOC->calSize[1], convEndian_);
                if (dataSize0 + dataSize1 < size)
                {
                    calFileData_[1].resize(dataSize1);
                    std::memcpy(calFileData_[1].data(), data+dataSize0, dataSize1);
                    parseCalFileData(true);
                }
            }
            else
            {
                sensorPlus = convEndian32(footer->calNumber, convEndian_) == 2;
            }
            // to process std cal file part - set the real dataSize
            if (dataSize0 < size)
                dataSize = dataSize0;
        }
    }

    calFileData_[sensorPlus].resize(dataSize);
    std::memcpy(calFileData_[sensorPlus].data(), data, dataSize);
    parseCalFileData(sensorPlus);
}

// private function parsing cal file
void IIQCalFile::parseCalFileData(bool sensorPlus)
{
    defPixels_[sensorPlus].clear();
    defCols_[sensorPlus].clear();
    calTags_[sensorPlus].clear();

    if (calFileData_[sensorPlus].size() < sizeof(TIIQHeader))
        return;

    const TIIQHeader* hdr = (TIIQHeader*)calFileData_[sensorPlus].data();
    convEndian_ = (hdr->iiqMagic == IIQ_BIGENDIAN);
    uint32_t ifdOffset = convEndian32(hdr->dirOffset, convEndian_);

    if (calFileData_[sensorPlus].size() < ifdOffset+8+sizeof(TIiqCalTagEntry))
        return;

    const uint8_t* data = (uint8_t*)calFileData_[sensorPlus].data();
    const uint8_t* end = data + calFileData_[sensorPlus].size();

    uint32_t entries = convEndian32(*(uint32_t*)(data+ifdOffset), convEndian_);
    const TIiqCalTagEntry* tagEntry = (TIiqCalTagEntry*)(data+ifdOffset+8);

    if ((uint8_t*)(tagEntry + entries) > end)
        return;

    for (int i=0; i<entries; ++i)
    {
        uint32_t tag = convEndian32(tagEntry[i].tag, convEndian_);
        const uint8_t* tagData = data + convEndian32(tagEntry[i].data, convEndian_);
        uint32_t sizeBytes = convEndian32(tagEntry[i].sizeBytes, convEndian_);

        calTags_[sensorPlus].emplace(tag);

        if (sizeBytes == 0)
        {
            tagData = (uint8_t*)(&tagEntry[i].data);
            sizeBytes = 4;
        }

        if (tag == CAL_SerialNumber)
            calSerial_ = (char*)tagData;
        else if (tag == CAL_DefectCorrection)
        {
            if (tagData+sizeBytes > end)
                break;

            // process defects
            auto totalDefects = sizeBytes/sizeof(TDefectEntry);
            const TDefectEntry* defect = (TDefectEntry*)tagData;
            for (int j=0; j<totalDefects; ++j)
                switch (convEndian16(defect[j].defectType, convEndian_))
                {
                    case DEF_COL:
                    case DEF_COL_2:
                    case DEF_COL_3:
                    case DEF_COL_4:
                        addDefCol(convEndian16(defect[j].col, convEndian_), sensorPlus);
                        break;
                    case DEF_PIXEL:
                        addDefPixel(convEndian16(defect[j].col, convEndian_),
                                    convEndian16(defect[j].row, convEndian_),
                                    sensorPlus);
                        break;
                }
        }
    }

    hasChanges_[sensorPlus] = false;
}

bool IIQCalFile::rebuildCalFileData(std::vector<uint8_t>& calFileData, bool sensorPlus) const
{
    std::vector<uint8_t> newCalData;
    newCalData.reserve(calFileData_[sensorPlus].size());

    // copy header
    newCalData.resize(sizeof(TIIQHeader)+8);
    std::memcpy(newCalData.data(), calFileData_[sensorPlus].data(), sizeof(TIIQHeader));

    // mod time
    uint32_t modTime = (uint32_t)std::time(nullptr);

    // populate defect list
    std::vector<TDefectEntry> newDef(defCols_[sensorPlus].size()+defPixels_[sensorPlus].size());
    int i=0;
    for (auto col: defCols_[sensorPlus])
    {
        newDef[i].defectType = DEF_COL;
        newDef[i].col = col;
        newDef[i].row = 0;
        newDef[i].extra = 0;
        newDef[i].defectType = convEndian16(newDef[i].defectType, convEndian_);
        newDef[i].col = convEndian16(newDef[i].col, convEndian_);
        ++i;
    }

    for (auto [col, row]: defPixels_[sensorPlus])
    {
        newDef[i].defectType = DEF_PIXEL;
        newDef[i].col = col;
        newDef[i].row = row;
        newDef[i].extra = 0;
        newDef[i].defectType = convEndian16(newDef[i].defectType, convEndian_);
        newDef[i].col = convEndian16(newDef[i].col, convEndian_);
        newDef[i].row = convEndian16(newDef[i].row, convEndian_);
        ++i;
    }

    // parse and copy original file
    bool hasDefTag = calTags_[sensorPlus].find(CAL_DefectCorrection) != calTags_[sensorPlus].end();
    bool hasCreateTime = calTags_[sensorPlus].find(CAL_TimeCreated) != calTags_[sensorPlus].end();
    TIIQHeader* hdr = (TIIQHeader*)calFileData_[sensorPlus].data();
    uint32_t ifdOffset = convEndian32(hdr->dirOffset, convEndian_);

    if (calFileData_[sensorPlus].size() < ifdOffset+8+sizeof(TIiqCalTagEntry))
        return false;

    const uint8_t* data = (uint8_t*)calFileData_[sensorPlus].data();
    const uint8_t* end = data + calFileData_[sensorPlus].size();

    uint32_t entries = convEndian32(*(uint32_t*)(data+ifdOffset), convEndian_);
    const TIiqCalTagEntry* tagEntry = (TIiqCalTagEntry*)(data+ifdOffset+8);

    if (!entries || (uint8_t*)(tagEntry + entries) > end)
        return false;

    std::vector<TIiqCalTagEntry> newCalTags(entries+!hasDefTag+!hasCreateTime);

    int newIdx=0;
    for (int idx=0; idx<entries; ++idx, ++newIdx)
    {
        uint32_t tag = convEndian32(tagEntry[idx].tag, convEndian_);
        const uint8_t* tagData = data + convEndian32(tagEntry[idx].data, convEndian_);
        uint32_t sizeBytes = convEndian32(tagEntry[idx].sizeBytes, convEndian_);

        newCalTags[newIdx] = tagEntry[idx];

        uint32_t newDataOffs = newCalData.size();
        if (tag != CAL_DefectCorrection && sizeBytes)
        {
            newCalData.resize(newDataOffs+((sizeBytes+3)&~3));
            std::memcpy(newCalData.data()+newDataOffs, tagData, sizeBytes);
            newCalTags[newIdx].data = convEndian32(newDataOffs, convEndian_);
        }

        // add mod time if needed
        if (tag == CAL_TimeCreated || tag == CAL_TimeModified)
        {
            newCalTags[newIdx].data = convEndian32(modTime, convEndian_);
        }
        else if (tag == CAL_DefectCorrection)
        {
            if (tagData+sizeBytes > end)
            {
                // add non pixel and non col defects
                auto totalDefects = sizeBytes/sizeof(TDefectEntry);
                const TDefectEntry* defect = (TDefectEntry*)tagData;
                for (int i=0; i<totalDefects; ++i)
                {
                    uint16_t defType = convEndian16(defect[i].defectType, convEndian_);
                    if (defType != DEF_COL   && defType != DEF_COL_2 &&
                        defType != DEF_COL_3 && defType != DEF_COL_4 &&
                        defType != DEF_PIXEL)
                    {
                        newDef.push_back(defect[i]);
                    }
                }
            }

            sizeBytes = newDef.size()*sizeof(TDefectEntry);
            newCalData.resize(newDataOffs+sizeBytes);
            std::memcpy(newCalData.data()+newDataOffs, newDef.data(), sizeBytes);
            newCalTags[newIdx].data = convEndian32(newDataOffs, convEndian_);
            newCalTags[newIdx].sizeBytes = convEndian32(sizeBytes, convEndian_);
            hasDefTag = true;
        }
    }

    // add missing tags
    if (!hasCreateTime && newIdx<newCalTags.size())
    {
        newCalTags[newIdx].tag = convEndian32(CAL_TimeCreated, convEndian_);
        newCalTags[newIdx].sizeBytes = 0;
        newCalTags[newIdx].data = convEndian32(modTime, convEndian_);
        ++newIdx;
    }
    if (!hasDefTag && newIdx<newCalTags.size())
    {
        uint32_t newDataOffs = newCalData.size();
        uint32_t sizeBytes = newDef.size()*sizeof(TDefectEntry);
        newCalData.resize(newDataOffs+sizeBytes);
        std::memcpy(newCalData.data()+newDataOffs, newDef.data(), sizeBytes);
        newCalTags[newIdx].data = convEndian32(newDataOffs, convEndian_);
        newCalTags[newIdx].sizeBytes = convEndian32(sizeBytes, convEndian_);
    }

    // add IFD
    ifdOffset = newCalData.size();
    newCalData.resize(ifdOffset+8+newCalTags.size()*sizeof(TIiqCalTagEntry));
    std::memcpy(newCalData.data()+ifdOffset+8,
                newCalTags.data(),
                newCalTags.size()*sizeof(TIiqCalTagEntry));
    *(uint32_t*)(newCalData.data()+ifdOffset) = convEndian32(newCalTags.size(), convEndian_);

    // fix the header
    hdr = (TIIQHeader*)newCalData.data();
    hdr->dirOffset = convEndian32(ifdOffset, convEndian_);

    // swap the vectors
    calFileData.swap(newCalData);

    return true;
}

// Copied LibRaw internal functionality
#define meta_length  libraw_internal_data.unpacker_data.meta_length
#define meta_offset  libraw_internal_data.unpacker_data.meta_offset
#define order        libraw_internal_data.unpacker_data.order
#define ifp          libraw_internal_data.internal_data.input
#define ph1          imgdata.color.phase_one_data

// IIQFile functions
IIQFile::~IIQFile()
{
    if (is_phaseone_compressed() &&
        imgdata.rawdata.raw_image &&
        imgdata.rawdata.raw_alloc != imgdata.rawdata.raw_image)
        phase_one_free_tempbuffer();
}

IIQCalFile IIQFile::getIIQCalFile()
{
    if (is_phaseone_compressed() && meta_length)
    {
        std::vector<uint8_t> calData(meta_length);
        ifp->seek(meta_offset, SEEK_SET);
        if (ifp->read(calData.data(), 1, calData.size()) == calData.size())
            return IIQCalFile(calData.data(), calData.size());
    }
    return IIQCalFile();
}

bool IIQFile::isSensorPlus()
{
    if (is_phaseone_compressed() && meta_length>sizeof(TSensorPlusFooter))
    {
        uint32_t data;
        ifp->seek(meta_offset, SEEK_SET);
        if (ifp->read(&data, 1, sizeof(data)) == sizeof(data))
            convEndian_ = (data == IIQ_BIGENDIAN);
        TSensorPlusFooter footer;
        ifp->seek(meta_offset+meta_length-sizeof(footer), SEEK_SET);
        if (ifp->read(&footer, 1, sizeof(footer)) == sizeof(footer) &&
            convEndian32(footer.calFooterMagic, convEndian_) == CAL_FOOTER_MAGIC)
        {
            return convEndian32(footer.calNumber, convEndian_) == 2;
        }
    }
    return false;
}

// Applies corrections
void IIQFile::applyPhaseOneCorr(const IIQCalFile& calFile, bool sensorPlus, bool applyDefects)
{
    if (!is_phaseone_compressed() || !imgdata.rawdata.raw_alloc)
        return;

    try
    {
        if (imgdata.rawdata.raw_image &&
            imgdata.rawdata.raw_alloc != imgdata.rawdata.raw_image)
            phase_one_free_tempbuffer();

        phase_one_allocate_tempbuffer();
        int rc = phase_one_subtract_black((ushort *)imgdata.rawdata.raw_alloc,
                                          imgdata.rawdata.raw_image);
        if (rc == 0)
        {
            std::vector<uint8_t> data;
            if (applyDefects && calFile.hasUnsavedChanges())
                calFile.saveToData(data, sensorPlus);

            const auto& calData = applyDefects && calFile.hasUnsavedChanges()
                                    ? data : calFile.getCalFileData(sensorPlus);
            calData_ = calData.data();
            calDataEnd_ = calData_ + calData.size();
            calDataCurPtr_ = calData_;

            rc = phase_one_correct(applyDefects);
            calData_ = calDataEnd_ = calDataCurPtr_ = nullptr;
        }
    }
    catch (const std::bad_alloc&)
    {
        recycle();
    }
    catch (const LibRaw_exceptions& err)
    {
        recycle();
    }
}

inline uint32_t abs32(int32_t x)
{
    // Branchless version.
    uint32_t sm = x >> 31;
    return (uint32_t) ((x + sm) ^ sm);
}

inline uint32_t min32(uint32_t x, uint32_t y)
{
    return x < y ? x : y;
}

inline uint32_t max32(uint32_t x, uint32_t y)
{
    return x > y ? x : y;
}

template <typename T>
inline T constrain(T x, T l, T u)
{
    return x < l ? l : (x > u ? u : x);
}

void IIQFile::dataSetPos(uint32_t pos, bool fromCur)
{
    auto newDataPtr = (fromCur ? calDataCurPtr_ : calData_) + pos;
    if (newDataPtr<calDataEnd_)
        calDataCurPtr_ = newDataPtr;
    else
        calDataCurPtr_ = calDataEnd_;
}

void IIQFile::getShorts(uint16_t *shorts, uint32_t count)
{
    if (calDataCurPtr_+(count<<1) > calDataEnd_)
        throw LIBRAW_EXCEPTION_IO_CORRUPT;
    if (convEndian_)
        swab((char*)calDataCurPtr_, (char*)shorts, count<<1);
    else
        std::memcpy(shorts, calDataCurPtr_, count<<1);
    calDataCurPtr_ += count<<1;
}

uint16_t IIQFile::get16()
{
    if (calDataCurPtr_+2 > calDataEnd_)
        throw LIBRAW_EXCEPTION_IO_CORRUPT;
    uint16_t* data = (uint16_t*)calDataCurPtr_;
    calDataCurPtr_+=2;

    return convEndian16(*data, convEndian_);
}

uint32_t IIQFile::get32()
{
    if (calDataCurPtr_+4 > calDataEnd_)
        throw LIBRAW_EXCEPTION_IO_CORRUPT;
    uint32_t* data = (uint32_t*)calDataCurPtr_;
    calDataCurPtr_+=4;

    return convEndian32(*data, convEndian_);
}

float IIQFile::getFloat()
{
    if (calDataCurPtr_+4 > calDataEnd_)
        throw LIBRAW_EXCEPTION_IO_CORRUPT;

    uint32_t* data = (uint32_t*)calDataCurPtr_;
    calDataCurPtr_+=4;
    union {
        uint32_t i;
        float f;
    } u;

    u.i = convEndian32(*data, convEndian_);
    return u.f;
}

int IIQFile::p1rawc(unsigned row, unsigned col, unsigned& count) const
{
    return row < imgdata.sizes.raw_height && col < imgdata.sizes.raw_width
            ? (++count, RAW(row, col)) : 0;
}

int IIQFile::p1raw(unsigned row, unsigned col) const
{
    return row < imgdata.sizes.raw_height && col < imgdata.sizes.raw_width
            ? RAW(row, col) : 0;
}

// DNG SDK version of fixing pixels in bad column using averages sets
// corrected not to use pixels in the same column
void IIQFile::phase_one_fix_col_pixel_avg(unsigned row, unsigned col)
{
    static const int8_t dir[3][8][2] = {
        { {-2,-2}, {-2, 2}, {2,-2}, {2, 2}, { 0, 0}, { 0, 0}, {0, 0}, {0, 0} },
        { {-2,-4}, {-4,-2}, {2,-4}, {4,-2}, {-2, 4}, {-4, 2}, {2, 4}, {4, 2} },
        { {-4,-4}, {-4, 4}, {4,-4}, {4, 4}, { 0, 0}, { 0, 0}, {0, 0}, {0, 0} } };

    for (int set=0; set < 3; ++set)
    {
        uint32_t total = 0;
        uint32_t count = 0;
        for (int i = 0; i < 8; ++i)
        {
            if (!dir[set][i][0] && !dir[set][i][1])
                break;

            total += p1rawc(row+dir[set][i][0], col+dir[set][i][1], count);
        }

        if (count)
        {
            RAW(row,col) = (uint16_t)((total + (count >> 1)) / count);
            break;
        }
    }
}

// DNG SDK version of fixing pixels in bad column using gradient prediction
void IIQFile::phase_one_fix_pixel_grad(unsigned row, unsigned col)
{
    static const int8_t grad_sets[7][12][2] = {
        { {-4,-2}, { 4, 2}, {-3,-1}, { 1, 1}, {-1,-1}, { 3, 1},
          {-4,-1}, { 0, 1}, {-2,-1}, { 2, 1}, { 0,-1}, { 4, 1} },
        { {-2,-2}, { 2, 2}, {-3,-1}, {-1, 1}, {-1,-1}, { 1, 1},
          { 1,-1}, { 3, 1}, {-2,-1}, { 0, 1}, { 0,-1}, { 2, 1} },
        { {-2,-4}, { 2, 4}, {-1,-3}, { 1, 1}, {-1,-1}, { 1, 3},
          {-2,-1}, { 0, 3}, {-1,-2}, { 1, 2}, { 0,-3}, { 2, 1} },
        { { 0,-2}, { 0, 2}, {-1,-1}, {-1, 1}, { 1,-1}, { 1, 1},
          {-1,-2}, {-1, 2}, { 0,-1}, { 0,-1}, { 1,-2}, { 1, 2} },
        { {-2, 4}, { 2,-4}, {-1, 3}, { 1,-1}, {-1, 1}, { 1,-3},
          {-2, 1}, { 0,-3}, {-1, 2}, { 1,-2}, { 0, 3}, { 2,-1} },
        { {-2, 2}, { 2,-2}, {-3, 1}, {-1,-1}, {-1, 1}, { 1,-1},
          { 1, 1}, { 3,-1}, {-2, 1}, { 0,-1}, { 0, 1}, { 2,-1} },
        { {-4, 2}, { 4,-2}, {-3, 1}, { 1,-1}, {-1, 1}, { 3,-1},
          {-4, 1}, { 0,-1}, {-2, 1}, { 2,-1}, { 0, 1}, { 4,-1} } };

    uint32_t est[7], grad[7];
    uint32_t lower = min32(p1raw(row,col-2), p1raw(row, col+2));
    uint32_t upper = max32(p1raw(row,col-2), p1raw(row, col+2));
    uint32_t minGrad = 0xFFFFFFFF;
    for (int i = 0; i<7; ++i)
    {
        est[i] = p1raw(row+grad_sets[i][0][0], col+grad_sets[i][0][1]) +
                         p1raw(row+grad_sets[i][1][0], col+grad_sets[i][1][1]);
        grad[i] = 0;
        for (int j=0; j<12; j+=2)
            grad[i] += abs32(p1raw(row+grad_sets[i][j][0], col+grad_sets[i][j][1]) -
                                             p1raw(row+grad_sets[i][j+1][0], col+grad_sets[i][j+1][1]));
        minGrad = min32(minGrad, grad[i]);
    }

    uint32_t limit = (minGrad * 3) >> 1;
    uint32_t total = 0;
    uint32_t count = 0;
    for (int i = 0; i<7; ++i)
        if (grad[i] <= limit)
        {
            total += est[i];
            count += 2;
        }
    RAW(row, col) = constrain((total + (count >> 1)) / count, lower, upper);
}

void IIQFile::phase_one_flat_field(int is_float, int nc)
{
    ushort head[8];
    unsigned wide, high, y, x, c, rend, cend, row, col;
    float *mrow, num, mult[4];

    getShorts(head, 8);
    if (head[2] == 0 || head[3] == 0 || head[4] == 0 || head[5] == 0)
        return;
    wide = head[2] / head[4] + (head[2] % head[4] != 0);
    high = head[3] / head[5] + (head[3] % head[5] != 0);
    mrow = (float *)calloc(nc * wide, sizeof *mrow);
    merror(mrow, "phase_one_flat_field()");
    for (y = 0; y < high; ++y)
    {
        checkCancel();
        for (x = 0; x < wide; x++)
            for (c = 0; c < (unsigned)nc; c += 2)
            {
                num = is_float ? getFloat() : get16() / 32768.0;
                if (y == 0)
                    mrow[c * wide + x] = num;
                else
                    mrow[(c + 1) * wide + x] = (num - mrow[c * wide + x]) / head[5];
            }
        if (y == 0)
            continue;
        rend = head[1] + y * head[5];
        for (row = rend - head[5];
             row < imgdata.sizes.raw_height && row < rend && row < unsigned(head[1] + head[3] - head[5]);
             row++)
        {
            for (x = 1; x < wide; x++)
            {
                for (c = 0; c < (unsigned)nc; c += 2)
                {
                    mult[c] = mrow[c * wide + x - 1];
                    mult[c + 1] = (mrow[c * wide + x] - mult[c]) / head[4];
                }
                cend = head[0] + x * head[4];
                for (col = cend - head[4];
                     col < imgdata.sizes.raw_width && col < cend && col < unsigned(head[0] + head[2] - head[4]);
                     col++)
                {
                    c = nc > 2 ? FC(row - imgdata.sizes.top_margin, col - imgdata.sizes.left_margin) : 0;
                    if (!(c & 1))
                    {
                        c = RAW(row, col) * mult[c];
                        RAW(row, col) = constrain(c, 0u, 65535u);
                    }
                    for (c = 0; c < (unsigned)nc; c += 2)
                        mult[c] += mult[c + 1];
                }
            }
            for (x = 0; x < wide; x++)
                for (c = 0; c < (unsigned)nc; c += 2)
                    mrow[c * wide + x] += mrow[(c + 1) * wide + x];
        }
    }
    free(mrow);
}

// This is essentially a copy of LibRaw phase_one_correct but without defects fixing
int IIQFile::phase_one_correct(bool applyDefects)
{
    unsigned entries, tag, data, save, col, row, type;
    int len, i, j, k, cip, val[4], dev[4], sum, max;
    int head[9], diff, mindiff = INT_MAX, off_412 = 0;
    const signed char dir[12][2] = {
            {-1, -1}, {-1, 1}, {1, -1},  {1, 1},  {-2, 0}, {0, -2},
            {0, 2},   {2, 0},  {-2, -2}, {-2, 2}, {2, -2}, {2, 2}};
    float poly[8], num, cfrac, frac, mult[2];
    ushort *xval[2];
    int qmult_applied = 0, qlin_applied = 0;
    std::vector<unsigned> badCols;

    if (!calData_ || !calDataEnd_ || calDataEnd_ - calData_ == 0)
        return 0;
    dataSetPos(0);
    convEndian_ = (get32() == IIQ_BIGENDIAN);
    dataSetPos(4, true);
    dataSetPos(get32());
    entries = get32();
    get32();

    try
    {
        while (entries--)
        {
            checkCancel();
            tag = get32();
            len = get32();
            data = get32();
            save = dataGetPos();
            dataSetPos(data);
            if (tag == 0x0400 && applyDefects)
            { /* Sensor defects */
                while ((len -= 8) >= 0)
                {
                    col = get16();
                    row = get16();
                    type = get16();
                    get16();
                    if (col >= imgdata.sizes.raw_width)
                        continue;
                    if (type == 131 || type == 137) /* Bad column */
                        badCols.push_back(col);
                    else if (type == 129)
                    { /* Bad pixel */
                        if (row >= imgdata.sizes.raw_height)
                            continue;
                        j = (FC(row - imgdata.sizes.top_margin, col - imgdata.sizes.left_margin) != 1) * 4;
                        unsigned count = 0;
                        for (sum = 0, i = j; i < j + 8; i++)
                            sum += p1rawc(row + dir[i][0], col + dir[i][1], count);
                        if (count)
                            RAW(row, col) = (sum + (count >> 1)) / count;
                    }
                }
            }
            else if (tag == 0x0419)
            { /* Polynomial curve */
                for (get32(), i = 0; i < 8; i++)
                    poly[i] = getFloat();
                poly[3] += (ph1.tag_210 - poly[7]) * poly[6] + 1;
                for (i = 0; i < 0x10000; i++)
                {
                    num = (poly[5] * i + poly[3]) * i + poly[1];
                    imgdata.color.curve[i] = constrain((int)num, 0, 65535);
                }
                goto apply; /* apply to right half */
            }
            else if (tag == 0x041a)
            { /* Polynomial curve */
                for (i = 0; i < 4; i++)
                    poly[i] = getFloat();
                for (i = 0; i < 0x10000; i++)
                {
                    for (num = 0, j = 4; j--;)
                        num = num * i + poly[j];
                    imgdata.color.curve[i] = constrain((int)(num + i), 0, 65535);
                }
            apply: /* apply to whole image */
                for (row = 0; row < imgdata.sizes.raw_height; row++)
                {
                    checkCancel();
                    for (col = (tag & 1) * ph1.split_col;
                         col < imgdata.sizes.raw_width;
                         ++col)
                        RAW(row, col) = imgdata.color.curve[RAW(row, col)];
                }
            }
            else if (tag == 0x0401)
            { /* All-color flat fields */
                phase_one_flat_field(1, 2);
            }
            else if (tag == 0x0416 || tag == 0x0410)
            {
                phase_one_flat_field(0, 2);
            }
            else if (tag == 0x040b)
            { /* Red+blue flat field */
                phase_one_flat_field(0, 4);
            }
            else if (tag == 0x0412)
            {
                // XYZ corrections are not supported - they are stored outside
                // of cal file and is one of P1 weirdness
            }
            else if (tag == 0x041f && !qlin_applied)
            { /* Quadrant linearization */
                ushort lc[2][2][16], ref[16];
                int qr, qc;
                for (qr = 0; qr < 2; qr++)
                    for (qc = 0; qc < 2; qc++)
                        for (i = 0; i < 16; i++)
                            lc[qr][qc][i] = get32();
                for (i = 0; i < 16; i++)
                {
                    int v = 0;
                    for (qr = 0; qr < 2; qr++)
                        for (qc = 0; qc < 2; qc++)
                            v += lc[qr][qc][i];
                    ref[i] = (v + 2) >> 2;
                }
                for (qr = 0; qr < 2; qr++)
                {
                    for (qc = 0; qc < 2; qc++)
                    {
                        int cx[19], cf[19];
                        for (i = 0; i < 16; i++)
                        {
                            cx[1 + i] = lc[qr][qc][i];
                            cf[1 + i] = ref[i];
                        }
                        cx[0] = cf[0] = 0;
                        cx[17] = cf[17] = ((unsigned int)ref[15] * 65535) / lc[qr][qc][15];
                        cf[18] = cx[18] = 65535;
                        cubic_spline(cx, cf, 19);

                        for (row = (qr ? ph1.split_row : 0);
                             row < unsigned(qr ? imgdata.sizes.raw_height : ph1.split_row);
                             ++row)
                        {
                            checkCancel();
                            for (col = (qc ? ph1.split_col : 0);
                                 col < unsigned(qc ? imgdata.sizes.raw_width : ph1.split_col);
                                 ++col)
                                RAW(row, col) = imgdata.color.curve[RAW(row, col)];
                        }
                    }
                }
                qlin_applied = 1;
            }
            else if (tag == 0x041e && !qmult_applied)
            { /* Quadrant multipliers */
                float qmult[2][2] = {{1, 1}, {1, 1}};
                get32();
                get32();
                get32();
                get32();
                qmult[0][0] = 1.0 + getFloat();
                get32();
                get32();
                get32();
                get32();
                get32();
                qmult[0][1] = 1.0 + getFloat();
                get32();
                get32();
                get32();
                qmult[1][0] = 1.0 + getFloat();
                get32();
                get32();
                get32();
                qmult[1][1] = 1.0 + getFloat();
                for (row = 0; row < imgdata.sizes.raw_height; ++row)
                {
                    checkCancel();
                    for (col = 0; col < imgdata.sizes.raw_width; ++col)
                    {
                        i = qmult[row >= (unsigned)ph1.split_row][col >= (unsigned)ph1.split_col] *
                                RAW(row, col);
                        RAW(row, col) = constrain(i, 0, 65535);
                    }
                }
                qmult_applied = 1;
            }
            else if (tag == 0x0431 && !qmult_applied)
            { /* Quadrant combined */
                ushort lc[2][2][7], ref[7];
                int qr, qc;
                for (i = 0; i < 7; i++)
                    ref[i] = get32();
                for (qr = 0; qr < 2; qr++)
                    for (qc = 0; qc < 2; qc++)
                        for (i = 0; i < 7; i++)
                            lc[qr][qc][i] = get32();
                for (qr = 0; qr < 2; qr++)
                {
                    for (qc = 0; qc < 2; qc++)
                    {
                        int cx[9], cf[9];
                        for (i = 0; i < 7; i++)
                        {
                            cx[1 + i] = ref[i];
                            cf[1 + i] = ((unsigned)ref[i] * lc[qr][qc][i]) / 10000;
                        }
                        cx[0] = cf[0] = 0;
                        cx[8] = cf[8] = 65535;
                        cubic_spline(cx, cf, 9);
                        for (row = (qr ? ph1.split_row : 0);
                             row < unsigned(qr ? imgdata.sizes.raw_height : ph1.split_row);
                             ++row)
                        {
                            checkCancel();
                            for (col = (qc ? ph1.split_col : 0);
                                 col < unsigned(qc ? imgdata.sizes.raw_width : ph1.split_col);
                                 ++col)
                                RAW(row, col) = imgdata.color.curve[RAW(row, col)];
                        }
                    }
                }
                qmult_applied = 1;
                qlin_applied = 1;
            }
            dataSetPos(save);
        }
        if (!badCols.empty())
        {
            std::sort(badCols.begin(), badCols.end());
            bool prevIsolated = true;
            for (i = 0; i < badCols.size(); ++i)
            {
                bool nextIsolated = i == badCols.size()-1 || badCols[i+1]>badCols[i]+4;
                for (row = 0; row < imgdata.sizes.raw_height; ++row)
                    if (prevIsolated && nextIsolated)
                        phase_one_fix_pixel_grad(row,badCols[i]);
                    else
                        phase_one_fix_col_pixel_avg(row,badCols[i]);
                prevIsolated = nextIsolated;
            }
        }
    }
    catch (...)
    {
        return LIBRAW_CANCELLED_BY_CALLBACK;
    }
    return 0;
}


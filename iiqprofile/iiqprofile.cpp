/*
    iiqprofile.cpp - Extracts camera profiles from IIQ into .ICC and .DCP format

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
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <vector>
#include <filesystem>

#define CMS_NO_PTHREADS
#define CMS_NO_REGISTER_KEYWORD
#include <lcms2.h>

#include "matrix3x3.h"

#pragma pack(push)
#pragma pack(1)

#define TIFF_VERSION_CLASSIC 42

#define TIFF_BIGENDIAN      0x4d4d
#define TIFF_LITTLEENDIAN   0x4949

#define IIQ_BIGENDIAN      0x4d4d4d4d
#define IIQ_LITTLEENDIAN   0x49494949

#define IIQ_RAW  0x526177

// copy constructors in variable array structs
#pragma warning( disable : 4200 )

// All the structures for the types in binary files go in C style declaration
// to maintain struct packing and avoid C++ alignment/padding

struct TTiffHeader
{
    uint16_t magic;      // magic number (defines uint8_t order)
    uint16_t version;    // TIFF version number
    uint32_t dirOffset;  // uint8_t offset to first directory
};

struct TTiffTagEntry
{
    uint16_t tiffTag;       // 0
    uint16_t dataType;      // 2
    uint32_t dataCount;     // 4
    uint32_t dataOffset;    // 8
};

struct TIIQHeader
{
    uint32_t iiqMagic;   // TIFF magic - 32bit
    uint32_t rawMagic;   // 'RawB','RawC','RawT','RawH' for raw or version for calibration
    uint32_t dirOffset;
};

struct TIiqTagEntry
{
    uint32_t tag;           // 0
    uint32_t dataType;      // 4
    uint32_t sizeBytes;     // 8
    uint32_t data;          // 12
};

struct TIiqCalTagEntry
{
    uint32_t tag;           // 0
    uint32_t sizeBytes;     // 4
    uint32_t data;          // 8
};

enum EIIQTag
{
    IIQ_RommMatrix      = 0x0106,
    IIQ_CamWhite        = 0x0107,
    IIQ_RommThumbMatrix = 0x0226,
    TIFF_Make           = 271,
    TIFF_Model          = 272
};

enum ETiffDataType
{
    TIFF_NOTYPE    = 0,      // placeholder
    TIFF_BYTE      = 1,      // 8-bit unsigned integer
    TIFF_ASCII     = 2,      // 8-bit bytes w/ last byte null
    TIFF_SHORT     = 3,      // 16-bit unsigned integer
    TIFF_LONG      = 4,      // 32-bit unsigned integer
    TIFF_RATIONAL  = 5,      // 64-bit unsigned fraction
    TIFF_SBYTE     = 6,      // 8-bit signed integer
    TIFF_UNDEFINED = 7,      // 8-bit untyped data
    TIFF_SSHORT    = 8,      // 16-bit signed integer
    TIFF_SLONG     = 9,      // 32-bit signed integer
    TIFF_SRATIONAL = 10,     // 64-bit signed fraction
    TIFF_FLOAT     = 11,     // 32-bit IEEE floating point
    TIFF_DOUBLE    = 12,     // 64-bit IEEE floating point
    TIFF_IFD       = 13      // 32-bit unsigned integer (offset)
};

#pragma pack(pop)

// DNG SDK redefines basic datatypes and incorrecrly so we need
// local version of the fromBigEndian functions hence the include
vector3 stdWhiteDaylight;

matrix3x3 stdMatrixDaylight;
matrix3x3 stdMatrixDaylightThumb;

std::string cameraModel;

matrix3x3 proPhotoMatrix(0.7976685, 0.1351929, 0.0313416,
                         0.2880402, 0.7118835, 0.0000916,
                         0.0000000, 0.0000000, 0.8249054);

vector3 D50XYZ(cmsD50X, cmsD50Y, cmsD50Z);

bool useLinearCurve = false;
bool extWB = false;
bool doICC = false;
double iccGamma = 1.8;

// Standard illuminants taken from DNG
enum EIlluminant
{
    lsUnknown              =  0,
    lsDaylight             =  1,
    lsFluorescent          =  2,
    lsTungsten             =  3,
    lsFlash                =  4,
    lsFineWeather          =  9,
    lsCloudyWeather        = 10,
    lsShade                = 11,
    lsDaylightFluorescent  = 12,   // D 5700 - 7100K
    lsDayWhiteFluorescent  = 13,   // N 4600 - 5400K
    lsCoolWhiteFluorescent = 14,   // W 3900 - 4500K
    lsWhiteFluorescent     = 15,   // WW 3200 - 3700K
    lsStandardLightA       = 17,
    lsStandardLightB       = 18,
    lsStandardLightC       = 19,
    lsD55                  = 20,
    lsD65                  = 21,
    lsD75                  = 22,
    lsD50                  = 23,
    lsISOStudioTungsten    = 24,

    lsOther                = 255
};

enum EDCPPolicy
{
	pepAllowCopying	  = 0,
	pepEmbedIfUsed    = 1,
	pepEmbedNever     = 2,
	pepNoRestrictions = 3
};

enum EDCPTag
{
    DCPTAG_UniqueCameraModel            = 50708,
    DCPTAG_ColourMatrix1                = 50721,
    DCPTAG_ColourMatrix2                = 50722,
    DCPTAG_ReductionMatrix1             = 50725,
    DCPTAG_ReductionMatrix2             = 50726,
    DCPTAG_CalibrationIlluminant1       = 50778,
    DCPTAG_CalibrationIlluminant2       = 50779,
    DCPTAG_ProfileCalibrationSignature  = 50932,
    DCPTAG_ProfileName                  = 50936,
    DCPTAG_ProfileHueSatMapDims         = 50937,
    DCPTAG_ProfileHueSatMapData1        = 50938,
    DCPTAG_ProfileHueSatMapData2        = 50939,
    DCPTAG_ProfileToneCurve             = 50940,
    DCPTAG_ProfileEmbedPolicy           = 50941,
    DCPTAG_ProfileCopyright             = 50942,
    DCPTAG_ForwardMatrix1               = 50964,
    DCPTAG_ForwardMatrix2               = 50965,
    DCPTAG_ProfileLookTableDims         = 50981,
    DCPTAG_ProfileLookTableData         = 50982,
    DCPTAG_ProfileHueSatMapEncoding     = 51107,
    DCPTAG_ProfileLookTableEncoding     = 51108,
    DCPTAG_BaselineExposureOffset       = 51109,
    DCPTAG_DefaultBlackRender           = 51110
};

// DCP Profile
struct DCPProfile
{
    struct TFPoint { float x; float y; };
    using TToneCurve = std::vector<TFPoint>;

    std::string name;
    matrix3x3   cm[2];
    matrix3x3   fm[2];
    uint16_t    calIllum[2];
    std::string cameraModel;
    std::string copyright;
    uint32_t    embedPolicy;
    TToneCurve  toneCurve;

    void writeToFile(const std::string& filename);
};

uint32_t getTagDataSize(uint32_t dataType)
{
    static const uint8_t tiffTagDataSize[] ={ 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };

    if (dataType < sizeof(tiffTagDataSize))
    {
        return tiffTagDataSize[dataType];
    }

    return 0;
}

using TDataVec = std::vector<uint8_t>;
inline void addTiffData(TTiffTagEntry& entry, TDataVec& tiffData, const void *data, const size_t size)
{
    if (size > 0 && size <= 4)
        std::memcpy(&entry.dataOffset, data, size);
    else
    {
        // size padding to nearest 4 bytes
        auto paddingSize = ((size + 3) & ~0x3) - size;

        entry.dataOffset = tiffData.size();
        tiffData.resize(tiffData.size()+size+paddingSize);
        std::memcpy(tiffData.data()+entry.dataOffset, data, size);

        if (paddingSize)
        {
            uint32_t pad = 0;
            std::memcpy(tiffData.data()+entry.dataOffset+size, &pad, paddingSize);
        }
    }
}

void DCPProfile::writeToFile(const std::string& fileName)
{
    std::vector<TTiffTagEntry> ifd;
    TDataVec data;
    TTiffHeader hdr;

    // set byte order
    uint16_t t = 1;
    uint8_t* b = (uint8_t*)&t;
    if (b[0])
        std::memcpy(&hdr, "IIRC", 4);
    else
        std::memcpy(&hdr, "MMCR", 4);
    hdr.dirOffset = sizeof(TTiffHeader);

    if (!cameraModel.empty())
    {
        TTiffTagEntry tag {DCPTAG_UniqueCameraModel, TIFF_ASCII, (uint32_t)cameraModel.length()+1, 0};
        addTiffData(tag, data, cameraModel.c_str(), tag.dataCount);
        ifd.push_back(tag);
    }

    if (!name.empty())
    {
        TTiffTagEntry tag {DCPTAG_ProfileName, TIFF_ASCII, (uint32_t)name.length()+1, 0};
        addTiffData(tag, data, name.c_str(), tag.dataCount);
        ifd.push_back(tag);
    }

    if (!copyright.empty())
    {
        TTiffTagEntry tag {DCPTAG_ProfileCopyright, TIFF_ASCII, (uint32_t)copyright.length()+1, 0};
        addTiffData(tag, data, copyright.c_str(), tag.dataCount);
        ifd.push_back(tag);
    }

    TTiffTagEntry tag {DCPTAG_ProfileEmbedPolicy, TIFF_LONG, 1, embedPolicy};
    ifd.push_back(tag);

    if (calIllum[0] != lsUnknown)
    {
        // add illuminant 1
        TTiffTagEntry tag {DCPTAG_CalibrationIlluminant1, TIFF_SHORT, 1, calIllum[0]};
        ifd.push_back(tag);

        // add FM 1
        TSRational m[9];
        fm[0].toRational(m);
        tag = TTiffTagEntry  {DCPTAG_ForwardMatrix1, TIFF_SRATIONAL, 9, 0};
        addTiffData(tag, data, m, sizeof(m));
        ifd.push_back(tag);

        // add CM 1
        cm[0].toRational(m);
        tag = TTiffTagEntry  {DCPTAG_ColourMatrix1, TIFF_SRATIONAL, 9, 0};
        addTiffData(tag, data, m, sizeof(m));
        ifd.push_back(tag);
    }

    if (calIllum[1] != lsUnknown)
    {
        // add illuminant 2
        TTiffTagEntry tag {DCPTAG_CalibrationIlluminant2, TIFF_SHORT, 1, calIllum[1]};
        ifd.push_back(tag);

        // add FM 2
        TSRational m[9];
        fm[1].toRational(m);
        tag = TTiffTagEntry  {DCPTAG_ForwardMatrix2, TIFF_SRATIONAL, 9, 0};
        addTiffData(tag, data, m, sizeof(m));
        ifd.push_back(tag);

        // add CM 2
        cm[1].toRational(m);
        tag = TTiffTagEntry  {DCPTAG_ColourMatrix2, TIFF_SRATIONAL, 9, 0};
        addTiffData(tag, data, m, sizeof(m));
        ifd.push_back(tag);
    }

    if (toneCurve.size()>1)
    {
        TTiffTagEntry tag {DCPTAG_ProfileToneCurve, TIFF_FLOAT, (uint32_t)toneCurve.size()*2, 0};
        addTiffData(tag, data, toneCurve.data(), tag.dataCount*sizeof(float));
        ifd.push_back(tag);
    }

    // sort IFD
    std::sort(ifd.begin(), ifd.end(), [](const auto& a, const auto& b) {
        return a.tiffTag < b.tiffTag;
    });

    // correct IFD data offsets
    uint32_t dataOffset = sizeof(TTiffHeader) + sizeof(TTiffTagEntry)*ifd.size() + 6;
    for (auto& entry : ifd)
        if (getTagDataSize(entry.dataType)*entry.dataCount>4)
            entry.dataOffset += dataOffset;

    if (auto file = fopen(fileName.c_str(), "wb"))
    {
        fwrite(&hdr, 1, sizeof(hdr), file);
        uint16_t ifdSize = (uint16_t)ifd.size();
        fwrite(&ifdSize, 1, sizeof(ifdSize), file);
        fwrite(ifd.data(), sizeof(TTiffTagEntry), ifd.size(), file);
        uint32_t nextIfd = 0;
        fwrite(&nextIfd, 1, sizeof(nextIfd), file);

        fwrite(data.data(), 1, data.size(), file);

        fclose(file);
    }
}

// Endianness
static bool bigEndian = false;

std::map<uint32_t, uint8_t> iiqTagDataTypes =
{
    // INT32, type 1, single val
    { 0x100, TIFF_LONG },
    { 0x101, TIFF_LONG },
    { 0x103, TIFF_LONG },
    { 0x104, TIFF_LONG },
    { 0x105, TIFF_LONG },
    { 0x108, TIFF_LONG },
    { 0x109, TIFF_LONG },
    { 0x10A, TIFF_LONG },
    { 0x10B, TIFF_LONG },
    { 0x10C, TIFF_LONG },
    { 0x10D, TIFF_LONG },
    { 0x10E, TIFF_LONG },
    { 0x112, TIFF_LONG },
    { 0x113, TIFF_LONG },
    { 0x20B, TIFF_LONG },
    { 0x20C, TIFF_LONG },
    { 0x20E, TIFF_LONG },
    { 0x212, TIFF_LONG },
    { 0x213, TIFF_LONG },
    { 0x214, TIFF_LONG },
    { 0x215, TIFF_LONG },
    { 0x217, TIFF_LONG },
    { 0x218, TIFF_LONG },
    { 0x21A, TIFF_LONG },
    { 0x21D, TIFF_LONG },
    { 0x21E, TIFF_LONG },
    { 0x220, TIFF_LONG },
    { 0x222, TIFF_LONG },
    { 0x224, TIFF_LONG },
    { 0x227, TIFF_LONG },
    { 0x242, TIFF_LONG },
    { 0x243, TIFF_LONG },
    { 0x246, TIFF_LONG },
    { 0x247, TIFF_LONG },
    { 0x248, TIFF_LONG },
    { 0x249, TIFF_LONG },
    { 0x24A, TIFF_LONG },
    { 0x24B, TIFF_LONG },
    { 0x24C, TIFF_LONG },
    { 0x24D, TIFF_LONG },
    { 0x24E, TIFF_LONG },
    { 0x24F, TIFF_LONG },
    { 0x250, TIFF_LONG },
    { 0x251, TIFF_LONG },
    { 0x253, TIFF_LONG },
    { 0x254, TIFF_LONG },
    { 0x255, TIFF_LONG },
    { 0x256, TIFF_LONG },
    { 0x25B, TIFF_LONG },
    { 0x261, TIFF_LONG },
    { 0x263, TIFF_LONG },
    { 0x264, TIFF_LONG },
    { 0x265, TIFF_LONG },
    { 0x26B, TIFF_LONG },
    { 0x300, TIFF_LONG },
    { 0x304, TIFF_LONG },
    { 0x311, TIFF_LONG },
    { 0x404, TIFF_LONG },
    { 0x406, TIFF_LONG },
    { 0x407, TIFF_LONG },
    { 0x408, TIFF_LONG },
    { 0x409, TIFF_LONG },
    { 0x411, TIFF_LONG },
    { 0x413, TIFF_LONG },
    { 0x420, TIFF_LONG },
    { 0x450, TIFF_LONG },
    { 0x451, TIFF_LONG },
    { 0x452, TIFF_LONG },
    { 0x460, TIFF_LONG },
    { 0x463, TIFF_LONG },
    { 0x536, TIFF_LONG },
    { 0x537, TIFF_LONG },
    { 0x53E, TIFF_LONG },
    { 0x540, TIFF_LONG },
    { 0x541, TIFF_LONG },
    { 0x542, TIFF_LONG },
    { 0x543, TIFF_LONG },
    { 0x547, TIFF_LONG },
    // ASCII, length as specified, type 4?
    { 0x102, TIFF_ASCII },
    { 0x200, TIFF_ASCII },
    { 0x201, TIFF_ASCII },
    { 0x203, TIFF_ASCII },
    { 0x204, TIFF_ASCII },
    { 0x262, TIFF_ASCII },
    { 0x301, TIFF_ASCII },
    { 0x310, TIFF_ASCII },
    { 0x312, TIFF_ASCII },
    { 0x410, TIFF_ASCII },
    { 0x412, TIFF_ASCII },
    { 0x530, TIFF_ASCII },
    { 0x531, TIFF_ASCII },
    { 0x532, TIFF_ASCII },
    { 0x533, TIFF_ASCII },
    { 0x534, TIFF_ASCII },
    { 0x535, TIFF_ASCII },
    { 0x548, TIFF_ASCII },
    { 0x549, TIFF_ASCII },
    // FLOAT(32bits), length as specified
    { 0x106, TIFF_FLOAT },
    { 0x107, TIFF_FLOAT },
    { 0x205, TIFF_FLOAT },
    { 0x216, TIFF_FLOAT },
    { 0x226, TIFF_FLOAT },
    { 0x53D, TIFF_FLOAT },
    // INT32, type 2, pointer
    { 0x10F, TIFF_LONG },
    { 0x110, TIFF_LONG },
    { 0x202, TIFF_LONG },
    { 0x20A, TIFF_LONG },
    { 0x20D, TIFF_LONG },
    { 0x21F, TIFF_LONG },
    { 0x223, TIFF_LONG },
    { 0x225, TIFF_LONG },
    { 0x258, TIFF_LONG },
    { 0x259, TIFF_LONG },
    { 0x25A, TIFF_LONG },
    { 0x260, TIFF_LONG },
    { 0x26A, TIFF_LONG },
    // undefined, type 4?
    { 0x111, TIFF_UNDEFINED },
    { 0x219, TIFF_UNDEFINED },
    // FLOAT, type 1
    { 0x20F, TIFF_FLOAT },
    { 0x210, TIFF_FLOAT },
    { 0x211, TIFF_FLOAT },
    { 0x21B, TIFF_FLOAT },
    { 0x221, TIFF_FLOAT },
    { 0x22A, TIFF_FLOAT },
    { 0x22B, TIFF_FLOAT },
    { 0x22C, TIFF_FLOAT },
    { 0x22F, TIFF_FLOAT },
    { 0x244, TIFF_FLOAT },
    { 0x245, TIFF_FLOAT },
    { 0x252, TIFF_FLOAT },
    { 0x257, TIFF_FLOAT },
    { 0x269, TIFF_FLOAT },
    { 0x320, TIFF_FLOAT },
    { 0x321, TIFF_FLOAT },
    { 0x322, TIFF_FLOAT },
    { 0x400, TIFF_FLOAT },
    { 0x401, TIFF_FLOAT },
    { 0x402, TIFF_FLOAT },
    { 0x403, TIFF_FLOAT },
    { 0x414, TIFF_FLOAT },
    { 0x415, TIFF_FLOAT },
    { 0x416, TIFF_FLOAT },
    { 0x417, TIFF_FLOAT },
    { 0x461, TIFF_FLOAT },
    { 0x462, TIFF_FLOAT },
    { 0x538, TIFF_FLOAT },
    { 0x539, TIFF_FLOAT },
    { 0x53A, TIFF_FLOAT },
    { 0x53F, TIFF_FLOAT },
    // INT32, type 2
    { 0x21C, TIFF_LONG },
    { 0x25C, TIFF_LONG },
    { 0x25D, TIFF_LONG }
};

uint16_t fromBigEndian16(uint16_t ulValue)
{
    if (!bigEndian)
        return ulValue;

    uint8_t *tmp = (uint8_t*) & ulValue;

    // convert from big endian
    return ((uint16_t)tmp[0] << 8) | (uint16_t)tmp[1];
}

uint32_t fromBigEndian(uint32_t ulValue)
{
    if (!bigEndian)
        return ulValue;

    unsigned char *tmp = (unsigned char*) & ulValue;

    // convert from big endian
    return ((uint32_t)tmp[0] << 24) | ((uint32_t)tmp[1] << 16) | ((uint32_t)tmp[2] << 8) | (uint32_t)tmp[3];
}

uint64_t fromBigEndian64(uint64_t ulValue)
{
    if (!bigEndian)
        return ulValue;

    unsigned char *tmp = (unsigned char*) & ulValue;

    // convert from big endian
    return ((uint64_t)tmp[0] << 56) | ((uint64_t)tmp[1] << 48) | ((uint64_t)tmp[2] << 40) | ((uint64_t)tmp[2] << 32) |
           ((uint64_t)tmp[4] << 24) | ((uint64_t)tmp[5] << 16) | ((uint64_t)tmp[6] << 8)  | (uint64_t)tmp[7];
}

matrix3x3 getAdaptationMatrix(const vector3 &whiteFrom, const vector3 &whiteTo)
{
    // Use the linearized Bradford adaptation matrix.
    matrix3x3 mb( 0.8951,  0.2664, -0.1614,
                 -0.7502,  1.7135,  0.0367,
                  0.0389, -0.0685,  1.0296);

    vector3 w1 = mb*whiteFrom;
    vector3 w2 = mb*whiteTo;

    // Negative white coordinates are kind of meaningless.
    w1[0] = std::max(w1[0], 0.0);
    w1[1] = std::max(w1[1], 0.0);
    w1[2] = std::max(w1[2], 0.0);
    w2[0] = std::max(w2[0], 0.0);
    w2[1] = std::max(w2[1], 0.0);
    w2[2] = std::max(w2[2], 0.0);

    // Limit scaling to something reasonable.
    matrix3x3 a;
    a[0][0] = std::max(0.1, std::min(w1[0]>0.0?w2[0]/w1[0]:10.0, 10.0));
    a[1][1] = std::max(0.1, std::min(w1[1]>0.0?w2[1]/w1[1]:10.0, 10.0));
    a[2][2] = std::max(0.1, std::min(w1[2]>0.0?w2[2]/w1[2]:10.0, 10.0));

    return Invert(mb)*a*mb;
}

void printMatrix(const char *title, const matrix3x3 &m)
{
    uint16_t i, j;
    if (title)
        std::cout << title << "=";
    for (i=0; i<3; i++)
    {
        for(j=0; j<3; j++)
            std::cout << " " << m[i][j];
        std::cout << std::endl;
    }
}

void printVector(const char *title, const vector3 &v)
{
    uint16_t i;
    if (title)
        std::cout << title << "=";
    for (i=0; i<3; i++)
        std::cout << " " << v[i];
    std::cout << std::endl;
}

void processTag(bool isTiff, uint16_t iiqTag, uint16_t dataType, uint32_t sizeBytes, uint8_t *data)
{
    uint32_t *ptr32 = (uint32_t*)data;

    vector3* wbVector = 0;
    matrix3x3* matrix = 0;

    switch (iiqTag) {
        case TIFF_Model:
            if (isTiff)
            {
                cameraModel = "Phase One ";
                cameraModel += (char*)data;
            }
            return;

        case IIQ_CamWhite:
            if (!extWB)
                wbVector = &stdWhiteDaylight;
            break;

        case IIQ_RommMatrix:
            matrix = &stdMatrixDaylight;
            break;

        case IIQ_RommThumbMatrix:
            matrix = &stdMatrixDaylightThumb;
            break;

        default:
            return;
    }

    if (wbVector && sizeBytes==3*sizeof(float))
    {
        uint32_t intVal = fromBigEndian(ptr32[0]);
        float* fPtr = (float*)(&intVal);
        (*wbVector)[0] = *fPtr ? 1.0 / *fPtr : *fPtr;
        intVal = fromBigEndian(ptr32[1]);
        (*wbVector)[1] = *fPtr ? 1.0 / *fPtr : *fPtr;
        intVal = fromBigEndian(ptr32[2]);
        (*wbVector)[2] = *fPtr ? 1.0 / *fPtr : *fPtr;

        if (wbVector->max() != 0)
            wbVector->scale(1/wbVector->max());
    }

    if (matrix && sizeBytes==9*sizeof(float))
    {
        uint32_t intVal = fromBigEndian(ptr32[0]);
        float* fPtr = (float*)(&intVal);
        (*matrix)[0][0] = *fPtr;
        intVal = fromBigEndian(ptr32[1]);
        (*matrix)[0][1] = *fPtr;
        intVal = fromBigEndian(ptr32[2]);
        (*matrix)[0][2] = *fPtr;
        intVal = fromBigEndian(ptr32[3]);
        (*matrix)[1][0] = *fPtr;
        intVal = fromBigEndian(ptr32[4]);
        (*matrix)[1][1] = *fPtr;
        intVal = fromBigEndian(ptr32[5]);
        (*matrix)[1][2] = *fPtr;
        intVal = fromBigEndian(ptr32[6]);
        (*matrix)[2][0] = *fPtr;
        intVal = fromBigEndian(ptr32[7]);
        (*matrix)[2][1] = *fPtr;
        intVal = fromBigEndian(ptr32[8]);
        (*matrix)[2][2] = *fPtr;

        // convert to RGB-->XYZ matrix
        *matrix = proPhotoMatrix * (*matrix);

        // normalise
        for (int i=0; i < 3; ++i)
        {
            double sum = 0.0;
            for (int j=0; j < 3; ++j)
                sum += (*matrix)[i][j];
            for (int j=0; j < 3; ++j)
                (*matrix)[i][j] /= sum;
        }

        // get max white XYZ (whitepoint)
        vector3 maxRGB(1, 1, 1);
        vector3 wp = (*matrix)*maxRGB;

        // adaptation
        *matrix = getAdaptationMatrix(wp, D50XYZ) * (*matrix);
    }
}

void processIiqIfd(uint8_t* buf, uint32_t size, uint32_t ifdOffset)
{
    uint8_t* end = buf + size;

    uint32_t entries = fromBigEndian(*(uint32_t*)(buf+ifdOffset));
    TIiqTagEntry* tagData = (TIiqTagEntry*)(buf+ifdOffset+8);

    while (entries > 0)
    {
        uint32_t iiqTag = fromBigEndian(tagData->tag);
        uint32_t data = fromBigEndian(tagData->data);
        uint32_t dataType = iiqTagDataTypes[iiqTag] > 0
                                ? iiqTagDataTypes[iiqTag]
                                : fromBigEndian(tagData->dataType);
        uint32_t sizeBytes = fromBigEndian(tagData->sizeBytes);
        if (sizeBytes <= 4)
            data = (uint8_t*)(&(tagData->data)) - buf;

        processTag(false, iiqTag, dataType, sizeBytes, buf + data);

        // calculate offset for next tag
        --entries;
        ++tagData;
        if ((uint8_t*)tagData > end)
            return;
    }
}

void processTiffIfd(uint8_t* buf, uint32_t size, uint32_t ifdOffset)
{
    uint8_t* end = buf + size;

    uint32_t entries = fromBigEndian(*(uint16_t*)(buf+ifdOffset));
    TTiffTagEntry* tagData = (TTiffTagEntry*)(buf+ifdOffset+2);

    while (entries > 0)
    {
        uint32_t tiffTag = fromBigEndian(tagData->tiffTag);
        uint32_t data = fromBigEndian(tagData->dataOffset);
        uint32_t dataType = fromBigEndian16(tagData->dataType);
        uint32_t sizeBytes = fromBigEndian(tagData->dataCount) * getTagDataSize(dataType);
        if (sizeBytes <= 4)
            data = (uint8_t*)(&(tagData->dataOffset)) - buf;

        processTag(true, tiffTag, dataType, sizeBytes, buf + data);

        // calculate offset for next tag
        --entries;
        ++tagData;
        if ((uint8_t*)tagData > end)
            return;
    }
}

bool processIiq(char* iiqFileName)
{
    FILE *file = 0;
    uint32_t inSize = (uint32_t)std::filesystem::file_size(iiqFileName);

    if (inSize)
    {
        if ((file = fopen(iiqFileName,"rb"))==NULL)
            return false;

        auto inBuf = std::make_unique<uint8_t[]>(inSize+4);
        uint32_t len=(uint32_t )fread(inBuf.get(), 1, inSize, file);
        fclose(file);

        if (inSize != len)
            return false;

        TTiffHeader* tiffHeader = (TTiffHeader*)inBuf.get();
        TIIQHeader* iiqHeader = (TIIQHeader*)(inBuf.get()+sizeof(TTiffHeader));

        bool validMagic = (tiffHeader->magic == TIFF_LITTLEENDIAN ||
                            tiffHeader->magic == TIFF_BIGENDIAN) &&
                            (iiqHeader->iiqMagic == IIQ_LITTLEENDIAN ||
                            iiqHeader->iiqMagic == IIQ_BIGENDIAN);

        bigEndian = iiqHeader->iiqMagic == IIQ_BIGENDIAN;

        if (!validMagic ||
            fromBigEndian(iiqHeader->rawMagic)>>8 != IIQ_RAW ||
            fromBigEndian(iiqHeader->dirOffset) == 0xbad0bad)
        {
            printf("The %s is not a IIQ file!\n", iiqFileName);
            return false;
        }

        processTiffIfd(inBuf.get(), inSize, fromBigEndian(tiffHeader->dirOffset));
        processIiqIfd((uint8_t*)iiqHeader,
                      (uint32_t)(inSize-sizeof(TTiffHeader)),
                      fromBigEndian(iiqHeader->dirOffset));
    }
    else
    {
        printf("Cannot read %s file!\n", iiqFileName);
        return false;
    }

    return true;
}

std::string makeDcpName(const char* profName)
{
    std::string name = profName;

    if (useLinearCurve && !doICC)
        name += " Linear";

    return name;
}

std::string makeName(const char* profName)
{
    std::string name = cameraModel;

    name += ' ';
    name += profName;

    if (useLinearCurve && !doICC)
        name += " Linear";

    return name;
}

void correctFileName(std::string &fileName)
{
    std::string::iterator iter = fileName.begin();
    while (iter != fileName.end())
    {
        if (*iter == ' ' || *iter == '\\' || *iter == '/')
            *iter = '_';
        ++iter;
    }
}

bool setTextICCTag(cmsHPROFILE hProfile, cmsTagSignature sig, const char* data)
{
    bool success = true;
    cmsMLU *mluStr = cmsMLUalloc(0, 1);

    if (mluStr)
    {
        success = success && cmsMLUsetASCII(mluStr, "en", "US", data);
        success = success && cmsWriteTag(hProfile, sig, mluStr);

        if (mluStr)
            cmsMLUfree(mluStr);
    }
    else
        success = false;

    return success;
}

void createICC(const char* profName, matrix3x3 &matrix)
{
    std::string name = makeName(profName);

    std::cout << "   Creating \"" << name << "\" ICC profile" << std::endl;

    bool success = true;

    cmsHPROFILE hICC;
    cmsCIEXYZTRIPLE Colorants;

    hICC = cmsCreateProfilePlaceholder(0);
    if (!hICC)
    {
        std::cerr << "Error creating \"" << name << "\" ICC profile" << std::endl;
        return;
    }

    // cmsSetProfileVersion(hICC, 4.3);
    cmsSetProfileVersion(hICC, 2.2);

    cmsSetDeviceClass(hICC,      cmsSigInputClass);
    cmsSetColorSpace(hICC,       cmsSigRgbData);
    cmsSetPCS(hICC,              cmsSigXYZData);

    cmsSetHeaderRenderingIntent(hICC,  INTENT_PERCEPTUAL);


    // Implement profile using following tags:
    //
    //  1 cmsSigProfileDescriptionTag
    //  2 cmsSigMediaWhitePointTag
    //  3 cmsSigRedColorantTag
    //  4 cmsSigGreenColorantTag
    //  5 cmsSigBlueColorantTag
    //  6 cmsSigRedTRCTag
    //  7 cmsSigGreenTRCTag
    //  8 cmsSigBlueTRCTag

    // text tags
    success = success && setTextICCTag(hICC, cmsSigProfileDescriptionTag, name.c_str());
    success = success && setTextICCTag(hICC, cmsSigDeviceModelDescTag, cameraModel.c_str());
    success = success && setTextICCTag(hICC, cmsSigCopyrightTag, "Free to use");

    // write D50 as white point
    success = success && cmsWriteTag(hICC, cmsSigMediaWhitePointTag, cmsD50_XYZ());

    // prepare XYZ matrix
    Colorants.Red.X   = matrix[0][0];
    Colorants.Red.Y   = matrix[1][0];
    Colorants.Red.Z   = matrix[2][0];

    Colorants.Green.X = matrix[0][1];
    Colorants.Green.Y = matrix[1][1];
    Colorants.Green.Z = matrix[2][1];

    Colorants.Blue.X  = matrix[0][2];
    Colorants.Blue.Y  = matrix[1][2];
    Colorants.Blue.Z  = matrix[2][2];

    success = success && cmsWriteTag(hICC, cmsSigRedColorantTag,   (void*) &Colorants.Red);
    success = success && cmsWriteTag(hICC, cmsSigGreenColorantTag, (void*) &Colorants.Green);
    success = success && cmsWriteTag(hICC, cmsSigBlueColorantTag,  (void*) &Colorants.Blue);

    // use gamma tone curve
    cmsToneCurve* tone = cmsBuildGamma(0, iccGamma);

    success = success && cmsWriteTag(hICC, cmsSigRedTRCTag, (void*)tone);
    success = success && cmsLinkTag (hICC, cmsSigGreenTRCTag, cmsSigRedTRCTag);
    success = success && cmsLinkTag (hICC, cmsSigBlueTRCTag, cmsSigRedTRCTag);

    if (tone)
        cmsFreeToneCurve(tone);

    // save to the file
    std::string fName = name + ".ICC";
    correctFileName(fName);

    success = success && cmsSaveProfileToFile(hICC, fName.c_str());

    if (hICC)
        cmsCloseProfile(hICC);

    if (!success)
        std::cerr << "Error saving \"" << fName << "\" file!" << std::endl;
}

void createDCP(const char* profName, uint32_t light, matrix3x3 &matrix, vector3 &wp)
{
    std::string name = makeName(profName);

    std::cout << "   Creating \"" << name << "\" DCP profile" << std::endl;

    bool success = true;

    // populate the profile
    DCPProfile dcp;
    dcp.name = makeDcpName(profName);
    dcp.cm[0] = wp.asDiagMatrix() * Invert(matrix);
    dcp.fm[0] = matrix;
    dcp.calIllum[0] = light;
    dcp.cameraModel = cameraModel.c_str();
    dcp.copyright = "Free to use";
    dcp.embedPolicy = pepNoRestrictions;
    if (useLinearCurve)
        dcp.toneCurve = { {0, 0}, {1, 1} };

    // write it to a file
    std::string fName = name + ".DCP";
    correctFileName(fName);
    dcp.writeToFile(fName);
}

void printUsage(char* appName)
{
    fprintf(stderr,"IIQ Profile extraction utility\n");
    fprintf(stderr,"Usage: %s -iglw [optional data] <IIQ file>\n", appName);
    fprintf(stderr,"  No options     - generates DCP profiles with Adobe curve\n");
    fprintf(stderr,"  -l             - generates DCP profiles with linear curve as opposed to Adobe standard\n");
    fprintf(stderr,"  -w <R> <G> <B> - specifies neutral white as R, G, B levels to use for DCP profile\n");
    fprintf(stderr,"  -w <R> <B>     - specifies neutral white as R, B exposure corrections (like RPP)\n");
    fprintf(stderr,"                   to use for DCP profile\n");
    fprintf(stderr,"  -i             - generates ICC profiles instead of DCP\n");
    fprintf(stderr,"  -g <gamma>     - specifies gamma (1-2.8) to use for TRC in ICC profile (1.8 by default)\n");
    fprintf(stderr,"                   (should only be used with -i option)\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 6 || (argc > 2 && argc <= 4 && *argv[1] != '-'))
    {
        printUsage(argv[0]);
        return 1;
    }

    // parse parameters
    bool hadGamma = false;
    bool wbLevelsValid = false;
    if (argc > 2)
    {
        char *param = argv[1]+1;

        while (*param)
        {
            switch (*param)
            {
            case 'i':
                doICC = true;
                break;

            case 'l':
                useLinearCurve = true;
                break;

            case 'g':
                iccGamma = atof(argv[2]);
                hadGamma = true;
                break;

            case 'w':
                if (argc == 6)
                {
                    stdWhiteDaylight[0] = atof(argv[2]);
                    stdWhiteDaylight[1] = atof(argv[3]);
                    stdWhiteDaylight[2] = atof(argv[4]);
                    wbLevelsValid = stdWhiteDaylight[0] > 0 &&
                                    stdWhiteDaylight[1] > 0 &&
                                    stdWhiteDaylight[2] > 0;
                }
                else if (argc == 5)
                {
                    // wb is specified RPP style R and B expocorrection in stops
                    stdWhiteDaylight[0] = pow(2, atof(argv[2]));
                    stdWhiteDaylight[1] = 1;
                    stdWhiteDaylight[2] = pow(2, atof(argv[3]));
                    wbLevelsValid = true;
                }
                extWB = true;
                break;

            default:
                printUsage(argv[0]);
                return 1;
            }
            param++;
        }
    }

    if ((doICC && hadGamma && argc!=4) || (!doICC && hadGamma) ||
        (extWB && !wbLevelsValid) || (doICC && extWB))
    {
        fprintf(stderr, "Invalid set of command line parameters!\n\n");
        printUsage(argv[0]);
        return 1;
    }
    if (hadGamma && (iccGamma < 1 || iccGamma > 2.8))
    {
        fprintf(stderr, "Invalid gamma value specified!\n\n");
        printUsage(argv[0]);
        return 1;
    }

    if (wbLevelsValid && stdWhiteDaylight.max() != 0)
        stdWhiteDaylight.scale(1/stdWhiteDaylight.max());

    char* iiqName = argv[argc-1];

    if (!processIiq(iiqName))
        return 1;

    std::cout << "Generating profiles from IIQ matrices..." << std::endl;

    if (doICC)
    {
        // create ICCs
        createICC("Daylight", stdMatrixDaylight);
        createICC("Daylight Thumb", stdMatrixDaylightThumb);
    }
    else
    {
        // create single illuminant DCPs
        createDCP("Daylight", lsD55, stdMatrixDaylight, stdWhiteDaylight);
        createDCP("Daylight Thumb", lsD55, stdMatrixDaylightThumb, stdWhiteDaylight);
    }

    std::cout << "...Done" << std::endl;

    return 0;
}

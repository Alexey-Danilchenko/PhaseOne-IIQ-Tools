/*
    iiqutils.cpp - IIQ File inspection utility for Phase One IIQ files

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
#ifndef IIQ_UTILS_H
#define IIQ_UTILS_H

#include <cstdint>

#pragma pack(push)
#pragma pack(1)

#define TIFF_VERSION_CLASSIC 42

#define TIFF_BIGENDIAN      0x4d4d
#define TIFF_LITTLEENDIAN   0x4949

#define IIQ_BIGENDIAN      0x4d4d4d4d
#define IIQ_LITTLEENDIAN   0x49494949

#define IIQ_RAW  0x526177

#define TAG_EXIF_IFD        34665
#define TAG_EXIF_MAKERNOTE  37500

// copy constructors in variable array structs
#pragma warning( disable : 4200 )

// All the structures for the types in binary files go in C style declaration
// to maintain struct packing and avoid C++ alignment/padding

// IIQ file structure:
//      TiffHeader
//      MakerNote with raw (IIQ header etc)
//      Tiff strips data
//      Tiff IFD + tag data
//      EXIF IFD + tag data

struct TTiffHeader
{
    uint16_t magic;      // magic number (defines byte order)
    uint16_t version;    // TIFF version number
    uint32_t dirOffset;  // byte offset to first directory
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
    TIFF_IFD       = 13,     // 32-bit unsigned integer (offset)

    // non standard ones - just to aid printing IIQ values
    IIQ_TIMESTAMP  = 128     // 32 bit integers timestamp from epoch
};

enum EIIQTag
{
    IIQ_Flip                     = 0x0100,
    IIQ_BodySerial               = 0x0102,
    IIQ_RommMatrix               = 0x0106,
    IIQ_CamWhite                 = 0x0107,
    IIQ_RawWidth                 = 0x0108,
    IIQ_RawHeight                = 0x0109,
    IIQ_LeftMargin               = 0x010a,
    IIQ_TopMargin                = 0x010b,
    IIQ_Width                    = 0x010c,
    IIQ_Height                   = 0x010d,
    IIQ_Format                   = 0x010e,
    IIQ_RawData                  = 0x010f,
    IIQ_CalibrationData          = 0x0110,
    IIQ_KeyOffset                = 0x0112,
    IIQ_Software                 = 0x0203,
    IIQ_SystemType               = 0x0204,
    IIQ_SensorTemperatureMax     = 0x0210,
    IIQ_SensorTemperatureMin     = 0x0211,
    IIQ_Aperture                 = 0x0401,
    IIQ_Tag21a                   = 0x021a,
    IIQ_StripOffset              = 0x021c,
    IIQ_BlackData                = 0x021d,
    IIQ_SplitColumn              = 0x0222,
    IIQ_BlackColumns             = 0x0223,
    IIQ_SplitRow                 = 0x0224,
    IIQ_BlackRows                = 0x0225,
    IIQ_RommThumbMatrix          = 0x0226,
    IIQ_FirmwareString           = 0x0301,
    IIQ_FocalLength              = 0x0403,
    IIQ_Body                     = 0x0410,
    IIQ_Lens                     = 0x0412,
    IIQ_MaxAperture              = 0x0414,
    IIQ_MinAperture              = 0x0415,
    IIQ_MinFocalLength           = 0x0416,
    IIQ_MaxFocalLength           = 0x0417
};

enum EIIQCalTag
{
    IIQ_Cal_DefectCorrection       = 0x0400,
    IIQ_Cal_LumaAllColourFlatField = 0x0401,
    IIQ_Cal_TimeCreated            = 0x0402,
    IIQ_Cal_TimeModified           = 0x0403,
    IIQ_Cal_SerialNumber           = 0x0407,
    IIQ_Cal_BlackGain              = 0x0408,
    IIQ_Cal_ChromaRedBlue          = 0x040b,
    IIQ_Cal_Luma                   = 0x0410,
    IIQ_Cal_XYZCorrection          = 0x0412,
    IIQ_Cal_LumaFlatField2         = 0x0416,
    IIQ_Cal_DualOutputPoly         = 0x0419,
    IIQ_Cal_PolynomialCurve        = 0x041a,
    IIQ_Cal_KelvinCorrection       = 0x041c,
    IIQ_Cal_OutputOffsetCorrection = 0x041b,
    IIQ_Cal_FourTileOutput         = 0x041e,
    IIQ_Cal_FourTileLinearisation  = 0x041f,
    IIQ_Cal_OutputCorrectCurve     = 0x0423,
    IIQ_Cal_FourTileTracking       = 0x042C,
    IIQ_Cal_FourTileGainLUT        = 0x0431
};

#pragma pack(pop)

#endif

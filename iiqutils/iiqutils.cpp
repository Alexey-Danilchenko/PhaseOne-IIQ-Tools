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
#define _CRT_SECURE_NO_WARNINGS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "iiqutils.h"
#include <ctime>
#include <filesystem>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <tuple>

bool doList = false;
bool doPrint = false;
bool doDecimal = false;
bool tagsExcluded = false;
bool doFormatKnown = false;
bool doPrintUnused = false;
bool doPrintRawRational = false;
bool doExtractCal = false;

std::set<uint16_t> tagNumbers;
std::vector<std::tuple<uint32_t, uint8_t*, uint32_t>> ifdEntries;

// Phase One developers unlike Kodak did not design this well - their
// adopted TIFF tag like system lacks consistent type definitions so
// much that P1 own development has to hardcode tag types in Capture
// One instead of using the types supplied in TIFF format.
// It is a real mess.
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

std::map<uint32_t, uint8_t> calTagDataTypes =
{
    // ASCII
    { 0x404, TIFF_ASCII },
    { 0x405, TIFF_ASCII },
    { 0x406, TIFF_ASCII },
    { 0x407, TIFF_ASCII },
    // INT32
    { 0x402, IIQ_TIMESTAMP },
    { 0x403, IIQ_TIMESTAMP },
    // INT16
    { 0x40F, TIFF_SHORT },
    { 0x418, TIFF_SHORT },
    { 0x400, TIFF_SHORT },
    { 0x416, TIFF_SHORT },
    { 0x410, TIFF_SHORT },
    { 0x40B, TIFF_SHORT },
    // float
    { 0x041c, TIFF_FLOAT},
    // double
    { 0x408, TIFF_DOUBLE },
    { 0x413, TIFF_DOUBLE }
};

// TIFF tag number to name map
struct TTiffTagName
{
    const char* tagName;
    uint32_t    tagNumber;
};

static const TTiffTagName standardTagNames[] =
{
    { "TIFFTAG_SUBFILETYPE",                254 },
    { "TIFFTAG_OSUBFILETYPE",               255 },
    { "TIFFTAG_IMAGEWIDTH",                 256 },
    { "TIFFTAG_IMAGELENGTH",                257 },
    { "TIFFTAG_BITSPERSAMPLE",              258 },
    { "TIFFTAG_COMPRESSION",                259 },
    { "TIFFTAG_PHOTOMETRIC",                262 },
    { "TIFFTAG_THRESHHOLDING",              263 },
    { "TIFFTAG_CELLWIDTH",                  264 },
    { "TIFFTAG_CELLLENGTH",                 265 },
    { "TIFFTAG_FILLORDER",                  266 },
    { "TIFFTAG_DOCUMENTNAME",               269 },
    { "TIFFTAG_IMAGEDESCRIPTION",           270 },
    { "TIFFTAG_MAKE",                       271 },
    { "TIFFTAG_MODEL",                      272 },
    { "TIFFTAG_STRIPOFFSETS",               273 },
    { "TIFFTAG_ORIENTATION",                274 },
    { "TIFFTAG_SAMPLESPERPIXEL",            277 },
    { "TIFFTAG_ROWSPERSTRIP",               278 },
    { "TIFFTAG_STRIPBYTECOUNTS",            279 },
    { "TIFFTAG_MINSAMPLEVALUE",             280 },
    { "TIFFTAG_MAXSAMPLEVALUE",             281 },
    { "TIFFTAG_XRESOLUTION",                282 },
    { "TIFFTAG_YRESOLUTION",                283 },
    { "TIFFTAG_PLANARCONFIG",               284 },
    { "TIFFTAG_PAGENAME",                   285 },
    { "TIFFTAG_XPOSITION",                  286 },
    { "TIFFTAG_YPOSITION",                  287 },
    { "TIFFTAG_FREEOFFSETS",                288 },
    { "TIFFTAG_FREEBYTECOUNTS",             289 },
    { "TIFFTAG_GRAYRESPONSEUNIT",           290 },
    { "TIFFTAG_GRAYRESPONSECURVE",          291 },
    { "TIFFTAG_GROUP3OPTIONS",              292 },
    { "TIFFTAG_T4OPTIONS",                  292 },
    { "TIFFTAG_GROUP4OPTIONS",              293 },
    { "TIFFTAG_T6OPTIONS",                  293 },
    { "TIFFTAG_RESOLUTIONUNIT",             296 },
    { "TIFFTAG_PAGENUMBER",                 297 },
    { "TIFFTAG_COLORRESPONSEUNIT",          300 },
    { "TIFFTAG_TRANSFERFUNCTION",           301 },
    { "TIFFTAG_SOFTWARE",                   305 },
    { "TIFFTAG_DATETIME",                   306 },
    { "TIFFTAG_ARTIST",                     315 },
    { "TIFFTAG_HOSTCOMPUTER",               316 },
    { "TIFFTAG_PREDICTOR",                  317 },
    { "TIFFTAG_WHITEPOINT",                 318 },
    { "TIFFTAG_PRIMARYCHROMATICITIES",      319 },
    { "TIFFTAG_COLORMAP",                   320 },
    { "TIFFTAG_HALFTONEHINTS",              321 },
    { "TIFFTAG_TILEWIDTH",                  322 },
    { "TIFFTAG_TILELENGTH",                 323 },
    { "TIFFTAG_TILEOFFSETS",                324 },
    { "TIFFTAG_TILEBYTECOUNTS",             325 },
    { "TIFFTAG_BADFAXLINES",                326 },
    { "TIFFTAG_CLEANFAXDATA",               327 },
    { "TIFFTAG_CONSECUTIVEBADFAXLINES",     328 },
    { "TIFFTAG_SUBIFD",                     330 },
    { "TIFFTAG_INKSET",                     332 },
    { "TIFFTAG_INKNAMES",                   333 },
    { "TIFFTAG_NUMBEROFINKS",               334 },
    { "TIFFTAG_DOTRANGE",                   336 },
    { "TIFFTAG_TARGETPRINTER",              337 },
    { "TIFFTAG_EXTRASAMPLES",               338 },
    { "TIFFTAG_SAMPLEFORMAT",               339 },
    { "TIFFTAG_SMINSAMPLEVALUE",            340 },
    { "TIFFTAG_SMAXSAMPLEVALUE",            341 },
    { "TIFFTAG_CLIPPATH",                   343 },
    { "TIFFTAG_XCLIPPATHUNITS",             344 },
    { "TIFFTAG_YCLIPPATHUNITS",             345 },
    { "TIFFTAG_INDEXED",                    346 },
    { "TIFFTAG_JPEGTABLES",                 347 },
    { "TIFFTAG_OPIPROXY",                   351 },
    { "TIFFTAG_GLOBALPARAMETERSIFD",        400 },
    { "TIFFTAG_PROFILETYPE",                401 },
    { "TIFFTAG_FAXPROFILE",                 402 },
    { "TIFFTAG_CODINGMETHODS",              403 },
    { "TIFFTAG_VERSIONYEAR",                404 },
    { "TIFFTAG_MODENUMBER",                 405 },
    { "TIFFTAG_DECODE",                     433 },
    { "TIFFTAG_IMAGEBASECOLOR",             434 },
    { "TIFFTAG_T82OPTIONS",                 435 },
    { "TIFFTAG_JPEGPROC",                   512 },
    { "TIFFTAG_JPEGIFOFFSET",               513 },
    { "TIFFTAG_JPEGIFBYTECOUNT",            514 },
    { "TIFFTAG_JPEGRESTARTINTERVAL",        515 },
    { "TIFFTAG_JPEGLOSSLESSPREDICTORS",     517 },
    { "TIFFTAG_JPEGPOINTTRANSFORM",         518 },
    { "TIFFTAG_JPEGQTABLES",                519 },
    { "TIFFTAG_JPEGDCTABLES",               520 },
    { "TIFFTAG_JPEGACTABLES",               521 },
    { "TIFFTAG_YCBCRCOEFFICIENTS",          529 },
    { "TIFFTAG_YCBCRSUBSAMPLING",           530 },
    { "TIFFTAG_YCBCRPOSITIONING",           531 },
    { "TIFFTAG_REFERENCEBLACKWHITE",        532 },
    { "TIFFTAG_STRIPROWCOUNTS",             559 },
    { "TIFFTAG_XMLPACKET",                  700 },
    { "TIFFTAG_OPIIMAGEID",                 32781 },
    { "TIFFTAG_REFPTS",                     32953 },
    { "TIFFTAG_REGIONTACKPOINT",            32954 },
    { "TIFFTAG_REGIONWARPCORNERS",          32955 },
    { "TIFFTAG_REGIONAFFINE",               32956 },
    { "TIFFTAG_MATTEING",                   32995 },
    { "TIFFTAG_DATATYPE",                   32996 },
    { "TIFFTAG_IMAGEDEPTH",                 32997 },
    { "TIFFTAG_TILEDEPTH",                  32998 },
    { "TIFFTAG_PIXAR_IMAGEFULLWIDTH",       33300 },
    { "TIFFTAG_PIXAR_IMAGEFULLLENGTH",      33301 },
    { "TIFFTAG_PIXAR_TEXTUREFORMAT",        33302 },
    { "TIFFTAG_PIXAR_WRAPMODES",            33303 },
    { "TIFFTAG_PIXAR_FOVCOT",               33304 },
    { "TIFFTAG_PIXAR_MATRIX_WORLDTOSCREEN", 33305 },
    { "TIFFTAG_PIXAR_MATRIX_WORLDTOCAMERA", 33306 },
    { "TIFFTAG_WRITERSERIALNUMBER",         33405 },
    { "TIFFTAG_COPYRIGHT",                  33432 },
    { "TIFFTAG_RICHTIFFIPTC",               33723 },
    { "TIFFTAG_IT8SITE",                    34016 },
    { "TIFFTAG_IT8COLORSEQUENCE",           34017 },
    { "TIFFTAG_IT8HEADER",                  34018 },
    { "TIFFTAG_IT8RASTERPADDING",           34019 },
    { "TIFFTAG_IT8BITSPERRUNLENGTH",        34020 },
    { "TIFFTAG_IT8BITSPEREXTENDEDRUNLENGTH",34021 },
    { "TIFFTAG_IT8COLORTABLE",              34022 },
    { "TIFFTAG_IT8IMAGECOLORINDICATOR",     34023 },
    { "TIFFTAG_IT8BKGCOLORINDICATOR",       34024 },
    { "TIFFTAG_IT8IMAGECOLORVALUE",         34025 },
    { "TIFFTAG_IT8BKGCOLORVALUE",           34026 },
    { "TIFFTAG_IT8PIXELINTENSITYRANGE",     34027 },
    { "TIFFTAG_IT8TRANSPARENCYINDICATOR",   34028 },
    { "TIFFTAG_IT8COLORCHARACTERIZATION",   34029 },
    { "TIFFTAG_IT8HCUSAGE",                 34030 },
    { "TIFFTAG_IT8TRAPINDICATOR",           34031 },
    { "TIFFTAG_IT8CMYKEQUIVALENT",          34032 },
    { "TIFFTAG_FRAMECOUNT",                 34232 },
    { "TIFFTAG_PHOTOSHOP",                  34377 },
    { "TIFFTAG_EXIFIFD",                    34665 },
    { "TIFFTAG_ICCPROFILE",                 34675 },
    { "TIFFTAG_IMAGELAYER",                 34732 },
    { "TIFFTAG_JBIGOPTIONS",                34750 },
    { "TIFFTAG_GPSIFD",                     34853 },
    { "TIFFTAG_FAXRECVPARAMS",              34908 },
    { "TIFFTAG_FAXSUBADDRESS",              34909 },
    { "TIFFTAG_FAXRECVTIME",                34910 },
    { "TIFFTAG_FAXDCS",                     34911 },
    { "TIFFTAG_STONITS",                    37439 },
    { "TIFFTAG_FEDEX_EDR",                  34929 },
    { "TIFFTAG_INTEROPERABILITYIFD",        40965 },
    { "TIFFTAG_DNGVERSION",                 50706 },
    { "TIFFTAG_DNGBACKWARDVERSION",         50707 },
    { "TIFFTAG_UNIQUECAMERAMODEL",          50708 },
    { "TIFFTAG_LOCALIZEDCAMERAMODEL",       50709 },
    { "TIFFTAG_CFAPLANECOLOR",              50710 },
    { "TIFFTAG_CFALAYOUT",                  50711 },
    { "TIFFTAG_LINEARIZATIONTABLE",         50712 },
    { "TIFFTAG_BLACKLEVELREPEATDIM",        50713 },
    { "TIFFTAG_BLACKLEVEL",                 50714 },
    { "TIFFTAG_BLACKLEVELDELTAH",           50715 },
    { "TIFFTAG_BLACKLEVELDELTAV",           50716 },
    { "TIFFTAG_WHITELEVEL",                 50717 },
    { "TIFFTAG_DEFAULTSCALE",               50718 },
    { "TIFFTAG_DEFAULTCROPORIGIN",          50719 },
    { "TIFFTAG_DEFAULTCROPSIZE",            50720 },
    { "TIFFTAG_COLORMATRIX1",               50721 },
    { "TIFFTAG_COLORMATRIX2",               50722 },
    { "TIFFTAG_CAMERACALIBRATION1",         50723 },
    { "TIFFTAG_CAMERACALIBRATION2",         50724 },
    { "TIFFTAG_REDUCTIONMATRIX1",           50725 },
    { "TIFFTAG_REDUCTIONMATRIX2",           50726 },
    { "TIFFTAG_ANALOGBALANCE",              50727 },
    { "TIFFTAG_ASSHOTNEUTRAL",              50728 },
    { "TIFFTAG_ASSHOTWHITEXY",              50729 },
    { "TIFFTAG_BASELINEEXPOSURE",           50730 },
    { "TIFFTAG_BASELINENOISE",              50731 },
    { "TIFFTAG_BASELINESHARPNESS",          50732 },
    { "TIFFTAG_BAYERGREENSPLIT",            50733 },
    { "TIFFTAG_LINEARRESPONSELIMIT",        50734 },
    { "TIFFTAG_CAMERASERIALNUMBER",         50735 },
    { "TIFFTAG_LENSINFO",                   50736 },
    { "TIFFTAG_CHROMABLURRADIUS",           50737 },
    { "TIFFTAG_ANTIALIASSTRENGTH",          50738 },
    { "TIFFTAG_SHADOWSCALE",                50739 },
    { "TIFFTAG_DNGPRIVATEDATA",             50740 },
    { "TIFFTAG_MAKERNOTESAFETY",            50741 },
    { "TIFFTAG_CALIBRATIONILLUMINANT1",     50778 },
    { "TIFFTAG_CALIBRATIONILLUMINANT2",     50779 },
    { "TIFFTAG_BESTQUALITYSCALE",           50780 },
    { "TIFFTAG_RAWDATAUNIQUEID",            50781 },
    { "TIFFTAG_ORIGINALRAWFILENAME",        50827 },
    { "TIFFTAG_ORIGINALRAWFILEDATA",        50828 },
    { "TIFFTAG_ACTIVEAREA",                 50829 },
    { "TIFFTAG_MASKEDAREAS",                50830 },
    { "TIFFTAG_ASSHOTICCPROFILE",           50831 },
    { "TIFFTAG_ASSHOTPREPROFILEMATRIX",     50832 },
    { "TIFFTAG_CURRENTICCPROFILE",          50833 },
    { "TIFFTAG_CURRENTPREPROFILEMATRIX",    50834 },
    { "TIFFTAG_DCSHUESHIFTVALUES",          65535 },
    { "EXIFTAG_EXPOSURETIME",               33434 },
    { "EXIFTAG_FNUMBER",                    33437 },
    { "EXIFTAG_EXPOSUREPROGRAM",            34850 },
    { "EXIFTAG_SPECTRALSENSITIVITY",        34852 },
    { "EXIFTAG_ISOSPEEDRATINGS",            34855 },
    { "EXIFTAG_OECF",                       34856 },
    { "EXIFTAG_EXIFVERSION",                36864 },
    { "EXIFTAG_DATETIMEORIGINAL",           36867 },
    { "EXIFTAG_DATETIMEDIGITIZED",          36868 },
    { "EXIFTAG_COMPONENTSCONFIGURATION",    37121 },
    { "EXIFTAG_COMPRESSEDBITSPERPIXEL",     37122 },
    { "EXIFTAG_SHUTTERSPEEDVALUE",          37377 },
    { "EXIFTAG_APERTUREVALUE",              37378 },
    { "EXIFTAG_BRIGHTNESSVALUE",            37379 },
    { "EXIFTAG_EXPOSUREBIASVALUE",          37380 },
    { "EXIFTAG_MAXAPERTUREVALUE",           37381 },
    { "EXIFTAG_SUBJECTDISTANCE",            37382 },
    { "EXIFTAG_METERINGMODE",               37383 },
    { "EXIFTAG_LIGHTSOURCE",                37384 },
    { "EXIFTAG_FLASH",                      37385 },
    { "EXIFTAG_FOCALLENGTH",                37386 },
    { "EXIFTAG_SUBJECTAREA",                37396 },
    { "EXIFTAG_MAKERNOTE",                  37500 },
    { "EXIFTAG_USERCOMMENT",                37510 },
    { "EXIFTAG_SUBSECTIME",                 37520 },
    { "EXIFTAG_SUBSECTIMEORIGINAL",         37521 },
    { "EXIFTAG_SUBSECTIMEDIGITIZED",        37522 },
    { "EXIFTAG_FLASHPIXVERSION",            40960 },
    { "EXIFTAG_COLORSPACE",                 40961 },
    { "EXIFTAG_PIXELXDIMENSION",            40962 },
    { "EXIFTAG_PIXELYDIMENSION",            40963 },
    { "EXIFTAG_RELATEDSOUNDFILE",           40964 },
    { "EXIFTAG_FLASHENERGY",                41483 },
    { "EXIFTAG_SPATIALFREQUENCYRESPONSE",   41484 },
    { "EXIFTAG_FOCALPLANEXRESOLUTION",      41486 },
    { "EXIFTAG_FOCALPLANEYRESOLUTION",      41487 },
    { "EXIFTAG_FOCALPLANERESOLUTIONUNIT",   41488 },
    { "EXIFTAG_SUBJECTLOCATION",            41492 },
    { "EXIFTAG_EXPOSUREINDEX",              41493 },
    { "EXIFTAG_SENSINGMETHOD",              41495 },
    { "EXIFTAG_FILESOURCE",                 41728 },
    { "EXIFTAG_SCENETYPE",                  41729 },
    { "EXIFTAG_CFAPATTERN",                 41730 },
    { "EXIFTAG_CUSTOMRENDERED",             41985 },
    { "EXIFTAG_EXPOSUREMODE",               41986 },
    { "EXIFTAG_WHITEBALANCE",               41987 },
    { "EXIFTAG_DIGITALZOOMRATIO",           41988 },
    { "EXIFTAG_FOCALLENGTHIN35MMFILM",      41989 },
    { "EXIFTAG_SCENECAPTURETYPE",           41990 },
    { "EXIFTAG_GAINCONTROL",                41991 },
    { "EXIFTAG_CONTRAST",                   41992 },
    { "EXIFTAG_SATURATION",                 41993 },
    { "EXIFTAG_SHARPNESS",                  41994 },
    { "EXIFTAG_DEVICESETTINGDESCRIPTION",   41995 },
    { "EXIFTAG_SUBJECTDISTANCERANGE",       41996 },
    { "EXIFTAG_GAINCONTROL",                41991 },
    { "EXIFTAG_GAINCONTROL",                41991 },
    { "EXIFTAG_IMAGEUNIQUEID",              42016 }
};

static const TTiffTagName iiqTagNames[] =
{
    { "IIQ_Flip",                          0x0100 },
    { "IIQ_BodySerial",                    0x0102 },
    { "IIQ_RommMatrix",                    0x0106 },
    { "IIQ_CamWhite",                      0x0107 },
    { "IIQ_RawWidth",                      0x0108 },
    { "IIQ_RawHeight",                     0x0109 },
    { "IIQ_LeftMargin",                    0x010a },
    { "IIQ_TopMargin",                     0x010b },
    { "IIQ_Width",                         0x010c },
    { "IIQ_Height",                        0x010d },
    { "IIQ_Format",                        0x010e },
    { "IIQ_RawData",                       0x010f },
    { "IIQ_CalibrationData",               0x0110 },
    { "IIQ_KeyOffset",                     0x0112 },
    { "IIQ_Software",                      0x0203 },
    { "IIQ_SystemType",                    0x0204 },
    { "IIQ_SensorTemperatureMax",          0x0210 },
    { "IIQ_SensorTemperatureMin",          0x0211 },
    { "IIQ_Tag21a",                        0x021a },
    { "IIQ_StripOffset",                   0x021c },
    { "IIQ_BlackData",                     0x021d },
    { "IIQ_SplitColumn",                   0x0222 },
    { "IIQ_BlackColumns",                  0x0223 },
    { "IIQ_SplitRow",                      0x0224 },
    { "IIQ_BlackRows",                     0x0225 },
    { "IIQ_RommThumbMatrix",               0x0226 },
    { "IIQ_FirmwareString",                0x0301 },
    { "IIQ_Aperture",                      0x0401 },
    { "IIQ_FocalLength",                   0x0403 },
    { "IIQ_Body",                          0x0410 },
    { "IIQ_Lens",                          0x0412 },
    { "IIQ_MaxAperture",                   0x0414 },
    { "IIQ_MinAperture",                   0x0415 },
    { "IIQ_MinFocalLength",                0x0416 },
    { "IIQ_MaxFocalLength",                0x0417 }
};

static const TTiffTagName calTagNames[] =
{
    { "IIQ_Cal_DefectCorrection",          0x0400 },
    { "IIQ_Cal_LumaAllColourFlatField",    0x0401 },
    { "IIQ_Cal_TimeCreated",               0x0402 },
    { "IIQ_Cal_TimeModified",              0x0403 },
    { "IIQ_Cal_SerialNumber",              0x0407 },
    { "IIQ_Cal_BlackGain",                 0x0408 },
    { "IIQ_Cal_ChromaRedBlue",             0x040b },
    { "IIQ_Cal_Luma",                      0x0410 },
    { "IIQ_Cal_XYZCorrection",             0x0412 },
    { "IIQ_Cal_LumaFlatField2",            0x0416 },
    { "IIQ_Cal_DualOutputPoly",            0x0419 },
    { "IIQ_Cal_PolynomialCurve",           0x041a },
    { "IIQ_Cal_KelvinCorrection",          0x041c },
    { "IIQ_Cal_OutputOffsetCorrection",    0x041b },
    { "IIQ_Cal_FourTileOutput",            0x041e },
    { "IIQ_Cal_FourTileLinearisation",     0x041f },
    { "IIQ_Cal_OutputCorrectCurve",        0x0423 },
    { "IIQ_Cal_FourTileTracking",          0x042C },
    { "IIQ_Cal_FourTileGainLUT",           0x0431 }
};

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

const std::map<uint32_t, const char*> tiffTagDataTypeNames =
{
    { TIFF_NOTYPE   , "?" },
    { TIFF_BYTE     , "Byte" },
    { TIFF_ASCII    , "ASCII" },
    { TIFF_SHORT    , "Short" },
    { TIFF_LONG     , "Long" },
    { TIFF_RATIONAL , "Rational" },
    { TIFF_SBYTE    , "Signed uint8_t" },
    { TIFF_UNDEFINED, "Undefined" },
    { TIFF_SSHORT   , "Signed short" },
    { TIFF_SLONG    , "Signed long" },
    { TIFF_SRATIONAL, "Signed rational" },
    { TIFF_FLOAT    , "Float" },
    { TIFF_DOUBLE   , "Double" },
    { IIQ_TIMESTAMP , "Timestamp" }
};

const std::map<uint32_t, uint32_t> tagDataSize =
{
    { TIFF_NOTYPE   , 1 },
    { TIFF_BYTE     , 1 },
    { TIFF_ASCII    , 1 },
    { TIFF_SHORT    , 2 },
    { TIFF_LONG     , 4 },
    { TIFF_RATIONAL , 8 },
    { TIFF_SBYTE    , 1 },
    { TIFF_UNDEFINED, 1 },
    { TIFF_SSHORT   , 2 },
    { TIFF_SLONG    , 4 },
    { TIFF_SRATIONAL, 8 },
    { TIFF_FLOAT    , 4 },
    { TIFF_DOUBLE   , 8 },
    { IIQ_TIMESTAMP , 4 }
};

// Endianness
static bool bigEndian = false;

// tag name context
static uint32_t tagNameContext = 0;

static std::string bodySerial;

uint16_t fromBigEndian16(uint16_t ulValue) {
    if (!bigEndian)
        return ulValue;

    uint8_t *tmp = (uint8_t*) & ulValue;

    // convert from big endian
    return ((uint16_t)tmp[0] << 8) | (uint16_t)tmp[1];
}

uint32_t fromBigEndian(uint32_t ulValue) {
    if (!bigEndian)
        return ulValue;

    unsigned char *tmp = (unsigned char*) & ulValue;

    // convert from big endian
    return ((uint32_t)tmp[0] << 24) | ((uint32_t)tmp[1] << 16) | ((uint32_t)tmp[2] << 8) | (uint32_t)tmp[3];
}

uint64_t fromBigEndian64(uint64_t ulValue) {
    if (!bigEndian)
        return ulValue;

    unsigned char *tmp = (unsigned char*) & ulValue;

    // convert from big endian
    return ((uint64_t)tmp[0] << 56) | ((uint64_t)tmp[1] << 48) | ((uint64_t)tmp[2] << 40) | ((uint64_t)tmp[2] << 32) |
           ((uint64_t)tmp[4] << 24) | ((uint64_t)tmp[5] << 16) | ((uint64_t)tmp[6] << 8)  | (uint64_t)tmp[7];
}

static char *iiqFileName = 0;

const char* getTiffTagName(uint32_t tagNumber)
{
    if (tagNameContext == TAG_EXIF_MAKERNOTE)
    {
        // IIQ tags
        int tagsCount = sizeof(iiqTagNames) / sizeof(TTiffTagName);
        for (int i=0; i<tagsCount; ++i)
        {
            if (iiqTagNames[i].tagNumber == tagNumber)
                return iiqTagNames[i].tagName;
        }
    }

    if (tagNameContext == IIQ_CalibrationData)
    {
        int tagsCount = sizeof(calTagNames) / sizeof(TTiffTagName);
        for (int i=0; i<tagsCount; ++i)
        {
            if (calTagNames[i].tagNumber == tagNumber)
                return calTagNames[i].tagName;
        }
    }

    // functional tags
    // at last lookup standard ones
    int tagsCount = sizeof(standardTagNames) / sizeof(TTiffTagName);
    for (int i=0; i<tagsCount; ++i)
    {
        if (standardTagNames[i].tagNumber == tagNumber)
            return standardTagNames[i].tagName;
    }

    return "Unknown";
}

const char* getTagDataTypeName(uint32_t dataType)
{
    auto it = tiffTagDataTypeNames.find(dataType);
    return it == tiffTagDataTypeNames.cend() ? "?" : it->second;
}

uint32_t getTagDataSize(uint32_t dataType)
{
    auto it = tagDataSize.find(dataType);
    return it == tagDataSize.cend() ? 1 : it->second;
}

void listTag(uint16_t tiffTag, uint16_t dataType, uint32_t sizeBytes, uint32_t dataOffset)
{
    if (doPrint)
        printf("Tag: %d (%X) : %s, Datatype: %s, Size(bytes): %u (%X), Offset: %X, Data:\n",
               tiffTag,
               tiffTag,
               getTiffTagName(tiffTag),
               getTagDataTypeName(dataType),
               sizeBytes,
               sizeBytes,
               dataOffset);
    else
        printf("Tag: %5d (%4X) : %-40s, Datatype: %-15s, Size(bytes): %8u (%6X), Offset: %08X\n",
               tiffTag,
               tiffTag,
               getTiffTagName(tiffTag),
               getTagDataTypeName(dataType),
               sizeBytes,
               sizeBytes,
               dataOffset);
}

void printfRational(uint32_t n, uint32_t d, bool isSigned)
{
    uint32_t uN = fromBigEndian(n);
    uint32_t uD = fromBigEndian(d);
    double val = isSigned? 0.0 : double(uN)/uD;

    if (isSigned)
    {
        int32_t* iN = (int32_t*)&uN;
        int32_t* iD = (int32_t*)&uD;
        val = double(*iN)/(*iD);
    }
    printf("%f", val);
}

void printfHexValue(bool alignData, uint16_t dataType, void *data, uint32_t index=0)
{
    static char str[20];
    uint8_t   *ptr8  = (uint8_t*)data;
    uint16_t *ptr16 = (uint16_t*)data;
    uint32_t *ptr32 = (uint32_t*)data;
    uint64_t* ptr64 = (uint64_t*)data;
    *str = 0;
    switch (dataType)
    {
        case TIFF_BYTE:
        case TIFF_UNDEFINED:
        case TIFF_SBYTE:
            if (ptr8[index])
                sprintf(str, "0x%hhX", ptr8[index]);
            else
                strcpy(str,"0");
            if (alignData)
                printf("%4s", str);
            else
                printf("%s", str);
            break;

        case TIFF_SHORT:
        case TIFF_SSHORT:
            if (ptr16[index])
                sprintf(str, "0x%hX", fromBigEndian16(ptr16[index]));
            else
                strcpy(str,"0");
            if (alignData)
                printf("%6s", str);
            else
                printf("%s", str);
            break;

        case TIFF_LONG:
        case TIFF_SLONG:
        case TIFF_FLOAT:
            if (ptr32[index])
                sprintf(str, "0x%X", fromBigEndian(ptr32[index]));
            else
                strcpy(str,"0");
            if (alignData)
                printf("%10s", str);
            else
                printf("%s", str);
            break;

        case TIFF_DOUBLE:
            if (ptr64[index])
                sprintf(str, "0x%llX", fromBigEndian64(ptr64[index]));
            else
                strcpy(str, "0");
            if (alignData)
                printf("%18s", str);
            else
                printf("%s", str);
            break;

        case TIFF_RATIONAL:
        case TIFF_SRATIONAL:
            if (doPrintRawRational)
                printf("%X/%X", fromBigEndian(ptr32[index*2]), fromBigEndian(ptr32[index*2+1]));
            else
                printfRational(ptr32[index*2], ptr32[index*2+1], dataType==TIFF_SRATIONAL);
            break;
    }
}

void printfDecimalValue(bool alignData, uint16_t dataType, void *data, uint32_t index=0)
{
    uint8_t   *ptr8  = (uint8_t*)data;
    uint16_t *ptr16 = (uint16_t*)data;
    uint32_t *ptr32 = (uint32_t*)data;
    uint64_t* ptr64 = (uint64_t*)data;

    switch (dataType)
    {
        case TIFF_BYTE:
        case TIFF_UNDEFINED:
            if (alignData)
                printf("%3hhu", ptr8[index]);
            else
                printf("%hhu", ptr8[index]);
            break;

        case TIFF_SBYTE:
            if (alignData)
                printf("%4hhd", ptr8[index]);
            else
                printf("%hhd", ptr8[index]);
            break;

        case TIFF_SHORT:
            if (alignData)
                printf("%5hu", fromBigEndian16(ptr16[index]));
            else
                printf("%hu", fromBigEndian16(ptr16[index]));
            break;

        case TIFF_SSHORT:
            if (alignData)
                printf("%6hd", fromBigEndian16(ptr16[index]));
            else
                printf("%hd", fromBigEndian16(ptr16[index]));
            break;

        case TIFF_LONG:
            if (alignData)
                printf("%10u", fromBigEndian(ptr32[index]));
            else
                printf("%u", fromBigEndian(ptr32[index]));
            break;

        case TIFF_SLONG:
            if (alignData)
                printf("%11d", fromBigEndian(ptr32[index]));
            else
                printf("%d", fromBigEndian(ptr32[index]));
            break;

        case TIFF_RATIONAL:
            if (doPrintRawRational)
                printf("%u/%u", fromBigEndian(ptr32[index*2]), fromBigEndian(ptr32[index*2+1]));
            else
            {
                double val = (double)fromBigEndian(ptr32[index*2]) / fromBigEndian(ptr32[index*2+1]);
                printf("%f", val);
            }
            break;

        case TIFF_SRATIONAL:
            if (doPrintRawRational)
                printf("%d/%d", fromBigEndian(ptr32[index*2]), fromBigEndian(ptr32[index*2+1]));
            else
                printfRational(ptr32[index*2], ptr32[index*2+1], dataType==TIFF_SRATIONAL);
            break;
    }
}

inline void printfHexValue(uint16_t dataType, void *data, uint32_t index=0)
{
    printfHexValue(true, dataType, data, index);
}

inline void printfDecimalValue(uint16_t dataType, void *data, uint32_t index=0)
{
    printfDecimalValue(true, dataType, data, index);
}

void printDefectList(void *data, uint32_t sizeBytes)
{
    // Defect List printing
    TDefectEntry *defList = (TDefectEntry*)data;
    uint32_t defectCount = sizeBytes / sizeof(TDefectEntry);
    uint8_t prevDefectType = -1;
    std::map<uint16_t,std::vector<TDefectEntry*>> defects;

    for (uint32_t i=0; i<defectCount; ++i, ++defList)
        defects[fromBigEndian16(defList->defectType)].emplace_back(defList);

    printf("    Total defects: %d", defectCount);
    for (const auto& [type, list] : defects)
    {
        printf("\n");
        switch(type)
        {
            case DEF_COL:
            case DEF_COL_2:
            case DEF_COL_3:
            case DEF_COL_4:
                 printf("    Column defects (type: %d, count: %d):\n    {\n", type, (int)list.size());
                 break;
            case DEF_PIXEL:
                 printf("    Pixel defects (type: %d, count: %d):\n    {\n", type, (int)list.size());
                 break;
            case DEF_PIXEL_ROW:
                 printf("    Pixel row defects (type: %d, count: %d):\n    {\n", type, (int)list.size());
                 break;
            case DEF_PIXEL_ISO:
                 printf("    Pixel ISO defects (type: %d, count: %d):\n    {\n", type, (int)list.size());
                 break;
            default:
                 printf("    Other type of defects (type: %d, count: %d):\n    {\n", type, (int)list.size());
                 break;
        }
        for (const auto& defect : list)
        {
            printf("        ");
            switch(type)
            {
                case DEF_COL:
                case DEF_COL_2:
                case DEF_COL_3:
                case DEF_COL_4:
                case DEF_PIXEL:
                     printf("col: %hu, row: %hu, extra: %hd",
                            fromBigEndian16(defect->col),
                            fromBigEndian16(defect->row),
                            fromBigEndian16(defect->extra));
                     break;
                case DEF_PIXEL_ROW:
                     printf("col: %hu, rows: %hu - $hu",
                            fromBigEndian16(defect->col),
                            (uint16_t)(fromBigEndian16(defect->row)+fromBigEndian16(defect->extra)));
                     break;
                case DEF_PIXEL_ISO:
                     printf("col: %hu, row: %hu, applicable for ISO >= %hd",
                            fromBigEndian16(defect->col),
                            fromBigEndian16(defect->row),
                            (uint16_t)(fromBigEndian16(defect->row)+fromBigEndian16(defect->extra)));
                     break;
                default:
                     printf("col: %hu, row: %hu, extra: %hd",
                            fromBigEndian16(defect->col),
                            fromBigEndian16(defect->row),
                            fromBigEndian16(defect->extra));
                     break;
            }
            printf("\n");
        }
        printf("    }");
    }
}

bool printKnownTag(uint16_t tag, uint32_t sizeBytes, void *data)
{
    uint8_t valuesPerLine = 8;
    bool success = false;

    if (tagNameContext == IIQ_CalibrationData && tag == IIQ_Cal_DefectCorrection)
    {
        printDefectList(data, sizeBytes);
        success = true;
    }

    return success;
}

void printTag(uint16_t tiffTag, uint16_t dataType, uint32_t sizeBytes, uint8_t *data)
{
    uint16_t *ptr16 = (uint16_t*)data;
    uint32_t *ptr32 = (uint32_t*)data;
    uint64_t *ptr64 = (uint64_t*)data;
    uint32_t tmp32 = 0;
    uint64_t tmp64 = 0;
    uint8_t valuesPerLine = 16;
    float* tmpFloat = (float*)&tmp32;
    double* tmpDouble = (double*)&tmp64;

    if (dataType)
    {
        if (dataType==TIFF_RATIONAL || dataType==TIFF_SRATIONAL ||
            dataType==TIFF_FLOAT || dataType==TIFF_DOUBLE)
            valuesPerLine = 8;
        else if (dataType == IIQ_TIMESTAMP)
            valuesPerLine = 1;

        if (!doList)
            printf("Tag: %d (%X) : %s, Datatype: %s, Count: %u (%X), Data:\n",
                   tiffTag,
                   tiffTag,
                   getTiffTagName(tiffTag),
                   getTagDataTypeName(dataType),
                   sizeBytes,
                   sizeBytes);

        printf("{\n");
        if (dataType == TIFF_ASCII)
            printf("     \"%s\"\n", data);
        else
        {
            bool printed = false;

            if (doFormatKnown)
                printed = printKnownTag(tiffTag, sizeBytes, data);

            if (!printed)
            {
                printf("     ");
                for (uint32_t i=0; i<sizeBytes/getTagDataSize(dataType); ++i)
                {
                    if (i)
                    {
                        printf(", ");
                        if (!(i%valuesPerLine))
                            printf("\n     ");
                    }

                    if (dataType == IIQ_TIMESTAMP)
                    {
                        char timestr[64] = {0};
                        std::time_t timestamp = fromBigEndian(ptr32[i]);
                        strftime(timestr, sizeof(timestr), "%a %b %e %H:%M:%S %Y",
                                                           std::localtime(&timestamp));
                        printf("\"%s\"", timestr);
                    }
                    else if (dataType == TIFF_FLOAT)
                    {
                        tmp32 = fromBigEndian(ptr32[i]);
                        printf("%f", (double)(*tmpFloat));
                    }
                    else if (dataType == TIFF_DOUBLE)
                    {
                        tmp64 = fromBigEndian64(ptr64[i]);
                        printf("%f", *tmpDouble);
                    }
                    else if (doDecimal)
                        printfDecimalValue(dataType, data, i);
                    else
                        printfHexValue(dataType, data, i);
                }
            }
        }

        printf("\n}\n\n");
    }
}

void writeCalibFile(void* data, uint32_t dataSize)
{
    std::string fName = bodySerial.empty() ? "calibration" : bodySerial.c_str();
    fName += ".cal";

    FILE *cal = fopen(fName.c_str(),"wb");

    if (cal)
    {
        fwrite(data, 1, dataSize, cal);
        fclose(cal);
    }
}

void processIiqCalIfd(uint8_t* buf, uint32_t size, uint32_t ifdOffset)
{
    uint8_t* end = buf + size;

    uint32_t entries = fromBigEndian(*(uint32_t*)(buf+ifdOffset));
    TIiqCalTagEntry* tagData = (TIiqCalTagEntry*)(buf+ifdOffset+8);

    while (entries > 0)
    {
        uint32_t iiqTag = fromBigEndian(tagData->tag);
        uint32_t data = fromBigEndian(tagData->data);
        uint32_t sizeBytes = fromBigEndian(tagData->sizeBytes);
        uint32_t dataType = calTagDataTypes[iiqTag] > 0
                                ? calTagDataTypes[iiqTag]
                                : 1;

        if (sizeBytes == 0)
        {
            data = (uint8_t*)(&(tagData->data)) - buf;
            sizeBytes = 4;
        }

        if (tagNumbers.size() == 0 ||
            (tagsExcluded && tagNumbers.find(iiqTag) == tagNumbers.end()) ||
            (!tagsExcluded && tagNumbers.find(iiqTag) != tagNumbers.end()))
        {
            if (doList)
                listTag(iiqTag, dataType, sizeBytes, data);
            if (doPrint)
                printTag(iiqTag, dataType, sizeBytes, buf + data);
        }

        // calculate offset for next tag
        --entries;
        ++tagData;
        if ((uint8_t*)tagData > end)
            return;
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

        if (tagNumbers.size() == 0 ||
            (tagsExcluded && tagNumbers.find(iiqTag) == tagNumbers.end()) ||
            (!tagsExcluded && tagNumbers.find(iiqTag) != tagNumbers.end()))
        {
            if (doList)
                listTag(iiqTag, dataType, sizeBytes, data);
            if (doPrint && iiqTag != IIQ_RawData && iiqTag != IIQ_CalibrationData)
                printTag(iiqTag, dataType, sizeBytes, buf + data);
        }

        // add extra IFDs
        if (iiqTag == IIQ_CalibrationData && buf + data + sizeBytes < end)
            ifdEntries.emplace_back(iiqTag, buf + data, sizeBytes);

        if (iiqTag == IIQ_BodySerial)
            bodySerial = std::string((const char*)(buf + data), sizeBytes);

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

        if (tagNumbers.size() == 0 ||
            (tagsExcluded && tagNumbers.find(tiffTag) == tagNumbers.end()) ||
            (!tagsExcluded && tagNumbers.find(tiffTag) != tagNumbers.end()))
        {
            if (doList)
                listTag(tiffTag, dataType, sizeBytes, data);
            if (doPrint && tiffTag != TAG_EXIF_MAKERNOTE)
                printTag(tiffTag, dataType, sizeBytes, buf + data);
        }

        // add extra IFDs
        if (tiffTag == TAG_EXIF_IFD && buf + data + sizeBytes < end)
            ifdEntries.emplace_back(tiffTag, buf + fromBigEndian(tagData->dataOffset), 0);
        if (tiffTag == TAG_EXIF_MAKERNOTE && buf + data + sizeBytes < end)
            ifdEntries.emplace_back(tiffTag, buf + data, sizeBytes);

        // calculate offset for next tag
        --entries;
        ++tagData;
        if ((uint8_t*)tagData > end)
            return;
    }
}

void processIfd(uint8_t* inBuf, uint32_t inSize)
{
    while (!ifdEntries.empty())
    {
        auto [tag, buf, size] = ifdEntries.back();
        ifdEntries.pop_back();

        uint32_t ifdOffset = buf-inBuf;

        if (tag == TAG_EXIF_MAKERNOTE || tag == IIQ_CalibrationData)
        {
            TIIQHeader* iiqHeader = (TIIQHeader*)buf;
            bigEndian = iiqHeader->iiqMagic == IIQ_BIGENDIAN;
            if ((iiqHeader->iiqMagic != IIQ_LITTLEENDIAN &&
                iiqHeader->iiqMagic != IIQ_BIGENDIAN)  ||
                fromBigEndian(iiqHeader->dirOffset) == 0xbad0bad)
            {
                printf("The %d(%X) tag is not a IIQ entity!\n", tag, tag);
                continue;
            }

            if (doExtractCal && tag == IIQ_CalibrationData)
                writeCalibFile(buf, size);

            ifdOffset = fromBigEndian(iiqHeader->dirOffset);
        }
        else
            buf = inBuf;

        printf("---------------------------------------------------------------\n");
        if (tag == 0)
            printf("    Main directory at %X offset:\n", ifdOffset);
        else
            printf(" Tag %s %d(%X) directory at %X offset:\n",
                   getTiffTagName(tag), tag, tag, (int)(buf-inBuf+ifdOffset));
        printf("---------------------------------------------------------------\n");

        tagNameContext = tag;

        if (tag == IIQ_CalibrationData)
            processIiqCalIfd(buf, size, ifdOffset);
        else if (tag == TAG_EXIF_MAKERNOTE)
            processIiqIfd(buf, size, ifdOffset);
        else
            processTiffIfd(inBuf, inSize, ifdOffset);
        printf("\n");
    }
}

bool parseTags(char* tags)
{
    bool success = true;
    char* token = strtok(tags,",");
    while (token != NULL)
    {
        uint16_t tag = 0;
        uint16_t tag2 = 0;
        if (strstr(token, "0x") != NULL)
            // in hex
            if (sscanf(token, "0x%hx-0x%hx", &tag, &tag2) == 2)
                // add range
                for (uint16_t i=tag; i<=tag2; i++)
                    tagNumbers.insert(i);
            else if (sscanf(token, "0x%hx", &tag) == 1)
                tagNumbers.insert(tag);
            else
                success = false;
        else
            // in decimal
            if (sscanf(token, "%hu-%hu", &tag, &tag2) == 2)
                // add range
                for (uint16_t i=tag; i<=tag2; i++)
                    tagNumbers.insert(i);
            else if (sscanf(token, "%hu", &tag) == 1)
                tagNumbers.insert(tag);
            else
                success = false;

        token = strtok(NULL,",");
    }

    return success;
}

inline void printHelp()
{
    printf("iiqutils -clpdxfur <filename.IIQ> [tag1,tag2-tag3,...]\n\n");
    printf("Options (can be combined in any way):\n"
           "        -c - extract the calibration file (written as <back serial>.cal)\n"
           "        -l - list contents of the IIQ file (tags)\n"
           "        -p - prints contents of the tags in IIQ file\n"
           "        -d - prints tag values in decimal rather than hexadecimal\n"
           "        -x - treats specified tag range as excluded (default included)\n"
           "        -f - formats printed data structures for known tags\n"
           "        -u - prints unused/uknown values when -f is specified\n"
           "        -r - prints rational numbers as rations as opposed to calculate the values\n\n"
           "The tag range is optional and if specified will be used to limit scope of the options.\n"
           "The tags in a range can either be decimal or, if preceeded by 0x, hexadecimal.\n"
           "The tag values for float/double data types are always printed in decimal.\n");
}

bool parseCmdLine(int argc, char* argv[])
{
    bool paramError = false;

    if (argc>1 && argc<4)
    {
        if (*argv[1] == '-')
        {
            char *param = argv[1]+1;

            while (*param && !paramError)
            {
                switch (*param)
                {
                    case 'c':
                        doExtractCal = true;
                        break;

                    case 'l':
                        doList = true;
                        break;

                    case 'p':
                        doPrint = true;
                        break;

                    case 'd':
                        doDecimal = true;
                        break;

                    case 'x':
                        tagsExcluded = true;
                        break;

                    case 'f':
                        doFormatKnown = true;
                        break;

                    case 'u':
                        doPrintUnused = true;
                        break;

                    case 'r':
                        doPrintRawRational = true;
                        break;

                    default:
                        paramError = true;
                        break;
                }
                param++;
            }

            if (argc>2)
                iiqFileName = argv[2];
            else
                paramError = true;

            if (!paramError)
            {
                if (argc>3)
                    paramError = !parseTags(argv[3]);

                if (tagNumbers.size() == 0 && tagsExcluded)
                    paramError = true;

            }
        }
        else
            paramError = true;
    }
    else
        paramError = true;

    if (paramError)
        printHelp();

    return !paramError;
}

int main(int argc, char* argv[])
{
    FILE *file = 0;
    uint8_t* inBuf = 0;
    uint32_t len = 0;

    if (parseCmdLine(argc, argv))
    {
        uint32_t inSize = (uint32_t)std::filesystem::file_size(iiqFileName);

        if (inSize)
        {
            if ((file = fopen(iiqFileName,"rb"))==NULL)
            {
                return 1;
            }

            inBuf = new uint8_t[inSize+4];
            memset(inBuf, 0, inSize+4);
            len=(int)fread(inBuf, 1, inSize, file);
            fclose(file);

            if (inSize != len)
            {
                delete[] inBuf;
                return 1;
            }

            TTiffHeader* tiffHeader = (TTiffHeader*)inBuf;
            TIIQHeader* iiqHeader = (TIIQHeader*)(inBuf+sizeof(TTiffHeader));

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
                delete[] inBuf;
                return 1;
            }

            ifdEntries.emplace_back(0, inBuf+fromBigEndian(tiffHeader->dirOffset), 0);
            processIfd(inBuf, inSize);

            delete[] inBuf;
        }
    }

    return 0;
}

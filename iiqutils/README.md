# IIQ Utilities

This is a command line utility that allows to explore and dump Phase One specific contents of the IIQ file sections and extract a digital back calibration files (those could be uploaded to back via Phase One Firmware Update utility if the back supports calibration data in case something goes wrong - good for backup). It can also do the same for calibration file if specified instead of IIQ (dump contents etc).

## Running IIQ Utilities

The IIQ utils is essentially a command line tool and has the following format
```
    iiqutils -clpdxfur <filename> [tag1,tag2-tag3,...]

    Options (can be combined in any way):
            -c - extract the calibration file (written as <back serial>.cal)
            -l - list contents of the IIQ file (tags)
            -p - prints contents of the tags in IIQ file
            -d - prints tag values in decimal rather than hexadecimal
            -x - treats specified tag range as excluded (default included)
            -f - formats printed data structures for known tags
            -u - prints unused/uknown values when -f is specified
            -r - prints rational numbers as rations as opposed to calculate the values

    The tag range is optional and if specified will be used to limit scope of the options.
    The tags in a range can either be decimal or, if preceeded by 0x, hexadecimal.
    The tag values for float/double data types are always printed in decimal.
```

For example invoking the following will dump all known tags from CF000602.IIQ with contents into DUMP.TXT file:
```
    iiqutils -lpfd CF000602.IIQ >DUMP.TXT
```

Or the same as above when invoking the utility for DK020261.calib calibration file:
```
    iiqutils -lpfd DK020261.calib >DUMP.TXT
```

And  invoking the following will extract contents of the calibration file into <back_serial>.cal:
```
    iiqutils -c CF000602.IIQ
```

## Structure of the IIQ file

The IIQ file is essentially standard TIFF file that contains a small preview image and the whole complete RAW image in an EXIF MakerNote.

The RAW image is a kind of a TIFF - it has modified TIFF tag structure (essentially everything is 32 bits) with single IFD there that contains data in Phase One own internal tags. The raw data of the image comes as large tag, the calibration file comes as a tag and all the shot metadata is recorded in tags. This approach is similar to what Kodak did for their DCR cameras/backs but in case of Phase One it is very disorganised - for example all the tags come with the TIFF data type populated but that is ignored by the Phase One own software and instead it populates the type based on tag itself. It must be very combersome even for Phase One own development to code.

The IIQ utility will allow to dump those tags using deduced names (for those known) and dump their content in various shapes (decoding the data where it can). The utility can also decode the calibration file as sub file when doing dump and will attempt print contents of that as well.

## Structure of the calibration file

This file is in essence almost in the same format as IIQ (has a Phase One modified TIFF header) but tag structure finally gave in to the fact that Phase One ignores tag data type and excludes that so tag structure is shorter. The calibration file tags are also using their own designations and overlap with main set of tags in IIQ (32 bit integer range was evidently not enough for Phase One to create non overalpping tag range). This utility can decode some of them with deduced names and print their internal structure when known. It particularly concentrates on defect remap information (defective pixels and columns) and formats that to all know defects read by Capture One.

## Building from sources
The CMake is used as a self contained build system for this project.

In the project directory run the following in command line:
```
mkdir build
cd build
cmake ..
cmake --build .
cmake --install .
```
For MS Visual Studio/C++ the last build step is slightly different as it needs to specify the config type:
```
cmake --build --config Release .
cmake --install .
```
After the build succeeded the executable  will be in bin/<platform name> directory.

Alternatively all can be build with CMake tools in VS.Code by opening this folder as a project.

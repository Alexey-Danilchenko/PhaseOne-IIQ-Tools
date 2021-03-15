# IIQ Profile

This is a command line utility that allows to extract built in Phase One colour matrices into profiles (.ICC and .DCP in various shapes).

## Running IIQ Profile

The IIQ Profile is essentially a command line tool and has the following format

```
    IIQ Profile extraction utility
    Usage: iiqprofile -iglw [optional data] <IIQ file>
      No options     - generates DCP profiles with Adobe curve
      -l             - generates DCP profiles with linear curve as opposed to Adobe standard
      -w <R> <G> <B> - specifies neutral white as R, G, B levels to use for DCP profile
      -w <R> <B>     - specifies neutral white as R, B exposure corrections (like RPP)
                       to use for DCP profile
      -i             - generates ICC profiles instead of DCP
      -g <gamma>     - specifies gamma (1-2.8) to use for TRC in ICC profile (1.8 by default)
                       (should only be used with -i option)
```

For example invoking the following will generate .ICC profiles from matrices in CF000602.IIQ with gamma 2.35:
```
    iiqprofile -ig 2.35 CF000602.IIQ
```

And  invoking the following will generate .DCP profiles with default ACR curve for given daylight levels from matrices
in CF000602.IIQ (the levels obtained from RawDigger sampling of gray patch of the CC24 target under daylight):
```
    iiqprofile -w 12440.28 25390.79 20272.61 CF000602.IIQ
```

## A few notes
The matrices included in IIQ files seem to be common across all backs of the same model so clearly no significant effort on Phase One part went into individual back calibration. Even Kodak ProBacks provided better matrices in their DCR file (including individually calibrated for the given sensor). These IIQ matrices are also sloppily built - at least for P25+ they tend to give overly purplish reds. I built a couple of alternative profiles for Phase One P25+ and they are provided separately in [Profiles section](../profiles).


## Building from sources
The CMake is used as a self contained build system for this project. The only dependency is LittleCMS2 library.

Set environment variable CMAKE_PREFIX_PATH to point to LCMS2 library.

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
After the build succeeded the executable with the dependencies will be in bin/<platform name> directory.

Alternatively all can be build with CMake tools in VS.Code by opening this folder as a project.

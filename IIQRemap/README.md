# IIQ Remap

This software allows to perform defective pixels and columns remap for Phase One digital backs. Currently supported
digital back models are Phase One Pnn/Pnn+ backs. Newer IIQ backs may work as well but were not extensively tested.

## Windows version
Windows version has a depndency on Microsoft Visual Studio runtime which may or may not be on your system. So if the prebuild version
does not startup properly, get the [vc_redist.x64.exe for Visual Studio 2019 from Microsoft](https://support.microsoft.com/en-us/topic/the-latest-supported-visual-c-downloads-2647da03-1eea-4433-9aff-95f26a218cc0) and install it on your Windows system.

## Building from sources
The build system has been ported to CMake so it should be easier to build. The dependencies for this project are (with minimum versions - those were tested):
* QT v5.15
* LibRaw v0.20.2
* Intel TBB v2021.1

Once those are installed/compiled build procedure is pretty standard for CMake project.

Set environment variable CMAKE_PREFIX_PATH to point to QT, LibRaw and TBB.

In the IIQRemap root directory run the following in command line:
```
mkdir build
cd build
cmake ..
cmake --build .
```
For MS Visual Studio/C++ the last build step is slightly different as it needs to specify the config type:
```
cmake --build --config Release .
```

After the build succeeded the complete executable (or application bundle for Mac OS) with all dependencies will be in the build directory (build/Release for MS Visual C++). For MS VC++ the runtime redistributable may be needed on target platform (downloadable from Microsoft).

Alternatively all can be build with CMake tools in VS.Code by opening this folder as a project (and setting up CMAKE_PREFIX_PATH as above).

## First a few concepts

In Phase One digital backs the sensor corrections of various kind (smoothing, linearisation, defect remaps etc) are
stored in special calibration file. That file is written into every IIQ raw file by the camera for the raw processing
software to apply the corrections. This file can be also uploaded onto digital back by Phase One Firmware Updater and
this can be leveraged to amend existing defects (add new defected pixels and columns, remove one that no longer
defective etc). This is the point of this utility - to manage defects and produce an amended calibration file for
uploading into digital back.

## Remap procedure for Phase One Pnn+ backs:

1) On Phase One Pnn/Pnn+ digital backs based on Kodak senosrs the remap generally is better performed on dark frames
(where people can see distinct defective columns when pulling the shadows or using high ISOs which on those digital
backs are frequently software based). To perform a remap a dark shot raw file is needed. I would suggest a few shots
at a different ISO with the closed lens cap at at 1/2 sec exposure and for Pnn+ backs also a few shots of couple
minutes exposure (to trigger long exposure mode). To improve stability of a column remap, several dark shots can be
combined - the IIQ Remap software can stack up to 7 raw files in a median stack leaving only stable defects in (this
is most useful to detect bad columns).

2) Load up raw file or several raw files in a median stack

3) You can always create a back up calibration file by loading up raw IIQ File and saving calibration immediately -
it will just save the calibration file from IIQ.

4) **Columns remap:** adjust the exposure in a raw display tab to make the defects visible, use composite or
grayscale raw display mode, switch on the column remapping mode and mark the defective columns with the mouse.
I strongly recommend using grayscale raw display modes for manual dark frame based remaps.

5) **Points remap:** these can also be done manually but it would be more effective to use "Apply defect corr."
and then use adaptive auto remap with selected thresholds (more details below in the application help).

6) You can always validate the remap by selecting "Apply defect corr." (deselect it first if it was selected
to refresh raw display) and untick all defects in defect selection section to see raw file clearly.

7) Repeat the procedure by loading abother dark raw or stack of them for the same digital back (remapped
defects will be preserved across the load).

8) Save the calibration file when satisfied (or do it intermediately whilst progressing with remap).

9) Use Phase One Firmware Update utility to upload calibration file to your back and enjoy the results.

Of course the above is only a recommended procedure. You can do manual remap with just about any file
(if the defects can be clearly seen). The automatic remaps of defective points however will only work
if the raw shot is uniform (i.e. roughly all of the same level - all dark or all grey or all white etc).

## The IIQ Remap program features.

The program functionality and controls are divided into two functional areas: one that has to do with raw file
appearance and another that has to do with remap of defects. This can be seen on the two separate tabs to the
left holding all the user interface for each of those areas in separate tabs.

All the menus (except help) are duplicated with buttons on the right side of the main window so won't be
described here separately.

Most of the user interface elements have appropriate tooltips to make it easier to remember the functionality.

### Main window view.

![](common/help/page0_en.jpg)

#### Toolbar

Toolbar contains buttons that control zoom levels (100%, Fit to Window, Zoom in, Zoom
level selection and Zoom out) and manual defect mode selector buttons (columns remap and
points remap and rows remap).

Fit to Window zoom level is a bit different from commonly accepted. When selected, it will
pick the largest zoom (from available list in drop-down) with which the raw file will fit
into window. It will also change it when resizing. The reason it is done this way is that
with specifics of raw display only the selected list of zooms allow to do it effectively.

The remap mode buttons are all mutually exclusive and will switch on the appropriate remap
mode when the columns or points may be selected with mouse cursor and either set or unset
as defective. The selected mode has to correspond to defects selection so it will be
synchronised with those settings.

#### Status bar

This is used to display some dynamic statistics - the row and column under mouse cursor,
the R,G,B,G2 raw channel values for pixel under mouse cursor (if raw file is loaded) and
the total counts of currently remapped defects for columns and points. The total defects
count is affected by the defects selection settings.

#### Common buttons section

#### Load RAW(s)

Loads and displays single of multiple raw files. If multiple raw files are selected
(up to 7), they all stacked into single raw file using median stack (to reduce noise
and eliminate non stable defects).

#### Load Calibration

Loads the external calibration file and defect remap from there. The raw file has
to be loaded for this option tobe available. If the calibration file does not match
up loaded raw file, then a message will be displayed and file will not be loaded.
This option allows to load a previously saved calibration file instead using the one
from the raw file and thus keep doing remap iteratively.

#### Save Calibration

Saves the remapped defects into calibration file. When saving for the first time and when
calibration was not loaded from external file, a dialog asking directory to save file to
will be presented. Calibration file is always saved with "back serial".calib file name.

#### Reset

Discards the current calibration file contents and remapped defects and loads reloads calibration
file from the raw file.


### Main window (Defects page)

This page will become enabled when raw file is loaded. The full list if its settings and user
interface elements is described in the following sections.

![](common/help/page0_en.jpg)

#### Clear selected defects

This button will delete all the remap of all defects selected by defects selection checkboxes
This may be useful to start remap of certain defects from scratch.

#### Apply defects corr.

This tickbox when selected will apply current defects remap to the raw file just as it would be when
it is processed by any raw processing software. This action is static and only happens when tickbox is
toggled or raw or calibration file loaded. So to trigger new remapped defects to be applied deselect
this option and select again. Selecting this is crucial and mandatory for successful auto remap so
always apply it first before proceeding with automatic remap.

#### Defects Colour

This button allows to pick the colours for displaying defects. The defects are always displayed
on top of the raw file but this setting may come useful in different type of images (dark, light
etc.) where a specific colour may be obscured by raw file.

#### Defects selection

This block of checkboxes allows to select what defects are currently displayed and being remapped.
Deselecting the type of defects currently being remapped will also disable the remap mode (buttons
in toolbar). The selection affects the defects count displayed in status line. This option is useful
to reduce clutter and improve visibility.

#### Auto remap defect thresholds

These define per channel thresholds used for defective points auto remap. The standard auto remap is
performed by comparing the pixel values from the raw file to the calculated average value for the
channel and marking it as defective if selected threshold is exceeded. If adaptive remap is selected,
then the raw values are compared to a median value for the channel calculated within a block of pixels
of selected size and marking it as defective if threshold for that channel is exceeded. The adaptive
remaps works better in general so is recommended as a default method.

#### Adaptive remap

Enables adaptive auto remap. Selecting this enables block size drop-down for adaptive remap as well.
Adaptive remap will look at the raw image blocks of the indicated size and calculate median values
for all channels for each block. The remap is then performed by comparing the raw values in the block
against the medians to see if they are exceeding per-channels thresholds.

#### Block size

This drop-down allows selecting the block size (in pixels) for adaptive remap. The blocks have an
even size and range from 4x4 to 64x64. Larger blocks will be less localised where the bigger sample
may mean more accurate median. Smaller blocks are nevertheless effective for remap of standalone pixels.

#### Detect from RAW

Selecting this button attempts to default (or re-default) thresholds from loaded raw file. It will only
work for uniform raw files (dark shots and the like).

#### Auto remap

Pressing this button will perform standard or adaptive points auto remap according to the selected
parameters. Auto remap is only effective for uniform raw files (dark, gray or white raw etc).

#### Currently loaded RAW statistics

This shows statistics for loaded raw file channels - min, max and average values and the count of
defects found that would be auto remapped with the current settings and suto remap methods.

### Main window (RAW Display page)

This page will also become enabled when raw file(s) are loaded. The full list if its settings
and user interface elements is described in the following sections.

The first group of control provides options that affect how the raw file is displayed.

![](common/help/page1_en.jpg)

#### RGB Render

When selected the raw file is displayed using RGB rendering for each pixel. The missing colours
are taken from neighbouring pixels in 2x2 pixel block thus making this effectively display large
2x2 pixels (each 2x2 RGBG block ends up being of the same colour). It is very convenient to spot
colour differences in pixels but not very useful for remap - since every defect will show up as
2x2 block. For manual remap, I'd recommend to use it to spot defects and then witch to one of the
other two methods to actually remap defects. Status bar in this mode shows the RGBG2 values for
2x2 block not the pixel.

#### Composite

When selected the raw file is displayed using the pixel colour only. For Red the pixel will be red,
for Green/Green2 it will be green and for Blue it will be blue. With all channels selected that may
make defects more difficult to spot so this is good for remapping itself not for searching for
defects.

#### Grayscale

It is essentially the same as composite but each pixel has all 3 components (R, G and B) set to the
underlying pixel value. It is basically a composite mode displayed in grayscale. It is a compromise
between Composite and RGB Render and can be used for looking for defects as well as remapping. It
is best suited for remapping dark raw file defects.

#### Channel selection

These set of checkboxes allows to pick the raw channels to be displayed. The deselected channels are
simply rendered black. The channels layout follow the Bayer pattern so columns and rows match those
on a sensor (for easier de-selection of the even/odd rows and columns).

#### Gamma 2.2

This enables gamma 2.2 correction applied to raw data and improves the display of the raw files (and
a need to apply large exposure correction).

#### Black level zeroed

This switches on a different method of applying black level. When selected the black levels are simply
zeroed. I.e. each value up to selected black level in a channel is set to 0. When it is not selected,
black levels calculated by traditional subtraction. This could be useful for spotting faint defective
columns in dark frames for Kodak sensor based digital backs and is enabled by default.

#### WB (from RAW)

This button sets all per-channel exposure boost settings to white balance multipliers extracted from
raw file (if none is specified, it uses daylight ones provided by LibRaw). This is absolutely useless
for remap but good to have a for playing with the raw files and analysing post remapped raw (to make
them look more natural).

#### Reset Corrections

Resets all exposure, contrast and black level corrections when pressed.

#### Exposure Boost

This set of sliders and values controls exposure corrections. The values for these are indicated in
stops and cover the range from -10 to +10 stops. The number values have more precision than sliders
but setting the exposure with sliders is faster. The exposure correction can be done at individual
channel levels and overall. These controls should be used to bring up the dark raw areas to a visible
range and make a defects in certain channels more prominent.

#### Contrast

This section has two contrast related sliders. The Contrast slider controls level of contrast boost
applied (steepness of the slope of S-curve). The Midpoint slider controls the middle point of contrast
S curve. These controls should be used to make defect more prominent (and are quite useful for dark
frame remaps).

#### Black levels

These control set the black levels for each channel. They can be used to cut out the noise when boosting
exposure on dark raw files.

#### Corrections order

All the corrections are applied to the raw data in this order: black levels -> gamma -> exposure correction -> contrast.




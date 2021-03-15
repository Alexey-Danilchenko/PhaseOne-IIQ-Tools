# Profiles for Phase One P25+ digital back

Dissatisfied with existing very bad profiles in Adobe ACR/Lightroom and bad quality matrices that come with IIQ files, I had constructed a few profiles of my own.

All ICC profiles here were tested with [Raw Photo Processor](https://www.raw-photo-processor.com/) and are build with 2.35 gamma tonal curve.

DCP profiles are built in two versions: with linear tonal curve and with default ACR tonal curve. In addition to that the DCP profiles were adjusted to my sample of P25+ back white balance - i.e. daylight levels R=12440.28, G=25390.79, B=20272.61 (or expressed as exposure correction stops: R=1.0293, B=0.3248).

The following is the list of available profiles:

* [Adobe Standard Matrix ICC](Phase_One_P25+_Standard_Daylight.ICC). This ICC profile made from Adobe Standard matrix for Phase One P25+ with gamma 2.35 tone curve and D50 white point.
* [Daylight ICC](P25+_Daylight.icc). This is a matrix profile build from individually measured Color Checker SG target unders clear daylight sky illuminant.
* [Adobe Standard Matrix DCP](Phase_One_P25+_Standard_Daylight.DCP). Adobe standard matrix, no hue twists, adjusted to D50 and my camera whitebalance with default ACR tonal curve.
* [Adobe Standard Matrix Linear DCP](Phase_One_P25+_Standard_Daylight_Linear.DCP). Adobe standard matrix, no hue twists, adjusted to D50 and my camera whitebalance with linear tonal curve.
* [Daylight DCP](P25+_Daylight.dcp). DCP matrix profile build from individually measured Color Checker SG target under a clear daylight sky illuminant with default ACR tonal curve.
* [Daylight Linear DCP](P25+_Daylight_Linear.dcp). DCP matrix profile build from individually measured Color Checker SG target under a clear daylight sky illuminant with linear tonal curve.


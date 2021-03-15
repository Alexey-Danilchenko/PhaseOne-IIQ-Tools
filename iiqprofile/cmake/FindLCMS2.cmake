#
# FindLCMS2
# ----------
#
# Find the Little CMS 2 library - static only for Windows
#
# Imported Targets
# ----------------
#
# This module provides the following imported targets, if found:
#    LCMS2::LCMS2
#
# Result Variables
# ----------------
#
# This will define the following variables:
#
#   LCMS2_FOUND        - True if the system has the Foo library.
#   LCMS2_VERSION      - The version of the Foo library which was found.
#   LCMS2_INCLUDE_DIRS - Include directories needed to use Foo.
#   LCMS2_LIBRARIES    - Libraries needed to link to Foo.
#
# Cache Variables
# ---------------
#
# The following cache variables may also be set:
#
#   LCMS2_INCLUDE_DIR  - The directory containing lcms2.h
#   LCMS2_LIBRARY      - The path to the LCMS library.
#
# Copyright (c) 2021 Alexey Danilchenko
#
# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.
#

find_path(LCMS2_INCLUDE_DIR lcms2.h
          PATH_SUFFIXES lcms2 liblcms2)

find_library(LCMS2_LIBRARY
             NAMES liblcms2.a lcms2
             PATH_SUFFIXES bin)
find_file(_LCMS2_DLL lcms2.dll PATH_SUFFIXES bin)

if(LCMS2_INCLUDE_DIR AND LCMS2_LIBRARY)
   set(LCMS2_FOUND TRUE)
else()
   set(LCMS2_FOUND FALSE)
endif()

if(LCMS2_FOUND)
   file(READ ${LCMS2_INCLUDE_DIR}/lcms2.h _LCMS2_version_content)
   string(REGEX MATCH "#define LCMS_VERSION[ ]*([0-9]*)\n" _LCMS2_version_match ${_LCMS2_version_content})
   set(_LCMS2_version_match "${CMAKE_MATCH_1}")
   if(_LCMS2_version_match)
      string(SUBSTRING ${_LCMS2_version_match} 0 1 LCMS2_MAJOR_VERSION)
      string(SUBSTRING ${_LCMS2_version_match} 1 2 LCMS2_MINOR_VERSION)
      set(LCMS2_VERSION ${LCMS2_MAJOR_VERSION}.${LCMS2_MINOR_VERSION})
   else()
      if(NOT LCMS2_FIND_QUIETLY)
         message(STATUS "Found lcms2 but failed to find version ${LCMS2_LIBRARIES}")
      endif()
      set(LCMS2_VERSION NOTFOUND)
   endif()
endif()

# set plural vars and
if(LCMS2_FOUND)
   # Handle exported variables
   include(FindPackageHandleStandardArgs)
   find_package_handle_standard_args(LCMS2
                                    FOUND_VAR LCMS2_FOUND
                                    REQUIRED_VARS LCMS2_LIBRARY LCMS2_INCLUDE_DIR
                                    VERSION_VAR LCMS2_VERSION)

   set(LCMS2_LIBRARIES ${Foo_LIBRARY})
   set(LCMS2_INCLUDE_DIRS ${LCMS2_INCLUDE_DIR})

   mark_as_advanced(LCMS2_VERSION
                    LCMS2_INCLUDE_DIR
                    LCMS2_LIBRARIES)
endif()

# Create imported target LCMS2::LCMS2
if(LCMS2_FOUND AND NOT TARGET LCMS2::LCMS2)
   if(_LCMS2_DLL)
      add_library(LCMS2::LCMS2 SHARED IMPORTED)
      set_target_properties(LCMS2::LCMS2 PROPERTIES
                            IMPORTED_IMPLIB "${LCMS2_LIBRARY}"
                            IMPORTED_LOCATION "${_LCMS2_DLL}"
                            INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIR}"
      )
   else()
      add_library(LCMS2::LCMS2 UNKNOWN IMPORTED)
      set_target_properties(LCMS2::LCMS2 PROPERTIES
                            IMPORTED_LOCATION "${LCMS2_LIBRARY}"
                            INTERFACE_INCLUDE_DIRECTORIES "${LCMS2_INCLUDE_DIR}"
      )
   endif()
endif()

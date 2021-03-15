cmake_minimum_required(VERSION 3.17)

project(IIQProfile)

# Search path for cmake modules
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

# Default build type release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)

# static linking for MSVC
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# LCMS2
find_package(LCMS2 REQUIRED)
if (${LCMS2_FOUND})
    message("Found LCMS2 " ${LCMS2_VERSION})
else()
    message("Couldn't find LCMS2")
endif()

add_executable(iiqprofile
    matrix3x3.h
    iiqprofile.h
    iiqprofile.cpp
)

include(CheckIPOSupported)
check_ipo_supported(RESULT result)
if(result)
  set_property(TARGET iiqprofile PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

target_link_libraries(iiqprofile LCMS2::LCMS2)

# install built plugins to bin directory
set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/bin/${CMAKE_SYSTEM_NAME})
install(TARGETS iiqprofile
        DESTINATION .
)

get_target_property(lcms2Type LCMS2::LCMS2 TYPE)
if(lcms2Type STREQUAL SHARED)
    add_custom_command(TARGET iiqprofile POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:LCMS2::LCMS2> $<TARGET_FILE_DIR:iiqprofile>
    )

    install(FILES $<TARGET_FILE:LCMS2::LCMS2>
            DESTINATION .
    )
endif()

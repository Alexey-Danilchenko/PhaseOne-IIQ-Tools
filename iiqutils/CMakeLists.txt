cmake_minimum_required(VERSION 3.17)

project(IIQUtils)

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

add_executable(iiqutils
    iiqutils.h
    iiqutils.cpp
)

# install built plugins to bin directory
set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/bin/${CMAKE_SYSTEM_NAME})
install(TARGETS iiqutils
        DESTINATION .
)


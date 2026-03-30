#!/bin/bash

# Build script for Mandel project
# Use --debug flag to build in Debug mode, otherwise builds in Release mode

set -e  # Exit on error

# --debug => Debug; --release-static => Release, static SDL2 required, separate build dir
if [[ "$1" == "--debug" ]]; then
    BUILD_TYPE="Debug"
    BUILD_SUBDIR="Debug"
elif [[ "$1" == "--release-static" ]]; then
    BUILD_TYPE="Release"
    BUILD_SUBDIR="Release-static"
else
    BUILD_TYPE="Release"
    BUILD_SUBDIR="Release"
fi

# Get the project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/${BUILD_SUBDIR}"

echo "Building Mandel in ${BUILD_TYPE} mode..."

# Configure CMake (Release => static linking by default for portable binaries)
CMAKE_OPTS=(-B "${BUILD_DIR}" -S "${SCRIPT_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
if [[ "${BUILD_TYPE}" == "Release" ]]; then
    CMAKE_OPTS+=(-DSTATIC_LINKING=ON)
fi
if [[ "$1" == "--release-static" ]]; then
    CMAKE_OPTS+=(-DSTATIC_LINKING_STRICT=ON)
fi
echo "Configuring CMake..."
cmake "${CMAKE_OPTS[@]}"

# Build the project
echo "Building project..."
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}"

echo "Build completed successfully!"


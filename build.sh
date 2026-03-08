#!/bin/bash

# Build script for Mandel project
# Use --debug flag to build in Debug mode, otherwise builds in Release mode

set -e  # Exit on error

# Check if --debug flag is provided
if [[ "$1" == "--debug" ]]; then
    BUILD_TYPE="Debug"
else
    BUILD_TYPE="Release"
fi

# Get the project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/${BUILD_TYPE}"

echo "Building Mandel in ${BUILD_TYPE} mode..."

# Configure CMake (Release => static linking by default for portable binaries)
CMAKE_OPTS=(-B "${BUILD_DIR}" -S "${SCRIPT_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
if [[ "${BUILD_TYPE}" == "Release" ]]; then
    CMAKE_OPTS+=(-DSTATIC_LINKING=ON)
fi
echo "Configuring CMake..."
cmake "${CMAKE_OPTS[@]}"

# Build the project
echo "Building project..."
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}"

echo "Build completed successfully!"


cmake_minimum_required(VERSION 3.15)
project(Mandel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure build types for optimal debugging and performance
if(MSVC)
    # MSVC compiler flags
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Od /RTC1")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /Ob2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MD /O2 /Ob2 /Zi /DNDEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "/MD /O1 /Ob1 /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /LTCG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO")
else()
    # GCC/Clang compiler flags
    # Debug: debuggable with symbols, no optimizations
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")
    
    # Release: maximum optimizations for speed
    # Start with -O3 for maximum standard optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    
    # Enable LTO (Link Time Optimization) for Release
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    
    # RelWithDebInfo: optimized but still debuggable
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
    # MinSizeRel: optimize for size
    set(CMAKE_CXX_FLAGS_MINSIZEREL "-Os -DNDEBUG")
    
    # Add architecture-specific and aggressive optimizations for Release
    include(CheckCXXCompilerFlag)
    
    # Architecture-specific optimizations
    check_cxx_compiler_flag("-march=native" COMPILER_SUPPORTS_MARCH_NATIVE)
    if(COMPILER_SUPPORTS_MARCH_NATIVE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native -mtune=native")
    endif()
    
    # Fast math optimizations (safe for Mandelbrot calculations)
    # Allows more aggressive floating-point optimizations
    check_cxx_compiler_flag("-ffast-math" COMPILER_SUPPORTS_FAST_MATH)
    if(COMPILER_SUPPORTS_FAST_MATH)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffast-math")
    endif()
    
    # Loop unrolling optimizations
    check_cxx_compiler_flag("-funroll-loops" COMPILER_SUPPORTS_UNROLL_LOOPS)
    if(COMPILER_SUPPORTS_UNROLL_LOOPS)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -funroll-loops")
    endif()
    
    # More aggressive inlining
    check_cxx_compiler_flag("-finline-functions" COMPILER_SUPPORTS_INLINE_FUNCTIONS)
    if(COMPILER_SUPPORTS_INLINE_FUNCTIONS)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -finline-functions")
    endif()
    
    # Vectorization hints (if available)
    check_cxx_compiler_flag("-ftree-vectorize" COMPILER_SUPPORTS_TREE_VECTORIZE)
    if(COMPILER_SUPPORTS_TREE_VECTORIZE)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ftree-vectorize")
    endif()
endif()

# Set default build type to Release if not specified (single-config generators)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(UNIT_TEST "Enable unit test mode (disables threading, verifies pixel coverage)" OFF)

# Find packages
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# Include directories
include_directories(${SDL2_INCLUDE_DIRS})
include_directories(${OPENGL_INCLUDE_DIRS})

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# Check for imgui in external/ first, otherwise fetch it
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui/imgui.h")
    set(imgui_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
    message(STATUS "Using imgui from external/imgui")
else()
    # Fetch imgui
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_BRANCH docking
    )
    
    FetchContent_MakeAvailable(imgui)
    message(STATUS "Fetched imgui using FetchContent")
endif()

# ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

set(IMGUI_HEADERS
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imgui_internal.h
    ${imgui_SOURCE_DIR}/imconfig.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h
)

# Source files (exclude test file)
file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.hpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/mandel_test.cpp")

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${IMGUI_SOURCES})

# Enable interprocedural optimization (LTO) for Release builds
# This enables link-time optimizations when building in Release mode
set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

# Link nlohmann/json (header-only, but provides include directories)
# Try different possible target names
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
elseif(TARGET nlohmann_json)
    target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json)
elseif(EXISTS "${json_SOURCE_DIR}/include/nlohmann/json.hpp")
    target_include_directories(${PROJECT_NAME} PRIVATE "${json_SOURCE_DIR}/include")
elseif(EXISTS "${json_SOURCE_DIR}/single_include/nlohmann/json.hpp")
    target_include_directories(${PROJECT_NAME} PRIVATE "${json_SOURCE_DIR}/single_include")
endif()

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
)

# Platform-specific settings
if(APPLE)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(IOKIT_LIBRARY IOKit)
    find_library(COREVIDEO_LIBRARY CoreVideo)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${COCOA_LIBRARY}
        ${IOKIT_LIBRARY}
        ${COREVIDEO_LIBRARY}
    )
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dl
        pthread
    )
endif()

# Build directory - executable goes directly in build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Unit test mode
if(UNIT_TEST)
    target_compile_definitions(${PROJECT_NAME} PRIVATE UNIT_TEST)
    message(STATUS "Unit test mode enabled: threading disabled, pixel coverage verification active")
    
    # Create test executable
    add_executable(${PROJECT_NAME}_test 
        src/mandel_test.cpp
        src/mandel.cpp
        src/thread_pool.cpp
    )
    target_include_directories(${PROJECT_NAME}_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_compile_definitions(${PROJECT_NAME}_test PRIVATE UNIT_TEST)
    target_link_libraries(${PROJECT_NAME}_test PRIVATE pthread)
    
    # Test doesn't need SDL/OpenGL/ImGui, but if we want to test without them,
    # we might need to mock or conditionally compile those parts.
    # For now, we'll need minimal dependencies.
    message(STATUS "Test executable ${PROJECT_NAME}_test will be created")
endif()


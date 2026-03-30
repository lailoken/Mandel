cmake_minimum_required(VERSION 3.15)
project(Mandel VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure build types for optimal debugging and performance
if(MSVC)
    # MSVC compiler flags
    set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Od /RTC1 /D_DEBUG /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /Ob2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MD /O2 /Ob2 /Zi /DNDEBUG")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "/MD /O1 /Ob1 /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO /LTCG")
    set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "/DEBUG /INCREMENTAL:NO")
else()
    # GCC/Clang compiler flags
    # Debug: debuggable with symbols, no optimizations
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -D_DEBUG -DDEBUG")
    
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

# Options (static linking defaults ON for Release for portable binaries across distros)
set(STATIC_LINKING_DEFAULT OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(STATIC_LINKING_DEFAULT ON)
endif()
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(STATIC_LINKING "Link all libraries statically (reduces runtime dependencies)" ${STATIC_LINKING_DEFAULT})
option(STATIC_LINKING_STRICT "With STATIC_LINKING: fail if SDL2 cannot be linked statically (no dynamic SDL2 fallback)" OFF)
option(UNIT_TEST "Enable unit test mode (disables threading, verifies pixel coverage)" OFF)

# Static linking configuration: prefer static SDL2 when available
if(STATIC_LINKING)
    message(STATUS "Static linking enabled (SDL2 static preferred, fallback to dynamic)")
    # Embed libstdc++ and libgcc so the binary does not depend on host's C++ runtime
    # (avoids Fedora-built binary dumping on Ubuntu due to libstdc++.so version mismatch)
    if(UNIX AND NOT APPLE AND NOT MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        message(STATUS "Static C++ runtime: -static-libgcc -static-libstdc++")
    endif()
    # Note: glibc stays dynamic; building on a newer glibc (e.g. Fedora) can make the binary
    # fail on older glibc (e.g. Ubuntu). For max portability, build on oldest target (e.g. Ubuntu container).
endif()

# Find packages
find_package(PkgConfig QUIET)
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

# Define _DEBUG for debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)

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
if(STATIC_LINKING)
    # SDL2: Try static first, fall back to dynamic
    set(SDL2_LINKED_STATIC FALSE)
    if(TARGET SDL2::SDL2static)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2static)
        set(SDL2_LINKED_STATIC TRUE)
        message(STATUS "Linking SDL2 statically (SDL2::SDL2static)")
    else()
        # Use pkg-config to get SDL2 + all static deps (X11, Wayland, etc.) so nothing is left dynamic
        if(PkgConfig_FOUND)
            execute_process(
                COMMAND ${PKG_CONFIG_EXECUTABLE} --static --libs SDL2
                OUTPUT_VARIABLE SDL2_STATIC_PC_LIBS
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE SDL2_PC_RESULT
            )
            if(SDL2_PC_RESULT EQUAL 0 AND SDL2_STATIC_PC_LIBS)
                separate_arguments(SDL2_STATIC_PC_LIST NATIVE_COMMAND "${SDL2_STATIC_PC_LIBS}")
                target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_STATIC_PC_LIST})
                set(SDL2_LINKED_STATIC TRUE)
                message(STATUS "Linking SDL2 statically via pkg-config (SDL2 + system deps)")
            endif()
        endif()
        if(NOT SDL2_LINKED_STATIC)
            find_program(SDL2_CONFIG_EXECUTABLE NAMES sdl2-config)
            if(SDL2_CONFIG_EXECUTABLE)
                execute_process(
                    COMMAND ${SDL2_CONFIG_EXECUTABLE} --static-libs
                    OUTPUT_VARIABLE SDL2_STATIC_CONFIG_LIBS
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE SDL2_CONFIG_RESULT
                )
                if(SDL2_CONFIG_RESULT EQUAL 0 AND SDL2_STATIC_CONFIG_LIBS)
                    separate_arguments(SDL2_STATIC_CONFIG_LIST NATIVE_COMMAND "${SDL2_STATIC_CONFIG_LIBS}")
                    target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_STATIC_CONFIG_LIST})
                    set(SDL2_LINKED_STATIC TRUE)
                    message(STATUS "Linking SDL2 statically via sdl2-config --static-libs")
                endif()
            endif()
        endif()
        if(NOT SDL2_LINKED_STATIC)
            set(CMAKE_FIND_LIBRARY_SUFFIXES_BACKUP ${CMAKE_FIND_LIBRARY_SUFFIXES})
            set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
            find_library(SDL2_STATIC_LIB
                NAMES SDL2
                PATHS ${SDL2_LIBRARY_DIRS} /usr/lib64 /usr/lib
                PATH_SUFFIXES lib64 lib
            )
            set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES_BACKUP})
            if(SDL2_STATIC_LIB)
                target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_STATIC_LIB})
                set(SDL2_LINKED_STATIC TRUE)
                message(WARNING
                    "Linking only ${SDL2_STATIC_LIB}; if linking fails, install SDL2 dev (sdl2-config/pkg-config) for full static dependency list.")
            endif()
        endif()
    endif()
    if(NOT SDL2_LINKED_STATIC)
        if(STATIC_LINKING_STRICT)
            message(FATAL_ERROR
                "STATIC_LINKING_STRICT: SDL2 could not be linked statically. "
                "Install a static SDL2 (e.g. libsdl2-dev with libSDL2.a, or SDL2 built with -DBUILD_SHARED_LIBS=OFF).")
        endif()
        if(TARGET SDL2::SDL2)
            target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
        else()
            target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})
        endif()
        message(STATUS "SDL2 static not found, using dynamic SDL2")
    endif()
    
    # OpenGL: Keep dynamic for driver compatibility
    message(STATUS "OpenGL will remain dynamically linked for driver compatibility")
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARIES})
    
else()
    # Dynamic linking (default)
    if(TARGET SDL2::SDL2)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE ${SDL2_LIBRARIES})
    endif()
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARIES})
endif()

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
    # System libraries needed for Linux
    # Note: These are typically already linked via SDL2::SDL2static or SDL2::SDL2 targets,
    # but we link them explicitly for cases where manual SDL2 linking is used
    if(STATIC_LINKING)
        # For static linking, ensure system libraries are available
        # (SDL2 static may pull them in, but be explicit)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}  # Dynamic loader (dlopen/dlsym)
            pthread            # POSIX threads
        )
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE
            dl       # Dynamic loader
            pthread  # POSIX threads
        )
    endif()
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
    target_compile_definitions(${PROJECT_NAME}_test PRIVATE 
        UNIT_TEST
        $<$<CONFIG:Debug>:_DEBUG>
    )
    target_link_libraries(${PROJECT_NAME}_test PRIVATE pthread)
    
    # Test doesn't need SDL/OpenGL/ImGui, but if we want to test without them,
    # we might need to mock or conditionally compile those parts.
    # For now, we'll need minimal dependencies.
    message(STATUS "Test executable ${PROJECT_NAME}_test will be created")
endif()

# Add unit test executables (always available, not just in UNIT_TEST mode)
add_executable(test_mandel_worker
    test_mandel_worker.cpp
    src/mandel.cpp
    src/mandel_worker.cpp
    src/thread_pool.cpp
)
target_include_directories(test_mandel_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(test_mandel_worker PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
target_link_libraries(test_mandel_worker PRIVATE pthread)

add_executable(test_translation_swap
    test_translation_swap.cpp
    mandel_ui_test.cpp
    src/overscan_viewport.cpp
    src/mandel_ui.cpp
    src/mandel_worker.cpp
    src/mandel.cpp
    src/thread_pool.cpp
    src/dummy_worker.cpp
    src/config.cpp
    src/mandel_control.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(test_translation_swap PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)
# Link nlohmann/json (header-only, but provides include directories)
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(test_translation_swap PRIVATE nlohmann_json::nlohmann_json)
elseif(TARGET nlohmann_json)
    target_link_libraries(test_translation_swap PRIVATE nlohmann_json)
elseif(EXISTS "${json_SOURCE_DIR}/include/nlohmann/json.hpp")
    target_include_directories(test_translation_swap PRIVATE "${json_SOURCE_DIR}/include")
elseif(EXISTS "${json_SOURCE_DIR}/single_include/nlohmann/json.hpp")
    target_include_directories(test_translation_swap PRIVATE "${json_SOURCE_DIR}/single_include")
endif()
# Link SDL2 for test (needed by ImGui backend)
if(TARGET SDL2::SDL2)
    target_link_libraries(test_translation_swap PRIVATE SDL2::SDL2)
else()
    target_link_libraries(test_translation_swap PRIVATE ${SDL2_LIBRARIES})
endif()
target_link_libraries(test_translation_swap PRIVATE pthread ${OPENGL_LIBRARIES})
target_compile_definitions(test_translation_swap PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
)
target_link_libraries(test_translation_swap PRIVATE pthread)

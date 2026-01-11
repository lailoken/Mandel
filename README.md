# Mandel - ImGui + SDL2 + OpenGL3

A C++ project using Dear ImGui with SDL2 and OpenGL3 backends.

## Prerequisites

- CMake 3.15 or higher
- C++17 compatible compiler
- SDL2 development libraries
- OpenGL development libraries

### Installing dependencies on Linux (Fedora/Nobara)

```bash
sudo dnf install cmake gcc-c++ SDL2-devel mesa-libGL-devel
```

## Building

1. Configure the project:
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
```

2. Build:
```bash
cmake --build build
```

The executable will be in `build/bin/Mandel`.

## ImGui Setup

The project will automatically fetch imgui using CMake's FetchContent if it's not found in `external/imgui/`. 

To use a local copy of imgui:
```bash
mkdir -p external
cd external
git clone https://github.com/ocornut/imgui.git
cd ..
```

## VS Code Debugging

Press **F5** to build and debug the project. The configuration files are in `.vscode/`:
- `launch.json` - Debug configuration
- `tasks.json` - Build tasks
- `settings.json` - CMake settings

## Project Structure

```
Mandel/
├── CMakeLists.txt       # CMake build configuration
├── src/                 # Source files (.cpp, .hpp)
├── external/            # External dependencies (optional)
├── build/               # Build directory (generated)
└── .vscode/             # VS Code configuration
```


# VisionFlow

## Overview
VisionFlow is a C++23 Windows project built with CMake.

The current codebase provides a foundational runtime with logging, mouse control interfaces,
and a Win32-backed serial/device boundary for hardware communication.

## Requirements
- Windows 10 or Windows 11
- CMake 4.0 or newer
- Visual Studio 2022 or Visual Studio 2026
- MSVC toolchain with C++23 support
- Git (required for FetchContent dependencies)

## Project Structure
- `include/`: Public headers and interface contracts.
- `src/`: Source implementations.
- `third_party/`: External dependencies.
- `build/`: Generated build artifacts.

## Build
```bash
cmake -S . -B build
cmake --build build
```

`spdlog` is fetched automatically during configure/build.

## Run
```bash
./build/VisionFlow.exe
```

Log files are created at runtime under a `logs/` directory relative to the working directory.

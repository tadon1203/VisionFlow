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
- `tests/`: Automated tests (`tests/unit/` for unit tests, `tests/integration/` reserved for future integration tests).
- `third_party/`: External dependencies.
- `build/`: Generated build artifacts.

## Build
```bash
python.exe build.py
python.exe build.py --config Debug
python.exe build.py --config Release
python.exe build.py --config RelWithDebInfo
python.exe build.py --no-test
```

All builds must go through `build.py` (do not run `cmake` directly).  
By default, `build.py` runs both build and tests.

## Test
```bash
python.exe build.py --config Debug --test
```

## Run
```bash
./build/RelWithDebInfo/VisionFlow.exe
```

Log files are created at runtime under a `logs/` directory relative to the working directory.

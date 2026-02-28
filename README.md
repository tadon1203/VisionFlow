# VisionFlow

## Overview
VisionFlow is a C++23 Windows project built with CMake.

The current codebase provides a foundational runtime with logging, mouse control interfaces,
and a WinRT-backed serial/device boundary for hardware communication.

## Where to Start Reading Code
If you're new to the repo, this order minimizes context switching:

1. `docs/ARCHITECTURE.md` (system map + data/control flow)
2. `src/main.cpp` (process lifetime + config/platform init)
3. `src/core/app.cpp` (main loop + shutdown + error propagation)
4. `src/core/composition/app_factory.cpp` (composition root / wiring)
5. Domain entrypoints:
  - Capture: `src/capture/sources/winrt/`
  - Inference: `src/inference/composition/`, `src/inference/engine/`, `src/inference/backend/dml/`
  - Input: `src/input/`, `src/input/makcu/`, `src/input/platform/`

Unit tests are also a good "living spec" for behavioral contracts:
- `tests/unit/core/app_test.cpp`
- `tests/unit/core/config_loader_test.cpp`
- `tests/unit/input/makcu_controller_test.cpp`
- `tests/unit/capture/capture_source_winrt_test.cpp`

## Requirements
- Windows 10 or Windows 11
- CMake 4.0 or newer
- Visual Studio 2022 or Visual Studio 2026
- MSVC toolchain with C++23 support
- Git (required for FetchContent dependencies)

## Project Structure
- `include/`: Public headers and interface contracts.
- `src/`: Source implementations.
- `config/`: Runtime configuration files.
- `tests/`: Automated tests (`tests/unit/` for unit tests, `tests/integration/` reserved for future integration tests).
- `third_party/`: External dependencies.
- `build/`: Generated build artifacts.

## Configuration
VisionFlow loads runtime settings from `config/visionflow.json`.

If the file does not exist, the program creates a default config file at that path.

Minimal example:
```json
{
  "app": {
    "reconnectRetryMs": 500
  },
  "makcu": {
    "remainderTtlMs": 200
  },
  "capture": {
    "preferredDisplayIndex": 0
  },
  "inference": {
    "modelPath": "model.onnx",
    "confidenceThreshold": 0.25
  }
}
```

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

## Format
```bash
python.exe scripts/run-clang-format.py --all
git diff --cached | python.exe scripts/run-clang-format.py --diff
```

## Git Hooks
```bash
python.exe scripts/install-hooks.py
```

The repository uses a managed `pre-commit` hook under `.githooks/`.
The hook runs diff-based clang-format for staged changes.

- If formatting is already clean, commit continues.
- If formatting is required, the hook applies fixes and blocks the commit once.
- Re-stage and retry:
  - `git add -A`
  - `git commit`

## Lint
```bash
python.exe scripts/run-clang-tidy.py --all
python.exe scripts/run-clang-tidy.py --all --config Release
git diff --cached | python.exe scripts/run-clang-tidy.py --diff
```

`run-clang-tidy.py` runs `cmake --preset <config>` automatically before analysis and uses
`build/compile_commands.json`.

## Run
```bash
./build/RelWithDebInfo/VisionFlow.exe
```

Log files are created at runtime under a `logs/` directory relative to the working directory.

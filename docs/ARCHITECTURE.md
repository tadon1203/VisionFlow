# VisionFlow Architecture (Core)

## Purpose
This document describes the core architecture of VisionFlow so contributors can reason about
boundaries and responsibilities before changing behavior.

This document is descriptive (how the system is currently structured), not normative policy.
For mandatory workflow and coding rules, see `AGENTS.md`.

## Abstraction Level & Scope
This document explains architecture at component and boundary level.

### Included in This Document
- Layer boundaries and dependency direction
- Component responsibilities and ownership
- Lifecycle and control flow at step level
- Error and concurrency models at policy level

### Excluded from This Document
- Line-by-line implementation details
- Full protocol frame catalogs or exhaustive API flag listings
- Build, CI, and PR workflow rules (see `AGENTS.md`)
- Platform API call-by-call tutorials

### When More Detail Is Needed
- Link to the relevant source/header files instead of embedding code-level walkthroughs here.

## Non-Goals
- Full protocol specification
- Full platform API reference
- Exhaustive module-by-module documentation

## System at a Glance

```text
main
  -> loadConfig()
  -> createMouseController()
    -> App
      -> IMouseController (interface)
        -> MakcuController (implementation)
          -> IDeviceScanner / ISerialPort (interfaces)
            -> Win32DeviceScanner / Win32SerialPort (platform adapters)
```

## Core Layers
- `include/`: public contracts and interface boundaries
- `src/core/`: application lifecycle and logging
- `src/input/`: input domain orchestration and protocol behavior
- `src/input/win32_*`: Win32-specific implementation details (private boundary)

## Core Components

### Config Loader
- Loads `config/visionflow.json` at startup
- Parses and validates runtime settings into `VisionFlowConfig`
- Uses `std::expected<..., std::error_code>` for explicit error propagation

### App
- Owns one `IMouseController`
- Handles startup/shutdown flow
- Acts as reconnect supervisor: retries `connect()` for recoverable failures with a fixed interval
- Initializes logging and drives the main loop

### Logger
- Provides one shared core logger
- Exposes level macros (`VF_INFO`, `VF_WARN`, etc.) for consistent runtime logging

### IMouseController
- Public behavioral contract:
  - `connect()`
  - `disconnect()`
  - `move(dx, dy)`
- Uses `std::expected<void, std::error_code>` for error reporting

### MakcuController
- Implements `IMouseController`
- Owns controller state machine:
  - `Idle`, `Opening`, `Ready`, `Stopping`, `Fault`
- Coordinates serial handshake and move command dispatch
- Owns one sender thread (`std::jthread`) and command synchronization (`mutex` + `condition_variable`)
- On runtime send failure, closes serial, transitions back to `Idle`, and allows a fresh `connect()` attempt

## Platform Boundary and Composition
- Public code composes controller instances via `createMouseController()`
- `MakcuController` depends on abstractions (`ISerialPort`, `IDeviceScanner`)
- Win32 concrete types stay in private `src/` headers and source files
- Public headers remain platform-independent

## Core Flow

### Connect Path
1. Scan COM port by target hardware ID
2. Open serial at 115200 baud
3. Send baud-change binary frame
4. Reconfigure host serial DCB to 4000000 baud
5. Start sender thread
6. If connect fails, return error immediately; retry policy is handled by `App`

### Move Path
1. `move(dx, dy)` writes pending command under lock
2. Sender thread wakes by condition variable
3. Thread serializes command and writes to serial port
4. If controller is not `Ready`, `move()` returns `NotConnected`

### Disconnect Path
1. Transition to stopping state
2. `request_stop -> notify -> join`
3. Clear pending command
4. Close serial and transition to final state

## Error Model
- Core operations return `std::expected<void, std::error_code>`
- Config loading returns `std::expected<VisionFlowConfig, std::error_code>`
- Failures are logged at the point of decision
- Recoverable connection failures return to `Idle` to allow retry
- `App` classifies connection errors into retryable vs non-retryable paths

## Concurrency Model
- `MakcuController` is the sole owner of its worker thread
- Shared mutable state is protected by explicit mutexes
- Shutdown sequence is explicit and deterministic

## Core-Relevant Structure
- `include/VisionFlow/core/*`: public core contracts
- `include/VisionFlow/input/*`: public input contracts and composition entrypoints
- `src/core/*`: app lifecycle and logging implementations
- `src/input/*`: input orchestration and protocol implementations
- `src/input/win32_*`: private platform adapters
- `config/*`: runtime configuration inputs

## Extension Guidelines (Core)
- Add new platform adapters behind existing interfaces.
- Favor composition through factory wiring over exposing concrete types upward.
- For mandatory boundary and include rules, follow `AGENTS.md`.

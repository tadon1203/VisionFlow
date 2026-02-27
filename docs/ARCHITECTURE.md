#VisionFlow Architecture(Core)

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
  -> WinRtPlatformContext (RAII)
  -> App(config)
    -> IInferenceProcessor (interface)
      -> OnnxDmlInferenceProcessor
        -> OnnxDmlSession (DirectML + IO Binding)
      -> InferenceResultStore
    -> CaptureRuntime (interface)
      -> WinrtCaptureRuntime (implementation)
        -> WinrtCaptureSource
          -> WinrtCaptureSession
          -> IWinrtFrameSink (private src boundary)
            -> OnnxDmlInferenceProcessor
    -> createMouseController()
      -> IMouseController (interface)
        -> MakcuMouseController (implementation, `MakcuController` is an alias)
          -> IDeviceScanner / ISerialPort (interfaces)
            -> WinRtDeviceScanner / WinRtSerialPort (platform adapters)
```

## Core Layers
- `include/`: public contracts and interface boundaries
- `src/core/`: application lifecycle and logging
- `include/VisionFlow/capture/`: public capture contracts
- `src/input/`: input domain orchestration and protocol behavior
- `src/input/platform/`: WinRT-backed serial/device adapters (private boundary)
- `src/input/makcu/`: Makcu internal state/queue/ack components (private boundary)
- `src/capture/common/`: capture-internal shared data types (`capture_frame_info`, `inference_result`)
- `src/capture/`: capture domain shared/abstract components (`capture_error`)
- `src/capture/pipeline/`: platform-agnostic capture pipeline components (`frame_sequencer`)
- `src/inference/api/`: inference composition entrypoints for runtime wiring
- `src/inference/platform/dml/`: DirectML/DX12 backend implementation details
- `src/inference/engine/`: inference orchestrator/backend implementations (`onnx_dml_inference_processor`, `debug_inference_processor`, `inference_result_store`)
- `src/capture/sources/winrt/`: WinRT capture source and sink boundary
- `src/capture/runtime/`: capture runtime composition
- `src/platform/winrt/`: platform runtime lifecycle

## Core Components

### Config Loader
- Loads `config/visionflow.json` at startup
- Parses and validates runtime settings into `VisionFlowConfig`
- Uses `std::expected<..., std::error_code>` for explicit error propagation

### App
- Owns one `IMouseController`
- Owns one `ICaptureRuntime`
- Owns one `IInferenceProcessor`
- Owns one `InferenceResultStore`
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

### MakcuMouseController
- Implements `IMouseController`
- Composes focused internal components:
  - `MakcuStateMachine`
  - `MakcuCommandQueue`
  - `MakcuAckGate`
- Coordinates serial handshake and sender worker lifecycle
- On runtime send failure, closes serial, transitions back to `Idle`, and allows a fresh `connect()` attempt

### WinrtCaptureSource + WinrtCaptureSession
- `WinrtCaptureSource` owns high-level capture state transitions and frame delivery to `IWinrtFrameSink`
- `WinrtCaptureSession` owns WinRT/D3D device setup, frame pool lifecycle, and capture session start/stop
- `WinrtCaptureSource` delegates platform session management to `WinrtCaptureSession`
- `WinrtCaptureRuntime` is capture-only and does not own inference lifecycle
- `AppFactory` attaches `IInferenceProcessor` to `WinrtCaptureRuntime`; runtime resolves private
  `IWinrtFrameSink` internally
- `WinrtCaptureRuntime` validates state transitions through `CaptureRuntimeStateMachine`

## Platform Boundary and Composition
- `main` owns platform runtime scope and initializes platform context
- `AppFactory` is the composition root for capture runtime, inference processor, and result store
- Controller composition still uses `createMouseController()`
- `MakcuMouseController` depends on abstractions (`ISerialPort`, `IDeviceScanner`)
- Platform concrete types stay in private `src/` headers and source files
- Public headers remain platform-independent
- Capture processor contract with platform texture types is private under `src/capture/`

## Core Flow

### Connect Path
1. Scan COM port by target hardware ID
2. Open serial at 115200 baud
3. Send baud-change binary frame
4. Reconfigure host serial baud rate to 4000000 (WinRT `SerialDevice` setting)
5. Start sender thread
6. If connect fails, return error immediately; retry policy is handled by `App`

### Capture Path
0. App starts inference processor first, then starts capture runtime
1. Enumerate display monitors
2. Select configured display index (fallback to 0 when out of range)
3. Create `GraphicsCaptureItem` from selected monitor
4. Create frame pool and subscribe `FrameArrived`
5. Start capture session
6. Push each texture frame to capture processor
6.1. `WinrtCaptureSource` stage flow: `snapshot sink -> acquire frame -> forward to sink`
7. Keep only the freshest frame in the processor and drop stale frames
8. `OnnxDmlInferenceProcessor` forwards only the latest frame to `DmlImageProcessor`
9. `DmlImageProcessor` owns shared texture/fence bridging (D3D11/D3D12 interop)
10. `DmlImageProcessor` owns preprocess pipeline setup/recording
11. `OnnxDmlSession` consumes that D3D12 buffer via ONNX Runtime DirectML (`DML1`) IO Binding
12. `OnnxDmlSession` implementation is split by translation unit:
  - `onnx_dml_session.cpp`
  - `onnx_dml_session_stub.cpp`

### Inference Result Path
1. Inference worker publishes result to `InferenceResultStore`
2. `App::tickOnce()` consumes one result via `InferenceResultStore::take()`
3. App applies the result to runtime actions (mouse/output behavior)

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
- `App` propagates component error codes directly (no app-layer remapping)
- `poll()` contract is fault-only failure:
  - Success in `Idle`, `Starting`, `Running`, `Stopping`
  - Failure in `Fault` or structurally invalid state (for example missing required dependency)

## Concurrency Model
- `MakcuMouseController` is the sole owner of its worker thread
- `OnnxDmlInferenceProcessor` is the sole owner of its inference thread
- Shared mutable state is protected by explicit mutexes
- Shutdown sequence is explicit and deterministic

## Core-Relevant Structure
- `include/VisionFlow/core/*`: public core contracts
- `include/VisionFlow/capture/*`: public capture contracts
- `include/VisionFlow/input/*`: public input contracts and composition entrypoints
- `src/core/*`: app lifecycle and logging implementations
- `src/input/*`: input orchestration and protocol implementations
- `src/input/platform/*`: private WinRT serial/device adapters
- `src/input/makcu/*`: private Makcu orchestration helpers
- `src/capture/common/*`: private capture shared data contracts
- `src/capture/*`: private capture shared/abstract components
- `src/capture/pipeline/*`: private capture pipeline components
- `src/inference/platform/dml/*`: private DML backend components
  - `dml_image_processor` orchestrates preprocessing
  - `dml_image_processor_interop` owns D3D11/D3D12 shared resource/fence interop
  - `dml_image_processor_preprocess` owns compute preprocess pipeline setup/dispatch
- `src/inference/engine/*`: private debug backend components
- `src/capture/sources/winrt/*`: private WinRT capture components
- `src/capture/runtime/*`: private runtime composition
- `src/platform/winrt/*`: private platform runtime context
- `config/*`: runtime configuration inputs

## Extension Guidelines (Core)
- Add new platform adapters behind existing interfaces.
- Favor composition through factory wiring over exposing concrete types upward.
- For mandatory boundary and include rules, follow `AGENTS.md`.

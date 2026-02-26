# AGENTS

## Scope
- This is the global agent policy for the entire repository.
- Apply these rules unless a user instruction explicitly overrides them.

## Communication Policy
- Respond to the user in Japanese.
- Write and edit repository files in English unless the user explicitly requests another language.

## Quick Commands
- Build + test (default RelWithDebInfo): `python.exe build.py`
- Build + test (Debug): `python.exe build.py --config Debug`
- Build + test (Release): `python.exe build.py --config Release`
- Build + test (RelWithDebInfo): `python.exe build.py --config RelWithDebInfo`
- Build only (skip tests): `python.exe build.py --no-test`
- Lint (all, default Debug DB): `python.exe scripts/run-clang-tidy.py --all`
- Lint (diff, default Debug DB): `git diff --cached | python.exe scripts/run-clang-tidy.py --diff`
- Install git hooks: `python.exe scripts/install-hooks.py`
- Run binary (Windows preset output): `./build/RelWithDebInfo/VisionFlow.exe`

## Boundaries
### Always Do
- Read `.clang-format` and `.clang-tidy` before code changes.
- Read `docs/ARCHITECTURE.md` before changing architecture boundaries or ownership flows.
- Read `docs/CONTRIBUTING.md` before branch/PR/commit operations.
- Update `docs/ARCHITECTURE.md` when architecture boundaries or ownership flows change.
- Keep changes scoped to the user request.
- Preserve existing architecture boundaries and interfaces when possible.
- Add logs for critical state transitions and failures in runtime-critical paths.

### Ask First
- Large refactors that change file/module boundaries.
- API changes that affect call sites outside the current task.
- Dependency upgrades, protocol changes, or behavior changes with operational risk.

### Never Do
- Edit generated files under `build/` as part of source changes.
- Modify `third_party/` code unless the user explicitly asks.
- Run destructive git operations (`reset --hard`, history rewrite) without explicit approval.
- Introduce secrets, tokens, or environment-specific credentials into source files.

## Validation Requirements
- Code changes: build and test the project (`python.exe build.py`) and report results.
- Interface changes: verify all impacted call sites compile.
- Runtime path changes: validate startup/shutdown and error-path behavior.
- Build/config changes: rerun configure + build.
- If validation cannot be executed, clearly state what was not run and why.

## Testing Guidelines
- Test scope priority: unit tests first; integration tests are introduced in a later phase when external dependencies and environments are stabilized.
- Place tests under `tests/`:
  - `tests/unit/`: unit tests
  - `tests/integration/`: integration tests (reserved / phased rollout)
- Prefer mocking boundary interfaces (`ISerialPort`, `IDeviceScanner`, `IMouseController`) to isolate logic and keep tests deterministic.
- Add or update unit tests for new logic and bug fixes when practical.
- Use descriptive test names that encode behavior and scenario.
- Standard local test run:
  - `python.exe build.py --config Debug`
- Build-only fallback (when tests are intentionally skipped):
  - `python.exe build.py --no-test`

## Coding Guidelines

### 1. Architectural Integrity
- **SOLID**: Maintain focused responsibilities. Prefer clear abstractions (interfaces/abstract classes) so modules can evolve without cascading edits.
- **LSP (Liskov Substitution Principle)**: Implementations must remain substitutable for their interfaces. Do not introduce hidden preconditions or behavior that violates interface expectations.
- **Composition over Inheritance**: Favor assembling behavior from small, decoupled components.
- **DRY (Don't Repeat Yourself)**: Keep each piece of logic in one authoritative place.
- **Platform Boundary Isolation**: Isolate platform-specific dependencies behind explicit boundaries. Keep upper layers dependent on abstractions, not direct OS/API calls.
- **Abstraction Ownership Rule**: Public interfaces (for example `i_*.hpp`) MUST remain implementation-agnostic. Do not expose platform-specific types, names, or semantics in interface contracts.

### 2. Resource, Performance, and Constants
- **RAII (Resource Acquisition Is Initialization)**: Bind resource lifecycles (memory, mutexes, handles) to object lifetimes.
- **Rule of Zero**: Prefer designs where ownership and cleanup are handled by standard library types so custom special member functions are unnecessary. Use explicit `= delete` or custom special members only when ownership semantics require it.
- **Value Semantics**: Prefer value/reference semantics where practical.
- **Hot Path Allocation Rule**: Avoid unnecessary dynamic allocation in hot paths. Reuse buffers and keep per-iteration overhead predictable.
- **Magic Number Rule (`constexpr`)**: Replace magic numbers with named `constexpr` constants.

### 3. Simplicity & Pragmatism
- **KISS (Keep It Simple, Stupid)**: In this project, "simple" means code that a reader can understand correctly in the shortest time. Choose the design with the clearest intent and no unnecessary structural elements.
- **Avoid Over-Engineering**: Do not add layered abstractions based only on hypothetical future needs.
- **Linear Logic**: Prefer boring, explicit control flow over clever or overly compact constructs when readability is better.
- **YAGNI (You Aren't Gonna Need It)**: Do not add hypothetical extension points.
- **Zero-Overhead Principle**: Do not pay runtime cost for unused abstractions.
- **Helper Placement Rule**:
  - If logic does not require class state (member variables), it MUST be implemented in the `.cpp` anonymous namespace (or a `static` free function), not as a member function.
  - Use a private member function only when it reads or writes class internal state.

### 4. Safety & Robustness
- **Interface Safety**: Make APIs easy to use correctly and hard to misuse.
- **No-Discard Rule**: Functions with failure-prone or side-effectful outcomes should return values that must be checked (use `[[nodiscard]]` where appropriate). If discarding is intentional, do it explicitly (for example `static_cast<void>(result)`).
- **State Machine Discipline**: Manage mutable state with explicit `enum class` state models and validated transitions.
- **Thread Ownership & Shutdown Rule**: Assign each thread a single owner and enforce explicit shutdown sequencing (`request stop -> wake -> join`).
- **Principle of Least Astonishment**: Keep behavior intuitive and consistent.
- **Const Correctness**: Use `const` by default where possible.
- **Null Pointer Check Rule**: Use explicit null checks (`ptr == nullptr` / `ptr != nullptr`). Do not rely on implicit pointer-to-bool conversion in conditions.

### 5. Naming Guidelines
- **Variable Naming**: Use nouns in `camelBack`.
- **Boolean Exception (Formal)**: Boolean variables are a formal exception to noun-only naming. State words in `camelBack` (for example `empty`, `connected`, `pending`) are allowed; avoid `is`/`has` prefixes when context is clear.
- **Function Naming Rules**: Use descriptive verb phrases in `lowerCamelCase`. Prefer imperative or direct action-oriented forms.
- **Exceptions**: Small-scope counters and iterators (for example `i`, `j`) are permitted.

### 6. Include Guidelines
- Keep include blocks grouped in this order:
  1. Related header (matching header for the current source file, e.g. `foo.cc` -> `foo.h`)
  2. C system headers (for example `<stdint.h>`, `<stdlib.h>`)
  3. C++ standard library headers (for example `<vector>`, `<string>`, `<expected>`)
  4. Third-party and OS headers (for example `<spdlog/spdlog.h>`, `<gtest/gtest.h>`, `<Windows.h>`, `<unistd.h>`)
  5. Project headers
- Insert one blank line between include groups.
- Use `<...>` for:
  - C++ standard library headers
  - C standard library headers
  - Third-party library headers
  - OS/platform headers
- Use `"..."` for project headers in this repository.
- **Self-contained Headers**: Every header must compile in isolation with its own required includes or forward declarations, and must not depend on include order side effects.
- Path base rules for project headers:
  - Public headers: write paths from `include/` root.
  - Private headers: write paths from `src/` root.
- Public Header Platform Independence:
  - Public headers under `include/` MUST remain platform-independent.
  - Do not include platform/OS headers (for example `<Windows.h>`, `<SetupAPI.h>`, `<unistd.h>`) from public headers.
  - Do not expose platform-specific types/macros/constants in public API signatures.
  - Keep platform-specific includes and concrete implementations in `src/` private headers/sources, behind abstract interfaces.
- Example project include path:
  - `"VisionFlow/input/i_serial_port.hpp"`

### 7. CMake Guidelines
- **Project Constants/Locals (`k` Prefix)**: Project-specific CMake constants and non-cache variables MUST use `k` prefix (for example `kOnnxRuntimeRoot`) to avoid confusion with built-in `CMAKE_*` variables.
- **Options/Flags (`VF_` Prefix)**: Public cache options and build flags exposed to CLI MUST use `VF_` prefix with uppercase snake case (for example `VF_HAS_ONNXRUNTIME_DML`).
- **Functions/Macros (`vf_` Prefix)**: Custom CMake functions/macros MUST use `vf_` prefix to prevent namespace collisions and make project ownership explicit.
- **Target Scope Rule**: Directory-scope APIs (`include_directories`, `link_libraries`, `add_definitions`) MUST NOT be used for project configuration; use `target_*` APIs instead.
- **Visibility Rule**: `target_*` calls MUST always specify `PRIVATE`, `PUBLIC`, or `INTERFACE` explicitly.
- **Cache Variable Rule**: Only variables intentionally exposed for external configuration may use `CACHE`; internal flow variables MUST use normal (non-cache) variables.
- **Option Definition Rule**: Feature toggles MUST be declared with `option(VF_... "..." <default>)`, and SHOULD include a short comment for intent/scope impact.
- **Configuration Path Rule**: Build-config-specific paths MUST NOT hardcode one config directory (`Debug`/`Release`/`RelWithDebInfo` only); resolve using active configuration (`CMAKE_BUILD_TYPE` and/or generator expressions).
- **Dependency Failure Policy**: CMake modules MUST define behavior for missing dependencies: required dependencies use `FATAL_ERROR`; optional dependencies auto-disable with explicit `STATUS` logs.
- **Module Placement Rule**: Custom CMake modules MUST be placed under `cmake/`, and module filenames SHOULD use `PascalCase.cmake`.
- **Message Rule**: Use `message(STATUS ...)` for normal diagnostics; `WARNING` and `FATAL_ERROR` MUST be reserved for actionable risk states.
- **Policy Rule**: `cmake_policy(SET ...)` changes MUST be centralized at top-level CMake entrypoint and include a brief reason.
- **Test Integration Rule**: Test-only CMake configuration MUST stay under `BUILD_TESTING` guards and MUST NOT add unnecessary runtime dependencies to production targets.
- **FetchContent Rule**: `FetchContent` dependencies MUST pin reproducible versions (`GIT_TAG` commit/tag), and updates MUST be reviewed for reproducibility, license, and binary-size impact.

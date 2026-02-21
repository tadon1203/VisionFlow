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
- Run binary (Windows preset output): `./build/RelWithDebInfo/VisionFlow.exe`

## Project Structure
- `include/`: Public headers and interface contracts.
- `src/`: Implementations.
- `third_party/`: External dependencies; do not modify unless explicitly requested.
- `build/`: Generated artifacts only.

## Boundaries
### Always Do
- Read `.clang-format` and `.clang-tidy` before code changes.
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

## Git Workflow
- Keep one logical change per commit scope.
- Do not revert unrelated user changes.
- Minimize diff size; avoid drive-by edits.
- Keep commit-ready code deterministic and reviewable.
- Prefer branch-based development unless a change is explicitly allowed for direct `main` commits.
- For branch-based changes, merge into `main` using **Squash and Merge**.
- Do not use merge commits or rebase-and-merge for normal branch integration.
- Before squash merging, ensure the PR title already matches Conventional Commits format.

### Main Direct Commit Policy
- Direct commits to `main` are allowed only for **lightweight** `docs/`, `chore/`, and `build/` changes.
- "Lightweight" means text-only or non-behavioral updates (for example typo fixes, comment wording, small config housekeeping).
- If behavior, logic, architecture, API contracts, or runtime semantics may change, use a branch.
- When uncertain, choose branch-based development.
- If a change goes through a PR (including `docs/`, `chore/`, `build/`), use **Squash and Merge**.

### Branch Naming Convention
- Use: `prefix/short-description`
- Use lowercase and kebab-case for `short-description`.
- Keep `short-description` concise and descriptive.
- Allowed prefixes: `docs/`, `chore/`, `feat/`, `fix/`, `refactor/`, `perf/`, `build/`, `test/`, `ci/`.

### Pull Request Policy
- PR descriptions MUST use `.github/PULL_REQUEST_TEMPLATE.md`.
- `Summary` MUST describe intent and scope of the change concisely.
- `Validation Results` MUST be updated based on actual execution.
- If a validation item is not executed, include a short reason in the PR description.
- For `--diff` validators, pass diff input via stdin (for example `git diff --cached | ...`).
- PR titles MUST follow Conventional Commits (same rule as squash commit titles).

### Prefix Policy Matrix
| Prefix | Direct `main` Commit | Branch Usage | Purpose |
| --- | --- | --- | --- |
| `docs/` | Allowed (lightweight) | Allowed (large) | Documentation and comment updates |
| `chore/` | Allowed (lightweight) | Allowed (large) | Housekeeping, config files, `.gitignore` |
| `build/` | Allowed (lightweight) | Allowed (large) | Build configuration, `build.py`, presets |
| `feat/` | Forbidden | Required | New features |
| `fix/` | Forbidden | Required | Bug fixes |
| `refactor/` | Forbidden | Required | Code restructuring without feature intent |
| `perf/` | Forbidden | Required | Performance improvements |
| `test/` | Forbidden | Required | Test additions and updates |
| `ci/` | Forbidden | Required | CI pipeline changes |

## Commit Message Convention
- Follow **Conventional Commits** for all commits.
- PR titles MUST also follow Conventional Commits because they become the final commit title in Squash and Merge.
- Required header format: `<type>[optional scope][optional !]: <description>`
- Use lowercase `type` and concise imperative descriptions.
- `type` values:
  - `feat`: new feature
  - `fix`: bug fix
  - `build`, `chore`, `ci`, `docs`, `style`, `refactor`, `perf`, `test`, `revert`: allowed and recommended when applicable
- `scope` is optional and should be a noun in parentheses (for example `fix(serial): ...`).
- Breaking changes:
  - Use `!` before `:`, and/or
  - Add footer `BREAKING CHANGE: <description>` (uppercase token required).
- Body is optional and must start after one blank line from the header.
- Footers are optional and must start after one blank line from body (or header if no body).
- Prefer splitting mixed-purpose changes into multiple commits instead of using one ambiguous type.

## Coding Guidelines

### 1. Architectural Integrity
- **SOLID**: Maintain focused responsibilities. Prefer clear abstractions (interfaces/abstract classes) so modules can evolve without cascading edits.
- **Composition over Inheritance**: Favor assembling behavior from small, decoupled components.
- **DRY (Don't Repeat Yourself)**: Keep each piece of logic in one authoritative place.
- **Platform Boundary Isolation**: Isolate platform-specific dependencies behind explicit boundaries. Keep upper layers dependent on abstractions, not direct OS/API calls.

### 2. Resource, Performance, and Constants
- **RAII (Resource Acquisition Is Initialization)**: Bind resource lifecycles (memory, mutexes, handles) to object lifetimes.
- **Value Semantics**: Prefer value/reference semantics where practical.
- **Hot Path Allocation Rule**: Avoid unnecessary dynamic allocation in hot paths. Reuse buffers and keep per-iteration overhead predictable.
- **Magic Number Rule (`constexpr`)**: Replace magic numbers with named `constexpr` constants.

### 3. Simplicity & Pragmatism
- **KISS (Keep It Simple, Stupid)**: In this project, "simple" means code that a reader can understand correctly in the shortest time. Choose the design with the clearest intent and no unnecessary structural elements.
- **YAGNI (You Aren't Gonna Need It)**: Do not add hypothetical extension points.
- **Zero-Overhead Principle**: Do not pay runtime cost for unused abstractions.

### 4. Safety & Robustness
- **Interface Safety**: Make APIs easy to use correctly and hard to misuse.
- **State Machine Discipline**: Manage mutable state with explicit `enum class` state models and validated transitions.
- **Thread Ownership & Shutdown Rule**: Assign each thread a single owner and enforce explicit shutdown sequencing (`request stop -> wake -> join`).
- **Principle of Least Astonishment**: Keep behavior intuitive and consistent.
- **Const Correctness**: Use `const` by default where possible.

### 5. Naming Guidelines
- **Variable Naming**: Use nouns in `camelBack`.
- **Boolean Exception (Formal)**: Boolean variables are a formal exception to noun-only naming. State words in `camelBack` (for example `empty`, `connected`, `pending`) are allowed; avoid `is`/`has` prefixes when context is clear.
- **Function Naming Rules**: Use descriptive verb phrases in `lowerCamelCase`. Prefer imperative or direct action-oriented forms.
- **Exceptions**: Small-scope counters and iterators (for example `i`, `j`) are permitted.

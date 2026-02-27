# Contributing Guide

## Scope
- This document defines repository workflow and contribution process rules.
- These rules apply to branch/PR/commit operations and are operational, not architectural.

## Git Workflow
- Keep one logical change per commit scope.
- Do not revert unrelated user changes.
- Minimize diff size; avoid drive-by edits.
- Keep commit-ready code deterministic and reviewable.
- Always use pull requests for integration; direct integration to `main` is not allowed.
- Use branch-based development for all changes; do not commit directly to `main`.
- For branch-based changes, merge into `main` using **Squash and Merge**.
- Do not use merge commits or rebase-and-merge for normal branch integration.
- Before squash merging, ensure the PR title already matches Conventional Commits format.
- Enable repository-managed hooks with `python.exe scripts/install-hooks.py` after clone/setup.
- Pre-commit runs diff-based format check via `run-clang-format.py --diff`.
- If formatting updates files, the hook rewrites them and blocks commit once; re-stage and commit again.

### Main Direct Commit Policy
- Direct commits to `main` are forbidden.
- All changes must always go through a pull request and **Squash and Merge**.
- If branch protection blocks direct push, do not bypass with policy exceptions.

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

### PR Body Safety Rule
- When creating or editing PR descriptions with GitHub CLI, DO NOT pass markdown directly via `--body`.
- Use `--body-file <path>` for `gh pr create` and `gh pr edit`.
- Generate body files with single-quoted heredoc (`cat <<'EOF'`) to prevent shell expansion.
- Treat backticks, `$()`, and `$VAR` as expansion-sensitive content and always keep them in body files.
- After creating or updating a PR, verify the rendered body with `gh pr view --json body`.
- If `gh pr edit` fails due to environment/API constraints, `gh api repos/<owner>/<repo>/pulls/<number> -X PATCH --raw-field body=...` is an allowed fallback.

Example:
```bash
cat > /tmp/pr_body.md <<'EOF'
## Summary
- ...

## Validation Results
- [x] `python.exe build.py` (Build & Test OK)
EOF

gh pr create --base main --head <branch> --title "<title>" --body-file /tmp/pr_body.md
gh pr view --json body --jq .body
```

### Prefix Policy Matrix
All prefixes follow the same workflow rule: direct `main` commit is forbidden, and branch + PR are required.

| Prefix | Purpose |
| --- | --- |
| `docs/` | Documentation and comment updates |
| `chore/` | Housekeeping, config files, `.gitignore` |
| `build/` | Build configuration, `build.py`, presets |
| `feat/` | New features |
| `fix/` | Bug fixes |
| `refactor/` | Code restructuring without feature intent |
| `perf/` | Performance improvements |
| `test/` | Test additions and updates |
| `ci/` | CI pipeline changes |

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

## ONNX Runtime Asset Deployment
- The repository does not version ONNX Runtime binary payloads under source control for CI portability.
- Package and publish ONNX Runtime binaries to GitHub Releases, then update CMake constants.

### Workflow
1. Place updated binaries under `third_party/onnxruntime/<version>/`.
2. Run `python.exe scripts/deploy_assets.py --version <version>`.
3. Copy emitted `kOnnxRuntimeAssetUrl` and `kOnnxRuntimeAssetSha256` values into `cmake/OnnxRuntimeDml.cmake`.
4. Commit the CMake update and run CI.

### Packaging Rules
- ZIP compression MUST be `deflate` with compression level `9`.
- Archive root MUST contain `include/...` and `lib/win-x64/...` directly (no `third_party/...` prefix).

## Smart Pointer Policy
- `std::shared_ptr` is prohibited in this repository for production code.
- Exception: logger implementation (`VisionFlow/core/logger.*`) may use `std::shared_ptr` because of spdlog ownership APIs.
- Preferred ownership model:
  - Ownership: `std::unique_ptr`
  - Non-owning references: raw pointers (`T*`) or references (`T&`)
- Do not introduce new shared ownership unless the logger exception explicitly applies.

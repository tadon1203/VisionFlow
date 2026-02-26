# Review Guide

## Purpose
- This document defines how to review changes, not what to implement.
- Do not restate rules from `AGENTS.md` `## Coding Guidelines`; reference them when needed.
- Keep review comments focused on risk, evidence, and required action.

## Review Priority (Risk-First)
- Review in this order and record all findings:
  1. Correctness and regression risk (behavior mismatch, broken existing flows)
  2. Safety and robustness risk (state transitions, error paths, shutdown sequencing)
  3. Architecture boundary risk (ownership leaks, boundary violations, abstraction bypass)
  4. Performance and resource risk (hot-path allocations, leaks, unnecessary copies)
  5. Test and validation risk (missing tests, weak validation coverage, skipped checks)

## Finding Format
- Each finding MUST include all fields below:
  - `Severity`: `critical` | `high` | `medium` | `low`
  - `Category`: one of the risk areas from this section
  - `Evidence`: concrete file path and line reference
  - `Required Action`: exact change needed to resolve the finding
- Suggested template:
  - `Severity=<...> | Category=<...> | Evidence=<path:line> | Required Action=<...>`

## Decision Policy (Block on All Findings)
- Any unresolved finding blocks approval (`Request changes`).
- **Cleanliness as a Requirement**: Findings are not limited to functional defects. Violations of `Coding Guidelines` that increase maintenance risk (for example: leaky abstractions, incorrect helper placement, or unnecessary coupling) MUST be reported and block approval.
- Maintainability-related findings default to `medium`; escalate to `high` when they affect architecture boundaries, ownership, or API clarity.
- Approval requires either:
  - all findings resolved, or
  - an explicit agreement in the PR for each deferred finding with rationale and a tracking issue/task ID.
- Deferred items without traceable tracking IDs are not acceptable for approval.

#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Iterable

SUPPORTED_EXTENSIONS = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
}
EXCLUDED_TOP_LEVEL = {".git", "build", "third_party"}
HUNK_PATTERN = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


@dataclass
class FilePatch:
    path: str
    ranges: list[tuple[int, int]] = field(default_factory=list)


def run_command(command: list[str]) -> int:
    print("+", " ".join(command))
    completed = subprocess.run(command, check=False)
    return completed.returncode


def resolve_binary(user_binary: str | None) -> str:
    if user_binary:
        return user_binary

    binary = shutil.which("clang-format")
    if binary:
        return binary

    lookup = subprocess.run(
        ["where.exe", "clang-format"],
        check=False,
        capture_output=True,
        text=True,
    )
    candidates = [line.strip() for line in lookup.stdout.splitlines() if line.strip()]
    if lookup.returncode != 0 or not candidates:
        print("error: could not find clang-format in PATH or via where.exe", file=sys.stderr)
        raise SystemExit(2)
    return candidates[0]


def is_target_file(path: Path) -> bool:
    if path.suffix.lower() not in SUPPORTED_EXTENSIONS:
        return False
    parts = set(path.parts)
    if parts & EXCLUDED_TOP_LEVEL:
        return False
    return True


def normalize_repo_path(path_text: str) -> Path:
    normalized = path_text.strip()
    if normalized.startswith("a/") or normalized.startswith("b/"):
        normalized = normalized[2:]
    return Path(normalized.replace("\\", "/"))


def merge_ranges(ranges: Iterable[tuple[int, int]]) -> list[tuple[int, int]]:
    sorted_ranges = sorted(ranges)
    if not sorted_ranges:
        return []

    merged: list[tuple[int, int]] = []
    current_start, current_end = sorted_ranges[0]
    for start, end in sorted_ranges[1:]:
        if start <= current_end + 1:
            current_end = max(current_end, end)
            continue
        merged.append((current_start, current_end))
        current_start, current_end = start, end
    merged.append((current_start, current_end))
    return merged


def parse_unified_diff(diff_text: str) -> dict[str, list[tuple[int, int]]]:
    patches: dict[str, FilePatch] = {}
    current_path: str | None = None

    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            path_token = line[4:].strip()
            if path_token == "/dev/null":
                current_path = None
                continue
            path = normalize_repo_path(path_token)
            if is_target_file(path):
                current_path = path.as_posix()
                patches.setdefault(current_path, FilePatch(path=current_path))
            else:
                current_path = None
            continue

        match = HUNK_PATTERN.match(line)
        if not match or current_path is None:
            continue

        start = int(match.group(1))
        count_text = match.group(2)
        count = int(count_text) if count_text else 1
        if count == 0:
            continue

        end = start + count - 1
        patches[current_path].ranges.append((start, end))

    return {
        file_path: merge_ranges(file_patch.ranges)
        for file_path, file_patch in patches.items()
        if file_patch.ranges
    }


def discover_all_files() -> list[str]:
    files: list[str] = []
    for path in Path(".").rglob("*"):
        if not path.is_file():
            continue
        if is_target_file(path):
            files.append(path.as_posix().lstrip("./"))
    return sorted(files)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run clang-format on all files or diff-selected line ranges"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--all", action="store_true", help="format all source/header files")
    mode.add_argument("--diff", action="store_true", help="format only line ranges from unified diff on stdin")
    parser.add_argument("--check", action="store_true", help="check only (no in-place changes)")
    parser.add_argument("--binary", help="path to clang-format executable")
    return parser.parse_args()


def run_all(binary: str, check: bool) -> int:
    files = discover_all_files()
    if not files:
        print("no files to process")
        return 0

    command = [binary, "--style=file"]
    if check:
        command.extend(["--dry-run", "--Werror"])
    else:
        command.append("-i")
    command.extend(files)
    return run_command(command)


def run_diff(binary: str, check: bool) -> int:
    diff_text = sys.stdin.read()
    if not diff_text.strip():
        print("error: --diff requires unified diff text on stdin", file=sys.stderr)
        return 2

    file_ranges = parse_unified_diff(diff_text)
    if not file_ranges:
        print("no matching changed lines found")
        return 0

    overall_exit_code = 0
    for file_path, ranges in sorted(file_ranges.items()):
        command = [binary, "--style=file"]
        if check:
            command.extend(["--dry-run", "--Werror"])
        else:
            command.append("-i")

        for start, end in ranges:
            command.append(f"--lines={start}:{end}")
        command.append(file_path)

        exit_code = run_command(command)
        if exit_code != 0:
            overall_exit_code = exit_code

    return overall_exit_code


def main() -> None:
    args = parse_args()
    binary = resolve_binary(args.binary)

    if args.all:
        code = run_all(binary, args.check)
    else:
        code = run_diff(binary, args.check)

    raise SystemExit(code)


if __name__ == "__main__":
    main()

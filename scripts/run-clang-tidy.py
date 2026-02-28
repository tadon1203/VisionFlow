#!/usr/bin/env python3

from __future__ import annotations

import argparse
import asyncio
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

DEFAULT_IREGEX = r".*\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inc)$"
DEFAULT_HEADER_FILTER = r"^(?:.*[\\/])?(?:include|src|tests)[\\/].*$"
EXCLUDED_TOP_LEVEL = {".git", "build", "third_party"}
HUNK_PATTERN = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")
CONFIG_TO_CONFIGURE_PRESET = {
    "Debug": "debug-configure",
    "Release": "release-configure",
    "RelWithDebInfo": "relwithdebinfo-configure",
}
CONFIG_TO_BUILD_PRESET = {
    "Debug": "debug",
    "Release": "release",
    "RelWithDebInfo": "relwithdebinfo",
}
COMPILE_COMMANDS_DIR = Path("build")
COMPILE_COMMANDS_PATH = COMPILE_COMMANDS_DIR / "compile_commands.json"


def normalize_repo_path(path_text: str) -> str:
    normalized = path_text.strip()
    if normalized.startswith("a/") or normalized.startswith("b/"):
        normalized = normalized[2:]
    return normalized.replace("\\", "/")


def is_excluded(path_text: str) -> bool:
    path = Path(path_text)
    return bool(set(path.parts) & EXCLUDED_TOP_LEVEL)


def resolve_binary(name: str, user_binary: str | None) -> str:
    if user_binary:
        return user_binary

    binary = shutil.which(name)
    if binary:
        return binary

    lookup = subprocess.run(
        ["where.exe", name],
        check=False,
        capture_output=True,
        text=True,
    )
    candidates = [line.strip() for line in lookup.stdout.splitlines() if line.strip()]
    if lookup.returncode != 0 or not candidates:
        print(f"error: could not find {name} in PATH or via where.exe", file=sys.stderr)
        raise SystemExit(2)
    return candidates[0]


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    if not ranges:
        return []

    ordered = sorted(ranges)
    merged: list[tuple[int, int]] = []
    start, end = ordered[0]
    for next_start, next_end in ordered[1:]:
        if next_start <= end + 1:
            end = max(end, next_end)
            continue
        merged.append((start, end))
        start, end = next_start, next_end
    merged.append((start, end))
    return merged


def parse_unified_diff(diff_text: str, file_pattern: re.Pattern[str]) -> dict[str, list[tuple[int, int]]]:
    file_ranges: dict[str, list[tuple[int, int]]] = {}
    current_path: str | None = None

    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            token = line[4:].strip()
            if token == "/dev/null":
                current_path = None
                continue
            normalized = normalize_repo_path(token)
            if is_excluded(normalized) or not file_pattern.match(normalized):
                current_path = None
                continue
            current_path = normalized
            file_ranges.setdefault(current_path, [])
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
        file_ranges[current_path].append((start, end))

    return {
        file_path: merge_ranges(ranges)
        for file_path, ranges in file_ranges.items()
        if ranges
    }


def discover_all_files(file_pattern: re.Pattern[str]) -> list[str]:
    files: list[str] = []
    for path in Path(".").rglob("*"):
        if not path.is_file():
            continue
        relative = path.as_posix().lstrip("./")
        if is_excluded(relative):
            continue
        if file_pattern.match(relative):
            files.append(relative)
    return sorted(files)


def build_line_filter(path: str, ranges: list[tuple[int, int]]) -> str:
    filter_payload = [{"name": path, "lines": [[start, end] for start, end in ranges]}]
    return json.dumps(filter_payload, separators=(",", ":"))


async def run_tidy_for_file(
    semaphore: asyncio.Semaphore,
    binary: str,
    file_path: str,
    header_filter: str,
    fix: bool,
    line_filter: str | None,
) -> tuple[str, int, str, str]:
    command = [binary, file_path, "-p", str(COMPILE_COMMANDS_DIR), "-header-filter", header_filter]
    if line_filter is not None:
        command.extend(["-line-filter", line_filter])
    if fix:
        command.append("--fix")

    async with semaphore:
        print("+", " ".join(command))
        process = await asyncio.create_subprocess_exec(
            *command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await process.communicate()
    return (
        file_path,
        process.returncode,
        stdout.decode("utf-8", errors="replace"),
        stderr.decode("utf-8", errors="replace"),
    )


async def run_jobs(
    binary: str,
    header_filter: str,
    fix: bool,
    jobs: int,
    targets: list[tuple[str, str | None]],
) -> int:
    semaphore = asyncio.Semaphore(jobs)
    tasks = [
        run_tidy_for_file(
            semaphore=semaphore,
            binary=binary,
            file_path=file_path,
            header_filter=header_filter,
            fix=fix,
            line_filter=line_filter,
        )
        for file_path, line_filter in targets
    ]
    results = await asyncio.gather(*tasks)

    failed = 0
    for file_path, return_code, stdout, stderr in results:
        if stdout.strip():
            print(stdout, end="" if stdout.endswith("\n") else "\n")
        if stderr.strip():
            print(stderr, end="" if stderr.endswith("\n") else "\n", file=sys.stderr)
        if return_code != 0:
            failed += 1
            print(f"failed: {file_path} (exit code {return_code})", file=sys.stderr)

    if failed:
        print(f"clang-tidy finished with {failed} failed file(s)", file=sys.stderr)
        return 1
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run clang-tidy on all files or diff-selected files/line filters"
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--all", action="store_true", help="run clang-tidy for all matching files")
    mode.add_argument("--diff", action="store_true", help="run clang-tidy from unified diff on stdin")

    parser.add_argument(
        "--config",
        choices=["Debug", "Release", "RelWithDebInfo"],
        default="Debug",
        help="configure preset used to refresh compile_commands.json (default: Debug)",
    )
    parser.add_argument("--fix", action="store_true", help="apply fix-its")
    parser.add_argument("-j", "--jobs", type=int, default=(os.cpu_count() or 1), help="parallel jobs")
    parser.add_argument("--binary", help="path to clang-tidy executable")
    parser.add_argument("--iregex", default=DEFAULT_IREGEX, help="regex for diff/all target files")
    parser.add_argument(
        "--header-filter",
        default=DEFAULT_HEADER_FILTER,
        help="clang-tidy header filter regex",
    )
    return parser.parse_args()


def run_command(command: list[str]) -> None:
    print("+", " ".join(command))
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def resolve_cmake_executable() -> str:
    cmake_from_path = shutil.which("cmake")
    if cmake_from_path:
        return cmake_from_path

    lookup = subprocess.run(
        ["where.exe", "cmake"],
        check=False,
        capture_output=True,
        text=True,
    )
    candidates = [line.strip() for line in lookup.stdout.splitlines() if line.strip()]
    if lookup.returncode != 0 or not candidates:
        print("error: could not find cmake in PATH or via where.exe", file=sys.stderr)
        raise SystemExit(2)
    return candidates[0]


def refresh_compile_commands(config: str) -> None:
    cmake_executable = resolve_cmake_executable()
    configure_preset = CONFIG_TO_CONFIGURE_PRESET[config]
    build_preset = CONFIG_TO_BUILD_PRESET[config]
    configure_command = [cmake_executable, "--preset", configure_preset]
    run_command(configure_command)
    sync_command = [cmake_executable, "--build", "--preset", build_preset, "--target", "syncCompileCommands"]
    run_command(sync_command)


def validate_inputs(args: argparse.Namespace) -> re.Pattern[str]:
    if args.jobs <= 0:
        print("error: --jobs must be greater than zero", file=sys.stderr)
        raise SystemExit(2)

    if not COMPILE_COMMANDS_PATH.exists():
        print(
            f"error: compile_commands.json not found at {COMPILE_COMMANDS_PATH}",
            file=sys.stderr,
        )
        raise SystemExit(2)

    try:
        file_pattern = re.compile(args.iregex)
    except re.error as error:
        print(f"error: invalid --iregex: {error}", file=sys.stderr)
        raise SystemExit(2) from error
    return file_pattern


def build_targets_from_all(file_pattern: re.Pattern[str]) -> list[tuple[str, str | None]]:
    return [(file_path, None) for file_path in discover_all_files(file_pattern)]


def build_targets_from_diff(file_pattern: re.Pattern[str]) -> list[tuple[str, str | None]]:
    diff_text = sys.stdin.read()
    if not diff_text.strip():
        print("error: --diff requires unified diff text on stdin", file=sys.stderr)
        raise SystemExit(2)

    ranges_by_file = parse_unified_diff(diff_text, file_pattern)
    return [
        (file_path, build_line_filter(file_path, ranges))
        for file_path, ranges in sorted(ranges_by_file.items())
    ]


def main() -> None:
    args = parse_args()
    refresh_compile_commands(args.config)
    file_pattern = validate_inputs(args)
    binary = resolve_binary("clang-tidy", args.binary)

    if args.all:
        targets = build_targets_from_all(file_pattern)
    else:
        targets = build_targets_from_diff(file_pattern)

    if not targets:
        print("no files to process")
        raise SystemExit(0)

    exit_code = asyncio.run(
        run_jobs(
            binary=binary,
            header_filter=args.header_filter,
            fix=args.fix,
            jobs=args.jobs,
            targets=targets,
        )
    )
    raise SystemExit(exit_code)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys


CONFIG_TO_BUILD_PRESET = {
    "Debug": "debug",
    "Release": "release",
    "RelWithDebInfo": "relwithdebinfo",
}


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
        print("error: could not find cmake executable in PATH or via where.exe cmake",
              file=sys.stderr)
        raise SystemExit(2)
    return candidates[0]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure and build VisionFlow from WSL via CMake presets"
    )
    parser.add_argument(
        "--config",
        choices=["Debug", "Release", "RelWithDebInfo"],
        default="RelWithDebInfo",
        help="build configuration (default: RelWithDebInfo)",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    cmake_executable = resolve_cmake_executable()
    build_preset = CONFIG_TO_BUILD_PRESET[args.config]

    configure_command = [cmake_executable, "--preset", "default"]
    run_command(configure_command)

    build_command = [cmake_executable, "--build", "--preset", build_preset]
    run_command(build_command)


if __name__ == "__main__":
    main()

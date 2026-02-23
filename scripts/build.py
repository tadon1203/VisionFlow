#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


CONFIG_TO_BUILD_PRESET = {
    "Debug": "debug",
    "Release": "release",
    "RelWithDebInfo": "relwithdebinfo",
}

CONFIG_TO_CONFIGURE_PRESET = {
    "Debug": "debug-configure",
    "Release": "release-configure",
    "RelWithDebInfo": "relwithdebinfo-configure",
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


def resolve_ctest_executable(cmake_executable: str) -> str:
    cmake_path = Path(cmake_executable)
    ctest_name = "ctest.exe" if cmake_path.name.lower().endswith(".exe") else "ctest"
    sibling_ctest = cmake_path.with_name(ctest_name)
    if sibling_ctest.exists():
        return str(sibling_ctest)

    ctest_from_path = shutil.which("ctest")
    if ctest_from_path:
        return ctest_from_path

    lookup = subprocess.run(
        ["where.exe", "ctest"],
        check=False,
        capture_output=True,
        text=True,
    )
    candidates = [line.strip() for line in lookup.stdout.splitlines() if line.strip()]
    if lookup.returncode != 0 or not candidates:
        print("error: could not find ctest executable in PATH or via where.exe ctest",
              file=sys.stderr)
        raise SystemExit(2)
    return candidates[0]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure, build, and optionally test VisionFlow via CMake presets"
    )
    parser.add_argument(
        "--config",
        choices=["Debug", "Release", "RelWithDebInfo"],
        default="RelWithDebInfo",
        help="build configuration (default: RelWithDebInfo)",
    )
    test_group = parser.add_mutually_exclusive_group()
    test_group.add_argument(
        "--test",
        dest="runTests",
        action="store_true",
        help="run tests after build (default behavior)",
    )
    test_group.add_argument(
        "--no-test",
        dest="runTests",
        action="store_false",
        help="skip tests",
    )
    parser.set_defaults(runTests=True)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    cmake_executable = resolve_cmake_executable()
    ctest_executable = resolve_ctest_executable(cmake_executable)
    configure_preset = CONFIG_TO_CONFIGURE_PRESET[args.config]
    build_preset = CONFIG_TO_BUILD_PRESET[args.config]

    configure_command = [cmake_executable, "--preset", configure_preset]
    run_command(configure_command)

    build_command = [cmake_executable, "--build", "--preset", build_preset]
    run_command(build_command)

    if args.runTests:
        test_dir = str(Path("build") / args.config.lower())
        test_command = [
            ctest_executable,
            "--test-dir",
            test_dir,
            "--output-on-failure",
        ]
        run_command(test_command)


if __name__ == "__main__":
    main()

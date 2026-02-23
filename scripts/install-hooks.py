#!/usr/bin/env python3

from __future__ import annotations

import argparse
import subprocess


def run_command(command: list[str]) -> None:
    print("+", " ".join(command))
    completed = subprocess.run(command, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def main() -> None:
    parser = argparse.ArgumentParser(description="Configure repository-managed git hooks")
    parser.parse_args()
    run_command(["git", "config", "core.hooksPath", ".githooks"])
    print("Configured git hooks path: .githooks")


if __name__ == "__main__":
    main()

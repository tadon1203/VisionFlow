#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile


def run_command(command: list[str], *, capture_output: bool = False) -> str:
    print("+", " ".join(command))
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        capture_output=capture_output,
    )
    if completed.returncode != 0:
        if capture_output:
            if completed.stdout:
                print(completed.stdout, file=sys.stderr)
            if completed.stderr:
                print(completed.stderr, file=sys.stderr)
        raise SystemExit(completed.returncode)
    return completed.stdout if capture_output else ""


def resolve_gh_executable() -> str:
    gh_from_path = shutil.which("gh")
    if gh_from_path:
        return gh_from_path

    print("error: could not find GitHub CLI executable 'gh' in PATH", file=sys.stderr)
    raise SystemExit(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package ONNX Runtime assets and upload to GitHub Release."
    )
    parser.add_argument(
        "--version",
        required=True,
        help="ONNX Runtime version directory name under third_party/onnxruntime (e.g. 1.25.0)",
    )
    parser.add_argument(
        "--source-root",
        default="third_party/onnxruntime",
        help="root directory containing versioned ONNX Runtime folders (default: third_party/onnxruntime)",
    )
    parser.add_argument(
        "--repo",
        default="",
        help="GitHub repository in owner/name format (default: current gh repo)",
    )
    return parser.parse_args()


def verify_input_layout(version_root: Path) -> None:
    required_paths = [
        version_root / "include",
        version_root / "lib" / "win-x64",
    ]
    missing = [str(path) for path in required_paths if not path.exists()]
    if missing:
        print("error: ONNX Runtime source layout is incomplete:", file=sys.stderr)
        for path in missing:
            print(f"  - missing: {path}", file=sys.stderr)
        raise SystemExit(2)


def build_zip(version_root: Path, output_zip: Path) -> None:
    files = sorted(path for path in version_root.rglob("*") if path.is_file())
    if not files:
        print(f"error: no files found under {version_root}", file=sys.stderr)
        raise SystemExit(2)

    with zipfile.ZipFile(
        output_zip,
        mode="w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for file_path in files:
            archive_name = file_path.relative_to(version_root).as_posix()
            archive.write(file_path, arcname=archive_name)


def verify_zip_layout(zip_path: Path) -> None:
    with zipfile.ZipFile(zip_path, mode="r") as archive:
        names = archive.namelist()

    has_include = any(name.startswith("include/") for name in names)
    has_lib = any(name.startswith("lib/win-x64/") for name in names)
    has_prefixed_root = any(name.startswith("third_party/") for name in names)
    if not has_include or not has_lib or has_prefixed_root:
        print("error: generated zip layout is invalid", file=sys.stderr)
        print("  expected: include/... and lib/win-x64/... at archive root", file=sys.stderr)
        print("  forbidden: third_party/... prefix in archive entries", file=sys.stderr)
        raise SystemExit(2)


def compute_sha256(file_path: Path) -> str:
    digest = hashlib.sha256()
    with file_path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_repo(gh: str, explicit_repo: str) -> str:
    if explicit_repo:
        return explicit_repo

    repo_name = run_command(
        [gh, "repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner"],
        capture_output=True,
    ).strip()
    if not repo_name:
        print("error: failed to resolve current GitHub repository", file=sys.stderr)
        raise SystemExit(2)
    return repo_name


def ensure_release_exists(gh: str, repo: str, tag: str) -> None:
    view_command = [gh, "release", "view", tag, "-R", repo]
    completed = subprocess.run(view_command, check=False, text=True, capture_output=True)
    if completed.returncode == 0:
        return

    create_command = [
        gh,
        "release",
        "create",
        tag,
        "--title",
        f"ONNX Runtime Assets {tag}",
        "--notes",
        f"Automated asset release for {tag}",
        "-R",
        repo,
    ]
    run_command(create_command)


def upload_asset(gh: str, repo: str, tag: str, asset_path: Path) -> None:
    command = [
        gh,
        "release",
        "upload",
        tag,
        str(asset_path),
        "--clobber",
        "-R",
        repo,
    ]
    run_command(command)


def main() -> None:
    args = parse_args()
    gh = resolve_gh_executable()

    run_command([gh, "auth", "status"])

    version_root = Path(args.source_root) / args.version
    verify_input_layout(version_root)

    tag = f"onnxruntime-assets-v{args.version}"
    asset_name = f"onnxruntime-{args.version}-win-x64.zip"

    with tempfile.TemporaryDirectory(prefix="onnxruntime-assets-") as temp_dir:
        zip_path = Path(temp_dir) / asset_name
        build_zip(version_root, zip_path)
        verify_zip_layout(zip_path)
        sha256 = compute_sha256(zip_path)

        repo = resolve_repo(gh, args.repo)
        ensure_release_exists(gh, repo, tag)
        upload_asset(gh, repo, tag, zip_path)

    download_url = f"https://github.com/{repo}/releases/download/{tag}/{asset_name}"
    print("\n=== Deployment Output ===")
    print(f"version      : {args.version}")
    print(f"tag          : {tag}")
    print(f"asset_name   : {asset_name}")
    print(f"download_url : {download_url}")
    print(f"sha256       : {sha256}")
    print("\n=== CMake Constants ===")
    print(f'set(kOnnxRuntimeAssetUrl "{download_url}")')
    print(f'set(kOnnxRuntimeAssetSha256 "{sha256}")')


if __name__ == "__main__":
    main()

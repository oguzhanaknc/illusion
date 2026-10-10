#!/usr/bin/env python3
"""Check (or fix) C++ formatting with the pinned clang-format version.

Usage:
    python tools/check_format.py          # report files that need formatting (exit 1 if any)
    python tools/check_format.py --fix    # reformat files in place

The clang-format binary is taken from $CLANG_FORMAT, then PATH. Its major version must match the one
pinned in tools/requirements-dev.txt, because different versions format differently.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ("engine", "tests", "benchmarks", "editor", "runtime", "modules", "samples")
EXTENSIONS = {".h", ".hpp", ".c", ".cpp", ".inl"}


def pinned_major() -> str:
    text = (ROOT / "tools" / "requirements-dev.txt").read_text(encoding="utf-8")
    match = re.search(r"^clang-format==(\d+)\.", text, re.MULTILINE)
    if not match:
        sys.exit("clang-format pin not found in tools/requirements-dev.txt")
    return match.group(1)


def find_clang_format() -> str:
    candidate = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not candidate:
        sys.exit("clang-format not found. Install it with: pip install -r tools/requirements-dev.txt")
    output = subprocess.run([candidate, "--version"], capture_output=True, text=True, check=True).stdout
    match = re.search(r"version (\d+)\.", output)
    expected = pinned_major()
    if not match or match.group(1) != expected:
        sys.exit(f"clang-format {expected}.x required, found: {output.strip()}")
    return candidate


def source_files() -> list[Path]:
    files: list[Path] = []
    for directory in SOURCE_DIRS:
        base = ROOT / directory
        if base.is_dir():
            files.extend(p for p in base.rglob("*") if p.suffix in EXTENSIONS and p.is_file())
    return sorted(files)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--fix", action="store_true", help="reformat files in place")
    args = parser.parse_args()

    clang_format = find_clang_format()
    files = source_files()
    if args.fix:
        subprocess.run([clang_format, "-i", "--style=file", *map(str, files)], check=True)
        print(f"formatted {len(files)} files")
        return 0

    bad: list[Path] = []
    for path in files:
        result = subprocess.run(
            [clang_format, "--dry-run", "--Werror", "--style=file", str(path)], capture_output=True, text=True
        )
        if result.returncode != 0:
            bad.append(path)
    for path in bad:
        print(f"needs formatting: {path.relative_to(ROOT).as_posix()}")
    print(f"{len(files) - len(bad)}/{len(files)} files correctly formatted")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

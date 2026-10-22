#!/usr/bin/env python3
"""Check the layering between the engine and the editor (ADR 0030).

The engine, samples, tools, engine tests and benchmarks must never reach editor code: they may not include
editor headers or Dear ImGui/ImPlot, and their CMake files may not name editor targets. The CMake boundary
check (cmake/IllusionEditor.cmake) catches link dependencies at configure time; this script also catches
includes and target names in configurations that are not built (for example with ILLUSION_BUILD_EDITOR=OFF).

Editor code may use the engine only through its public headers (<illusion/...>), never through relative
paths into another library's sources.

Exit code 1 lists every violation as path:line: message.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Trees that must stay free of editor code.
RUNTIME_TREES = ("engine", "samples", "tools", "tests", "benchmarks")
# Fixtures that describe a violation on purpose.
EXEMPT = (Path("tests") / "cmake" / "editor_boundary",)

SOURCE_SUFFIXES = {".hpp", ".cpp", ".h", ".inl", ".mm"}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
EDITOR_INCLUDE = re.compile(r"^(illusion/editor/|imgui|implot)")
EDITOR_TARGET = re.compile(r"\b(illusion::(editor\w*|imgui|implot)|illusion_(editor\w*|imgui|implot|ui_gallery\w*))\b")
RELATIVE_ESCAPE = re.compile(r"^(\.\./)+(engine|samples|tools)/")


def is_exempt(relative: Path) -> bool:
    return any(relative.is_relative_to(exempt) for exempt in EXEMPT)


def check_runtime_tree(problems: list[str]) -> None:
    for tree in RUNTIME_TREES:
        for path in sorted((ROOT / tree).rglob("*")):
            relative = path.relative_to(ROOT)
            if not path.is_file() or is_exempt(relative) or "__pycache__" in relative.parts:
                continue
            is_cmake = path.name == "CMakeLists.txt" or path.suffix == ".cmake"
            if path.suffix not in SOURCE_SUFFIXES and not is_cmake:
                continue
            text = path.read_text(encoding="utf-8")
            for number, line in enumerate(text.splitlines(), start=1):
                if is_cmake:
                    if line.lstrip().startswith("#"):
                        continue
                    match = EDITOR_TARGET.search(line)
                    if match:
                        problems.append(f"{relative.as_posix()}:{number}: names the editor target '{match.group(1)}'")
                else:
                    match = INCLUDE.match(line)
                    if match and EDITOR_INCLUDE.match(match.group(1)):
                        problems.append(f"{relative.as_posix()}:{number}: includes editor header '{match.group(1)}'")


def check_editor_tree(problems: list[str]) -> None:
    for path in sorted((ROOT / "editor").rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        relative = path.relative_to(ROOT)
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), start=1):
            match = INCLUDE.match(line)
            if match and RELATIVE_ESCAPE.match(match.group(1)):
                problems.append(
                    f"{relative.as_posix()}:{number}: includes '{match.group(1)}' by relative path; "
                    "use the library's public <illusion/...> header"
                )


def main() -> int:
    problems: list[str] = []
    check_runtime_tree(problems)
    check_editor_tree(problems)
    for problem in problems:
        print(problem)
    if problems:
        print(f"{len(problems)} layering violation(s); see ADR 0030", file=sys.stderr)
        return 1
    print("layering OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

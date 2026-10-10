#!/usr/bin/env python3
"""Turn compiler diagnostics in build logs into GitHub Actions annotations.

Usage: python tools/ci/annotate_log.py <log> [<log> ...]

Recognises GCC/Clang (`file:line:col: error: message`) and MSVC (`file(line): error C1234: message`)
diagnostics plus doctest failures, and prints `::error` / `::warning` workflow commands so that failures
show up on the pull request and in the run summary. Always exits 0.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

GNU = re.compile(r"^(?P<file>[^:\s][^:]*?):(?P<line>\d+):(?:(?P<col>\d+):)?\s*(?P<kind>fatal error|error|warning):\s*(?P<msg>.*)$")
MSVC = re.compile(r"^(?P<file>[^()]+)\((?P<line>\d+)(?:,(?P<col>\d+))?\):\s*(?P<kind>fatal error|error|warning)\s+(?P<msg>\w+:.*)$")
DOCTEST = re.compile(r"^(?P<file>.+?)\((?P<line>\d+)\):\s*(?P<kind>ERROR|FATAL ERROR):\s*(?P<msg>.*)$")
LIMIT = 30


def escape(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def main() -> int:
    emitted = 0
    seen: set[str] = set()
    for name in sys.argv[1:]:
        path = Path(name)
        if not path.is_file():
            continue
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip()
            match = GNU.match(line) or MSVC.match(line) or DOCTEST.match(line)
            if not match:
                continue
            kind = "warning" if match.group("kind") == "warning" else "error"
            message = f"{match.group('file')}:{match.group('line')}: {match.group('msg')}"
            if message in seen:
                continue
            seen.add(message)
            print(f"::{kind} title={kind} ({path.name})::{escape(message)}")
            emitted += 1
            if emitted >= LIMIT:
                return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())

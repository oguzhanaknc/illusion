#!/usr/bin/env python3
"""Check that the showcase module calls the whole scripting API.

The scripting API is the Doxygen group `scripting_api`: every class whose documentation says
`@ingroup scripting_api` (ModuleContext and the host service interfaces). Each public member function of
those classes must be called by samples/showcase/modules/showcase/showcase.cpp, as `.name(`, `->name(` or
`.name<`, so that adding a function to the API without exercising it fails CI. The engine test
tests/showcase/TestShowcaseModule.cpp then counts the calls at run time through the proxies of
tests/modules/CountingServices.hpp; this script also checks that each proxy has one counter per function.

Exit code 1 lists every problem.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GROUP = "@ingroup scripting_api"
SHOWCASE = ROOT / "samples" / "showcase" / "modules" / "showcase" / "showcase.cpp"
PROXIES = ROOT / "tests" / "modules" / "CountingServices.hpp"

CLASS = re.compile(r"\bclass\s+(\w+)(?:\s+final)?\s*(?::[^{;]*)?\{")
ACCESS = re.compile(r"\b(public|protected|private)\s*:(?!:)")
FUNCTION = re.compile(r"(~?\b\w+)\s*\(")
NOT_NAMES = {"alignas", "decltype", "noexcept", "requires", "sizeof", "static_assert", "operator"}
PROXY = re.compile(r"CountingProxy<(\w+),\s*(\d+)>")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def class_body(text: str, start: int) -> str:
    """The text between the brace at text[start] and its match, with nested blocks replaced by ';'."""
    depth = 0
    out: list[str] = []
    for i in range(start, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
            if depth == 2:
                out.append(";")
            continue
        if c == "}":
            depth -= 1
            if depth == 0:
                return "".join(out)
            continue
        if depth == 1:
            out.append(c)
    raise ValueError("unbalanced braces")


def public_functions(name: str, body: str) -> list[str]:
    """Names of the public member functions declared in a class body (constructors and operators excluded)."""
    access = "private"
    names: list[str] = []
    for declaration in body.split(";"):
        labels = list(ACCESS.finditer(declaration))
        if labels:
            access = labels[-1].group(1)
            declaration = declaration[labels[-1].end():]
        if access != "public" or "=" in declaration.split("(")[0]:
            continue
        declaration = re.sub(r"\[\[[^\]]*\]\]", " ", declaration)
        match = FUNCTION.search(declaration)
        if match is None:
            continue
        function = match.group(1)
        if function in NOT_NAMES or function.lstrip("~") == name or function in names:
            continue
        names.append(function)
    return names


def scripting_api() -> dict[str, tuple[Path, list[str]]]:
    """Every class of the group with its header and public functions."""
    classes: dict[str, tuple[Path, list[str]]] = {}
    for header in sorted((ROOT / "engine").glob("*/include/**/*.hpp")):
        raw = header.read_text(encoding="utf-8")
        if GROUP not in raw:
            continue
        for match in CLASS.finditer(raw):
            # The documentation block right above the class.
            above = raw[: match.start()].rstrip().splitlines()
            doc: list[str] = []
            while above and above[-1].lstrip().startswith("///"):
                doc.append(above.pop())
            if not any(GROUP in line for line in doc):
                continue
            code = strip_comments(raw)
            definition = re.search(rf"\bclass\s+{match.group(1)}\b[^;{{]*\{{", code)
            if definition is None:
                raise ValueError(f"{header}: cannot find the definition of {match.group(1)}")
            body = class_body(code, definition.end() - 1)
            classes[match.group(1)] = (header, public_functions(match.group(1), body))
    return classes


def main() -> int:
    problems: list[str] = []
    classes = scripting_api()
    if not classes:
        problems.append(f"no class says '{GROUP}'")
    showcase = strip_comments(SHOWCASE.read_text(encoding="utf-8"))
    total = 0
    for name, (header, functions) in classes.items():
        if not functions:
            problems.append(f"{header.relative_to(ROOT).as_posix()}: {name} has no public functions")
        for function in functions:
            total += 1
            if not re.search(rf"(\.|->)\s*{re.escape(function)}\s*[(<]", showcase):
                problems.append(f"{SHOWCASE.relative_to(ROOT).as_posix()}: {name}::{function} is never called")

    proxies = {match.group(1): int(match.group(2)) for match in PROXY.finditer(PROXIES.read_text(encoding="utf-8"))}
    for name, (header, functions) in classes.items():
        if name == "ModuleContext":
            continue  # the host owns it; there is nothing to proxy
        if name not in proxies:
            problems.append(f"{PROXIES.relative_to(ROOT).as_posix()}: no counting proxy of {name}")
        elif proxies[name] != len(functions):
            problems.append(f"{PROXIES.relative_to(ROOT).as_posix()}: the proxy of {name} counts {proxies[name]} "
                            f"functions, the interface has {len(functions)}")

    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f"scripting API coverage: the showcase module calls all {total} functions of {len(classes)} classes")
    return 0


if __name__ == "__main__":
    sys.exit(main())

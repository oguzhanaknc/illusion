#!/usr/bin/env python3
"""Check that the English and Turkish documentation are in sync.

For every page in docs/content/en there must be a page with the same path in docs/content/tr (and
vice versa). For each pair the script compares:

- the heading structure (sequence of heading levels),
- the fenced code blocks (must be byte-identical; code is never translated),
- the number of tables, admonitions and images.

Prose is not compared. Exit code 1 lists every mismatch.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONTENT = ROOT / "docs" / "content"
LANGUAGES = ("en", "tr")

FENCE = re.compile(r"^(\s*)(```|~~~)")
HEADING = re.compile(r"^(#{1,6})\s+\S")
# A table delimiter row contains at least one pipe; a bare "---" is a horizontal rule, not a table.
TABLE_SEPARATOR = re.compile(r"^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)+\|?\s*$|^\s*\|\s*:?-{3,}:?\s*\|\s*$")
ADMONITION = re.compile(r"^(!!!|\?\?\?)\s*\w+")
IMAGE = re.compile(r"!\[[^\]]*\]\(([^)]+)\)")


@dataclass
class PageStructure:
    headings: list[int] = field(default_factory=list)
    code_blocks: list[str] = field(default_factory=list)
    tables: int = 0
    admonitions: int = 0
    images: list[str] = field(default_factory=list)


def parse(path: Path) -> PageStructure:
    page = PageStructure()
    in_code = False
    fence = ""
    block: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = FENCE.match(line)
        if in_code:
            if match and match.group(2) == fence:
                page.code_blocks.append("\n".join(block))
                in_code = False
            else:
                block.append(line)
            continue
        if match:
            in_code = True
            fence = match.group(2)
            block = [line.strip()]  # include the info string (language)
            continue
        if heading := HEADING.match(line):
            page.headings.append(len(heading.group(1)))
        if TABLE_SEPARATOR.match(line):
            page.tables += 1
        if ADMONITION.match(line):
            page.admonitions += 1
        page.images.extend(IMAGE.findall(line))
    if in_code:
        page.code_blocks.append("\n".join(block) + "\n<unterminated>")
    return page


def pages(language: str) -> set[Path]:
    base = CONTENT / language
    return {p.relative_to(base) for p in base.rglob("*.md")}


def main() -> int:
    errors: list[str] = []
    en_pages, tr_pages = pages("en"), pages("tr")
    for missing in sorted(en_pages - tr_pages):
        errors.append(f"tr/{missing.as_posix()}: missing (exists in en)")
    for missing in sorted(tr_pages - en_pages):
        errors.append(f"en/{missing.as_posix()}: missing (exists in tr)")

    for relative in sorted(en_pages & tr_pages):
        name = relative.as_posix()
        en = parse(CONTENT / "en" / relative)
        tr = parse(CONTENT / "tr" / relative)
        if en.headings != tr.headings:
            errors.append(f"{name}: heading structure differs (en {en.headings} vs tr {tr.headings})")
        if len(en.code_blocks) != len(tr.code_blocks):
            errors.append(f"{name}: {len(en.code_blocks)} code blocks in en, {len(tr.code_blocks)} in tr")
        else:
            for index, (a, b) in enumerate(zip(en.code_blocks, tr.code_blocks), start=1):
                if a != b:
                    errors.append(f"{name}: code block {index} differs between en and tr")
        if en.tables != tr.tables:
            errors.append(f"{name}: {en.tables} tables in en, {tr.tables} in tr")
        if en.admonitions != tr.admonitions:
            errors.append(f"{name}: {en.admonitions} admonitions in en, {tr.admonitions} in tr")
        if en.images != tr.images:
            errors.append(f"{name}: images differ (en {en.images} vs tr {tr.images})")

    for error in errors:
        print(f"error: {error}")
    checked = len(en_pages & tr_pages)
    print(f"checked {checked} page pair(s): {'OK' if not errors else f'{len(errors)} problem(s)'}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())

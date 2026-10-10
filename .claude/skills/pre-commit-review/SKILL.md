---
name: pre-commit-review
description: Mandatory checklist and self code review before every illusion commit — style, tests, benchmarks, docs parity, Doxygen, and a correctness review of the diff. Use right before committing.
---

# Pre-commit review

Run every step. If one fails, fix it and start again from step 1.

## 1. Automated checks

```bash
python tools/check_format.py              # --fix to apply
python tools/check_docs_parity.py
cmake --preset ci-<platform>
cmake --build --preset ci-<platform>-debug && ctest --preset ci-<platform>-debug
cmake --build --preset ci-<platform>-release && ctest --preset ci-<platform>-release
mkdocs build --strict                     # if docs changed
doxygen docs/Doxyfile                     # if public headers changed
```

On Windows build with both `ci-windows-msvc` and `ci-windows-clang` when you touched C++: clang-cl
reports warnings (conversion, shadowing) that MSVC does not, and Linux/macOS CI uses Clang/GCC.

## 2. Review the diff

Read `git diff --staged` top to bottom as a reviewer would:

- **Correctness:** off-by-one, overflow (`u32` counts), integer conversions, empty/one/many cases,
  chunk boundaries, lifetime (who constructs, who destroys, moved-from state), aliasing (references that
  a structural change invalidates), error paths.
- **Threading:** data shared between jobs, memory orders on atomics, lock ordering, lost wake-ups.
- **Performance:** allocations or locks in per-frame code, virtual calls or `std::function` in hot loops,
  needless copies.
- **API:** `[[nodiscard]]`, `noexcept`, const-correctness, Doxygen comments on every public declaration,
  naming per AGENTS.md.
- **Tests:** new behaviour and bug fixes are covered; tests are deterministic.
- **Docs:** EN and TR updated together; ADR written for hard-to-reverse decisions; ROADMAP.md updated
  when a milestone item is completed.
- **Hygiene:** no debug prints, no commented-out code, no stray files, no secrets, no build output.

If a code-review tool is available (for example the `/code-review` command in Claude Code), run it on the
staged changes as well and address every confirmed finding.

## 3. Commit

- Conventional Commit subject (≤ 72 chars), body explains *why*.
- One logical change per commit.

# Handoff: local agent (2026-10-18)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md, docs/plans/m7-editor.md.

## State
- main: M0..M6 merged (#19 M6). ROADMAP ends with M9 (FSR/DLSS/XeSS) and M10 (hybrid RT).
- Branch claude/upbeat-cray-5vm0mp: M7 plan, M7a, the bcrypt/unexpected() fix, and the fix of every M7a
  review finding (MSVC debug allocations, LogRingSink re-entry, arena lifetimes, cross-document asserts,
  depth pre-scan, missing tests, nits, docs). PR "M7a: core toolkit for tools" opened from it.
- Local Windows checks after the fixes: MSVC Debug (413 tests) and Release (415, benchmarks), clang-cl Debug
  (397), format, docs parity, Doxygen and mkdocs --strict green.

## Next (in order)
1. Merge the M7a PR when CI is green (merge commit title "Merge M7a: ... (#N)").
2. M7b (localization runtime) per the plan, then M7c, M7d (UI foundation: show gallery screenshots to owner).
3. Owner decisions defaulted to the plan's recommendations (plan header). Live-agent CI (M7x) needs the
   owner's API key + budget: ask then.

## Lessons from the M7a review
- MSVC debug builds allocate when a standard container is constructed: allocation-free code keeps heap
  blocks in std::unique_ptr<T[]> created on demand (AGENTS.md testing rules).
- Never call user callbacks under a sink lock that writers also take through the Logger's lock (ABBA).
- Objects living in an arena must not be touched by destructors that may run after arena.reset().
- yyjson reports a valid but truncated prefix as YYJSON_READ_ERROR_UNEXPECTED_END (used for error order).

## Planned dependency pins (M7d)
- imgui v1.93.0-docking b916186e (archive sha256 746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5;
  fallback v1.92.9b-docking 90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc), ImPlot 851cdbd,
  Roboto v2.138 / Roboto Mono last Apache-2.0 commit / Material Symbols Rounded (Apache-2.0); exact files and
  hashes in the plan (section 5). No OFL fonts, no ImGuizmo (own gizmos).
- GitHub archive hash from a clone: `git archive --format=tar --prefix=<repo>-<ver>/ <tag> | gzip -n -6 |
  sha256sum` (reproduces GitHub's archive hash; checked against the doctest pin).

## Cloud box notes (only for cloud sessions)
- Proxy blocks github archive URLs -> clone deps to /home/user/deps and configure with
  `$(cat /home/user/deps/args.txt)`. TSan needs `sysctl -w vm.mmap_rnd_bits=28`. venv /home/user/venv has
  clang-format 22 and mkdocs (put it first on PATH for tools/check_format.py).

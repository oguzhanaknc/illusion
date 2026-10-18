# Handoff: cloud session -> local agent (2026-10-18)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md, docs/plans/m7-editor.md.

## State
- main: M0..M6 merged (#19 M6). ROADMAP ends with M9 (FSR/DLSS/XeSS) and M10 (hybrid RT).
- Branch claude/upbeat-cray-5vm0mp = main + 3 commits, no PR yet:
  - 1ca627c docs: M7 plan (docs/plans/m7-editor.md: 24 sub-milestones M7a..M7x, ADRs 0028..0046, command/
    RPC/MCP catalogue, localization, theme tokens, Tetris acceptance test) + ROADMAP M7 list.
  - efc3713 feat(core): M7a (Expected, Json.hpp over private yyjson, Random + secureRandomBytes, LogRingSink,
    ConsoleStreams::StderrOnly, World::structuralVersion, registry contains/tryInfo, LinearAllocator
    reallocate/owns, tests/AllocationCounter.cpp (test::ScopedAllocationCounter), BenchCore.cpp, docs json.md +
    random.md EN/TR, ADR 0028, Doxyfile: engine/modules/include + GENERATE_XML).
  - c5992f0 fix(core): windows.h before bcrypt.h (comment line keeps clang-format from re-sorting);
    unexpected() decays (unexpected("msg") works) + test.
- Local (Linux) checks on efc3713: clang/gcc Debug+Release, ASan/UBSan, TSan, format, docs, doxygen all green.
- CI (workflow_dispatch, no PR): run 39113417716 on efc3713: all green except Windows MSVC + clang-cl Debug
  build (expected: bcrypt order). Re-run queued on c5992f0: check Actions. Expect Windows Debug TEST failures
  next (finding 1 below).

## Next (in order)
1. Fix M7a review findings below, re-run checks, open PR "M7a: core toolkit for tools", merge when green
   (owner rule: one PR per sub-milestone, merge when CI green; merge commit title like
   "Merge M7a: ... (#N)").
2. M7b (localization runtime) per the plan, then M7c, M7d (UI foundation: show gallery screenshots to owner).
3. Owner decisions defaulted to the plan's recommendations (plan header). Live-agent CI (M7x) needs the
   owner's API key + budget: ask then.

## M7a open review findings (4 lenses; verified ones marked V)
Major:
- MSVC Debug STL (_ITERATOR_DEBUG_LEVEL=2) allocates a container proxy when a std::vector is constructed.
  Json.cpp SmallStack (~l.142, std::vector m_heap), Writer m_frames/m_sorted (~l.814), Validator
  m_heapEntries (~l.403) -> zero-allocation tests fail on Windows Debug: TestJson.cpp ~536 and ~746,
  TestMemory.cpp ~213 (construct `growing` before the counter). Fix: unique_ptr<T[]> allocated only in
  grow(); lazy validator scratch; note it in the ScopedAllocationCounter comment.
- LogRingSink::readSince() visitor that logs or asserts deadlocks (Log.cpp ~200: sink lock held, logger
  lock re-entered). Fix: document + detect re-entry (thread-local flag) or copy-out batches.
Minor:
- V Arena-backed JsonDocument/JsonMutDocument destructors touch arena memory after arena.reset()
  (repro: segfault). Keep an arena flag outside the arena; skip yyjson_*_free when arena-backed. Doc it.
- V push/add/set/setRoot accept JsonMutValue from another document -> dangling. ILL_ASSERT same m_state.
- maxDepth checked after yyjson built the whole DOM (8 MiB of '[' -> 72 MiB RSS). Verifier refuted for
  M7a (ADR 0028), but M7j feeds untrusted RPC input: add an O(n) bracket pre-scan before yyjson.
- Arena parse can still hit the heap (depth stack > 256, m_heapEntries); add a ScopedAllocationCounter test.
- readSince(UINT64_MAX) wraps to 0 and visits everything; no batch limit (can stall logging).
- ConsoleSink accepts null streams it later fprintf's (assert unless StderrOnly + null out).
- Tests missing: file-tail cut inside a multi-byte char; ring records outlive logged strings; duplicate
  keys compared decoded ("a" vs "a"); sortKeys spill past 128 members; failed() after absent handle;
  structuralVersion "+1" exactness; large-n below() / full-range range() golden values; benchmarks should
  check parse/write succeeded; ring capacity 0/1; maxBytes boundary.
Nits:
- V Json.hpp says JsonValue is "two pointers" (it is one) and write() reports "out of memory" (it does
  not); V duplicate-key message does not escape the key; V arena sizing guidance too low.
- [[nodiscard]] on Random draws; utf8Prefix() comment vs invalid UTF-8; MemorySink comment mentions the
  editor console; overview.md Core row; api.md Log.hpp types; AGENTS.md: Expected + ScopedAllocationCounter;
  ADR 0028 alternatives for Expected; plan item "integers above 2^53 as strings" not done (record it);
  TR wording: "standart" for canonical in tr/manual/json.md, a calque in tr/manual/logging.md.

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

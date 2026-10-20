# Handoff: local agent (2026-10-18)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md, docs/plans/m7-editor.md.

## State
- main: M0..M6 and M7a merged (#20). M7b localization is PR #21 (branch feat/m7b-localization; also fixes a
  Jolt TSan race: PhysicsWorld waits for the engine jobs of each step).
- Branch feat/m7c-platform-services (stacked on M7b): Window clipboard, cursor shapes, cursor position,
  position/maximize/restore/iconify/icon/IME rect, FilesDroppedEvent (+ Window::dropFiles), content-scale,
  moved, cursor-entered and refresh events, Platform::monitors()/wake()/keyDisplayName(), display tests
  (illusion_display_tests, label display, run with gpu under Xvfb). PR opened with base feat/m7b-localization.

## Next (in order)
1. Merge #21 when green; then `gh pr edit <M7c PR> --base main` and merge M7c when green.
2. M7d (UI foundation: editor tree and export boundary, Dear ImGui docking with NVRHI and platform
   backends, theme, fonts, icons, ui gallery; show gallery screenshots to the owner). Dependency pins below.
3. Owner decisions defaulted to the plan's recommendations (plan header). Live-agent CI (M7x) needs the
   owner's API key + budget: ask then.

## Lessons from M7b
- Localization keeps raw pointers to compiled messages; replaced locale files stay alive until the next
  resolve (superseded flag). Never store pointers into a std::vector that grows.
- Paths with non-ASCII characters: tools take UTF-8 arguments (wmain on Windows) and convert with
  std::filesystem::path(std::u8string_view); tests read data files the same way.
- Local only: multi-config Ninja 1.13.2 asserted on C++20 dyndep files after reconfigures; the local
  presets set CMAKE_CXX_SCAN_FOR_MODULES=OFF (the project uses no modules).

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

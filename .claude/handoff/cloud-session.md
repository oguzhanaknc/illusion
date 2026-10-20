# Handoff: local agent (2026-10-20)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md, docs/plans/m7-editor.md.

## State
- main: M0..M6, M7a (#20), M7b localization (#21), M7c platform services (#22) merged.
- Branch feat/m7d-ui-foundation: editor tree + export boundary (cmake/IllusionEditor.cmake, boundary
  fixture tests/cmake, tools/check_layering.py, CI Runtime-only build), Dear ImGui 1.93 docking + ImPlot,
  UiPlatformBackend, UiRenderer (gamma-space UI target, present pass), Theme (3 themes, contrast tests),
  FontManager, generated Icons.hpp (cmake/IllusionIcons.cmake), FrameText, widget kit v1, UiDriver,
  illusion_ui_gallery, editor tests (editor/tests, goldens in editor/tests/data/golden), benchmark
  ui/gallery-frame, ADRs 0030/0031, editor-ui skill.

## Next (in order)
1. Merge M7d when green. Goldens were made on an NVIDIA GPU; if the lavapipe job fails the golden test,
   compare the uploaded `ui-captures` artifact and widen the tolerance or regenerate on lavapipe.
2. Owner signs off the gallery look before M7l. M7e (reflection v2) is next.
3. Owner decisions defaulted to the plan's recommendations (plan header). Live-agent CI (M7x) needs the
   owner's API key + budget: ask then.

## Lessons from M7d
- Dear ImGui reports ItemInfo before ItemAdd for tabs: UiItemRegistry keeps a pending label.
- Slang/SPIR-V push constants: a uint3 after a uint starts at offset 16; use scalars to match C++ structs.
- ImGui ini stores windows by the part of the title from "###" on (without "###").
- Headless UI tests answer texture requests with HeadlessUi instead of a GPU renderer.

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

## Cloud box notes (only for cloud sessions)
- Proxy blocks github archive URLs -> clone deps to /home/user/deps and configure with
  `$(cat /home/user/deps/args.txt)`. TSan needs `sysctl -w vm.mmap_rnd_bits=28`. venv /home/user/venv has
  clang-format 22 and mkdocs (put it first on PATH for tools/check_format.py).

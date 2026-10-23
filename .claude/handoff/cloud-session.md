# Handoff: cloud agent (2026-10-23)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md, docs/plans/m7-editor.md.

## State
- main: M0..M6, M7a..M7h, M7i (#28, headless editor core) merged.
- M7i-2 on its own branch: transform, clipboard, asset (+ ReferenceIndex), material, log, scene/project
  settings (SettingsCatalog, SettingsChange op), 72 commands; ADR 0036 addendum; editor-commands skill patterns.

## Next (in order)
1. Get the M7i-2 PR green and merged (owner reviews; never merge without the owner's explicit go).
2. M7j: JSON-RPC server, Process API, illusion_ctl, acceptance skeleton (plan section M7j, ADRs 0037, 0038).
3. Live-agent CI (M7x) needs the owner's API key + budget: ask then.

## Lessons from M7i-2
- The editor must not run updateTransformHierarchy() after commands: it writes root LocalTransforms (saved
  values) outside the operation log, and multi-step redo/journal replay then diverge. EntityIndex::
  updateTransforms() derives only children's Transforms (not saved).
- Results write u64 fields as decimal strings (`size`, `revision`, `last`): read them with asString() in tests.
- project.file_write is an undo step, so the scene counts as dirty afterwards (scene.open needs discardChanges).
- Asset references are found through reflection only: Components of types the editor does not register
  (MeshRenderer without the render module) are not searched.

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
- Proxy blocks github archive URLs (release downloads and raw files work) -> `git clone --depth 1 --branch <tag>`
  each archive dependency of cmake/Dependencies.cmake to /home/user/deps/<name> (commit-pinned ones: `git fetch
  --depth 1 origin <sha>`), write `-DFETCHCONTENT_SOURCE_DIR_<NAME>=/home/user/deps/<name>` for each into
  /home/user/deps/args.txt and configure with `$(cat /home/user/deps/args.txt)`. GLFW needs the apt packages of
  the CI workflow (libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
  libxi-dev libxext-dev). TSan needs `sysctl -w vm.mmap_rnd_bits=28`. venv /home/user/venv has
  clang-format 22 and mkdocs (put it first on PATH for tools/check_format.py).

# AGENTS.md — rules for working on illusion

This file is the contract for every contributor, human or AI agent. Read it fully before changing code.
Task-specific, step-by-step guides live in `.claude/skills/*/SKILL.md` (plain Markdown; any agent can
read them).

## Project in one paragraph

illusion is a commercial-quality, performance-critical 3D game engine in C++20 for Windows, macOS and
Linux. The product goals are in `CLAUDE.md` (the project brief), the plan is in `ROADMAP.md`, and design
decisions are in `docs/adr/`. Do not reverse an accepted ADR silently: write a new ADR that supersedes it.

## Ground rules

- **Work only inside this repository.** Do not read or copy code from elsewhere on the machine.
- **No hacks.** No commented-out code, no `TODO` without an owner in ROADMAP.md, no test-only branches in
  engine code, no disabled warnings to make a build pass, no skipped tests.
- **Every change is tested.** New behaviour gets unit tests; every bug fix gets a regression test. Hot
  paths get a benchmark with a budget.
- **Every change is documented** in English *and* Turkish in the same commit (see "Documentation").
- **Review before commit.** Re-read the full diff for correctness, style, missing tests and docs.

## Repository map

| Path | What lives there |
| --- | --- |
| `engine/core/include/illusion/{core,ecs}` | Public headers of the core library |
| `engine/core/src` | Core implementation |
| `engine/platform` | Platform layer: windows, input, events from the OS, virtual file system, child processes (`Process`; GLFW is private) |
| `engine/text` | UTF-8, `searchFold`, `FuzzyMatcher`, plural rules, `MessageFormat`, string tables, `Localization` (core only) |
| `engine/input` | Gamepads (SDL3 private), `InputActions` mapping/rebinding/glyphs, string table `data/locales/en.json`; tests use `attachVirtualGamepad()` |
| `engine/assets` | CPU-side assets: `Image`, `MeshData`, glTF via `loadGltf`, environments (SH, sky), `.material` files (cgltf/stb private); no GPU code. `AssetId`/`AssetRef` live in core |
| `engine/physics` | `PhysicsWorld`, `RigidBody`/`Collider`/`CharacterController`/`Joint` Components (Jolt is private, runs on the Job System); test helpers in `tests/physics/PhysicsTestSupport.hpp` |
| `engine/audio` | `AudioWorld`, `AudioSource`/`AudioListener` Components, filters, reverb, background clip loading via `AssetStreamer` (miniaudio is private; the audio thread owns it); tests use `AudioOutput::Offline` + `render()` |
| `engine/modules` | `ModuleHost`: loads, checks, runs and hot-reloads game modules on attached Worlds (`illusion/core/Module.hpp` is the module side; `illusion_add_module` and its `USES` API targets in `cmake/IllusionModules.cmake`; ADR 0041); tests load `tests/modules/TestModule.cpp` variants |
| `engine/runtime` | `SceneLoaderService` (`illusion::runtime`) and the `SceneService` interface (`illusion::runtime_api`); the scripting API services of the other libraries are `*Service.hpp` in each library, with adapters (ADR 0041) |
| `engine/render` | Render device (Vulkan via NVRHI), swapchains, `RenderResources`, `SceneRenderer`, saved render Components (`SceneComponents.hpp`, GPU-free) and runtime ones (`Components.hpp`), `RenderAssetResolver`, `shaders/` (Slang, built with `illusion_add_shaders`) |
| `editor/` | The editor, never linked by games (ADR 0030): `core/` (headless core: projects, `EditorContext`, commands, `WorldEditor`, undo, journals; ADR 0036), `rpc/` (JSON-RPC server, stdio/`AF_UNIX` streams, endpoint file; ADR 0037), `mcp/` (`illusion::mcp_protocol`: pure MCP server, tools generated from the catalogue; ADR 0039), `client/` (`RpcClient`, `TranscriptRunner`, `EditorToolProvider`, `McpClient`; ADR 0038/0039), `shell/` (`illusion::editor_shell`: the window's UI, `EditorShell`; UI actions, keymaps and preferences live in `core/`), `viewport/` (`illusion::editor_viewport`: `EditorCamera`, `ViewportService` implementing the core's `ViewportHost`; `Grid.slang`, `Outline.slang`), `app/` (`illusion_editor`: the window, or `--headless`), `tools/ctl` (`illusion_ctl`), `tools/mcp` (`illusion_mcp`, the MCP bridge), `thirdparty/` (Dear ImGui, ImPlot), `strings/` (string table from `data/locales/en.json`), `ui/` (backends, `Theme`, `FontManager`, `Icons.hpp`, `FrameText`, widget kit, `OffscreenUi`, `HeadlessUi`, `UiDriver`), `tools/ui_gallery`, `tools/command_reference`, `data/` (themes, locales, project templates), `tests/` (golden images in `tests/data/golden`, command schemas in `tests/data/commands`, MCP golden transcripts per protocol era in `tests/data/mcp/golden`; core tests in `tests/core`, RPC and client tests in `tests/rpc`, MCP tests in `tests/mcp`, shell tests in `tests/shell`, viewport CPU tests in `tests/viewport`, GPU tests in `tests/gpu`), `benchmarks/` |
| `samples/` | Sample applications (`hello_triangle`, `input_viewer`, `gltf_viewer`, `physics_playground`); shared helpers in `common/` |
| `tests/` | doctest unit tests; one `Test<Feature>.cpp` per feature; shared helpers in `TestSupport.hpp` |
| `benchmarks/` | nanobench benchmarks with budgets (`ILL_BENCHMARK(name, nsPerOp)`) |
| `cmake/` | `IllusionOptions`, `IllusionCompilerSettings`, `Dependencies` (pinned), `IllusionModules`, `IllusionEditor` (editor-only targets, boundary check), `IllusionIcons` |
| `docs/content/{en,tr}` | MkDocs pages (must stay in sync) |
| `docs/adr/` | Architecture Decision Records (English only) |
| `tests/acceptance` | Agent acceptance contract and transcripts (`tetris/SPEC.md`, `scripted.jsonl`, `checks.jsonl`; ADR 0038) |
| `tools/` | `editor/illusion_rpc.py` (Python client of the editor protocol), `check_format.py`, `check_docs_parity.py`, `check_layering.py`, `windows/vsdev.cmd`; `pack/` builds `illusion_pack`; `textc/` builds `illusion_textc` (string tables, locale check) |

## Build, test, check

```bash
cmake --preset ci-linux-clang                # CI settings: warnings are errors, asserts on
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug
cmake --build --preset ci-linux-clang-release && ctest --preset ci-linux-clang-release  # + benchmarks
python tools/check_format.py                 # --fix to apply; needs clang-format 22 (tools/requirements-dev.txt)
python tools/check_docs_parity.py
python tools/check_layering.py               # engine/editor boundary (ADR 0030)
mkdocs build --strict                        # pip install -r docs/requirements.txt
doxygen docs/Doxyfile                        # undocumented public API is an error
```

Windows: prefix commands with `tools\windows\vsdev.cmd` (sets up MSVC). Presets: `windows-msvc`,
`windows-clang`, `ci-windows-msvc`, `ci-windows-clang`. If the checkout is in a synced folder
(OneDrive/Dropbox), put the build tree elsewhere with a `CMakeUserPresets.json` (git-ignored).

GPU tests (`tests/gpu`, CTest label `gpu`) need a Vulkan 1.3 driver, and display tests
(`illusion_display_tests`, label `display`) a real window system; CI runs both on Mesa lavapipe under Xvfb
and excludes them elsewhere with `-LE "gpu|display"`. Render code must keep `RenderDevice::validationErrorCount()` at zero. Clip space is y-up
with depth [0, 1] (ADR 0009); the scene renderer uses reversed-Z (ADR 0013).

A change is done only when: both Debug and Release build with zero warnings, all tests pass, benchmarks
are within budget, format and docs checks pass, and Doxygen reports nothing.

## C++ rules

- C++20, no compiler extensions. Must compile cleanly on MSVC, clang-cl, GCC 13+, Clang 18+, Apple Clang.
- Naming: types `PascalCase`; functions and variables `camelCase`; members `m_camelCase`; constants and
  `constexpr` values `kPascalCase`; macros `ILL_UPPER_CASE`; namespaces lowercase (`illusion`,
  `illusion::detail` for internals). Files are named after their main type.
- Formatting is clang-format 22 with the repository `.clang-format`. Never hand-format against it.
- Every public declaration in `include/` has a `///` Doxygen comment. Internals go in `src/` or
  `namespace detail`.
- Prefer `[[nodiscard]]`, `noexcept` (where truthful), `constexpr`, `std::span`, `std::string_view`.
- Use fixed-width aliases from `illusion/core/Base.hpp` (`u32`, `f32`, `usize`, ...).
- Errors: programmer errors → `ILL_ASSERT` (with a message). Unrecoverable runtime failures (out of
  memory, corrupted state) → `ILL_CHECK`. Recoverable failures → return values: `bool`, `std::optional`,
  or `Expected<T, E>` (`illusion/core/Expected.hpp`) when the caller needs to know why; never exceptions.
  Do not throw in engine code.
- Text shown to players or editor users comes from a string table (`illusion_add_string_table`), never a
  string literal; see `.claude/skills/localization/SKILL.md`. Logs, asserts and agent-facing text stay English.
- Platform-specific code is isolated behind a small function in a `.cpp` with `#if defined(ILL_PLATFORM_*)`.
  Include `<windows.h>` only in `.cpp` files.

## Performance rules

- No heap allocation in per-frame paths: use `LinearAllocator` (per thread/frame), `PoolAllocator`,
  pre-reserved containers, or `InplaceFunction`. Allocation at load time or on first use is fine.
- Data-oriented: store hot data in contiguous arrays (ECS components), avoid pointer chasing and virtual
  calls in inner loops.
- Parallelize with the Job System (`submit`/`wait`, `parallelFor`, `Query::parallelEach`). Jobs must not
  throw, block, or capture more than 48 bytes.
- Instrument new systems with `ILL_PROFILE_SCOPE()`.
- Add a benchmark in `benchmarks/` for every hot path; set the budget to roughly 5–10x the measured
  desktop result so that only real regressions fail.

## ECS rules

- Components are plain data, nothrow-movable, with unique fully qualified names (never in anonymous
  namespaces or local scopes). Empty structs are tags.
- No structural changes while a query iterates; use `CommandBuffer`.
- Components that belong in scene files are reflected with a stable, prefixed name
  (`reflect<T>("module.Name")`) and metadata (units on angles, hints on colours, an icon); never persist C++
  type names. Runtime-only state uses `reflectRuntime<T>()`.
- Refer to entities outside the running World (files, commands, undo records) by `EntityGuid`, never by
  `Entity` handle or index; call `assignMissingGuids()` before `saveScene()` (ADR 0033).
- Saved Components refer to assets with `AssetRef` (a path), never with GPU or physics handles; a resolver
  turns them into runtime Components (`MeshRenderer` → `RenderMesh`, ADR 0035).
- Systems read the world-space `Transform`. Change parents only through `Hierarchy.hpp` (`setParent`,
  `reorder`, `destroyRecursive`); code that writes `Hierarchy` values directly calls
  `world.markChanged<Hierarchy>()`. Engine Systems skip entities with the `Inactive` tag (ADR 0034).
- Do not keep references to components across structural changes.
- See `.claude/skills/ecs-components/SKILL.md`.

## Editor rules

- Editor code lives in `editor/` and is never linked by engine targets, samples, tools or games; create its
  targets with `illusion_add_editor_library()` / `illusion_add_editor_executable()`. The configure-time
  boundary check and `tools/check_layering.py` enforce this (ADR 0030).
- UI colours come from theme tokens, text from the editor string table, icons from `Icons.hpp`; every
  interactive item has a stable `###id` built with `FrameText`. Idle UI frames must not allocate.
- Panels read editor state and change it only through commands, so agents and humans share one API.
- Commands change the World and project files only through `WorldEditor` (`CommandContext::edit()`), which
  validates before it touches anything and records reversible operations. Validate every input; a command
  must never reach `ILL_ASSERT`/`ILL_CHECK`, whatever the params (the fuzz test runs every command). After
  adding or changing a command, run `illusion_editor_command_reference --write docs/content` and update
  `editor/tests/data/commands/catalog.json` with `ILLUSION_UPDATE_GOLDENS=1`; bump the command's `version`
  for incompatible changes. Every command is also an MCP tool generated from its `agentSummary` and schemas
  (ADR 0039): write the summary for an agent, with an example. See `.claude/skills/editor-commands/SKILL.md`.
- Commands that replace or save the scene or move through the undo history set
  `CommandFlags::outsideTransactions`. Work that spans frames is an async command (`registry.addAsync()`,
  never undoable); a command runs other commands only as `editor.batch` does, through `executeStep()`
  (ADR 0036 addendum).
- See `.claude/skills/editor-ui/SKILL.md` and the [editor architecture](docs/content/en/architecture/editor.md).

## Testing rules

- doctest. Test names are sentences describing behaviour.
- Test public behaviour, edge cases (empty, one, many, chunk boundaries), lifetimes
  (`test::LifetimeCounter`), and threading (TSan runs in CI). Use `test::ScopedAssertCatcher` to check
  that asserts fire (only through functions that are not `noexcept`).
- Prove that allocation-free paths stay allocation-free with `test::ScopedAllocationCounter`. MSVC debug
  builds allocate when a standard container is constructed, even an empty one: construct containers
  before the counter, and in such paths create heap blocks only when needed (`std::unique_ptr<T[]>`).
- Tests must be deterministic: fixed seeds, no sleeps for synchronization, no reliance on timing.

## Documentation rules

- Every user-visible change updates `docs/content/en/...` **and** `docs/content/tr/...` in the same commit.
- Code blocks must be identical in both languages (CI-enforced); code comments stay English.
- Turkish keeps technical terms in English: Entity, Component, System, Archetype, Chunk, Query, Job,
  Job System, Worker, Shader, Render Pass, Pipeline, Mesh, Material, Texture, Asset, Sink, Allocator,
  Handle, Benchmark, Build, Preset, Editor commands. Write them exactly as in English.
- See `.claude/skills/bilingual-docs/SKILL.md`.

## Dependencies

Add or bump dependencies only via `cmake/Dependencies.cmake` (exact version + SHA-256) and record the
licence in `THIRD_PARTY_NOTICES.md`. Only permissive licences (MIT, BSD, Zlib, Apache-2.0, public domain).
The Steamworks SDK is never committed. See `.claude/skills/add-dependency/SKILL.md`.

## Git

- Branch from `main`; Conventional Commits (`feat(ecs): ...`, `fix(jobs): ...`, `docs: ...`,
  `build: ...`, `test: ...`, `refactor: ...`, `perf: ...`, `ci: ...`).
- One logical change per commit. Commit only after the pre-commit checklist in
  `.claude/skills/pre-commit-review/SKILL.md` passes.
- Never commit build trees, `CMakeUserPresets.json`, secrets or SDKs.

## Skills index

| Skill | Use it when |
| --- | --- |
| `build-and-test` | Building, running tests/benchmarks, sanitizers, profiling builds |
| `ecs-components` | Writing components, systems, queries, command buffers |
| `bilingual-docs` | Writing or changing documentation |
| `write-adr` | Making a decision that is hard to reverse |
| `localization` | Adding user-visible text, string tables or a language |
| `editor-ui` | Writing editor UI: widgets, panels, theme tokens, icons, stable ids, golden images |
| `editor-commands` | Adding or changing editor commands, their params and results, undo and the command reference |
| `game-modules` | Writing game modules, the scripting API service interfaces and their adapters, hot reload |
| `add-dependency` | Adding or upgrading a third-party library |
| `pre-commit-review` | Before every commit |

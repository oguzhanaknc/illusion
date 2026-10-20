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
| `engine/platform` | Platform layer: windows, input, events from the OS, virtual file system (GLFW is private) |
| `engine/text` | UTF-8, `searchFold`, `FuzzyMatcher`, plural rules, `MessageFormat`, string tables, `Localization` (core only) |
| `engine/input` | Gamepads (SDL3 private), `InputActions` mapping/rebinding/glyphs, string table `data/locales/en.json`; tests use `attachVirtualGamepad()` |
| `engine/assets` | CPU-side assets: `Image`, `MeshData`, glTF via `loadGltf`, environments (SH, sky) (cgltf/stb private); no GPU code |
| `engine/physics` | `PhysicsWorld`, `RigidBody`/`Collider`/`CharacterController`/`Joint` Components (Jolt is private, runs on the Job System); test helpers in `tests/physics/PhysicsTestSupport.hpp` |
| `engine/audio` | `AudioWorld`, `AudioSource`/`AudioListener` Components, filters, reverb, background clip loading via `AssetStreamer` (miniaudio is private; the audio thread owns it); tests use `AudioOutput::Offline` + `render()` |
| `engine/modules` | `ModuleHost`: loads, checks, runs and hot-reloads game modules (`illusion/core/Module.hpp` is the module side; `illusion_add_module` in `cmake/IllusionModules.cmake`); tests load `tests/modules/TestModule.cpp` variants |
| `engine/render` | Render device (Vulkan via NVRHI), swapchains, `RenderResources`, `SceneRenderer`, render Components, `shaders/` (Slang, built with `illusion_add_shaders`) |
| `samples/` | Sample applications (`hello_triangle`, `input_viewer`, `gltf_viewer`, `physics_playground`); shared helpers in `common/` |
| `tests/` | doctest unit tests; one `Test<Feature>.cpp` per feature; shared helpers in `TestSupport.hpp` |
| `benchmarks/` | nanobench benchmarks with budgets (`ILL_BENCHMARK(name, nsPerOp)`) |
| `cmake/` | `IllusionOptions`, `IllusionCompilerSettings`, `Dependencies` (pinned), `IllusionModules` |
| `docs/content/{en,tr}` | MkDocs pages (must stay in sync) |
| `docs/adr/` | Architecture Decision Records (English only) |
| `tools/` | `check_format.py`, `check_docs_parity.py`, `windows/vsdev.cmd`; `pack/` builds `illusion_pack`; `textc/` builds `illusion_textc` (string tables, locale check) |

## Build, test, check

```bash
cmake --preset ci-linux-clang                # CI settings: warnings are errors, asserts on
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug
cmake --build --preset ci-linux-clang-release && ctest --preset ci-linux-clang-release  # + benchmarks
python tools/check_format.py                 # --fix to apply; needs clang-format 22 (tools/requirements-dev.txt)
python tools/check_docs_parity.py
mkdocs build --strict                        # pip install -r docs/requirements.txt
doxygen docs/Doxyfile                        # undocumented public API is an error
```

Windows: prefix commands with `tools\windows\vsdev.cmd` (sets up MSVC). Presets: `windows-msvc`,
`windows-clang`, `ci-windows-msvc`, `ci-windows-clang`. If the checkout is in a synced folder
(OneDrive/Dropbox), put the build tree elsewhere with a `CMakeUserPresets.json` (git-ignored).

GPU tests (`tests/gpu`, CTest label `gpu`) need a Vulkan 1.3 driver; CI runs them on Mesa lavapipe
under Xvfb. Render code must keep `RenderDevice::validationErrorCount()` at zero. Clip space is y-up
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
  (`reflect<T>("module.Name")`); never persist C++ type names.
- Do not keep references to components across structural changes.
- See `.claude/skills/ecs-components/SKILL.md`.

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
| `add-dependency` | Adding or upgrading a third-party library |
| `pre-commit-review` | Before every commit |

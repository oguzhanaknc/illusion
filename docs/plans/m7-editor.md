# M7 plan: editor and AI control

Status: Accepted. Date: 2026-10-18. Repository baseline: `03051ed` (M6 merged).

The open decisions in §13 are taken with their recommendations until the product owner says otherwise:
English and en-XA ship in M7 (Turkish follows as a data file), Azure accent, Roboto Mono, the variable
Material Symbols font, in-process play with journal recovery, no multi-viewport windows, no editor extensions
from game modules and no remote (HTTP) agents in M7, and "ask" as the GUI permission mode. The live-agent CI
job (M7x) needs the owner's API key and budget before it is enabled.

This plan delivers ROADMAP M7. It is built from three candidate plans (UX-first, AI-first, risk-first) and every disagreement between them was checked against the repository. The AI-first plan is the base. The product owner's requirements are:

- The editor is professional and state of the art, covers every engine feature and looks polished.
- UI text is English, and adding a language takes only a data file.
- Every editor operation is reachable over JSON-RPC and MCP.
- An agent builds a Tetris-like game with no human input.
- A test scene covers every Component and the whole scripting API.
- Module Components whose layout changed survive a hot reload (the M6 follow-up).

---

## 1. Plan scores

| Criterion (1–10) | Plan 1: UX-first | Plan 2: AI-first | Plan 3: risk-first |
| --- | --- | --- | --- |
| Editor quality and professionalism | 9 | 8 | 7 |
| AI controllability | 8 | 10 | 8 |
| Deliverability and testability on the CI matrix | 6 | 8 | 9 |
| Rule compliance (licences, no exceptions, no per-frame allocations, docs) | 8 | 9 | 9 |
| Coherence | 7 | 9 | 8 |
| **Total (of 50)** | **38** | **44** | **41** |

**Plan 1, UX-first (38).**

Strengths:
- It has the richest UX specification: theme tokens, widget kit, interaction patterns, inspector and outliner details, prefab overrides with nesting and variants, Map Check, view cube and picture-in-picture camera preview.
- Its localization design is strong: stable keys, compiled English, source hashes and a pseudo-locale.
- It has a complete command catalogue.

Weaknesses:
- It builds the shell (M7b) before reflection v2, entity identity and the command core exist.
- Several PRs are far bigger than one reviewable diff (M7b, M7i, M7o).
- Play mode clones the World through a new `ComponentInfo::copy`. That changes the module ABI and requires every Component to be copyable.
- Layout migration takes a fresh ComponentId on every migrating reload. That uses up the 256-type budget and leaves `componentId<T>()` caches in dependent modules stale.
- GUID sidecars per asset add a second identity system and churn in version control.
- The Tetris acceptance test arrives in the last PR, so the AI path is unguarded for 15 PRs.
- It lists a lavapipe GPU benchmark that CI does not run.

**Plan 2, AI-first (44): the base.**

Strengths:
- There is one typed command core. Params and results are reflected structs, so schemas, decoding, validation and docs cannot drift apart.
- It supports dry-run, idempotency tokens, `expectRevision`, scoped undo with footprint conflicts, `$ref` batches, and both push and pull events.
- Everything is headless on every OS. An acceptance skeleton exists from the RPC PR on, and one independent checker judges both replayed and live runs.
- Layout migration happens in place and keeps ComponentIds.
- Module `start`/`stop` stops `load()` from spawning entities into the edited scene.

Weaknesses:
- The UI arrives in PR 12 of 16.
- It renames the saved mesh Component to `MeshInstance`, which contradicts the ROADMAP wording ("MeshRenderer and SkyLight are saved").
- Its in-memory `AssetId` path table must be shared with module copies, which the plan does not address.
- It assumes RFC 6901 support from yyjson, but yyjson is built with `YYJSON_DISABLE_UTILS=1`.
- 128-bit entity GUIDs double the cost and give no practical gain.

**Plan 3, risk-first (41).**

Strengths:
- It is a walking skeleton: formats, then the headless core and the AI path, all before the GUI.
- It defines CI test tiers and a runtime-only CI job.
- `GameSession` lives in the engine, so play-in-editor runs the same code the M8 player will ship.
- It is the only plan that noticed logs go to stdout and would corrupt stdio protocols.
- It has an early, isolated UI-stack PR with screenshots for review.

Weaknesses:
- Hierarchy (M7l) and orthographic cameras (M7m) land after the scene format and after the Tetris transcript is recorded, so both have to churn.
- Prefabs are v1 only.
- MCP runs inside the editor, so it cannot attach to a running GUI editor and the tool list is static.
- Migration uses fresh ids, with the same budget problem as Plan 1.
- In-tree module builds reconfigure the engine build tree from inside a running test.

**Gaps in all three plans, found in the repository and fixed in this plan:**
- A shared module runs its own copy of every non-core engine library it links. Jolt's allocator and factory are installed only in the copy that constructs a `PhysicsWorld`. Objects created by module-copy code carry vtables into the module, which dangle after a reload. Calling `PhysicsWorld` from a module therefore breaks "the whole scripting API". **M7q** adds host service interfaces to fix this.
- Doxygen does not check `engine/modules/include` today.
- yyjson's JSON Pointer utilities are compiled out.

## 2. Synthesis

### 2.1 What comes from where

- **From Plan 2 (base):**
  - The headless command core with typed, reflection-described commands.
  - Transactions, undo and the journal.
  - JSON-RPC as the native protocol, with a separate dual-era MCP bridge.
  - The acceptance skeleton from the RPC PR on, with an independent checker.
  - In-place layout migration.
  - Module `start`/`stop` and multi-world hosting.
  - Scene snapshots for play mode.
  - A build-tree package for project modules.
  - Renderer extension points.
- **From Plan 1:**
  - The visual identity and theme tokens, the inspector, outliner and gizmo feature sets, interaction patterns, and the full prefab model.
  - `MeshRenderer` stays the name of the saved Component (the ROADMAP wording).
  - Localization as its own engine library.
  - `label###id` rules, permission and approval UX, and whole-UI screenshots.
- **From Plan 3:**
  - `GameSession` in `engine/runtime`.
  - A stderr-only console sink.
  - The runtime-only CI job.
  - An early, isolated UI-stack PR whose golden screenshots go to the product owner.
  - Test tiers, and test-engine hooks for headless UI tests.
  - A gamma-space UI target with a present pass.
  - An About/Licences page.
  - One `AF_UNIX` transport on all three OSes.
- **New in this plan:**
  - Host service interfaces for modules.
  - The asset path table installed into modules.
  - Staged type registration, so migrated types never expose old-layout storage to new code.
  - One C++ MessageFormat parser that also runs the CI locale check, instead of a second Python grammar.
  - GPU budgets kept local; CI enforces only CPU budgets.

### 2.2 Conflicts and decisions

| Topic | P1 | P2 | P3 | Decision | Why |
| --- | --- | --- | --- | --- | --- |
| Delivery order | Shell second | UI last | Formats, then AI, then GUI | A small core toolkit first. Then two early tracks: UI foundation (PR 4, reviewed via golden screenshots) and data formats (PRs 5–8). Then the headless core and the AI path (9–11), and the shell (12). | The owner sees real rendering early, the formats that are hard to reverse land early, and the AI path is guarded from PR 10. |
| Entity id | u64 | 128-bit | u64 | `EntityGuid {u64}`, written as 16 hex characters, never 0 | Compact. Collision chance is about 3e-8 at 1M entities. Duplicates are detected on load and regenerated on paste. |
| Hierarchy | `Parent{entity, order}` | `Hierarchy{parent, order}` | `Parent` plus intrusive links | `Hierarchy{parent, order}` + `LocalTransform` + a derived `HierarchyIndex` | Root order is saved too. No intrusive invariants. Child lists cost nothing until the hierarchy changes. |
| Asset identity | GUID sidecars | Path, `AssetId` in memory | Path string in memory | Paths as in ADR 0019. `AssetRef{AssetId}` (8 bytes) in memory, a process-wide `AssetPathTable` installed into modules, and a path string in JSON. The reader also accepts `{path, …}` objects, reserved for a future uid. | Readable for agents and diffs. Components stay trivially copyable. GUIDs can be added later without migrating any scene. |
| Saved mesh Component | `MeshRenderer` / `RenderMesh` | `MeshInstance` → `MeshRenderer` | `MeshAsset` → `MeshRenderer` | `MeshRenderer` is saved (in a GPU-free header). `RenderMesh` is the runtime form. | Matches the ROADMAP. Modules see no NVRHI headers. The cull loop stays lean. |
| Play mode | `World::cloneInto` | Scene v2 snapshot | JSON snapshot | In-memory scene v2 snapshot, ids kept | What you play is what you save. No ABI change. Runtime Components are regenerated by their systems. |
| Layout migration | Fresh id | In place | Fresh id | In place, keeping the id: staged registration, archetype purge, restore by field name | No 256-id leak. Cached ids in dependent modules stay valid. |
| Module lifecycle | `load`/`update` | `start`/`stop` | Events | ModuleApi v3: `load` registers only (`world()` is not available there); `start`/`update`/`stop` run per simulation World | Fixes the duplicate-spawn and old-layout hazards. |
| Module service calls | — | — | — | Abstract host service interfaces in header-only API targets. Shared modules link only core. | The repository facts on Jolt globals and vtables (§1). |
| Project module builds | Package | Package | In-tree include | Build-tree package that exports core and the API targets only | No change to the engine build tree. Path towards the M8 SDK. |
| MCP placement | Bridge | Bridge | In the editor | `illusion_mcp` bridge with `--launch`, `--attach` and `auto` | Attaches to the user's open editor, survives editor crashes, keeps MCP spec churn out of the editor. |
| Local IPC | UDS + named pipe | UDS + pipe + TCP | stdio, then `AF_UNIX` | stdio and `AF_UNIX` on all OSes (Windows 10 1803+). No TCP or HTTP in M7. | One code path, tested on every runner. No browser exposure. |
| Localization home | `engine/text` | core | `engine/localization` | `engine/text` → `illusion::text` | Core stays lean, and games link it only when they localize. |
| Locale CI check | Python | Python | Python | `illusion_textc check` run as a CTest in every build job | Exactly one MessageFormat parser. |
| Colour pipeline | Prefer UNORM swapchain | UNORM + fallback | UI target + present pass | Gamma-space `RGBA8_UNORM` UI target plus a present pass | Identical captures windowed and headless, on sRGB or UNORM swapchains. |
| Undo storage | Binary snapshots | Value blobs | JSON | Operation log whose values are reflected JSON (the scene v2 codec) | One codec for undo, clipboard, play snapshots and journal. A binary codec can come later behind the same interface. |
| GPU-only commands without a GPU | Unsupported | Not listed | — | Always listed. They return error 1400 `unsupported`. | Stable tool lists and transcripts. |
| Prefabs | Full | Nesting, variants later | v1 | Full overrides with nesting; variants fall out of nesting; delivered late (M7v) | The owner asked for "all features". Tetris does not depend on prefabs. |
| Turkish UI locale | en + en-XA | en + en-XA | en + en-XA | Ship English and en-XA. A `tr` fixture proves the pipeline. Turkish is a data-only follow-up. | "Arayüz İngilizce olsun". |

### 2.3 Repository facts behind the decisions

1. The next free ADR number is **0028**. `tools/check_format.py` already formats `editor/`, `runtime/` and `modules/` (`SOURCE_DIRS`).
2. `docs/Doxyfile` INPUT omits `engine/modules/include`, so `ModuleHost.hpp` is not checked today.
3. yyjson is built with `YYJSON_DISABLE_UTILS=1` and `YYJSON_DISABLE_NON_STANDARD=1` (`cmake/Dependencies.cmake`). JSON Pointer has to be implemented in-house, and locale and theme files are strict JSON, so translator comments must be fields.
4. `ConsoleSink` writes Info and lower levels to **stdout** (`engine/core/src/core/Log.cpp:41`), and the Logger installs it by default.
5. The scene loader accepts only version 1 (`SceneSerializer.cpp:365`). Unknown types are dropped (ADR 0022).
6. `render()` emplaces `MotionHistory` into the World (`SceneRenderer.cpp:672`). ADR 0017 notes that a second view of the same World gets zero motion. `GpuInstance::extra[1..3]` are unused (`SceneRenderer.cpp:81`).
7. The swapchain prefers sRGB formats (`Swapchain.cpp:30`). The tonemapper encodes sRGB itself for UNORM targets (`PostProcessor.cpp:79`; `samples/common/Screenshot.hpp`).
8. `computeShadowCascades()` reads perspective terms from the projection matrix (`Shadows.cpp:13`).
9. `render/Components.hpp` includes `RenderResources.hpp` and through it `nvrhi/nvrhi.h`.
10. `acquireJolt()` (`PhysicsWorld.cpp:73`) installs Jolt's allocator and factory only in the binary copy that constructs a `PhysicsWorld`. Meanwhile `manual/modules.md` shows `illusion_add_module(tetris … LINK illusion::physics)`.
11. Archetypes hold `const ComponentInfo*` into the registry, bake column sizes at construction (`Archetype.cpp:29`) and keep per-archetype edge maps. A layout can only be replaced after every archetype containing the type is purged.
12. The test module creates Components in `load()` (`tests/modules/TestModule.cpp:39`), although the manual says not to.
13. GPU benchmarks are Release-only with the `gpu` label. CI build jobs run `-LE "gpu|display"` and the gpu job runs Debug `-L "gpu|display"`, so **GPU budgets are local only**. Build jobs run every non-gpu test in both Debug and Release.
14. ADR 0019 already foresees "a GUID scheme … when the editor needs rename tracking". ADR 0035 records why paths are kept.
15. The cloud box cannot download GitHub archive URLs (`.claude/handoff/cloud-session.md`), but `raw.githubusercontent.com` files work.
16. MSVC builds use the dynamic CRT, so host and modules share one heap. That is required for `std::string`/`std::vector` crossing interfaces.
17. `World` uses `ILL_CHECK` on dead entities and duplicate Components (`World.cpp:227–229, 290`), `InputActions::addAction` asserts on duplicates, and asserts end the process. A validating command layer is therefore mandatory.

## 3. Architecture

### 3.1 Targets and layering

```text
Engine (may ship in games)                              Editor (never linked by games or exports)
illusion::core ── text ── platform ── input              illusion::imgui, illusion::implot (third party)
      │             assets ── physics ── audio           illusion::editor_strings  (generated string table)
      │                       render ──────┐             illusion::editor_core     (no GPU, ImGui or sockets)
      └── modules ─────────────────────────┴── runtime   illusion::editor_play     (+ runtime, modules)
Header-only API targets for modules:                     illusion::editor_viewport (+ render, gizmo math)
  physics_api, audio_api, input_api,                     illusion::editor_rpc      (JSON-RPC, transports)
  render_api (GPU-free Components), runtime_api          illusion::editor_client   (client, launcher, replay)
                                                         illusion::mcp_protocol    (pure MCP library)
                                                         illusion::editor_ui       (backends, theme, panels)
                                                         illusion_editor, illusion_mcp, illusion_ctl,
                                                         illusion_ui_gallery
```

Directory layout:

- `engine/text/`
- `engine/runtime/`
- `editor/{core,play,viewport,rpc,client,mcp,ui,app,tools,data}/`
- `tools/textc/`: string-table generator and locale checker (host tool)
- `cmake/{IllusionEditor,IllusionText,IllusionIcons,IllusionPackage}.cmake`
- `tests/{editor,gpu/editor,mcp,acceptance,cmake}/`
- `benchmarks/editor/`
- `samples/showcase/`

Editor executables go to `<build>/bin/<Config>/`, with `editor-data/` (fonts, themes, locales, templates, licences) beside them. That folder is mounted read-only as `editor:`.

Project mounts:

| Mount | Points at | Access |
| --- | --- | --- |
| `assets:` | `<project>/assets` | Read-write. All content: scenes, materials, models, prefabs, input, audio. |
| `settings:` | `<project>/settings` | Read-write |
| `modules:` | `<project>/modules` | Read-write. Module sources; editor only. |
| `cache:` | `<project>/.illusion/cache` | Read-write |
| `builtin:` | Virtual built-in assets | Read-only |
| `editor:` | `editor-data/` | Read-only |
| `sdk:` | Public headers of the build-tree package | Read-only; editor only |

`.illusion/` holds the cache, journal, autosave, run, build, thumbnails, trash and per-user view state. It is git-ignored.

### 3.2 Keeping editor code out of exported games

Six mechanisms enforce this:

1. `ILLUSION_BUILD_EDITOR` defaults to `PROJECT_IS_TOP_LEVEL`.
2. `illusion_add_editor_library()` and `illusion_add_editor_executable()` set the target property `ILLUSION_EDITOR_ONLY`. `illusion_check_editor_boundary()` runs at the end of configure (`cmake_language(DEFER)`). It walks `LINK_LIBRARIES` and `INTERFACE_LINK_LIBRARIES` from every target that is not editor-only (engine, samples, `illusion_pack`, every `illusion_add_module` target, runtime) and fails the configure on any path to an editor-only target.
3. `tools/check_layering.py` runs in the Format job. It rejects `illusion/editor/` and `imgui` includes outside `editor/`.
4. A new CI job, **Runtime-only build**, configures with the editor off.
5. Engine code never branches on an editor define. Editor support in the engine consists of opt-in runtime features (entity ids, DebugDraw, ReadbackRing) that games may use too.
6. M8 adds a symbol scan of the exported player.

Panels receive only `const` views of editor state and submit writes as commands. The type system enforces this, not a lint.

### 3.3 Threading and the frame loop

**Threads:**
- The **main thread** owns every World, the command registry, the undo stack, the ModuleHost, the RenderDevice and ImGui.
- An **RPC I/O thread** frames NDJSON, parses into per-connection arenas and authenticates. It looks methods up in an immutable catalog snapshot, pushes requests into a bounded MPSC queue (256 entries) and wakes the loop. In windowed mode it calls `Platform::wake()`. In headless mode the loop blocks on the queue's condition variable, because the null backend does not block in `waitEvents`.
- **Job System workers** run culling, physics, hierarchy propagation, imports and thumbnail decoding. The asset I/O thread and the audio thread are unchanged.
- **Process reader threads** feed bounded buffers. The **FileWatcher** delivers events at an explicit `poll()`.

**Frame order:**
1. Pump events.
2. Command point: RPC and UI-deferred commands, with an 8 ms budget in the GUI and no limit headless.
3. Streamer and upload queue, resolvers, file-watcher reimports, `reloadChanged()` when auto-reload is on.
4. Simulation tick through `GameSession`, or edit-world hierarchy propagation and resolvers.
5. UI frame: panels read state and push commands, then the deferred commands execute.
6. Render visible viewports (only dirty ones), then at most one thumbnail.
7. UI render and present, or offscreen capture.
8. Flush coalesced events, the crash journal, and the autosave timer.

Commands never run while a Query iterates or inside `SceneRenderer::render()`.

### 3.4 Modes, process model and safety

**Modes:**
- Windowed (the default).
- `--headless`: no OS window. The GPU is used if available, and the UI is rendered offscreen at `--ui-size` (1920x1080 by default) for `editor.screenshot`.
- `--headless --no-gpu`: no RenderDevice. GPU commands return error 1400.
- `--deterministic --seed N`: seeded GUIDs and Random, fixed 1/60 s steps, a fixed worker count, offline audio and no wall-clock values in results.

**Process model.** Game modules run in-process in M7. Mitigations:
- Every command validates against current state before touching a World. Fuzz tests prove that no input reaches `ILL_ASSERT` or `ILL_CHECK`.
- The editor's assert handler flushes logs and writes `.illusion/crash.json` (last command, origin, journal position) before aborting.
- An append-only journal of committed transactions is flushed every frame, autosaves rotate, and recovery is offered at the next start.
- `illusion_mcp` survives an editor crash, reports it with the last 50 log lines, and relaunches the editor with recovery.

Out-of-process play is an open question for M8 or later.

### 3.5 Performance rules applied to the editor

- After warm-up there are **zero heap allocations** per idle frame and per steady drag frame. Tests count them through ImGui's allocator hooks and a global counter in the test executables.
- Per-frame text goes into a `FrameText` LinearAllocator. Lists are virtualised with `ImGuiListClipper`.
- Derived views (outliner rows, inspector layouts, hierarchy index) are cached by `structuralVersion`, hierarchy version and selection version.
- Viewports render on demand: when dirty, while the camera moves, or until TAA converges. Secondary viewports use cheaper shadows. The editor throttles itself when unfocused.
- RPC uses reused per-connection arenas.
- Every hot path has a CPU benchmark with a budget. GPU benchmarks are local only, as today.

### 3.6 Engine feature coverage in the editor

| Engine feature | Editor support |
| --- | --- |
| ECS, reflection, scenes | Outliner, inspector (metadata driven, multi-edit), ECS debugger, scene documents v2, prefabs, Map Check |
| Hierarchy, Disabled | Drag to reparent, enable toggles, local/world transforms, gizmo pivots |
| Rendering | `MeshRenderer` with materials; lights, `SkyLight`/`ProceduralSky`, cameras (perspective and orthographic); render settings for CSM+PCSS, GTAO, bloom, TAA, exposure, occlusion culling and bindless/MDI info; debug views; GPU pass timers; material editor |
| Assets, streaming, packs | Asset browser with thumbnails, import settings, reimport, reference fix-up, cook and pack, streaming statistics |
| Physics | All Components, collider/joint/character visualisers, layer names and collision matrix, debug draw, queries, play and simulate |
| Audio | Sources and listener with distance/cone gizmos, mixer with meters, filters and reverb, clip previews with waveform |
| Input and gamepads | Input Actions editor (rebinding capture, conflicts, glyphs), gamepad tester (rumble, trigger rumble, LED, adaptive triggers, gyro, touchpad), play input routing and injection |
| Modules | Create, build, diagnostics, hot reload, migration reports, Open in IDE |
| Jobs, profiling, logging | Profiler (CPU scopes, GPU passes, budgets), console, Tracy hint |
| Localization, platform | Language preference, en-XA, DPI and UI scale, clipboard, OS drag and drop |
| AI control | MCP and RPC with full parity, AI Activity panel, permission modes, follow-agent |

## 4. Sub-milestones

Every sub-milestone is one PR that must be green on the whole CI matrix. Recommended order (each PR lists its own `Needs`):

```text
a ─┬─ b ─┬─ d ─────────────────┐
   ├─ c ─┘                     │
   └─ e ─ f ─ g ─ h ─ i ─ j ─ k ─ l ─ m ─ o ─ p ─ u ─ v ─ w ─ x
                    ├─ n ──────────────────┘   │
                    └─ q ─ r ─ s ─ t ──────────┘
```

Tracks can run in parallel: UI (b, c, d) next to data (e–h) after a; renderer (n) after h; modules (q) after h. Each PR's benchmark budgets are initial values. The PR sets the final budget to 5–10x the measured desktop result, as AGENTS.md requires.

### M7a: Core toolkit for tools (engine)
Needs: nothing.

**Scope**

- `Json.hpp`, a non-throwing DOM over the private yyjson:
  - `JsonDocument::parse(text, {maxDepth = 128, maxBytes = 64 MiB})` returns a value or `JsonError{offset, line, column, message}`.
  - `JsonValue` views offer typed getters, iteration in file order, `find`, and an in-house RFC 6901 `pointer()`.
  - `JsonMutDocument`/`JsonWriter` allocate from a caller `LinearAllocator` (`yyjson_alc`).
  - Canonical mode: two-space indent, scalar arrays on one line, shortest round-trip floats, optional sorted keys.
  - Integers keep their exact 64-bit value. Writing u64 values as decimal strings belongs to the value encoding and moved to M7e.
  - Duplicate keys are rejected.
- `Random.hpp`: `Random` is xoshiro256** seeded through splitmix64, with unbiased `below(n)` and `uniform01()`. It produces the same sequence on every platform. `secureRandomBytes(span)` uses BCryptGenRandom, getrandom or arc4random_buf behind `ILL_PLATFORM_*`.
- `LogRingSink(capacity)`: fixed records `{seq, level, category, text (bounded), file, line, thread, time}`, `readSince(seq, fn)` without copying, a dropped count, and no allocation after construction. `ConsoleSink` gains a stream policy (`Split`, `StderrOnly`) and injectable streams for tests.
- `World::structuralVersion()`, plus `ComponentRegistry::contains()` and `tryInfo()`, which do not assert.
- Doxyfile: add `engine/modules/include` to INPUT and STRIP_*, and set `GENERATE_XML = YES`.

**Files.**
- `engine/core/include/illusion/core/{Json,Random}.hpp` and `src/core/{Json,Random}.cpp`
- `Log.hpp`/`.cpp`, `ecs/World.*`, `ecs/Component.*`
- `docs/Doxyfile`
- `tests/core/{TestJson,TestRandom,TestLog}.cpp`, `tests/ecs/{TestWorld,TestComponent}.cpp`
- `benchmarks/BenchCore.cpp`

**Tests (unit).**
- JSON:
  - A corpus of valid and invalid documents: truncation, invalid UTF-8, the depth limit, NaN and Inf, duplicate keys, and exact u64 above 2^53.
  - Error line and column, and pointer escapes (`~0`, `~1`).
  - Canonical golden output, and an arena writer that allocates nothing after warm-up.
- Random and secure bytes:
  - Golden first 16 Random values for seed 42, identical on all CI platforms.
  - A chi-square test at a fixed seed, and distinct secure random bytes.
- Ring sink: 8 writers under TSan, monotonic sequence numbers, and drops counted on wrap.
- `StderrOnly` writes nothing to stdout.
- `structuralVersion` changes only on create, destroy, add, remove and clear.

**Benchmarks.** `json/parse` 10 ns/byte, `json/write` 10 ns/byte, `random/next` 3 ns, `log/ring-write` 300 ns per message.

**Docs (EN/TR).** `manual/json.md` (new), `manual/logging.md`, `manual/ecs.md`, mkdocs nav.

**ADR.** 0028: a non-throwing public JSON API over private yyjson.

### M7b: Localization runtime and string tables (`illusion::text`)
Needs: a.

**Scope**

- `engine/text` provides `illusion::text`, which depends on core:
  - `Utf8`: validate, decode, encode, iterate.
  - `SearchFold`: locale-independent and used only for search. It folds case and diacritics; I, ı, İ and i all fold to i, ş to s, ğ to g. Tables cover Latin-1, Latin Extended-A, Greek and Cyrillic.
  - `FuzzyMatcher`: subsequence matching with bonuses for word starts.
  - `LocaleId` (BCP 47), `NumberFormat`.
  - `PluralRules`: CLDR cardinal rules for en, tr, de, fr, es, it, pt, ru, uk, pl, cs, ja, zh, ko and ar.
  - `MessageFormat` subset, compiled into an op list: `{name}`, `{n, number}`, `{n, plural, =0 {…} one {# …} other {…}}`, `{x, select, …}`, and apostrophe quoting. Errors are returned as values.
  - `StringTable`, `Localization` (fallback chain, a switch applied at a frame boundary, each missing key logged once), and the `en-XA` pseudo-locale.
- `tools/textc` builds the `illusion_textc` host tool:
  - `generate` turns `en.json` into a header of `constexpr TextKey` constants plus a `.cpp` with the embedded English.
  - `check` compares a locale directory with `en.json`.
  - One parser serves the runtime, generation and CI.
- `cmake/IllusionText.cmake`: `illusion_add_string_table(<target> TABLE <name> SOURCE <en.json> NAMESPACE <ns> HEADER <path>)`.
- First engine table: `engine/input/data/locales/en.json` with `input.glyph.*` and `input.key.*`. `glyphLabelKey()` sits beside the unchanged English `glyphLabel()`.
- CTest `text.locales` (label `unit`) runs the checker over every table and `tests/data/locales/` in every build job.
- New skill `.claude/skills/localization/SKILL.md`. AGENTS.md repository map updated.

**Tests (unit).**
- CLDR sample numbers for every plural rule.
- MessageFormat: nesting, `#`, `=N`, escaping; malformed patterns return errors and never assert.
- Number formatting: en `1,234.5`, tr `1.234,5`.
- Fallback per key `tr-TR` → `tr` → `en`; the switch takes effect at the frame boundary; a missing key is logged once.
- Fold equivalences and a fuzzy ranking table.
- The pseudo-locale keeps placeholders and adds about 35%.
- Generator golden output.
- Checker fixtures: missing key, extra key, placeholder mismatch, missing plural category, stale hash, unsorted keys, non-strict JSON.
- Formatting into a LinearAllocator allocates nothing.

**Benchmarks.** `text/lookup` 5 ns, `text/format-plural` 400 ns per message, `text/fold` 10 ns per character, `text/fuzzy-score` 500 ns per candidate.

**Docs.** `manual/localization.md` (new), `contributing.md` (user-visible strings).

**ADR.** 0029: localization with stable keys, compiled English, data-file locales and a MessageFormat subset.

### M7c: Platform services for tools and the editor UI
Needs: a.

**Scope**

- `Window` gains:
  - Clipboard get/set (UTF-8).
  - `setCursorShape` (Arrow, TextInput, ResizeAll, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE, Hand, NotAllowed, Crosshair), using cached GLFW cursors.
  - `setCursorPosition`; `position()`/`setPosition()`, which return false where unsupported (Wayland).
  - `maximize`/`restore`/`iconify`/`isMaximized`; `WindowDesc::maximized`; `setIcon`; `setImeRect` (Win32 `ImmSetCompositionWindow`, no-op elsewhere).
- New queued events:
  - `FilesDroppedEvent{window, first, count, position}`, with `Window::droppedPath(i)` valid until the next `pollEvents`, so events stay trivially copyable.
  - `ContentScaleChangedEvent`, `WindowMovedEvent`, `CursorEnteredEvent`, `WindowRefreshEvent`.
- `Platform` gains `monitors()` (name, position, size, work area, content scale), a thread-safe `wake()` (`glfwPostEmptyEvent`), and a layout-aware `keyDisplayName(Key)` that falls back to US names.

**Files.** `engine/platform/include/illusion/platform/{Window,Platform,Events,Input}.hpp` and `src/*`; `tests/platform/TestWindow.cpp`; `tests/platform/TestWindowDisplay.cpp` (new label `display`).

**Tests.**
- Unit, on the null backend: clipboard round trip; cursor and maximize state; drop events delivered in order with their paths, through the same internal entry point the GLFW callback uses; ordering of content-scale events.
- Display (X11 under Xvfb, run by the gpu job): system clipboard, cursor shapes, and `wake()` from another thread ending `waitEvents(30 s)` promptly. The timeout is only a failure ceiling.

**Benchmarks.** None (not a hot path).

**Docs.** `manual/platform.md` (clipboard, cursors, windows, monitors, drop, DPI, wake, key names, Wayland limits).

**ADR.** None. These are API additions within ADRs 0008 and 0009.

### M7d: UI foundation (editor tree, Dear ImGui, backends, theme, fonts, icons, gallery)
Needs: b, c.

**Scope**

- **Editor tree and boundary:**
  - `ILLUSION_BUILD_EDITOR`, `editor/CMakeLists.txt`, `cmake/IllusionEditor.cmake` (editor-only targets and the deferred boundary check), `tools/check_layering.py`.
  - The CI job **Runtime-only build** (Linux GCC, editor off, Debug, `ctest -LE gpu`).
  - Output to `<build>/bin/<Config>/` with `editor-data/`.
- **Dependencies** (§5): imgui v1.93.0-docking, ImPlot 851cdbd, Roboto, Roboto Mono, Material Symbols Rounded and their licences. All are listed in THIRD_PARTY_NOTICES with "Shipped: No".
- **`illusion::imgui`** (static, SYSTEM). Its `IMGUI_USER_CONFIG` is `editor/ui/include/illusion/editor/ui/ImGuiConfig.hpp`:
  - `IMGUI_USE_WCHAR32`, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, `IM_ASSERT` mapped to `ILL_ASSERT`.
  - `IMGUI_ENABLE_TEST_ENGINE` hooks implemented by our `UiItemRegistry`.
  - No stock backends.
  - Allocation through `SetAllocatorFunctions` into a counting allocator.
- **`illusion::implot`.**
- **`illusion::editor_ui` foundation:**
  - `UiPlatformBackend`: EventBus and InputState feed ImGui IO; it also handles cursors, clipboard via `platform_io`, the IME rect and DPI events.
  - `UiRenderer` on NVRHI: `RendererHasTextures` (dynamic atlas), `RendererHasVtxOffset`, grow-only vertex and index buffers per frame in flight, push constants, a scissor per draw command, a binding-set cache, and a 64-bit tagged `ImTextureID`. Shaders are `editor/ui/shaders/{Ui,Present}.slang`.
  - **Colour pipeline:** the UI is drawn in gamma space into an `RGBA8_UNORM` UI target. A present pass decodes sRGB for `_SRGB` swapchains and passes through for UNORM swapchains, or the target is read back for captures. Viewport images are `RGBA8_UNORM`, encoded by the tonemapper.
  - Theme engine (§8). `FontManager` uses dynamic fonts, with `FontScaleDpi` = content scale × UI scale. Code is drawn against the 1.93 `ImDrawList` rules (`AddLineH`/`AddLineV`).
  - `Icons.hpp` is generated from the pinned `.codepoints` file (`cmake/IllusionIcons.cmake`), so an unknown icon name is a compile error.
  - `editor/data/locales/en.json` is compiled into `illusion::editor_strings`. Helpers `ui::label(key, "###id")` and `FrameText`.
  - Widget kit v1 (§8) and the `UiDriver` test helper.
- **`illusion_ui_gallery`**: a widget catalogue, a typography and icon sheet, and a composite editor-style page with sample data. Flags: `--headless --screenshot --theme --scale --locale`.
- Skill `.claude/skills/editor-ui/SKILL.md`.

**Tests**

- Headless UI, all OSes:
  - The IO mapping of injected events; InputText copy reaches the platform clipboard.
  - `UiDriver` finds and clicks items by `###id`.
  - en-XA leaves ids and the docking ini unchanged.
  - Every theme defines every token. Contrast: text ≥ 4.5:1, graphics ≥ 3:1, high contrast ≥ 7:1.
  - Every icon name resolves, and every character of en and en-XA strings has a glyph.
  - Zero allocations per idle gallery frame.
- Unit: a boundary fixture (`tests/cmake/editor_boundary`) whose configure must fail (`WILL_FAIL`).
- GPU:
  - Gallery goldens for dark, light and high contrast, each at 100% and 150%, plus en-XA.
  - Atlas texture create, update and destroy across frames.
  - Identical captures from the sRGB and UNORM present paths.
  - Validation errors 0.
- CI uploads the PNGs for the product owner's design review.

**Benchmarks.** `ui/gallery-frame` (CPU, `NewFrame` through `Render`): 2,000,000 ns per frame.

**Docs.** `architecture/editor.md` (new: tree, boundary, UI stack, colour pipeline, tokens), `contributing.md` (UI rules).

**ADRs.**
- 0030: a separate editor tree, a command-first editor and the export boundary.
- 0031: the editor UI stack (Dear ImGui docking, custom backends, gamma-space UI target, theme tokens, fonts and icons).

The product owner signs off the look before M7l.

### M7e: Reflection v2
Needs: a.

**Scope**

- **`FieldMeta`**, added through `field(name, member, FieldMeta{...})`:
  - Labels and grouping: `displayName`, `description`, `category`.
  - Units: `unit` (None, Meters, Radians, Seconds, Kilograms, Hertz, Newtons, MetersPerSecond, Percent, Decibels) and `displayUnit`.
  - Ranges: `min`/`max`/`step` and soft ranges.
  - `hint`: Color, HdrColor, Angle, Slider, LogSlider, Multiline, Layer, LayerMask.
  - Flags and links: `readOnly`, `hidden`, `advanced`, `renamedFrom`, `assetKind`.
- **`TypeMeta`**, added through `meta(TypeMeta{...})`: display name, category path, Material Symbols icon, description, docs anchor, required Components, singleton, and **Transient** (never saved, copied or edited).
- All metadata strings are owned by the registry.
- **New field types:** I8, I16, U8, U16, Struct, FixedArray (`std::array<T, N>`), Array (`std::vector<T>`), Flags, Optional and Json (for command params), and Asset (declared here, wired in M7h).
- **Struct reflection.** `StructRegistry` with `reflectStruct<T>(name, version)`. Reflected settings: `ShadowSettings`, `PostProcessSettings` (nested), `AudioReverbSettings`, `AudioBusFilter`, `ImageImportSettings`, `EnvironmentDesc`, `SkyDesc`, `MaterialData`.
- **Value and JSON API:**
  - `FieldValue`; `readField`, `writeField`, `fieldsEqual`, `defaultValue`.
  - `ComponentJson`: `write`/`read` in Replace or Merge mode; struct read/write.
  - `getPath`/`setPath` with dotted paths; per-field errors with JSON pointers.
  - Unit-tagged input `{"deg": 60}` and colour input `"#rrggbb"` (sRGB).
  - U64 fields are written as decimal strings, so that readers that store numbers as doubles keep them exact (moved here from M7a).
  - An `EntityResolver` hook.
  - `SceneSerializer` is rebuilt on top of it with byte-identical v1 output.
- **Migrations.** `migrate(fromVersion, fn(JsonMutValue&))` runs when a file is older; `renamedFrom` is applied first.
- **Engine metadata.** Every engine Component is annotated (for example: angles in radians shown in degrees, light colours as HDR colours, `RigidBody::layer` < 16, RigidBody requires Collider, AudioListener is a singleton). `reflectRuntime<T>()` gives Transient stable names to `MotionHistory`, `PhysicsBody`, `CharacterBody`, `PhysicsJoint` and `AudioPlayback`.

**Files.** `engine/core/include/illusion/ecs/{Reflection,ComponentJson,StructRegistry}.hpp` and `src`; metadata in `engine/{render,physics,audio}/src/*Components.cpp`; `tests/ecs/{TestReflection,TestComponentJson}.cpp`; the `ecs-components` skill (hints are required; Transient).

**Tests (unit).**
- Round trips for every field type, including 1-, 2-, 4- and 8-byte enums (signed and unsigned) and nested arrays of structs.
- A registry sweep: every angle field has a unit, every colour field has a hint, and Transient types are never saved.
- Metadata registered by a module survives its unload.
- `writeComponent` output equals `saveScene` output byte for byte; the existing M5a goldens are unchanged.
- Merge mode leaves other fields untouched; type errors report pointers such as `/position/1`.
- A v1→v2 migrate hook (rename plus unit change); a file version newer than the code warns and keeps the data.
- Struct defaults equal a default-constructed value; malformed JSON never asserts.

**Benchmarks.** `reflection/field-read-write` 20 ns, `reflection/component-json` 1,500 ns per Component; `ecs/scene` unchanged.

**Docs.** `manual/reflection.md` (new), `manual/scenes.md`.

**ADR.** 0032: reflection v2 (metadata, struct reflection, JSON value API, migrations). It extends ADR 0022's field types.

### M7f: Entity identity, names and scene format v2
Needs: e.

**Scope**

- **Identity Components.** `EntityGuid{u64}` (`illusion.EntityGuid`, read-only) is written as the entity's `"id"`. `GuidGenerator` is secure by default and seeded in deterministic mode. `Name{std::string}` (`illusion.Name`) rejects `/`, control characters and more than 256 bytes. `assignMissingGuids(World&)`.
- **Type-erased iteration.** `World::forEachMatching(all, none, fn)` iterates chunks without C++ types.
- **Scene format v2:**
  - Entity fields are written as guid strings or null.
  - A top-level `"settings": {structStableName: {...}}` block.
  - **Unknown Component types are preserved verbatim** in a runtime `UnknownComponents` holder that no engine query ever matches. Unknown fields of known types are dropped with a warning, after `renamedFrom` is applied.
  - `SceneLoadOptions{IdPolicy Keep|Regenerate, remap out, settings sink}` and `SceneSaveOptions{subset, order, settings}`.
  - Duplicate ids in a file are a load error.
  - The v1 reader is kept: ids are generated and index references remapped.
  - The default save order is by guid. M7g switches it to hierarchy pre-order.

**Tests (unit).**
- v1 goldens load and re-save as v2 deterministically, and a second save is byte-identical.
- Subset saves write references outside the subset as null; `Regenerate` remaps internal references.
- Unknown types survive byte-for-byte; duplicate ids are rejected.
- The seeded guid sequence is deterministic, and 1M secure guids have no collision.
- `UnknownComponents` never matches queries; settings round-trip.

**Benchmarks.** `scene/save-v2` and `scene/load-v2` 5,000 ns per entity (the same budget as `ecs/scene`), `guid/generate` 50 ns, `ecs/for-each-matching` 3 ns per entity.

**Docs.** `manual/scenes.md` (an annotated v2 file, ids, names, settings, passthrough, v1 compatibility), `manual/ecs.md`.

**ADR.** 0033: persistent entity ids, names and scene format v2. It supersedes the parts of ADR 0022 on dropping unknown types and on index references.

### M7g: Transform hierarchy, Disabled and spatial math
Needs: f.

**Scope**

- **Components.** `Hierarchy{Entity parent; u32 order}` (roots have a null parent; order is dense among siblings), `LocalTransform`, a `Disabled` tag (saved) and an `Inactive` tag (Transient: Disabled on itself or on an ancestor).
- **Hierarchy API** (`Hierarchy.hpp`):
  - `setParent(child, parent, KeepWorld | KeepLocal, index)` rejects cycles with an error value.
  - Also `reorder`, `detach`, `forEachChild`, `depthFirst`, `destroyRecursive`, `setEnabled`.
- **`HierarchyIndex`**: CSR child lists plus depth order, rebuilt lazily when the structural or hierarchy version changes.
- **Propagation.** `updateTransformHierarchy(World&, JobSystem*)` runs in parallel per depth level. `Transform` stays the world-space truth for every system. Dynamic bodies are world-space authorities: their `LocalTransform` is written back. Inactive is maintained through a CommandBuffer at the update point.
- **Engine systems honour Inactive.** `SceneRenderer` culls with `without<Inactive>`. `PhysicsWorld` removes the body and re-adds it on enable. `AudioWorld` stops the source.
- **Saving.** Scenes save in hierarchy pre-order (siblings by `order`, roots by order then guid). A child's `Transform` is derived and not saved.
- **glTF.** `spawnModel()` keeps the node hierarchy and node names.
- **`Math.hpp` additions:** `Ray`, intersections (AABB, sphere, plane, triangle, OBB), ray–segment closest points, `screenToRay` (reversed-Z, infinite far plane), `orthographicReversedZ()`, and Euler conversion with a continuity hint.

**Tests.**
- Unit:
  - Propagation at depths 1–8 with non-uniform scale (the shear limit is documented).
  - Keep-world reparenting within epsilon; cycles rejected; reorder; `destroyRecursive`.
  - Parallel propagation equals serial on 100k entities (TSan).
  - Disabled subtrees, and physics bodies removed and restored.
  - Silence in an offline audio render.
  - Save order stable after deletes and slot reuse.
  - `spawnModel` names and parents (`tests/data/gltf`).
  - Math edge cases: parallel rays, origin inside a box, grazing triangle edges, orthographic depth range.
- GPU: a disabled entity is not drawn (`RenderStats`), validation errors 0.

**Benchmarks.** `hierarchy/propagate` (100k entities, depth 8) 30 ns per entity; `hierarchy/flat` (no Hierarchy) 0.5 ns per entity; `math/ray-aabb` 5 ns.

**Docs.** `manual/ecs.md` (hierarchy, Disabled, Inactive), `manual/math.md` (new), `manual/scenes.md`, plus Inactive notes in `rendering.md`, `physics.md` and `audio.md`.

**ADR.** 0034: transform hierarchy with world-space `Transform` as the truth, and Disabled/Inactive.

### M7h: Asset references in scenes (ADR 0022 follow-up)
Needs: g.

**Scope**

- **`AssetId` moves to core** (`illusion/core/AssetId.hpp`; the assets library keeps a forwarding include).
- **`AssetRef{AssetId id}`** with `FieldType::Asset` and `FieldMeta::assetKind`.
- **Process-wide `AssetPathTable`** (thread-safe interning between path and id). `ModuleServices` gains `assetPaths`, which raises `kModuleApiVersion` to 2.
- **JSON form.** An AssetRef is written as a path string. The reader also accepts `{"path": …}`.
- **Sub-assets and built-ins.** Sub-asset fragments `#mesh/N`, `#material/N`, `#node/name`. The reserved mount `builtin:` provides `mesh/{cube,sphere,plane,capsule,cylinder}` (with new `makeCapsule`/`makeCylinder`), `material/default` and `sky/procedural`.
- **Authoring/runtime split:**
  - The new GPU-free header `illusion/render/SceneComponents.hpp` holds the reflected authoring Components:
    - `MeshRenderer{AssetRef mesh; u32 submesh; AssetRef material; u32 layers; bool castShadows; bool visible}`
    - Camera and lights
    - `SkyLight{AssetRef environment; f32 intensity; bool background}`
    - `ProceduralSky{SkyDesc fields}`
  - The runtime `RenderMesh{MeshHandle, submesh, MaterialHandle, layers, flags, source key}` and `RenderSky{EnvironmentHandle}` (both Transient) live in `Components.hpp`. Procedural code writes `RenderMesh` directly.
- **Material asset** `.material`: JSON `illusion.material` v1 holding `MaterialData` by field name plus texture AssetRefs, decoded through `AssetStreamer::loadFile`.
- **`RenderAssetResolver`** (one per World): compares `MeshRenderer`/`SkyLight` with the resolved key every frame in parallel and re-resolves only mismatches. It works through `AssetStreamer` and `UploadQueue`. A shared `RenderAssetCache` keeps reference counts across Worlds. It also provides `waitIdle()` and failure statistics.
- **Physics and audio.** `Collider` gains a reflected `AssetRef mesh` plus `meshIndex`, cooked by `PhysicsAssetResolver` (cached by path). `AudioSource::clip` becomes an `AssetRef`; its JSON is unchanged.
- **Migrations and tools.** Samples and tests move to asset `MeshRenderer`s, or to `RenderMesh` for procedural geometry. `illusion_pack` and `cookAssets` include `.material` files.

**Tests.**
- GPU: a scene with a glTF model, a material override, a built-in cube and an HDRI `SkyLight` renders identically after save, clear, load and resolve (golden image against handles built in code). Validation errors 0.
- Unit:
  - Material round trip; AssetRef normalisation and fragments; built-ins resolve without files.
  - A missing asset still loads, keeps its reference on re-save and is reported in `SceneLoadResult::missingAssets`.
  - The resolver deduplicates (100 instances, one upload), picks up material changes on the next update and allocates nothing in steady state.
  - Mesh and hull colliders from assets collide after a reload.
  - v1 AudioSource scenes load.
  - An AssetRef created by a shared test module resolves in the host.

**Benchmarks.** `render/asset-resolve-steady` (100k resolved) 3 ns per entity; `assets/path-intern` 150 ns.

**Docs.** `manual/assets.md` (references, built-ins, sub-assets, materials), `rendering.md` (`MeshRenderer` vs `RenderMesh`), `physics.md`, `audio.md`, `scenes.md`, `modules.md`.

**ADR.** 0035: asset references by virtual path, closing ADR 0022's consequence. It keeps ADR 0019's path ids and records why GUID sidecars were not chosen and when to revisit.

### M7i: Editor core (headless): projects, documents, typed commands, transactions, undo, journal
Needs: b, d, h.

**Scope**

- **Projects:**
  - `project.illusion` (JSON `illusion.project`: name, version, engine version, startup scene, modules).
  - The mounts from §3.1; templates `editor/data/templates/{empty,3d-starter}`.
- **VFS file management:** `remove`, `rename`/`move`, `copy`, `createDirectory`, `stat` (size, mtime) and `listRecursive`, with `FileError::AlreadyExists` and `NotEmpty`.
- **`illusion::editor_core`:**
  - `EditorContext` (edit World, `AssetPathTable`, `Localization`, `Selection`).
  - `EntityIndex` (guid ⇄ Entity, path and name lookup) and `SceneDocument` (path, revision, dirty flag).
- **Command registry:**
  - `CommandRegistry` holds `CommandDescriptor`s: name, version, `titleKey`, `descriptionKey`, English agent summary, category, icon, params and result as reflected structs, flags, permission class, palette visibility and a default shortcut (§6).
  - `execute<T>()` serves the UI and `execute(JsonValue)` serves RPC.
  - JSON Schema 2020-12 is generated from the reflected structs.
  - Validation errors carry JSON pointers and fuzzy suggestions. Entity references follow the grammar in §6.
- **`WorldEditor`** applies validated, reversible primitive operations:
  - create and destroy (a whole subtree, with repair of incoming Entity references)
  - add, remove and set Components
  - reparent and reorder
  - settings
  - file write, move and delete, through a blob store in `.illusion/trash`
- **`UndoStack`:**
  - Merge keys; `dryRun`; labels; origins.
  - A 256 MiB budget.
  - Selection before and after each step is restored; `history.jump`.
- **Journals and recovery.** `ChangeJournal` (per-frame coalesced changes with revisions) and `EventHub`. A crash journal (`.illusion/journal/<session>.jsonl`, flushed every frame), autosave and `project.recover`.
- **Command modules:** `editor`, `project`, `scene`, `entity`, `component`, `hierarchy`, `transform`, `selection`, `history`, `clipboard`, basic `asset` (list, info, create_folder, move with reference fix-up through a `ReferenceIndex` that scans scenes and materials via reflection, delete with a reference check, references, dependencies), `material` and `log`.
- **Generated docs.** `reference/editor-commands.md` (identical EN/TR tables) is regenerated by the CTest `editor.command_reference`, which fails when the docs are stale.
- Skill `.claude/skills/editor-commands/SKILL.md`.
- If the diff gets too large to review, split into i-1 (core and undo) and i-2 (command modules).

**Tests (unit, `illusion_editor_tests`).**
- Every command: the success path, every error kind, `dryRun` has no effect, and the result matches its output schema.
- A seeded run of 2,000 random commands: undoing everything gives the initial scene JSON byte for byte, and redoing everything gives the final one.
- 100 merged `component.set` calls form one undo step that keeps the first old value.
- Delete then undo restores the same guid and the incoming `Joint::connected` reference.
- Budget trimming notifies; journal recovery reproduces the scene.
- `asset.move` rewrites references in open and closed scenes; `asset.delete` refuses while the asset is referenced unless `force` is set.
- Schema goldens for every command.
- Schema-driven fuzz: invalid params (wrong types, missing fields, out of range, dead or unknown entities, unknown Components, duplicates) always produce structured errors and the process never aborts (also run in the ASan job).
- VFS file operations, including read-only mounts and paths that try to escape their mount.

**Benchmarks.** `editor/component-set` 10,000 ns; `editor/undo-capture-delete` 10,000 ns per entity; `editor/scene-query` 50 ns per entity.

**Docs.** `manual/editor/index.md` (new: projects, documents, commands, transactions, undo, references), `manual/editor/projects.md`, `reference/editor-commands.md`, `architecture/editor.md`, `manual/filesystem.md`.

**ADR.** 0036: a typed editor command core with transactions, operation-log undo, a crash journal and the project layout.

### M7j: JSON-RPC server, Process API, `illusion_ctl` and the acceptance skeleton
Needs: i.

**Scope**

- **Process API** in `engine/platform` (`Process.hpp`): spawn without a shell (arguments, working directory, added environment variables), stdio pipes drained into bounded buffers by reader threads, `poll`, `wait(timeout)`, `terminate` and exit codes. It uses `posix_spawn` and `CreateProcessW` with exact quoting.
- **`illusion::editor_rpc`:**
  - NDJSON framing and a JSON-RPC 2.0 dispatcher.
  - Transports: stdio (`--rpc-stdio`) and `AF_UNIX` (`--rpc-listen`). The socket lives in `<project>/.illusion/run/` (0700, or an owner-only ACL), or under a hashed name in `XDG_RUNTIME_DIR`/`TMPDIR` when the path would exceed the 104-byte `sun_path` limit.
  - Endpoint file, a 256-bit token, permission classes and modes, and limits (§6).
  - The command pump; async commands with `rpc.progress` and `rpc.cancel`.
  - Push and pull events, `editor.batch` with `$ref`, per-connection transactions, `clientToken`, `expectRevision`, undo with `scope: "mine"`, and `rpc.discover` (OpenRPC 1.3).
- **`illusion_editor`**, headless for now: `--project --headless --no-gpu --rpc-stdio --rpc-listen --deterministic --seed --permissions --frames`.
- **`illusion::editor_client`:** RPC client, endpoint discovery, a child launcher, and a transcript replayer and checker (§9).
- **`illusion_ctl`:** `call`, `batch`, `subscribe`, `replay`, `check`, `dump-catalog`.
- **`tools/editor/illusion_rpc.py`:** a client using only the Python standard library.
- **Acceptance skeleton.** `tests/acceptance/tetris/{SPEC.md, scripted.jsonl, checks.jsonl}`, starting with the static board scenario (checks C1, C3 and C4). CTest `acceptance.tetris.board` (label `acceptance`, Release only) runs on every build job.

**Tests (unit and integration).**
- Framing: split reads, several messages per read, an oversized message (-32600 and the connection closes), invalid UTF-8.
- Conformance:
  - Error codes -32700, -32600, -32601 and -32602 (with `data.errors`).
  - Notifications never get a reply; string and numeric ids both work.
  - JSON-RPC batch arrays are rejected with -32600 pointing to `editor.batch`.
- Authentication: required, wrong token, and the token is never logged.
- Endpoint file permissions: POSIX mode check and Windows ACL query.
- TSan: 8 clients × 1,000 requests keep per-connection order, and shutdown is race-free.
- Idempotency, `expectRevision` conflicts, scoped-undo conflicts, cancellation, event-queue overflow.
- Parser fuzzing: a fixed seed, 100k mutations, under ASan.
- Process: a helper executable checks arguments with spaces, quotes and Unicode, large output, exit codes, timeouts, `terminate` and the environment.
- Self-tests of the replayer and checker.

**Benchmarks.** `rpc/roundtrip` 20,000 ns per request; `rpc/framing` 5 ns/byte.

**Docs.** `manual/editor/automation.md` (RPC, transports, security, `illusion_ctl`, Python client), `reference/editor-protocol.md`.

**ADRs.**
- 0037: the editor RPC protocol (JSON-RPC 2.0 over NDJSON on stdio and `AF_UNIX`, token authentication, permission modes).
- 0038: agent acceptance tests from replayable transcripts.

### M7k: MCP bridge (`illusion_mcp`)
Needs: j.

**Scope**

- **`illusion::mcp_protocol`**, a pure library over core Json:
  - Era negotiation and tool generation from the command catalog.
  - Results (`structuredContent`, text summaries, images, `isError`), progress and cancellation mapping, `list_changed`, resources, prompts and instructions (§6).
- **`illusion_mcp`:**
  - stdio NDJSON. Logs go to stderr only (`ConsoleSink::StderrOnly`), and the process exits on stdin EOF.
  - Modes `--attach`, `--launch [--headless] [--no-gpu] [--deterministic --seed N]` and `auto` (the default: attach if a live endpoint exists, otherwise launch headless).
  - `--tools`, `--record <file.jsonl>`.
  - Crash detection with relaunch and recovery.
- **Editor side:** a client registry (name, version, era) and an activity ring.
- `project.create` writes `.mcp.json`.

**Tests.**
- Golden transcripts per era (`tests/mcp/golden/{2025-06-18,2025-11-25,2026-07-28}/`) against a fake `ToolProvider`:
  - handshake and discovery
  - order and contents of `tools/list`
  - call success and failure, unknown tool
  - cancellation and progress
  - version mismatch (-32022)
  - resources
- The same transcripts against a real headless editor.
- Tool names are unique and use only `[a-z0-9_]`; truncation and pagination; image size caps.
- stdout carries only protocol lines at every log level.
- Killing the child editor yields a structured error, and the next call relaunches it with recovery.
- `acceptance.tetris.board` replayed through MCP in both eras.

**Benchmarks.** `mcp/translate` 20,000 ns per message.

**Docs.** `manual/editor/ai-agents.md` (Claude Code setup, permission modes, limits, troubleshooting `MCP_PROTOCOL_NEGOTIATION`), `tutorials/first-agent-session.md`.

**ADR.** 0039: a dual-era MCP bridge with tools generated one-to-one from editor commands.

### M7l: Editor shell (windowed)
Needs: d, k.

**Scope**

- **Windowed main loop.** `Platform::wake()` from the RPC thread; the RenderDevice, swapchain and UI present pass.
- **Layouts.** `DockBuilder` defaults (Default, Level Design, Debug, Profiling, Agent). Named layouts are stored per user, a project default can be shipped, and Reset Layout restores defaults. ImGui ini handling uses `SaveIniSettingsToMemory` with `io.IniFilename = nullptr`.
- **Chrome:**
  - The menu bar (File, Edit, View, Entity, Assets, Play, Modules, Tools, Window, Help) is built from `UiAction` entries `{command, params}`; items are disabled until their milestone.
  - Main toolbar, status bar, toasts and a notification centre.
- **Navigation panels:**
  - **Command palette** (Ctrl+Shift+P or Ctrl+K): commands, `@` entities, `#` assets, `>` settings. Fuzzy matching runs on both the localized and the English titles, and argument forms are generated from schemas.
  - **Go to Anything** (Ctrl+P).
  - **Console**: on top of `LogRingSink`, with filters, search, collapsing, timestamps, source links and a command line.
  - **History**: origin badges, jump to a step, "Undo agent's changes".
- **AI Activity panel:** connected clients, eras and modes; a live call feed with arguments, duration, result and an undo link; approvals (Allow once, Allow for session, Deny); pausing or stopping agents; follow-agent.
- **Preferences:**
  - Appearance and Language (en, en-XA, plus any locale files found).
  - Keyboard: chords, presets (illusion, Unity, Unreal, Blender), conflict detection and rebinding capture.
  - AI Agents, Autosave, External Tools.
- **Project and help windows:** Welcome and project browser (recent projects, templates, Open through the in-editor VFS folder browser); About and the third-party licences viewer.
- **Input and commands.** ImGui gamepad navigation through `GamepadSystem`. The `ui.*` commands, and `editor.screenshot` (also headless). A UiAction parity test.

**Tests.**
- Headless UI:
  - The palette opens with its shortcut and runs a command; shortcut conflicts are detected; presets switch.
  - Layout save and load round-trips; en-XA keeps ids.
  - Every `UiAction` resolves to a registered command.
  - The ask-mode approval flow with a fake client.
  - Zero allocations per idle shell frame after 120 warm-up frames.
  - Project create, open and recent-list persistence; a corrupt `project.illusion` reports an error and does not abort.
- GPU: shell goldens at 1280x720 in dark, light and en-XA; `editor.screenshot` over RPC; validation errors 0.

**Benchmarks.** `editor/shell-idle-frame` (CPU) 2,000,000 ns per frame.

**Docs.** `manual/editor/interface.md`, `manual/editor/preferences.md`, `getting-started.md` (building and launching the editor; on Windows through `tools\windows\vsdev.cmd`), `ai-agents.md` (the panel).

**ADR.** None (covered by 0030, 0031, 0036 and 0037).

This is the product owner's second design review (shell screenshots).

### M7m: Outliner and Inspector
Needs: l.

**Scope**

- **Outliner:**
  - Display: flattened rows cached from `HierarchyIndex`; `ImGuiListClipper`; disabled entities in dimmed italics.
  - Selection: `BeginMultiSelect` (Ctrl, Shift, box); keyboard navigation.
  - Editing: drag to reparent or reorder with an insertion line (KeepWorld by default, Alt for KeepLocal); F2 inline rename; context menus; dragging onto Entity fields.
  - Filters: name, `t:Type`, `has:Component`, `disabled:`, `prefab:`.
  - Eye and lock toggles are per-user view state.
- **Inspector:**
  - **Header:** enabled toggle, name, a guid copy button.
  - **Transform section:** local/world toggle and Euler degrees with a per-entity hint cache, so angles never flip.
  - **Component cards:** icon, display name, and a kebab menu (reset, copy, paste values, remove, docs).
  - **Field widgets from `FieldMeta`:**
    - unit suffixes; drag-scrub with Shift ×10 and Alt ×0.1 and infinite drag through `CursorMode::Captured`
    - sliders and log sliders; an HDR colour picker with separate intensity
    - enum combos and flag chips
    - an entity picker with a viewport eyedropper; asset fields (path and picker; thumbnails come in M7u)
    - arrays and nested structs
    - Transient Components shown read-only during play
  - **Editing behaviour:**
    - Multi-edit shows mixed values as "—"; per-field reset.
    - Components can be copied and pasted.
    - The Add Component popup offers search, categories and descriptions, and adds required Components in the same transaction.
    - Inline validation offers fix-it buttons.
    - A custom-drawer registry covers Transform, Collider, Joint and AudioSource.
    - The inspector can be locked, and several inspectors can be open.
  - **Undo:** every edit is a command with a merge key, so one drag is one undo step.

**Tests.**
- Headless UI:
  - F2 rename; drag-reparent with undo; Ctrl and Shift selection.
  - Add Component: typing "rigid" and Enter adds RigidBody and Collider in one step.
  - A scrub is one transaction; multi-edit shows mixed values and is one undo step.
  - Typing a yaw of 180° keeps showing 180°; outliner filters.
- GPU: goldens of outliner and inspector in dark, light and en-XA.

**Benchmarks.** `editor/outliner-frame-100k` 3,000,000 ns per frame; `editor/outliner-rebuild` 200 ns per entity; `editor/inspector-transform` 200,000 ns per frame.

**Docs.** `manual/editor/scene-editing.md`.

### M7n: Renderer extension points
Needs: h (runs in parallel with i–m).

**Scope**

- **Motion history.** Kept per renderer: dense by entity index with a generation check. `render()` no longer changes the World's structure.
- **Entity ids.** `SceneRendererDesc::entityIds` adds an `R32G32_UINT` (index, generation) attachment written by the prepass from `GpuInstance::extra[1..2]`. The prepass entry points of both `ForwardPbr` and `ForwardPbrBindless` get `SV_Target1`, and an id-only pass covers blended items.
- **`viewTargets()`:** depth (SRV-capable), ids, hdr, motion, render size, output size, view-projection and unjittered view-projection.
- **Overlay contract.** Overlays are recorded after `render()`, sample depth as a texture and take render and output sizes explicitly, so they survive M9's resolution decoupling.
- **`ReadbackRing`:** staging resources plus event queries; never `waitForIdle`. A blocking variant serves headless commands.
- **`DebugDraw`:** pre-reserved lists, depth-tested and on-top modes, thick lines by quad expansion, wire helpers, and `DebugDraw.slang` with depth-sampled occlusion.
- **Camera v2:** `projection` (Perspective or Orthographic), `fovY`, `orthoHeight`, `near`, `far`, `exposure`, `priority`. `RenderView::fromCamera` handles orthographic projection, `findMainCamera()` picks the highest priority, and `computeShadowCascades()` is fitted from the inverse view-projection corners.
- **Per-entity filtering.** `RenderMesh` flags and layers are honoured in `cull()`, along with `RenderView::layerMask` and a hidden-entities bitset.
- **Diagnostics:**
  - `setClearColor`.
  - Debug views: Unlit, Albedo, Normals, Roughness, Metallic, AO, ShadowCascades, MotionVectors, and a wireframe overlay where `fillModeNonSolid` is available.
  - GPU pass timers (reported unavailable when timestamps are unsupported) and `RenderStats::lightsDropped`.

**Tests.**
- GPU (validation errors 0):
  - A Scene and a Game view of one World in the same frame give the same motion vectors as a single view (regression for ADR 0017).
  - Id readback is correct for instanced, masked and blended objects, both bindless and non-bindless.
  - `ReadbackRing` delivers without `waitForIdle` (counted).
  - Goldens: DebugDraw, orthographic with shadows, normals view.
  - `castShadows`, layer and hidden filters work; timers are non-zero where supported.
- CPU: cascades contain the frustum for both projections.

**Benchmarks.** `render/debugdraw-record` (100k lines, CPU) 20 ns per line. The existing GPU frame budgets are checked locally.

**Docs.** `manual/rendering.md` (orthographic cameras, priority, flags and layers, entity ids and readback, view targets and overlays, debug draw, debug views, timers).

**ADR.** 0040: renderer extension points for editors and tools. It supersedes ADR 0017's `MotionHistory` decision.

### M7o: Viewports
Needs: m, n.

**Scope**

- **`illusion::editor_viewport`:** `ViewportRenderer` uses one `SceneRenderer` per viewport, a resize that waits for the size to settle (150 ms or mouse release), and reduced shadows for secondary viewports.
- **`EditorCamera`:**
  - Movement: orbit, pan, dolly, fly (RMB + WASDQE, wheel sets the speed).
  - Framing: F focuses with an animated transition (reduced motion respected); frame all; bookmarks Ctrl+1..9.
  - Views: orthographic top, front and side; a view cube and axis navigator; navigation presets.
- **Game view:** the Camera with the highest priority, aspect presets and letterboxing.
- **Overlays:**
  - Adaptive infinite grid (`Grid.slang`, depth sampled).
  - Selection outline from the id buffer plus a selection bitset (`Outline.slang`, after tonemapping); hover highlight.
  - Icon billboards drawn with `ImDrawList`.
  - DebugDraw visualisers: light ranges and directions, camera frusta with a picture-in-picture preview, collider shapes, character capsules, joint anchors, axes and limits, audio distances and cones.
  - Stats overlay; render-mode and overlay menus.
- **Picking.** `PickService` reads ids asynchronously within a 5x5 tolerance, and repeated clicks cycle through overlapping objects. Box select uses a region readback.
- **Screenshots and commands.** `ScreenshotService` produces PNG. Commands: `viewport.*`, `camera.*`, `render.*`.

**Tests.**
- GPU:
  - `viewport.project` followed by `viewport.pick` returns the same guid; repeated clicks cycle.
  - The outline appears only around the selection; two screenshots of the same state are identical.
  - Overlay goldens.
  - `acceptance.tetris.board` gains C6 (game-view golden at 640x360, pick and sample checks) in the gpu job.
- CPU: navigation math (orbit keeps the pivot distance, focus frames the bounds), the resize policy, and `--no-gpu` returning 1400.

**Benchmarks.** `viewport/visualisers-record` 500 ns per object.

**Docs.** `manual/editor/viewport.md`.

### M7p: Gizmos, selection tools and transform utilities
Needs: o.

**Scope**

- **`GizmoMath`**, a pure library:
  - Handles: translate (axes, planes, screen), rotate (axis rings, view ring, trackball), scale (axes, uniform) and universal.
  - Interaction geometry: screen-space hit tests with tolerance scaled by DPI, drag planes that stay robust at grazing angles, constant screen size.
  - Spaces and pivots: world, local and view space; median, active, individual origins and bounds centre.
  - Snapping: grid, angle and scale increments (Ctrl toggles); vertex snap with V (CPU mesh data plus a per-mesh spatial grid); surface snap with align-to-normal (depth and normal readback); snap to ground with End.
  - Typed numeric input during a drag, and Blender-style G/R/S with X/Y/Z constraints.
- **`ImDrawList` renderer:** 1.93 anti-aliasing, dimmed back halves of rings, axis letters, and a colour-blind palette.
- **Transactions:** begin, update, commit; Esc or RMB cancels; multi-object and hierarchy-aware.
- **Utilities:** reset, align to view, align and distribute, drop to ground (render bounds; physics raycasts from M7w), measure and ruler.
- `gizmo.*` commands.

**Tests.**
- Unit: hit tests for every handle at many poses, perspective and orthographic, reversed-Z, DPI 1x and 2x, and 10 km from the origin; exact snapping; multi-selection pivots; one undo step per drag; cancel restores exactly.
- Headless UI: drag sequences produce the expected transforms.
- GPU: gizmo golden; surface snap lands on a plane.

**Benchmarks.** `gizmo/hit-test` 2,000 ns.

**Docs.** `manual/editor/transform-tools.md`.

### M7q: Module API v3, multi-world host and the scripting API
Needs: h (runs in parallel with i–p).

**Scope**

- **Lifecycle.** `kModuleApiVersion = 3`:
  - `ModuleApi` gains `start` and `stop`, called once per simulation World.
  - `load` and `unload` register types and handlers only. `ModuleContext::world()` asserts there with "use start()".
  - `update` runs once per simulation World.
- **Multi-world host.** `ModuleHost(JobSystem&, desc)` with `attachWorld(World&, WorldRole{Edit, Simulation, Preview}, ServiceSet)` and `detachWorld`. Stripping and orphaning run over every attached World. Services are resolved per World first, then from the host.
- **Service interfaces** (abstract classes implemented by host adapters; virtual dispatch runs host code, so host code creates the objects):
  - `PhysicsService` (`physics_api`): bodies, characters, joints, queries, contacts, meshes.
  - `AudioService` (`audio_api`): play, stop, one-shots, clips, mixer.
  - `InputService` (`input_api`): actions by name or id, state, devices, rumble, LED, triggers.
  - `SceneService` (`runtime_api`): instantiate scene and prefab assets (prefabs from M7v), find by guid, path or name.
- **Linking.**
  - `render_api` exposes the GPU-free `SceneComponents.hpp`.
  - `illusion_add_module(... USES physics audio input render runtime)` adds the API targets.
  - `LINK` of engine runtime libraries is rejected for `SHARED` modules at configure time.
  - Shared modules link only `illusion::core`.
- `manual/modules.md` examples are fixed. A new skill, `game-modules`, is added.

**Tests.**
- Shared test module:
  - It calls every service method. Counting proxies assert full coverage, and they are compile-time complete because they implement the pure interfaces.
  - `PhysicsService::createMesh` from module code, then a module reload, then simulation steps under ASan: regression for the module-copy hazard.
- Lifecycle and worlds:
  - Unloading strips module Components from both the edit and the play World before orphaning (regression for ADR 0027's consequence).
  - `start`/`stop` run once per session, and `update` never runs on the edit World.
  - `world()` asserts during `load` (caught through a non-noexcept path).
- Configuration: a fixture that links `illusion::physics` into a shared module fails to configure.

**Benchmarks.** `modules/update` keeps 25 ns per module per World; `services/virtual-call` 10 ns.

**Docs.** `manual/modules.md` (lifecycle v3, worlds, services, `USES`), `reference/scripting-api.md` (the Doxygen group "Scripting API").

**ADR.** 0041: module API v3 (lifecycle, multi-world host, host service interfaces). It supersedes ADR 0027's one-World-per-host and `LINK` guidance.

### M7r: Play mode and the GameSession runtime
Needs: o, q.

**Scope**

- **`engine/runtime`** (`illusion::runtime`):
  - **`GameSession`** owns a World, project settings, `PhysicsWorld`, `AudioWorld`, `InputActions` (from the project asset), gamepad routing, both resolvers, hierarchy propagation and the ModuleHost simulation attachment with services.
  - Per-step order: input, modules, physics, hierarchy, audio, resolvers. Deterministic fixed-step mode is supported.
  - The M8 player will run the same `GameSession`.
- **Project settings** (`settings:/*.json`, through struct reflection): `PhysicsSettings` (layer names, collision matrix, gravity, step), `AudioSettings`, `InputSettings`, `RenderSettings`.
- **Input actions asset** `.input` (JSON actions with type and bindings), plus `InputActions::removeAction`, `renameAction` and `clear`.
- **`illusion::editor_play` / `PlaySession`:**
  - Pressing Play takes an in-memory scene v2 snapshot of the edit World (ids kept) and loads it into a fresh play World. Stop destroys the play World.
  - Modes: Play, Simulate (physics only), Pause, Step N, time scale.
  - While playing, commands default to the play World (`world: "edit"` overrides). Play-world edits are not undoable.
  - `keep_changes` copies chosen values back to the edit World as one undoable step.
  - Runtime-spawned entities receive guids every tick (seeded in deterministic mode).
- **`EditHooks`:** RigidBody, Collider, CharacterController and Joint edits call `PhysicsWorld::rebuild`; a clip change calls `AudioWorld::play`; MeshRenderer and SkyLight edits invalidate the resolver.
- **Input routing:**
  - The Game view feeds a synthetic, viewport-local `InputState`.
  - Clicking captures the cursor and Shift+F1 releases it.
  - Gamepads go to the game while it has focus.
- **Commands:** `input.*` (including `inject` and `action_define`/`bind`) and `play.*` (including `run_until`).
- **Play UI:** toolbar group, play tint, Maximize on Play, autosave before play.

**Tests.**
- Unit and headless:
  - After stop, the edit World's scene JSON is byte-identical to before play.
  - The same seed gives the same reflected-state hash after 600 steps.
  - Injected keys and virtual gamepads drive actions; physics side effects apply while playing.
  - Simulate moves bodies without module updates; `keep_changes` is undoable; selection maps between Worlds through guids.
  - A TSan run of play with jobs.
- Acceptance: the scripted transcript gains play scenarios using `tests/acceptance/tetris/reference/` (built in-tree as a `SHARED` module and loaded with `module.load {path}`), covering C5 spawn, wall, hard drop and single clear.

**Benchmarks.** `play/snapshot` 5,000 ns per entity; `runtime/session-step-empty` 50,000 ns per step.

**Docs.** `manual/editor/play-mode.md`, `manual/runtime.md` (new), `manual/actions.md` (input actions asset), `manual/modules.md`.

**ADR.** 0042: play mode through scene snapshots and a shared `GameSession` runtime.

### M7s: Project modules built from the editor
Needs: r.

**Scope**

- **Build-tree package:**
  - `export(TARGETS …)` of `illusion_core`, `illusion_yyjson`, `illusion_glm` and the API targets (with `EXPORT_NAME`) into `<build>/sdk/illusionTargets.cmake`.
  - `illusionConfig.cmake` calls `find_dependency(Threads)`, includes `IllusionCompilerSettings` and `IllusionModules`, and records the compiler, make program, configuration, sanitizer and assert settings. It fails clearly when they do not match the build key.
  - Sanitizer and assert usage requirements are exported.
- **Build info.** A generated `EditorBuildInfo.hpp`.
- **`module.create`** template: `modules/<name>/{CMakeLists.txt, src/<Name>.cpp}`, with a reflected Component and `start`/`update`.
- **`module.build`** (async):
  - Configures on first use: `cmake -S modules/<name> -B .illusion/build/<cfg>/<name> -G Ninja -DCMAKE_BUILD_TYPE=<cfg> -DCMAKE_CXX_COMPILER=… -DCMAKE_MAKE_PROGRAM=… -Dillusion_DIR=<sdk>`.
  - Then runs `cmake --build`, streaming progress.
  - GCC, Clang, MSVC and CMake diagnostics are parsed into `{file, line, column, severity, message}`.
- **Other module commands:** `module.load`, `reload`, `unload`, `list`, `status`, `diagnostics`, `open_in_ide` (VS Code, Visual Studio, CLion, Xcode). Auto-reload follows a successful build.
- **Modules panel:** build with Ctrl+B, an auto-reload toggle, clickable diagnostics, reload history.
- **File access.** `project.file_*` is confined to project mounts (`..` and escaping symlinks are rejected). The read-only `sdk:` mount backs the MCP resource `illusion://sdk/{header}`.

**Tests.**
- Label `module_build` (Release only, `RUN_SERIAL`), on every build job: scaffold, build, load, edit, rebuild and hot reload, out-of-tree through the package. A failing build returns 1301 with diagnostics.
- Unit: diagnostics parser fixtures from captured compiler output, path confinement, and config-mismatch messages.
- Acceptance: the scripted transcript now writes the reference sources with `project.file_write`, including a deliberately broken first version that returns 1301 and then a fixed one, and builds them with `module.build`.

**Benchmarks.** `module/diagnostics-parse` 200 ns per line.

**Docs.** `manual/editor/modules.md`, `manual/modules.md` (projects, package), `tutorials/first-game-module.md`.

**ADR.** 0043: project modules built against a build-tree package with the editor's toolchain.

### M7t: Component layout migration on hot reload (M6 follow-up)
Needs: s.

**Scope**

- **Registry and World support:**
  - The registry counts archetype uses per ComponentId across Worlds.
  - **Staged registration:** while a module reloads, a retired type registered with a new size or alignment is staged and its new reflection is held as pending.
  - `World::purgeArchetypesWith(id)` destroys empty archetypes containing the type, removes edges that point at them and resets the query cache.
  - `ComponentRegistry::replaceLayout(id, info, reflection)` applies only when the use count is 0, and keeps the id and stable name.
- **Reload pipeline:**
  1. Open and validate the new library.
  2. Call the old `unload`, then retire the module's types.
  3. Call the new `load`, which only registers types.
  4. For each staged type, snapshot its instances in every attached World by field name through the old reflection.
  5. Strip those instances while the old code is still loaded.
  6. Purge archetypes, then replace the layout.
  7. Restore the snapshot by field name with conversions: numeric widening, clamped narrowing with a warning, int ↔ float, enums by name, Vec2/3/4 padding or truncation, string → AssetRef, `renamedFrom`, defaults for new fields.
  8. Produce a `MigrationReport`.
  9. Strip types the new version no longer registers, then close the old library.
- **Failure handling.**
  - **Any failure rolls back** to the old layout and values.
  - Unreflected module types still roll back, with the message "reflect the type to migrate it".
  - At registration, a warning is logged when reflected fields cover less than `sizeof(T)`.
- **Results.** `module.reload` returns the report and emits `module.migrated`. The ROADMAP M6 follow-up is ticked.

**Tests.**
- Unit:
  - Test module variant 3 (the widened counter) migrates during a live play session and keeps its values.
  - New variants 5–7: fields added, removed and reordered, i32 → i64, alignment changes, `renamedFrom`, Entity fields; edit and play Worlds together.
  - An induced load failure rolls back.
  - The ComponentId is unchanged and `componentId<T>()` caches stay valid.
  - 50 migrating reloads do not grow the registry count.
  - ASan and TSan.
- Acceptance: a hot-reload step adds a `combo` field to `tetris.Score` during play (C7).

**Benchmarks.** `modules/migrate` 2,000 ns per Component; query cost after a purge unchanged (`ecs/iterate`).

**Docs.** `manual/modules.md` (what survives a reload, conversion rules, reports).

**ADR.** 0044: in-place Component layout migration. It supersedes ADR 0027's rollback on layout conflicts.

### M7u: Asset pipeline and asset browser
Needs: o, m.

**Scope**

- **`AssetCatalog`** (editor): scans the project mounts and records type, size, mtime, xxHash64 content hash, dependencies and reverse references (parsed through reflection). Updates are incremental.
- **`FileWatcher`** (`engine/platform`): inotify, ReadDirectoryChangesW or FSEvents, with coalescing and delivery at an explicit `poll()`. A stat-polling backend covers network drives and exhausted watch limits.
- **Import settings sidecars** `<file>.import.json`, written only for non-default settings and honoured by `cookAssets`.
- **Live reload.** `AssetStreamer::invalidate(path)`/`reload` and `AssetReloadedEvent` make resolvers re-resolve while the editor runs.
- **`RenderResources` lifetime:** generation-checked handles; `release*()` deferred by frames in flight; free ranges in the geometry pools; `updateMaterial()`; an internal `texture(TextureHandle)` accessor.
- **Environment bake disk cache** (`AssetType::Environment`, keyed by source hash and `EnvironmentDesc`).
- **`ThumbnailRenderer`:** a preview World and a small `SceneRenderer` at 256 px, with at most one thumbnail and 2 ms per frame. PNGs are cached in `.illusion/thumbnails`.
- **Asset Browser panel:**
  - Navigation: folder tree, favourites, breadcrumbs, grid and list views, type chips, search including labels, sort, multi-select.
  - File operations: F2 rename; context menu with Reimport, Rename, Move (with reference fix-up), Delete (with a reference report), Reveal and Copy path; create menu.
  - Drag and drop: into the viewport (ghost preview, surface placement), into inspector fields and into the outliner; OS file drops import.
- **Editors and tools:**
  - Asset inspector: import settings with Apply and Revert; previews for models, material spheres, texture channels and mips, HDRIs, and audio waveforms with playback.
  - Material editor with live `updateMaterial`; Environment/Sky editor; Cook and Pack tool.
- **New commands:** `asset.import`, `reimport`, `set_import_settings`, `thumbnail`, `find_missing`, `relink`, `cook`, `pack`.
- **External renames** (a delete plus a create with an equal content hash) trigger a relink offer.

**Tests.**
- Unit:
  - The catalog scan is deterministic; rename fix-up across scenes, materials and prefabs is all-or-nothing.
  - Watcher: the native backend (create, modify, rename, delete in a TempDirectory, waiting on events with a failure ceiling) and the polling backend (deterministic mtimes).
  - Cache hits after a restart (no re-bake).
- GPU:
  - Reimport keeps resource counts and memory flat over 100 import/delete cycles; stale handles assert.
  - `updateMaterial` matches its golden without recreating bindings.
  - Thumbnails stay within their frame budget; a dropped file imports.
  - A cooked pack loads in a runtime-only process.

**Benchmarks.** `assets/catalog-scan` 20,000 ns per file; `assets/reference-index` 5,000 ns per reference.

**Docs.** `manual/editor/assets.md`, `manual/assets.md` (sidecars, reimport, bake cache), `manual/rendering.md` (resource lifetime).

**ADR.** 0045: the asset catalog, import settings sidecars, live reload and GPU resource lifetime.

### M7v: Prefabs
Needs: u.

**Scope**

- **Assets and scene records.** `.prefab` assets are a scene v2 subset with one root. A scene stores an instance as a record, `{"prefab": ref, "id": instanceGuid, "overrides": …}`, instead of expanded entities.
- **Instance identity.** Instance entity guids are `hash(instanceGuid, prefabLocalGuid)`.
- **Overrides.** Each override is addressed by prefab-local guid, Component and field path, plus Components added or removed and children added. They are computed at save time by diffing against the expanded prefab, which keeps them correct after raw edits.
- **Engine support.** `instantiatePrefab(World&, const PrefabData&, Entity parent, u64 instanceGuid)`. Prefabs are expanded on load. Nesting rejects cycles and is limited to depth 16. Variants are prefabs whose root is an overridden instance. `SceneService::instantiate` makes this available to modules.
- **Editor:**
  - Create a prefab from a selection by dragging it to the browser; instantiate by dragging into the viewport or outliner.
  - Prefab edit mode with a breadcrumb.
  - Overrides shown as a bold label with an accent bar.
  - Apply or revert per field, per Component or all; unpack.
  - Changes to a prefab file propagate into open scenes as an undoable step that keeps overrides.
  - Broken prefab references are validated.
  - `prefab.*` commands.

**Tests (unit).**
- Round trip; a prefab change propagates except to overridden fields.
- Nesting and variant precedence; guid determinism.
- Apply and revert are undoable; orphaned overrides are reported.
- A missing prefab keeps the instance data (passthrough).
- Runtime instantiation from a module; fuzzing of prefab commands.

**Benchmarks.** `prefab/instantiate` 10,000 ns per entity.

**Docs.** `manual/editor/prefabs.md`, `manual/scenes.md` (prefab records).

**ADR.** 0046: prefabs as instance records with field-level overrides and nesting.

### M7w: Engine feature panels, profiler and diagnostics
Needs: r, u.

**Scope**

- **Lighting and Rendering panel:** sky, ambient, clear colour, exposure, shadows with cascade view, GTAO, bloom, TAA, occlusion culling, bindless and MDI information, debug views, light-limit warnings.
- **Physics panel:**
  - Engine additions: `PhysicsWorld::debugGeometry()`, `meshGeometry(handle)`, `setPaused()`/`step()`.
  - UI: layer names and a triangular collision matrix; gravity and step; debug draw; statistics.
  - A paused edit-world `PhysicsWorld` answers `physics.raycast` and `drop_to_ground` in edit mode.
- **Audio Mixer:** `AudioWorld` meters (peak and RMS per bus and master, as atomics written by the audio thread) and `clipWaveform()`. The panel has dB faders, mute and pause, filters, reverb, voice statistics and audition.
- **Input Actions editor:** for `.input` assets, with glyph previews per gamepad family, Listen capture through `BindingCapture`, and conflicts from `findConflicts`.
- **Gamepad tester:** live state, rumble, trigger rumble, LED, adaptive triggers, gyro and touchpad.
- **Profiler** (ImPlot): a CPU frame graph with 16.6 ms and 33.3 ms budget lines; in-process `ILL_STAT_SCOPE` timers (fixed per-thread rings in core `Profiler.hpp`); GPU pass timers; memory, resource and streaming counters; a Tracy hint in profiling builds.
- **Other panels:** Statistics; ECS Debugger (archetypes, chunk occupancy, registry flags, owning module, ids used of 256); Project Settings (search, modified markers, reset).
- **Map Check / Problems panel.** `scene.validate` rules, each with a fix-it command: missing assets, RigidBody without Collider, no camera, several AudioListeners, light limits, a dynamic body under a scaled parent, a Joint without a connected entity, unreflected module types.
- If the PR gets too large, split into w-1 (rendering, physics, profiler) and w-2 (the rest).

**Tests.**
- Headless UI: for each panel, an edit runs the expected command and can be undone.
- Unit:
  - Offline audio meters for a known sine (peak and RMS); waveform summaries.
  - `.input` round trip and rebinding with a `VirtualGamepad`; tester rumble recorded through `VirtualGamepad` outputs.
  - The profiler allocates nothing.
  - Validation rules, positive and negative; debug-geometry enumeration.
- GPU: golden image after toggling bloom in the panel.

**Benchmarks.** `profiler/stat-scope` 30 ns; `audio/mix` budget unchanged with meters on.

**Docs.** `manual/editor/panels/{rendering,physics,audio,input,profiler,diagnostics}.md`, plus updates to `physics.md`, `audio.md`, `actions.md` and `profiling.md`.

### M7x: Showcase scene, scripting-API coverage and the Tetris acceptance test
Needs: all of the above.

**Scope**

- **`samples/showcase`:**
  - `assets:/scenes/everything.scene`: every reflected Component with non-default values, a hierarchy, a prefab instance with overrides, every built-in asset, mesh and hull colliders from assets, joints of every type, and audio sources with every attenuation, cones and buses.
  - A `showcase` module that calls the whole scripting API (core plus every service), with CC0 assets (a small Poly Haven HDRI, glTF, audio).
- **Coverage gates:**
  - `everything_scene_covers_every_reflected_component_and_field` iterates `ComponentRegistry` and `StructRegistry`.
  - Service counting proxies.
  - `tools/check_api_coverage.py` reads the Doxygen XML of the "Scripting API" group and fails in the docs job on any uncovered symbol.
- **Recorded transcript.** A live Claude Code session is recorded through `illusion_mcp --record` and normalised (§9) into `tests/acceptance/tetris/recorded.jsonl` and `files/`. The `files/` sources are also compiled in-tree as a `STATIC` module with a headless logic test, so engine API breaks fail the build.
- **Live workflow.** `.github/workflows/agent-acceptance.yml`.
- **Documentation and status.**
  - Tutorials.
  - The complete editor manual, with screenshots generated in the gpu job by `--headless --screenshot`.
  - A string freeze for translators; ROADMAP M7 ticked.

**Tests.**
- Acceptance C1–C8 for both the scripted and the recorded transcript (§9).
- The showcase plays 300 deterministic frames headless with a stable state hash, and matches a GPU golden.
- API coverage; every editor benchmark within budget on all platforms.

**Docs.** `tutorials/tetris-with-an-agent.md`, `tutorials/tetris-by-hand.md`, `manual/editor/ai-agents.md` (recording and refreshing transcripts), README, ROADMAP.

## 5. Dependencies

All are pinned in `cmake/Dependencies.cmake` (exact version and SHA-256), recorded in `THIRD_PARTY_NOTICES.md`, editor-only and never shipped in games. The research step reproduced the archive hashes with `git archive | gzip -n | sha256sum`; that method reproduces the existing doctest, yyjson and stb pins. CI checks them on first configure. Before merging M7d, check upstream for a 1.93.x patch release.

| Dependency | Version / commit | Licence | Source and SHA-256 |
| --- | --- | --- | --- |
| Dear ImGui (docking) | v1.93.0-docking, commit `b916186e66aa3c02b72c6ca17455516512ed8049` | MIT | `https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0-docking.tar.gz` · `746c265540bd3aa109570f29492ef4d8f5b421dd9fd0049c35ca35f21496f1a5` |
| (fallback) Dear ImGui | v1.92.9b-docking, commit `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | MIT | `90ded916bd57db2e0e171b6b098940a47c6f5042725dcdc67fb19940ca8bfdcc` |
| ImPlot | commit `851cdbdd60d1dfb64c23e987969694de0f547a6b` (has the 1.93 fixes) | MIT | `https://github.com/epezent/implot/archive/851cdbdd60d1dfb64c23e987969694de0f547a6b.tar.gz` · `e11623920915f2fea586773ee3be7340d4c405b943b0f526aafaf5a64bc7cbd1` |
| Roboto (UI text) | v2.138, commit `ab4876f650a4aeb9b661c83fee47f908c739bb70`. **Never move to 3.x (OFL).** | Apache-2.0 | `raw.githubusercontent.com/googlefonts/roboto/<commit>/src/hinted/Roboto-Regular.ttf` · `56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d`; `…/Roboto-Medium.ttf` · `2879a5ecb7fbfa13a7fc3e2cdd7fecbf73aa45e91b541dfdfa2c442eed0aac21`; `…/Roboto-Bold.ttf` · `61f89f8db49261c2f6106e8dccc35df7b2f7ed909020db40a3fc905e95f99334`; `LICENSE` (hash recorded when pinned) |
| Roboto Mono (console, code, numbers) | commit `30867cc8bedee415663cd39f5240b92453138840`, the last Apache-2.0 commit. **Never bump.** | Apache-2.0 | `raw.githubusercontent.com/googlefonts/RobotoMono/<commit>/fonts/ttf/RobotoMono-Regular.ttf` · `7432e74ff02682c6e207be405f00381569ec96aa247d232762fe721ae41b39e2`; `…/RobotoMono-Medium.ttf` · `0c4e5c4d4c3c37ca47cb4047ce8821965c8fbe51884d5b0098c79c7afa6e6ab0`; `LICENSE.txt` · `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4` |
| Material Symbols Rounded (icons) | google/material-design-icons commit `49d4db35df873165d6bd6ba09b063c7dafbac2f4` | Apache-2.0 | `raw.githubusercontent.com/google/material-design-icons/<commit>/variablefont/MaterialSymbolsRounded%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf` · `32e4011709e055596eb8d2dd0f7cb547b47c1d40ac9ecb8fad54f7f7eac202e7`; codepoints, identical for Rounded and Outlined: `…/variablefont/MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.codepoints` · `0c9dae3ed678a81549c3be046bc8045335e5f57f2cbe2287360fbf0c2cc1355f`; `LICENSE` (hash recorded when pinned) |
| (fallback mono) Go Mono | golang/image commit `41969df76e82aeec85fa3821b1e24955ea993001` | BSD-3-Clause | `…/font/gofont/ttfs/Go-Mono.ttf` · `8bc66a0154bbf69cd24e5bde41a12ec9495c8a242c5def2255e4a164900f1ed7`. Used only if Roboto Mono is rejected. |
| (fallback icons) Material Icons static | same commit as Material Symbols | Apache-2.0 | `font/MaterialIcons-Regular.ttf` · `ef149f08bdd2ff09a4e2c8573476b7b0f3fbb15b623954ade59899e7175bedda` (356 KB, fewer icons) |
| yyjson (existing) | 0.13.0 | MIT | Reused for RPC, MCP, locales, themes and settings |

**How fonts are fetched and installed.** Fonts are fetched as single files (`FetchContent_Declare(... DOWNLOAD_NO_EXTRACT TRUE)`; the cloud box can use `FETCHCONTENT_SOURCE_DIR_<NAME>`). They are installed to `<bin>/editor-data/fonts/{Roboto-Regular,Roboto-Medium,Roboto-Bold,RobotoMono-Regular,RobotoMono-Medium,MaterialSymbolsRounded}.ttf` and the licences to `<bin>/editor-data/licenses/`. `Icons.hpp` is generated from the pinned `.codepoints` file. stb_truetype reads the default instance of the variable font (FILL 0, wght 400, GRAD 0, opsz 24). 140 icons are above the BMP, so `IMGUI_USE_WCHAR32` is required. The fonts cover Latin, Latin Extended-A (every Turkish letter: ç ğ ı İ ö ş ü), Greek and Cyrillic.

**Rejected:**

| Rejected | Reason |
| --- | --- |
| ImGuizmo | Global context, Euler recomposition, no pivots or snapping |
| imgui-node-editor, imnodes, ImPlot3D | Not needed in M7 |
| nativefiledialog-extended | GTK (LGPL) or D-Bus; does not work headless |
| efsw | Exception audit needed; our own watcher fits the rules |
| IconFontCppHeaders | Header drifts from the pinned font |
| Lucide | ISC licence is not on the list |
| Tabler, Phosphor | Not needed |
| RmlUi | No docking or property grids |
| NoesisGUI, Qt, Slint | Licences |
| ImGui Test Engine | Not permissive; we implement its hooks ourselves |
| FreeType / imgui_freetype | FreeType licence |
| ICU | Unicode licence, about 30 MB |
| Every OFL font (Inter, Noto, JetBrains Mono, Roboto 3.x, Cousine) | OFL is not on the list |

## 6. Command, JSON-RPC and MCP API

### 6.1 Command model

- **Single entry point.** Every editor state change is a registered command. Menus, toolbar, shortcuts, palette, inspector, gizmo drags, drag and drop, the console command line, RPC and MCP all call the same registry.
- **`CommandDescriptor` fields:**
  - `name` (`domain.verb`, snake_case verbs) and `version`.
  - `titleKey` and `descriptionKey` (localized); `agentSummary` (English, with an example).
  - `category`, `icon`.
  - Params and result: reflected structs that generate the JSON Schema 2020-12 input and output schemas.
  - Flags: `readOnly`, `destructive`, `idempotent`, `undoable`, `async`, `needsGpu`, `mode` (edit, play, any).
  - Permission class: read, edit, destructive, files, build, system.
  - `palette`, `defaultShortcut`.
- **Execution.** Handlers run on the main thread at the command point, inside a transaction, after validation against current state.
- **Entity references** (strings):

  | Form | Example |
  | --- | --- |
  | 16-hex guid | `9f2c41aa0b7d3e15` |
  | Hierarchy path of Names | `/Game/Board/Cell 3` |
  | Unique name | `name:Board` |
  | Current selection | `$selection` |
  | Batch reference | `$board.entity.id` |

  Results always return `{id, name, path}`. Raw `Entity` bits never cross the API.
- **Value encoding** follows scene v2:
  - Vectors are arrays and quaternions are `[x, y, z, w]`; enums are names.
  - Entity fields are references; asset fields are path strings.
  - u64 values are decimal strings.
  - Angles take radians or `{"deg": n}`; colours take linear arrays or `"#rrggbb"`.
  - Transform commands also accept `{"euler": [x, y, z]}` in degrees.
- **Options on mutating calls:**
  - `dryRun`: validate and return the footprint without applying.
  - `clientToken`: idempotency over the last 1,024 tokens per client.
  - `expectRevision`: optimistic concurrency, error 1203 on mismatch.
  - `mergeKey`: continuous edits become one step.
- **Grouping.** `editor.batch {label, atomic, steps:[{as, method, params}]}` runs as one transaction with `$` references. `transaction.begin`, `commit` and `cancel` work per connection.
- **Undo scopes and origins.** `history.undo {steps, scope: all|mine}` refuses when a later transaction from another origin touched the same (guid, Component). Origins: `ui`, `shortcut`, `palette`, `inspector`, `gizmo`, `rpc:<client>`, `module`.

### 6.2 Method catalogue (about 190)

Flags: R = read-only. U = undoable. A = async (progress and cancel). G = needs a GPU (error 1400 otherwise). P = allowed while playing, defaulting to the play World. Bracketed classes need approval in ask mode.

| Domain | Methods |
| --- | --- |
| rpc | `authenticate`, `discover` (OpenRPC). Notifications: `rpc.cancel` (client→server), `rpc.progress`, `event` (server→client). |
| editor | `capabilities` R, `list_commands` R, `describe` R, `help` R, `batch` U, `wait` R A (assets_idle, module_ready, frames, jobs), `subscribe`, `unsubscribe`, `poll_events` R, `set_automation` [system], `preferences_get` R, `preferences_set` [system], `screenshot` R G, `quit` [system] |
| project | `create` [files], `open`/`close` [system], `info` R, `recover` U, `settings_get` R, `settings_set` U, `file_list`/`file_read`/`file_stat` R, `file_write`/`file_move`/`file_delete`/`file_mkdir` U [files] |
| scene | `new`, `open`, `save`/`save_as` [files], `close`, `list` R, `set_active`, `info` R, `summary` R (compact tree for model context), `tree` R, `query` R P (has, without, name glob, under, where, select, limit, cursor), `changes` R P (since revision), `diff` R, `validate` R, `settings_get` R, `settings_set` U, `reload` |
| entity | `create` U P (name, parent, index, components), `create_primitive` U P, `create_light` U, `create_camera` U, `instantiate` U P, `destroy` U P, `duplicate` U P, `rename` U P, `set_enabled` U P, `get` R P, `find` R P, `copy` R, `paste` U |
| component | `types` R, `schema` R, `add` U P, `remove` U P, `get` R P, `set` U P (merge patch), `set_field` U P, `reset` U P, `copy` R, `paste` U |
| hierarchy | `set_parent` U P (keepWorld, index), `reorder` U P, `children` R P |
| transform | `set`/`translate`/`rotate`/`scale` U P (space, pivot), `reset` U P, `align`, `distribute`, `drop_to_ground`, `snap_to_grid` U |
| selection | `get` R, `set`, `add`, `remove`, `clear`, `all`, `invert`, `select_children`, `frame`. These are not undo steps; selection is restored with history. |
| history | `undo`, `redo`, `list` R, `jump`, `clear` [destructive] |
| transaction | `begin`, `commit`, `cancel` |
| clipboard | `copy` R, `cut` U, `paste` U (scene v2 subset JSON, guids regenerated) |
| asset | `list` R, `info` R, `import` U A [files], `reimport` A, `set_import_settings` U, `create_folder`/`move`/`delete` U [files], `restore` U, `references` R, `dependencies` R, `find_missing` R, `relink` U, `thumbnail` R G, `cook`/`pack` A [files] |
| material / environment | `material.create` U [files], `material.get` R, `material.set` U, `material.preview` R G; `environment.create` U [files], `environment.set_sky` U, `environment.bake` A G |
| prefab | `create` U [files], `instantiate` U P, `open`, `close`, `apply` U [files], `revert` U, `unpack` U, `overrides` R |
| viewport / camera / gizmo | `viewport.list` R, `create`, `close`, `camera_get` R, `camera_set`, `view` (perspective, top, bottom, front, back, left, right), `frame`, `set_mode`, `set_overlays`, `pick`/`pick_rect`/`sample` R G, `project` R, `screenshot` R G; `camera.bookmark_set`/`bookmark_go`; `gizmo.set_tool`/`set_space`/`set_pivot`/`set_snap` |
| render | `settings_get` R, `settings_set` U, `stats` R G, `gpu_info` R, `diagnostics` R (validation error count) |
| play | `start` (play or simulate, seed, deterministic, fromEditorCamera), `stop`, `pause`, `resume`, `step` A, `state` R, `run_until` A (query condition, maxFrames), `set_time_scale`, `keep_changes` U |
| input | `actions_list` R, `action_define`/`action_remove`/`action_rename`/`action_bind`/`action_unbind` U, `conflicts` R, `inject` P (key, mouseButton, mouseMove, scroll, text, gamepadButton, gamepadAxis; frames), `devices` R, `state` R P, `gamepad_attach_virtual`, `gamepad_detach`, `glyph` R |
| physics / audio | `physics.settings_get` R, `settings_set` U, `layers_set` U, `raycast`/`shape_cast`/`overlap`/`stats` R P, `debug_draw`; `audio.mixer_get` R, `mixer_set` U, `preview`, `stop_preview`, `meters` R, `stats` R |
| module | `create` U [files], `list`/`status`/`diagnostics`/`migration_report` R, `build` A [build], `load`/`reload`/`unload` [build], `open_in_ide` [system] |
| log / job | `log.read` R (since seq, minLevel, category, contains, limit), `log.clear`, `log.set_level`; `job.list` R, `job.wait` R A, `job.cancel` |
| ui (only with a UI) | `panel_open`, `panel_close`, `panel_focus`, `layout_list` R, `layout_load`, `layout_save`, `layout_reset`, `theme_set`, `locale_set`, `notify`, `state` R, `follow_agent` |

**Event topics** (`editor.subscribe` for push, `editor.poll_events` for pull; queues of 1,024 per subscription, then `overflow` and a resync through `scene.changes`): `scene.changed` (coalesced per frame), `selection.changed`, `history.changed`, `play.state`, `log`, `assets.changed`, `module.status`, `module.migrated`, `build.progress`, `notification`, `agent.activity`, `commands.changed`.

### 6.3 JSON-RPC wire format, errors, transports and security

- **Framing.** One UTF-8 JSON object per line, at most 16 MiB. JSON-RPC batch arrays are rejected; use `editor.batch`.
- **Example request:**

  ```json
  {"jsonrpc":"2.0","id":7,"method":"entity.create","params":{"name":"Board","parent":"/Game","components":{"illusion.Transform":{"position":[4.5,9.5,0]},"illusion.MeshRenderer":{"mesh":"builtin:mesh/cube","material":"assets:/materials/frame.material"},"tetris.Board":{"width":10,"height":20}},"expectRevision":41,"clientToken":"c1-0007"}}
  ```

- **Example result:** `{"entity":{"id":"9f2c41aa0b7d3e15","name":"Board","path":"/Game/Board"},"_editor":{"revision":42,"transaction":17,"warnings":[]}}`
- **Protocol errors:** -32700, -32600, -32601, -32602 (`data.errors[{path, expected, got}]`), -32603.
- **Application errors** use positive codes, outside the JSON-RPC and MCP reserved range:

  | Code | Meaning |
  | --- | --- |
  | 1000 | unauthenticated |
  | 1001 | permission_denied |
  | 1002 | busy |
  | 1003 | timeout |
  | 1004 | cancelled |
  | 1100 | wrong_mode |
  | 1200 | not_found |
  | 1201 | already_exists |
  | 1202 | validation_failed |
  | 1203 | conflict |
  | 1204 | ambiguous |
  | 1205 | precondition_failed |
  | 1300 | io_error |
  | 1301 | build_failed (with diagnostics) |
  | 1302 | module_error (with report) |
  | 1400 | unsupported (e.g. `gpu_unavailable`) |
  | 1401 | limit_exceeded |
  | 1500 | internal (with journal reference) |

  Error data is `{kind, errors[], suggestions[], retryable, docs}`.
- **Transports:**
  - stdio: the parent owns the pipes, so no token is needed.
  - `AF_UNIX`: socket in `.illusion/run/` (0700 or owner-only ACL).
  - No TCP or HTTP in M7.
- **Endpoint file** `.illusion/run/editor-endpoint.json`: `{pid, protocolVersion, editorVersion, transport, address, token}`, written atomically with 0600 or an owner-only ACL, deleted on exit, stale entries detected by pid.
- **Token.** 256 bits from `secureRandomBytes`. `rpc.authenticate` must be the first message, within 2 s, and is compared in constant time.
- **Limits.** 8 connections, 64 in-flight requests per connection, an inbound queue of 256.
- **Permission modes** (Preferences > AI Agents, or `--permissions`):
  - read-only: only R commands run.
  - ask (GUI default): destructive, files, build and system calls wait for an approval toast; a timeout counts as a denial. Ask is always denied in headless mode.
  - auto (headless default).
- **Confinement and visibility.** File access is confined to project mounts. There is no generic shell or process command: `module.build` spawns fixed cmake invocations without a shell. The docs state plainly that `module.build` plus `load` runs agent-written code. The status bar shows connected agents.
- **Versioning.** The protocol is semver: 0.x until M7k, 1.0 at M7x. Each command also carries its own version. The `list_commands` golden test enforces version bumps.

### 6.4 MCP mapping (`illusion_mcp`)

> **Note:** The MCP spec facts below were read at spec commit `1155ca4`. Re-verify the golden transcripts against the spec while implementing M7k.

- **Eras:**
  - An `initialize` with `2025-06-18` or `2025-11-25` selects the stateful legacy mode for that process: `notifications/initialized`, `ping`, `tools/list` with cursors, `tools/call`, `resources/*`, `prompts/*`, `notifications/cancelled`, `notifications/progress`, and `notifications/tools/list_changed` with `listChanged: true`.
  - A request with `_meta["io.modelcontextprotocol/protocolVersion"] = "2026-07-28"` selects stateless mode: `server/discover` (`supportedVersions`, capabilities, instructions, serverInfo, `ttlMs`, `cacheScope`), `resultType: "complete"`, serverInfo in `_meta`, `ttlMs`/`cacheScope` on list results, and `subscriptions/listen` for list changes.
  - Unknown versions get -32022 with `data.supported`.
- **Tools:**
  - The name is the method with `.` replaced by `_` (`entity_create`), unique, in catalog order.
  - The description is the English summary plus generated parameter documentation (units, ranges, enum names) and an example.
  - `inputSchema` and `outputSchema` come from the catalog.
  - Annotations: `readOnlyHint`, `destructiveHint`, `idempotentHint`, and `openWorldHint: false`.
  - `--tools all|core|<domains>`; `illusion_batch` and `illusion_call` are escape hatches.
- **Results:**
  - `structuredContent` plus a compact text summary (up to about 4 KB, then a cursor hint).
  - Screenshots, previews and thumbnails are PNG image content: 768 px on the long side by default and at most 1024, because images count against Claude Code's 25k-token output limit.
  - Command failures return `isError: true` with `{code, kind, path, hint, suggestions}`. Malformed MCP traffic gets JSON-RPC errors.
- **Resources:** `illusion://guide`, `illusion://project`, `illusion://scene/current`, `illusion://scene/summary`, `illusion://components/{stableName}` (schema and docs), `illusion://log/recent`, `illusion://sdk/{header}`, `illusion://manual/{page}`, `illusion://commands`.
- **Prompts:** `new_game`, `add_component_type`.
- **Instructions:** discover, then create, save, build, play, verify; units conventions; entity references.
- **Claude Code setup.** Run `claude mcp add --transport stdio illusion -- <build>/bin/Release/illusion_mcp --project .` or use the `.mcp.json` that `project.create` writes:

  ```json
  {"mcpServers":{"illusion":{"type":"stdio","command":"<absolute path>/illusion_mcp","args":["--project","."],"timeout":600000}}}
  ```

## 7. Localization design

- **Keys:**
  - Stable ASCII, dot-separated namespaces matching `^[a-z][a-z0-9_]*(\.[A-Za-z0-9_]+)+$`.
  - Editor keys: `editor.menu.file.save`, `editor.inspector.add_component`.
  - Command keys: `cmd.<name>.title` / `cmd.<name>.description`.
  - Reflection keys: `type.<stableName>.name`, `field.<stableName>.<field>.label`/`.tooltip`, `enum.<stableName>.<value>`.
  - Engine keys: `input.glyph.*`.
- **Static and dynamic keys.** Keys for editor code are compiled. Reflection and module keys are dynamic: English defaults come from the metadata, and `illusion_editor --dump-strings` exports them for translators.
- **File format** (strict JSON, comments as fields). English is the source of truth: `editor/data/locales/en.json`, plus `engine/input/data/locales/en.json`.

  ```json
  {"format":"illusion.locale","version":1,"locale":"en","displayName":"English","plural":"en",
   "number":{"decimal":".","group":","},
   "strings":{
     "editor.menu.file.save":{"text":"Save","comment":"File menu: save the active scene"},
     "editor.outliner.selected":{"text":"{count, plural, one {# entity selected} other {# entities selected}}"}}}
  ```

  A translation (`locales/<bcp47>.json`) adds `"fallback"`, `"fonts"` (extra fallback fonts, for example Droid Sans Fallback, Apache-2.0, for CJK) and, per entry, `"source"`: the xxHash64 of the English text it was translated from, as 16 hex characters.
- **Code generation.** `illusion_textc generate` emits `inline constexpr TextKey kMenuFileSave{kTable, 0};` with the key and English as doc comments. A mistyped key is a compile error, and a lookup is an array index.
- **Call sites.** `ui::label(text::kMenuFileSave, "###menu.file.save")` and `ui::textf(text::kOutlinerSelected, {{"count", n}})`, both formatted into `FrameText`.
- **Fallback chain:** `tr-TR` → `tr` → the file's declared `fallback` → `en` (embedded and always complete), resolved per key. A missing key is logged once in Debug. Switching takes effect at the next frame boundary without a restart.
- **Formatting:**
  - The MessageFormat subset from M7b never throws; ICU and runtime `std::format` (which throws on bad patterns) are not used.
  - CLDR plural tables: Turkish has `one` (n = 1) and `other`.
  - Locale number separators apply to displayed numbers. Editable numeric fields use the invariant `.` and also accept the locale's decimal separator when it is `,`.
- **UI rules:**
  - Window, tab, menu and tree ids are always `Label###stable_id`, so layouts, widget state and AI command names survive a language switch.
  - Sentences are never concatenated.
  - Layouts allow 30–40% text expansion (auto-sized columns, ellipsis with a tooltip).
  - Search uses locale-independent `SearchFold` and matches both localized and English titles.
  - Agent-facing text (MCP descriptions, errors, enum names) is always English.
  - No text shaping or bidi (an ImGui limitation): Arabic, Hebrew and complex scripts are documented as unsupported.
- **Pseudo-locale.** `en-XA` is generated at runtime: accented characters, about 35% padding, `[brackets]`, placeholders intact. It can be selected in Preferences, and UI goldens run with it.
- **CI parity check.** CTest `text.locales` runs `illusion_textc check` in every build job:

  | Error | Warning |
  | --- | --- |
  | Extra key not in English | Missing key (falls back to English) |
  | Invalid key syntax | Stale source hash |
  | Unsorted keys | |
  | Pattern that does not parse | |
  | Argument names that differ from English | |
  | Missing plural category for the locale's rule | |
  | Non-strict JSON | |

  `--strict` (for M8 shipped locales) turns warnings into errors. A unit test renders every key in every locale with sample arguments.
- **Adding a language:**
  1. Copy `en.json` to `<tag>.json`.
  2. Set the metadata.
  3. Translate.
  4. Run `illusion_textc check`.
  5. Drop the file into `editor/data/locales` or the user locale folder. No code change is needed.
  6. Optionally, `tools/loc.py` exports and imports PO files for Weblate, Poedit or Crowdin.
- **M7 ships English and en-XA.** `tests/data/locales/tr.json` is a partial fixture that proves the pipeline end to end.

## 8. Theme and visual design

**Goal:** calm, dense and professional, like current DCC tools. One accent colour, quiet surfaces, crisp type, a 4 px grid, subtle rounding, icons on every primary action, and instant feedback.

**Tokens** (`editor/data/themes/{dark,light,high-contrast}.json`). Contrast ratios were computed with the WCAG formula:

| Token | illusion Dark (default) | illusion Light | High Contrast |
| --- | --- | --- | --- |
| `bg.base` (dockspace) | `#0F1115` | `#E9EBEF` | `#000000` |
| `bg.panel` | `#16191F` | `#F4F5F7` | `#000000` |
| `bg.raised` (frames, cards) | `#1D2128` | `#FFFFFF` | `#1A1A1A` |
| `bg.input` | `#12151A` | `#FFFFFF` | `#000000` |
| `bg.hover` / `bg.active` | `#262B34` / `#2E3440` | `#E3E6EB` / `#D7DBE2` | `#333333` / `#4D4D4D` |
| `border` / `separator` | `#2A2F38` / `#23272F` | `#D0D4DB` / `#E1E4E8` | `#FFFFFF` / `#FFFFFF` |
| `text.primary` | `#E3E6EB` (14.1:1) | `#1F2329` (14.5:1) | `#FFFFFF` (21:1) |
| `text.secondary` | `#A0A7B4` (7.3:1) | `#5A6270` (5.6:1) | `#D0D0D0` (13.6:1) |
| `text.disabled` | `#5C6370` | `#9AA1AC` | `#A0A0A0` |
| `accent` (focus, links, outlines) | `#4C8DFF` (5.5:1) | `#1B63D8` (5.0:1) | `#FFD400` (14.7:1) |
| `accent.fill` (primary buttons, white text) | `#2D6BD8` (5.0:1) | `#1B63D8` (5.5:1) | `#FFD400` with black text |
| `accent.subtle` (selected rows) | accent at 18% (`#202E47`, text 10.9:1) | `#DCE6F8` | `#4D4000` |
| `success` / `warning` / `error` / `info` | `#3FB950` / `#D29922` / `#F85149` / `#58A6FF` | `#1A7F37` / `#8A5D00` / `#CF222E` / `#0969DA` | `#5CFF7A` / `#FFB000` / `#FF6B6B` / `#00E5FF` |
| `play.tint` | `#E8912D` | `#B35900` | `#FFB000` |
| `viewport.selection` / `viewport.hover` | `#FF9F1A` / `#FFD08A` | same | `#00E5FF` / `#FFFFFF` |
| Axis X / Y / Z | `#F0524F` / `#7ACC29` / `#3D8DF5` | `#D1242F` / `#1A7F37` / `#0969DA` | `#FF5050` / `#50FF50` / `#5090FF` |
| Axis, colour-blind preset (Okabe-Ito) | `#D55E00` / `#009E73` / `#56B4E9` | `#D55E00` / `#009E73` / `#0072B2` | same as dark |

Users can pick the accent from presets (Azure is the default; Violet, Teal and Amber). Handles always carry axis letters, so colour is never the only cue.

**Metrics:**
- 4 px grid. Paddings and spacing: WindowPadding 10,10; FramePadding 8,5; ItemSpacing 8,6; ItemInnerSpacing 6,4; CellPadding 6,3; IndentSpacing 14; ScrollbarSize 12.
- Rounding: windows, child windows and popups 6; frames 4; tabs 5; grabs 4; scrollbars 9.
- Lines: 1 px separators instead of boxes; borders only on popups and tooltips; DockingSeparatorSize 2; the active tab gets a 2 px accent line.
- The style is rebuilt from tokens whenever theme, scale or DPI changes, never scaled cumulatively.

**Typography:**
- Roboto Regular 15 px for body text and 13 px for captions.
- Roboto Medium 15 px for headers, tabs and buttons; Bold 17 px for titles.
- Roboto Mono 14 px for the console, JSON, code and numeric columns.
- Material Symbols Rounded is merged at 18 px (with `GlyphOffset.y` tuned and `GlyphMinAdvanceX` 18) and pushed at 20 px in the toolbar and 32/48 px for tiles and the welcome screen. Dynamic fonts are rasterised on demand.
- UI scale runs from 75% to 200%, multiplied by the per-monitor content scale.

**Icons.** Every Component declares an icon in `TypeMeta`, for example:

| Component | Icon |
| --- | --- |
| Transform | `open_with` |
| MeshRenderer | `deployed_code` |
| Camera | `videocam` |
| DirectionalLight | `light_mode` |
| PointLight | `lightbulb` |
| SkyLight | `wb_twilight` |
| RigidBody | `weight` |
| Collider | `select_all` |
| CharacterController | `directions_walk` |
| Joint | `link` |
| AudioSource | `volume_up` |
| AudioListener | `hearing` |
| Outliner, AI agent | `account_tree`, `smart_toy` |

**Widget kit:**
- Component cards: icon, name, enable toggle, kebab menu, and a resizable two-column property table.
- Vec3 fields with coloured X/Y/Z chips and drag-scrub with modifiers and infinite drag.
- Toggle switches, segmented controls, icon buttons whose tooltips show shortcuts, and search fields with a clear button.
- Asset tiles with type badges, and breadcrumbs.
- HDR colour picker with separate intensity.
- Toasts with actions ("Deleted 12 entities. Undo").
- Empty states that guide the user ("Create entity · Drop a model · Ask your agent").
- Rich tooltips (description, unit, default value, docs link).

**Motion:** 120–180 ms ease-out for popups, toasts and camera transitions. It is time-based and allocation-free, and Reduce motion turns it off.

**Shell:**
- Menu bar and a 40 px toolbar: tools Q/W/E/R/T, space, pivot and snapping on the left; Play, Pause, Step and Simulate in the centre; layout, AI chip, notifications and search on the right.
- Docked panels: Outliner on the left (about 20%); Scene and Game tabs in the centre; Inspector, Rendering and Project Settings tabs on the right (about 25%); Assets, Console, History, Profiler and AI Activity along the bottom (about 28%).
- 24 px status bar: tasks, selection, revision and dirty marker, ms per frame, locale, agents, play state.
- While playing, the play tint marks the toolbar group, the viewport border and the status bar.

**Accessibility:**
- Full keyboard and gamepad navigation, a High Contrast theme, the colour-blind axis preset, UI scale, reduced motion, and hit targets of at least 20 px at 100% scale.
- Screen readers are not feasible with ImGui; this is documented.
- Theme tests enforce the contrast ratios above.

## 9. Tetris acceptance test

**Goal.** CI proves with no human and no LLM that the whole agent workflow works through the real MCP layer: create content, write C++, compile, fix a build error, play, verify the rules, hot reload with a layout change, and save. A nightly job proves it with a live agent.

**Contract** (`tests/acceptance/tetris/SPEC.md`; also the live agent's prompt):

1. Create the project with `project.create {template: "empty"}` in a fresh directory.
2. Write a module `tetris` (version 1.x) in `modules:/tetris`, using `illusion::core` with `USES input runtime`, and build it with `module.build`. Its reflected Components:

   | Component | Fields |
   | --- | --- |
   | `tetris.Board` | `width` U32 = 10, `height` U32 = 20, `gravitySeconds` F32 = 0.8, `origin` Vector3 |
   | `tetris.Cell` | `x` I32, `y` I32, `kind` Enum `tetris.Piece` (I, O, T, S, Z, J, L) |
   | `tetris.ActivePiece` | `kind`, `rotation` U32 (0–3), `x` I32, `y` I32. Four child entities tagged `tetris.ActiveBlock`. |
   | `tetris.PieceQueue` | `seed` U32, `forced` String: its characters are dealt first, then a 7-bag drawn with `illusion::Random` seeded from `seed` |
   | `tetris.Score` | `lines`, `points`, `level` U32, `gameOver` Bool |

3. Rules:
   - Cell (x, y) sits at `origin + (x, y, 0)`, with size 1 and y up.
   - A piece spawns with its bounding box at x = 3 and its top row at y = height − 1.
   - Rotation follows SRS with the standard wall kicks.
   - Gravity: one row per `gravitySeconds · 0.85^(level−1)`.
   - `soft_drop` moves one row per press; `hard_drop` drops and locks the piece.
   - Line clears score 100, 300, 500 or 800 × level; level = 1 + lines/10.
   - The game is over when a new piece overlaps a Cell.
   - Board state is derived from `tetris.Cell` entities on every update, never from module globals.
4. Input actions in `assets:/input/tetris.input` (all Button):

   | Action | Keyboard | Gamepad |
   | --- | --- | --- |
   | `tetris.move_left` | `Key.Left` | `Gamepad.DPadLeft` |
   | `tetris.move_right` | `Key.Right` | `Gamepad.DPadRight` |
   | `tetris.rotate_cw` | `Key.Up` | `Gamepad.South` |
   | `tetris.soft_drop` | `Key.Down` | `Gamepad.DPadDown` |
   | `tetris.hard_drop` | `Key.Space` | `Gamepad.North` |

5. Content:
   - Materials `assets:/materials/{i,o,t,s,z,j,l}.material` (cyan, yellow, purple, green, red, blue, orange) plus `frame.material`.
   - Startup scene `assets:/scenes/main.scene` containing:
     - an orthographic Camera with the highest priority, framing the board
     - a DirectionalLight and a SkyLight using `builtin:sky/procedural`
     - walls and floor made of `MeshRenderer {builtin:mesh/cube, frame}`
     - one Board, one PieceQueue and one Score
   - Locked cells are children of the Board, with `MeshRenderer {builtin:mesh/cube, <piece>}`.

**Transcripts and harness:**

- `scripted.jsonl` is maintained by us. It grows from M7j onwards (board, then GPU checks, play, module build, migration) and uses our reference sources in `reference/`. In M7x, `recorded.jsonl` and `files/` are added: the normalised session of a real agent.
- Each step has the form `{call, params, capture: {var: pointer}, expect: matcher, requires: "gpu", otherwise: matcher}`.
  - Matchers: subset, approx, regex, length, exists, oneOf.
  - Captured values replace recorded ids through `${var}`.
  - Special steps: `{restart: true}` and `{frames: n}` (which calls `play.step`, never a wall-clock wait).
- Steps that need a GPU still run under `--no-gpu` and must then match `otherwise` (error 1400). Nothing is skipped.
- The runner is `illusion_ctl replay --via-mcp --era legacy|modern`. It spawns `illusion_mcp --launch --headless [--no-gpu] --deterministic --seed 1 --permissions auto --project <temp>`, then `illusion_ctl check checks.jsonl` runs.
- Normalisation (`tools/acceptance/normalize_transcript.py`) turns ids into captures, extracts file payloads to `files/` (clang-formatted), strips timings and optionally drops discovery chatter. Reviewers read the diff. A transcript break is a protocol change and needs a version note.

**Checks** (`checks.jsonl`, the same for replayed and live runs):

| Check | What it verifies |
| --- | --- |
| **C1 Project** | `project.illusion`, `assets:/scenes/main.scene`, 8 materials, the `.input` asset and `modules:/tetris` exist. |
| **C2 Module** | `tetris` is loaded with semver 1.x and no error diagnostics. `component.schema` matches the contract (extra fields are allowed). |
| **C3 Scene** | Exactly one orthographic Camera with the highest priority. At least one DirectionalLight and one SkyLight that resolves. One Board (10x20), one PieceQueue and one Score. Every MeshRenderer reference resolves. `scene.validate` reports no errors. |
| **C4 Persistence** | After `{restart}`, the `scene.info` content hash is unchanged and a re-save is byte-identical. The MeshRenderer, SkyLight and material references survive (this proves the ADR 0022 follow-up). |
| **C5 Behaviour** | Each scenario sets edit-world values, calls `play.start {deterministic, seed: 1}`, then `input.inject` plus `play.step`, then queries, then `play.stop`. Scenarios: spawn (`forced: "T"`: T at x = 3, rotation 0, four blocks); wall (six move_left presses keep every block at x ≥ 0); kick (an I at the right wall rotates and stays inside); hard drop (four Cells on the floor and a new piece); single clear (row 0 prefilled at x 0–8, a vertical I in column 9: lines 1, points 100, three I cells shifted down); tetris (rows 0–3 prefilled: lines 4, points 800); gravity (one row after ceil(0.8·60)+1 steps); game over (`forced: "OOOOOOOOOOOO"` dropped in one column ends within 12 drops); the wall scenario again through `Gamepad.DPadLeft` on a virtual gamepad and through `Key.Left` press and release. After stop, the edit World's revision and hash are unchanged. |
| **C6 Rendering** (GPU) | `render.stats` reports at least as many visible objects as cells plus walls and 0 validation errors. For three locked cells, `viewport.project` then `viewport.pick` returns the same guid. `viewport.sample` gives a hue within 25° of the material's colour. The screenshot is not uniform. For the scripted transcript only, the 640x360 game view matches `golden/board.png` (per channel ≤ 3, at most 0.5% of pixels differing). |
| **C7 Hot reload** | During play, `tetris.Score` gains a `combo` field before `points`. `module.build` and `module.reload` return a `MigrationReport` that keeps `lines` and `points` and defaults `combo`. Play continues. |
| **C8 Hygiene** | No Error log entries except the expected build failure. No assert. Every mutating transaction has origin `rpc:illusion_mcp`, which is what "no human input" means. |

**CI wiring.** Label `acceptance`, Release configuration only.

| Job | What runs |
| --- | --- |
| Windows MSVC, Windows clang-cl, macOS, Linux GCC, Linux Clang | Scripted (legacy era) and recorded (modern era), `--no-gpu` |
| ASan | Both transcripts |
| TSan | Scripted |
| gpu (lavapipe, under `xvfb-run`, validation on) | Both transcripts with C6 |

The time budget is at most 6 minutes per job; module builds dominate. On failure the report shows the first diverging step, an expected-versus-actual JSON diff, the last 100 log lines, and the project directory as an artifact.

**Live agent** (`.github/workflows/agent-acceptance.yml`; nightly on `main` plus `workflow_dispatch`; never on pull requests or forks):
1. Build on Linux with lavapipe and install a pinned Claude Code CLI.
2. Run `claude -p` with `SPEC.md`, `--mcp-config tests/acceptance/tetris/mcp.json` (`illusion_mcp --launch --headless --record`), `--allowedTools "mcp__illusion__*"` only (no Bash, Edit or Write, so all work goes through the editor), a turn limit and a 60-minute timeout. Secret: `ANTHROPIC_API_KEY`.
3. Run the same `checks.jsonl`.
4. Upload the transcript, project, screenshots and a summary. A failure opens or updates an issue. Successful transcripts can be promoted after review.

## 10. ADRs

| ADR | Title | PR | Supersedes or amends |
| --- | --- | --- | --- |
| 0028 | Non-throwing public JSON API over private yyjson | M7a | — |
| 0029 | Localization: stable keys, compiled English, data-file locales, MessageFormat subset | M7b | — |
| 0030 | Separate editor tree, command-first editor and the export boundary | M7d | — |
| 0031 | Editor UI stack: Dear ImGui docking, custom backends, gamma-space UI target, theme, fonts, icons | M7d | — |
| 0032 | Reflection v2: metadata, struct reflection, JSON value API, migrations | M7e | extends 0022 |
| 0033 | Persistent entity ids, names and scene format v2 | M7f | 0022 in part (unknown types, index references) |
| 0034 | Transform hierarchy with world-space truth, Disabled/Inactive | M7g | — |
| 0035 | Asset references by virtual path | M7h | closes 0022's consequence; keeps 0019 |
| 0036 | Typed editor command core, transactions, undo, journal, projects | M7i | — |
| 0037 | Editor RPC protocol, transports and security | M7j | — |
| 0038 | Agent acceptance tests from replayable transcripts | M7j | — |
| 0039 | Dual-era MCP bridge with tools generated from commands | M7k | — |
| 0040 | Renderer extension points for editors and tools | M7n | 0017 in part (`MotionHistory`) |
| 0041 | Module API v3: lifecycle, multi-world host, host service interfaces | M7q | 0027 in part (one World, `LINK` guidance) |
| 0042 | Play mode through scene snapshots and a shared GameSession runtime | M7r | — |
| 0043 | Project modules built against a build-tree package | M7s | — |
| 0044 | In-place Component layout migration on hot reload | M7t | 0027 in part (layout-conflict rollback) |
| 0045 | Asset catalog, import settings sidecars, live reload, GPU resource lifetime | M7u | — |
| 0046 | Prefabs as instance records with field-level overrides and nesting | M7v | — |

Each ADR gets a row in both `architecture/decisions.md` pages. A partially superseded ADR gets a status note, for example "Accepted; MotionHistory superseded by 0040".

## 11. CI and tooling changes

- **New jobs:**
  - **Runtime-only build** (M7d).
  - **Agent acceptance**, nightly and manual (M7x).
- **gpu job:** runs `-L "gpu|display"` under `xvfb-run` (M7c) and uploads golden and screenshot artifacts and diffs (from M7d).
- **New CTest labels:**
  - `display`
  - `editor` (`illusion_editor_tests`)
  - `acceptance` (Release only)
  - `module_build` (Release, `RUN_SERIAL`)
- **New test executables:** `illusion_editor_tests`, `illusion_editor_gpu_tests`.
- **New benchmark executable:** `illusion_editor_benchmarks`.
- **Format job:** adds `tools/check_layering.py`.
- **Docs job:**
  - Doxygen INPUT gains `engine/modules/include` (M7a), `engine/text/include`, `engine/runtime/include` and `editor/*/include`, with `GENERATE_XML` on.
  - From M7x, `tools/check_api_coverage.py` runs too.
- **Build-job CTests:** `text.locales` and `editor.command_reference`.
- **Repository instructions and skills:**
  - New skills: `localization`, `editor-ui`, `editor-commands`, `game-modules`.
  - `ecs-components` is updated.
  - AGENTS.md gains the new map entries and rules: the editor boundary; validate before any World call; editor processes write only protocol on stdout; UI text through `tr()` with `###id`.
- **Golden images** live in `tests/data/golden/**`. `ILLUSION_UPDATE_GOLDENS=1` regenerates them, and tolerances absorb drift between Mesa versions on the pinned runner image.

## 12. Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| An in-process module crash or `ILL_CHECK` kills the editor | Validation before every World call, fuzzing in CI, journal every frame, autosave, crash.json, bridge relaunch with recovery; out-of-process play is an open question |
| MCP spec churn and differences between Claude Code's two runtimes | A pure library with golden transcripts per era; semver for our own protocol |
| ImGui 1.93.0 is days old | The 1.92.9b fallback is pinned in ADR 0031; re-check for 1.93.x before M7d merges |
| Layout migration changes the core ECS (archetype purge) | Rare path, purge resets caches, ASan and TSan coverage, rollback snapshot, `ecs/iterate` budget gate |
| Scene and asset formats are hard to reverse | v1 reader kept, golden files, path-based refs readable later as `{path, uid}`, ADRs reviewed in M7f and M7h |
| CI time (module builds, lavapipe goldens) | Release-only acceptance, transcripts split across eras, shared module build directories, small goldens (640x360) |
| GPU memory per viewport (64 MiB shadow map each at default settings) | Viewports render on demand and are created lazily; secondary viewports use cheaper shadows |
| Colour pipeline errors (double gamma) | The single UI-target convention from ADR 0031; tests that sRGB and UNORM present paths give identical captures |
| Per-frame allocations in immediate-mode UI | Counting allocator, `FrameText` arena, allocation tests per idle and drag frame |
| Agents compile and load arbitrary C++ | Permission classes, ask mode in the GUI, confinement to project mounts, no generic shell, clear docs |
| Golden drift and flaky display tests | Pinned runner image, tolerances, event-driven waits with failure ceilings, no sleeps |
| Several PRs are large (M7d, M7i, M7w) | Planned split points written into each PR's scope |
| Wayland limits (window positioning, key names) | APIs report unsupported; documented; multi-viewport deferred |
| No GPU runner for macOS or Windows | Headless and `--no-gpu` tests everywhere; GPU checks on Linux lavapipe; macOS packaging tracked in M8 |

## 13. Decisions needed from the product owner

1. **Turkish UI locale.** Ship `tr.json` in M7, or English plus en-XA only? *Recommendation:* English plus en-XA in M7, and `tr` as a data-only follow-up after the M7x string freeze.
2. **Accent colour.** Is Azure on graphite the signature look, or is a brand colour wanted (Violet, Teal)? Review this on the M7d gallery artifact.
3. **Roboto Mono pin.** Roboto Mono is pinned at its last Apache-2.0 commit; the alternative is Go Mono (BSD-3, an older look). *Recommendation:* Roboto Mono.
4. **Icon font size.** The variable Material Symbols font is 15.2 MB and editor-only; the static Material Icons font is 356 KB but older and smaller. *Recommendation:* the variable font.
5. **Live agent CI.** Approve the `ANTHROPIC_API_KEY` secret, a budget, a turn limit and the model, and decide whether failures open issues automatically.
6. **Crash isolation.** Is in-process play with journal recovery acceptable for M7, with out-of-process play scheduled for M8 or later?
7. **Detachable OS windows** (ImGui multi-viewport, not available on Wayland). After M7 behind a preference, or required now?
8. **Editor extensions from game modules** (panels, inspectors, commands exposed over MCP). *Recommendation:* after M7, with its own ADR, because ImGui contexts across module boundaries need a design.
9. **Remote agents.** Is MCP Streamable HTTP (agents in containers or on other machines) needed, and when? *Recommendation:* not in M7.
10. **Default permission mode in the GUI.** The plan uses "ask" for destructive, files, build and system calls. Confirm, or choose "auto".

## 14. Definition of done for every M7 PR

- Debug and Release build with zero warnings on Windows MSVC, Windows clang-cl, macOS, Linux GCC and Linux Clang.
- All tests pass, including ASan, TSan, the gpu job (Vulkan validation errors 0) and the runtime-only job.
- Benchmarks are within budget, and the budgets are set from measurements.
- Format, layering, docs parity, `mkdocs build --strict`, Doxygen (no undocumented public API) and locale checks all pass.
- EN and TR docs are updated in the same PR. Code blocks are identical in both languages, Turkish keeps technical terms in English, and new nav titles get `nav_translations`.
- ADRs move to Accepted in the implementing commit. Partially superseded ADRs get status notes. THIRD_PARTY_NOTICES and ROADMAP are updated.
- New public headers have `///` comments. Engine code throws no exceptions. Recoverable failures are return values. Hot paths are allocation-free and instrumented with `ILL_PROFILE_SCOPE`.
- UI PRs attach golden screenshots for review. Every UI path goes through commands, and the parity test passes.
- The pre-commit-review skill checklist is complete. Commits follow Conventional Commits, one logical change per commit.
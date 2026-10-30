# Roadmap

Milestones are delivered in order; each one ends with tests, benchmarks, bilingual documentation and
ADRs for its significant decisions. Items marked ✅ are done.

## M0 — Foundation ✅

- ✅ Repository, CMake presets, pinned dependencies (FetchContent + SHA-256)
- ✅ CI: Windows (MSVC, clang-cl), Linux (GCC, Clang, ASan/UBSan, TSan), macOS; format, docs, Doxygen
- ✅ Core: platform macros, assertions, logging with sinks, thread utilities, hashing, type names
- ✅ Memory: aligned allocation, `LinearAllocator`, `PoolAllocator`, `InplaceFunction`
- ✅ Job System: lock-free bounded queue, counters with helping waits, `parallelFor`
- ✅ ECS: archetypes with 16 KiB SoA chunks, tags, cached queries, chunk and parallel iteration,
  `CommandBuffer`
- ✅ Tracy integration, benchmark harness with budgets
- ✅ Documentation site (EN/TR, parity check), ADRs 0001–0006, AGENTS.md and skills

## M1 — Platform and first pixels ✅

- ✅ Window and keyboard/mouse input (GLFW), headless backend for tests
- ✅ High-resolution timer, frame clock, fixed timestep
- ✅ File system abstraction (virtual paths, atomic writes, platform user-data directories)
- ✅ Event bus (immediate and queued delivery)
- ✅ NVRHI device creation on Vulkan 1.3 with a run-time loader; swapchain with resize and vsync control
- ✅ Slang shader pipeline (build-time SPIR-V, embedded); diagnostic triangle and `hello_triangle` sample
- ✅ Vulkan + NVRHI validation in Debug; GPU tests on Mesa lavapipe in CI (offscreen and swapchain under Xvfb)
- macOS: bundle and verify MoltenVK (no GPU CI runner yet; tracked for M8 packaging)

## M2 — Input ✅

- ✅ SDL3 gamepad subsystem: Xbox and DualSense, hot-plug, player slots, rumble, trigger rumble, LED, gyro,
  touchpad, adaptive triggers; virtual gamepads for tests and automation
- ✅ Action-mapping layer (button/axis/vector actions), rebinding capture, text persistence, per-device glyphs
- `input_viewer` sample for manual verification with real controllers

## M3 — Renderer core ✅

- M3a: ✅ glTF 2.0 import (meshes, materials, textures), forward PBR, reversed-Z, parallel frustum culling,
  instancing, `gltf_viewer` sample (ADR 0013)
- M3b: ✅ IBL from HDRIs (Poly Haven): GPU-baked background and prefiltered specular cube maps, SH
  irradiance, multiple-scattering split sum, `SkyLight`, procedural sky (ADR 0014)
- M3c: ✅ CSM + PCSS soft shadows: up to 4 stable, texel-snapped cascades, per-pass culling masks,
  alpha-masked casters, blocker search sized by the light's angular diameter (ADR 0015)
- M3d: ✅ HDR pipeline with depth prepass, GTAO, energy-preserving bloom, ACES tonemapping, optional TAA
  (camera reprojection, variance clipping) (ADR 0016)
- M3e: ✅ Two-phase GPU occlusion culling with indirect draws, per-object motion vectors for TAA, GPU
  frame-time benchmarks (ADR 0017)
- M3f: ✅ Bindless materials and textures, shared geometry pools, multi-draw indirect where supported
  (ADR 0018)

## M4 — Assets and streaming ✅

- M4a: ✅ Binary serialization, stable asset ids, cooked image and model formats, import cache validated by
  source content hashes (xxHash64) (ADR 0019)
- M4b: ✅ Async streaming (dedicated I/O thread, Job System), shared asset handles published in update(),
  GPU upload queue with a byte budget (ADR 0020)
- M4c: ✅ Packed archives (`.illpak`, memory-mapped, hash-checked), VFS archive mounts, `illusion_pack` with
  asset cooking, shipping without sources (ADR 0021)

## M5 — Gameplay services

- M5a: ✅ Component reflection with stable, explicit names; JSON scene files (yyjson); type-erased World
  access; semantic versions (ADR 0022)
- M5b: ✅ Jolt Physics on the Job System: RigidBody/Collider Components, fixed step with interpolation,
  forces and impulses, raycasts, contact events, collision layers, `physics_playground` sample (ADR 0023)
- M5c: ✅ Audio with miniaudio: AudioSource/AudioListener Components, 3D attenuation, panning and doppler,
  decoded and streamed clips through the VFS (WAV, FLAC, MP3, Ogg Vorbis), mixer buses, voice priorities,
  allocation-free mixing on an audio thread fed by lock-free snapshots, offline rendering for tests
  (ADR 0024)
- M5d: ✅ Physics extensions: triangle mesh and convex hull colliders, character controller (steps, slopes,
  platforms, pushing), fixed/point/hinge/slider/distance joints with limits, motors and break forces, shape
  casts and overlaps (ADR 0025)
- M5e: ✅ Audio extensions: clips loaded in the background through the asset streamer (`loadFile`),
  per-sound low-pass filters, bus low/high-pass filters, a room reverb fed by per-sound sends, sound cones,
  contact events with impact speeds and impact sounds in `physics_playground` (ADR 0026)

## M6 — Game modules and hot reload ✅

- ✅ Module/plugin system with semver and dependencies; a module is a hot-reloadable shared library in
  development and linked statically in exports (`illusion_add_module`, `ModuleHost`) (ADR 0027)
- ✅ Service injection into modules (ADR 0003): Logger, ComponentRegistry, assert handler, thread and
  worker indices; module-owned component types handed over to the new code on reload, dropped types
  stripped from the World, failed reloads rolled back
- Reflection-based migration of module Components whose layout changed, so that such a change survives a
  hot reload instead of needing a restart (M7t)

## M7 — Editor and AI control

Plan: `docs/plans/m7-editor.md` (sub-milestones, command API, localization, theme, acceptance test).

- M7a: ✅ Core toolkit for tools: non-throwing JSON API with canonical writing and arena allocation,
  `Expected`, deterministic `Random` and secure random bytes, log ring sink, stderr-only console, World
  structural version (ADR 0028)
- M7b: ✅ Localization runtime (`illusion::text`): UTF-8, search folding and fuzzy matching, CLDR plural rules,
  MessageFormat subset, string tables with compiled English, pseudo-locale, `illusion_textc` and the locale
  check, translatable input glyph labels (ADR 0029)
- M7c: ✅ Platform services: clipboard, cursor shapes, window state and icons, IME placement, file drops,
  content-scale and move events, monitors, `wake()`, layout-aware key names; display tests
- M7d: ✅ UI foundation: editor tree and export boundary (configure-time check, layering check, runtime-only
  CI build), Dear ImGui docking with NVRHI and platform backends, gamma-space UI target, theme tokens with
  contrast tests, fonts and generated icon constants, per-frame localized labels with stable ids, widget kit
  v1, `UiDriver`, `illusion_ui_gallery` with golden-image tests (ADRs 0030, 0031)
- M7e: ✅ Reflection v2: field and type metadata (units, ranges, hints, icons, required Components,
  transient runtime state), struct reflection with the engine settings structs, I8/I16/U8/U16, Struct,
  FixedArray, Array, Flags, Optional and Json fields, `FieldValue`, the JSON value API (merge/replace,
  dotted paths, JSON Pointer errors, unit-tagged numbers, sRGB colours, u64 as strings), migrations and
  `renamedFrom`, scenes rebuilt on it with byte-identical v1 output (ADR 0032). The `Asset` field type
  and `FieldMeta::assetKind` come with `AssetRef` in M7h
- M7f: ✅ Entity ids, names and scene format v2: `EntityGuid` and `Name` Components, `GuidGenerator`
  (secure key or seeded), `assignMissingGuids()`, `GuidEntityResolver`, field validators,
  `World::forEachMatching()`; version 2 scene files with ids and id references, deterministic order,
  settings, preserved unknown Components and settings, `IdPolicy::Keep`/`Regenerate`; version 1 files still
  load (ADR 0033, which supersedes ADR 0022 in part)
- M7g: ✅ Transform hierarchy, `Disabled`/`Inactive`, spatial math: `Hierarchy` and `LocalTransform`
  Components with world-space `Transform` as the truth, `setParent` (keep world or local), `reorder`,
  `detach`, `destroyRecursive`, `HierarchyIndex` (CSR child lists, pre-order, levels; rebuilt only on
  hierarchy changes through the new `World::componentVersion()`), parallel `updateTransformHierarchy()`,
  `WorldSpaceAuthority` for dynamic bodies and characters, `Disabled`/`Inactive` honoured by the renderer,
  physics and audio, scenes saved in hierarchy pre-order without derived child Transforms, `spawnModel()`
  keeping glTF nodes and names, and `Math.hpp` rays, intersections, `screenToRay`,
  `orthographicReversedZ` and Euler conversions (ADR 0034)
- M7h: ✅ Asset references in scenes: `AssetId`, virtual paths and the thread-safe `AssetPathTable` in core
  (shared with modules, module API v2), `AssetRef` fields (`FieldType::Asset`, `FieldMeta::assetKind`),
  sub-asset fragments (`#mesh/N`, `#material/N`, `#node/name`) and `builtin:` meshes, material and
  procedural sky; saved `MeshRenderer`/`SkyLight`/`ProceduralSky` with runtime `RenderMesh`/`RenderSky`,
  `RenderAssetResolver` and a shared `RenderAssetCache` (parallel steady-state check, no allocations),
  `.material` files, `PhysicsAssetResolver` for collision meshes, `AudioSource::clip` as an asset
  reference, `SceneLoadResult::missingAssets` (ADR 0035). Not yet: freeing GPU memory of cache entries no
  resolver uses (`RenderResources` cannot destroy resources yet); it comes with live reload (M7u)
- M7i: ✅ Headless editor core (`illusion::editor_core`, ADR 0036): projects (`project.illusion`, mounts,
  `empty` and `3d-starter` templates), VFS file management (`listRecursive`, `stat`, `createDirectory`,
  `remove`, `rename`, `copy`), `EditorContext`, `EntityIndex` (ids, paths, `name:`, `$selection`),
  `SceneDocument`, `Selection`, typed commands with reflected params and results, JSON Schema 2020-12,
  validation with JSON Pointers and suggestions, `WorldEditor` primitives recorded as reversible operations,
  `UndoStack` (merge keys, 256 MiB budget, selection restore), `ChangeJournal` and `EventHub`, a crash
  journal with `project.recover`, autosave, and 46 commands in the `editor`, `project`, `scene`, `entity`,
  `component`, `hierarchy`, `selection` and `history` modules with a generated reference
  (`editor.command_reference`) and a schema golden file
- M7i-2: ✅ The remaining command modules (72 commands in all): `transform` (get, set, translate, rotate,
  scale, reset; world, parent and own axes, individual, center and point pivots, Euler input in degrees),
  `clipboard` (copy, cut, paste of scene v2 subtrees with new ids and world placement), `asset` (list, info,
  create_folder, move with reference fix-up, delete with a reference check, references, dependencies) over a
  reflection-based `ReferenceIndex` of scenes and materials, `material` (create, get, set), `log` (read,
  clear, set_level over a log ring), scene and project settings through a `SettingsCatalog` (`SettingsChange`
  operations, `illusion.settings` files), scene settings kept on save, world Transforms of children derived
  after every change, and the `editor-commands` skill patterns (ADR 0036 addendum)
- M7j: ✅ JSON-RPC 2.0 over NDJSON (ADR 0037): `Process` (spawn without a shell, bounded stdio buffers,
  timeouts, termination), `illusion::editor_rpc` (framing, conformance errors, stdio and `AF_UNIX` transports,
  endpoint file and owner-only permissions, 256-bit token authentication, permission modes, connection,
  in-flight and queue limits, the main-thread pump, `clientToken` idempotency, push and pull events,
  `rpc.discover` as OpenRPC 1.3), the headless `illusion_editor`, `project.create` and `project.open` inside
  a workspace, `scene.info` content hashes, `illusion::editor_client` (`RpcClient`, launcher,
  `TranscriptRunner` with captures and matchers), `illusion_ctl` (`call`, `batch`, `subscribe`, `replay`,
  `check`, `dump-catalog`), the Python client and the acceptance skeleton with the board scenario (C1, C3, C4;
  ADR 0038)
- M7j-2: ✅ Transactions that span commands (`transaction.begin/commit/cancel`, owned by a caller: one per
  RPC connection, cancelled when it closes; trial transactions for dry runs), `editor.batch` with `$`
  references and atomic or partial runs, `history.undo {scope: "mine"}` past independent later steps
  (`findOverlap()`, `conflict` otherwise), async commands (`addAsync`, `RunningCommand`, `editor.wait`) with
  `rpc.progress` and `rpc.cancel`, permission checks for every batch step (`CommandGate`), `commands.changed`
  with a rebuilt method catalogue, and protocol version 0.2.0 (ADR 0036 and 0037 addenda)
- M7k: ✅ MCP bridge `illusion_mcp` (ADR 0039): the pure `illusion::mcp_protocol` library serving both MCP
  eras (legacy `initialize` with 2025-06-18 and 2025-11-25, stateless 2026-07-28 with `server/discover`,
  `resultType`, `ttlMs`/`cacheScope`, `subscriptions/listen` and -32022), tools generated one to one from the
  `rpc.discover` catalogue (names, descriptions with parameter documentation, schemas, annotations, `--tools`
  filters, `illusion_call`/`illusion_batch`, `list_changed`, pagination), results with `structuredContent`, 4 KB
  text summaries, PNG image content with a size cap and flat `isError` objects, progress and cancellation
  mapped to `rpc.progress`/`rpc.cancel`, resources and the `illusion://components/{stableName}` template,
  prompts and instructions; `illusion_mcp` with `--auto`/`--attach`/`--launch`, `--record`, stderr-only
  logs, crash reports with the editor's last log lines and relaunch with `project.recover`;
  `editor.set_client`/`editor.clients`/`editor.activity` (protocol 0.3.0); `.mcp.json` from `project.create`;
  `illusion_ctl replay --via-mcp --era legacy|modern`; journals of crashed deterministic sessions are kept.
  Not yet: the `illusion://sdk/{header}` and `illusion://manual/{page}` resources (they need the `sdk:` mount
  and shipped manual pages: M7s and M8 docs), normalised recordings for the nightly live-agent job (M7x)
- M7l: ✅ Editor shell (`illusion::editor_shell`): the windowed `illusion_editor` (main loop woken by the RPC
  server, swapchain present through `OffscreenUi`, `--headless` renders the same UI offscreen for screenshots);
  UI actions with a parity test (every menu item, toolbar button and shortcut runs a command; actions of later
  milestones are disabled and name them); menu bar, toolbar, status bar, toasts and a notification centre; five
  built-in `DockBuilder` layouts plus named per-user layouts, a project default (`settings/editor_layout.ini`)
  and Reset Layout; the command palette and Go to Anything (fuzzy on localized and English titles, `@`
  entities, `>` preference pages, forms generated from params schemas); Console (level filters, search,
  collapsing, timestamps, source links, command line); History (origin badges, jump, undo an agent's
  changes); AI Activity (clients, eras, call feed with params, durations, results and undo links, pause and
  disconnect, permission mode); approval prompts for the `ask` mode (allow once, for the session, deny;
  batches ask once; RpcServer `approvals()`/`decide()`, ADR 0037 addendum); Preferences (appearance, language
  with en-XA and locale files, keymap presets illusion/Unity/Unreal/Blender with conflict detection and
  rebinding capture, AI agents, autosave, external tools) in `preferences.json`; Welcome window with recent
  projects, templates and the workspace folder browser; About and the third-party licence viewer; the `ui.*`
  commands, `preferences.get/set` and `editor.screenshot` (91 commands). Cut to later: gamepad navigation of
  the window through `GamepadSystem` (M7w), follow-agent (done in M7o-3), launching External Tools
  (M7s), `#` asset search in the palette (M7u)
- M7m: ✅ Outliner and Inspector: Outliner (`HierarchyIndex` row cache rebuilt on hierarchy, selection or
  view-state change, `ImGuiListClipper`, multi-select with Ctrl/Shift and keyboard navigation, F2 inline
  rename, drag-and-drop reparenting and sibling reordering with `keepWorld` or Alt `keepLocal`, context menus,
  search with `has:`/`t:`/`disabled:`/`prefab:` filters, per-session eye and lock toggles via `ViewState` and
  `view.get`/`view.set_entities`); Inspector (entity header with enabled toggle, name, lock and GUID copy;
  Local/World Transform card with per-entity Euler continuity cache; metadata-driven Component cards for every
  `FieldType` and `FieldHint`, unit conversions and suffixes, Shift ×10 / Alt ×0.1 drag speed and infinite
  drag via `CursorMode::Captured`, drag `mergeKey` undo coalescing, per-field reset via `component.reset`
  `path`, nested `FieldPaths` in `component.get`/`component.set_field`/`component.reset`, custom drawers for
  `Collider`/`Joint`/`AudioSource`, inline validation with fix-it buttons, multi-entity editing with `—` for
  mixed values, Component clipboard `component.copy`/`component.paste`, Add Component popup with fuzzy search
  and automatic `requiredComponents`, and Inspector pin/lock; 95 commands). Cut to later: viewport entity
  eyedropper (done in M7o-3), asset thumbnails in picker (M7u), multiple simultaneous Inspector
  instances (M7w)
- M7n: ✅ Renderer extension points (ADR 0040): per-view motion history without World mutation;
  optional Entity index/generation IDs for opaque, masked and blended objects; bounded event-query
  readback; depth-sampled DebugDraw with pixel-width quads; Camera v2 with orthographic projection and
  priority; inverse-projection shadow fitting; layer/hidden filters; diagnostic views, wireframe and
  optional GPU pass timings. CPU/GPU regressions and three image goldens cover the extension contract;
  the 100k-line recording Benchmark has a 20 ns/line budget.
- M7o-1: ✅ Viewports: `ViewportHost` in the core with GPU-free camera math, `illusion::editor_viewport`
  (`EditorCamera` orbit/pan/dolly/fly with eased moves and framing, `ResizePolicy`, `ViewportService` with one
  `SceneRenderer` per viewport, the adaptive `Grid.slang` and the ID-based `Outline.slang` overlays, asynchronous
  5x5 picking with front-to-back cycling that skips hidden and locked entities), the Scene panel with its
  shading/overlay/view toolbar and click selection, and the `viewport.list`/`camera_get`/`camera_set`/`view`/
  `frame`/`set_mode`/`set_overlays`/`project`/`pick` commands (104 commands). CPU navigation tests, GPU pick,
  cycle and outline tests, two overlay goldens and the `viewport/camera-update` (200 ns budget, 20 measured) and
  `viewport/pick-candidates` (1 µs budget, 127 ns measured) Benchmarks. Fix: `SceneRenderer` keeps the targets a resize replaces
  until the GPU has finished the frames that used them (an empty scene's clears did not keep them alive).
- M7o-2: ✅ Viewports, second part: the Game panel (the scene's main Camera, no editor overlays or editor-only
  entities, an empty state without a camera), aspect presets and fixed resolutions with letterboxing, up to three
  secondary Scene views with reduced shadows (`viewport.create`/`close`), camera bookmarks (Ctrl+1-9,
  `camera.bookmark_set`/`go`), box select (`viewport.pick_rect`), `viewport.screenshot` (PNG as an MCP image)
  and `viewport.sample` on a shared readback queue that waits for streamed assets, and `render.stats`/
  `gpu_info`/`diagnostics` (115 commands). Transcripts gained `$min`/`$max` and golden-image steps
  (`ImageCompare`), and acceptance C6 checks the game view against `golden/board.png` with picks and samples of
  the board (`acceptance.tetris.board.gpu`). Fix: the viewports publish streamed loads every frame (a `.material`
  stayed loading in a running editor).
- M7o-3: ✅ Viewports, third part: the view cube and axis navigator (GPU-free `ViewCube` math: faces, picking of
  faces, edges and corners, cameras for them; click for a view, drag to orbit), navigation presets
  (`viewportNavigation`: keymap/illusion/unity/unreal/blender, with Unreal's walk and Blender's middle-button
  orbit), a hover outline from an asynchronous id readback, icon billboards for cameras, lights and audio
  (click to select), DebugDraw visualisers (lights, camera frusta, colliders and sensors, characters, joints with
  limits, audio distances and cones; selected or all, never hidden or inactive) with a picture-in-picture
  preview of the selected Camera, a stats overlay, `viewport.set_overlays` for all of them, following an
  agent's selection (`followAgent`, from M7l) and the Inspector's entity eyedropper (from M7m); still 115
  commands. CPU tests for the cube, presets, visualisers and agent selection, GPU tests for hover, frusta, icons
  and the preview, and the `viewport/visualisers-record` Benchmark (500 ns per object, 396 measured).
- M7p-1: ✅ Gizmos, first part: the GPU-free `Gizmo` math in the core (Translate with axes, planes and the view
  plane; Rotate with axis rings, the view ring and a trackball; Scale along the active entity's axes or
  uniformly; Universal; constant screen size, hit tests with a tolerance that scales with the display, drag
  planes that stay stable at grazing angles, axes and planes that fade when they cannot be dragged, unwrapped
  angles), World/Local/View axes, Median/Active/Individual pivots, move, angle and scale snapping with Ctrl
  inverting it, Dear ImGui drawing with dimmed back halves of rings, axis letters and the colour-blind axis
  colours, drags as `transform.set` calls inside one transaction (one undo step; Esc or the right button
  restores exactly; hierarchy-aware), the toolbar's Q/W/E/R/T tools, axes, pivot and snapping controls, and
  `gizmo.get`/`set_tool`/`set_space`/`set_pivot`/`set_snap` and `transform.snap_to_grid` (121 commands). Hit
  tests at 192 poses (perspective and orthographic, 1x and 2x, 10 km from the origin), drag and snapping tests,
  headless UI drags, two gizmo goldens and the `gizmo/hit-test` Benchmark (2 µs budget; 297 ns
  measured for the hit test, 1,384 ns with the shape)
- M7p-2: ✅ Gizmos, second part: typed numbers during drags and modal transforms (`GizmoTypedInput`: three
  fields, Tab, sign, exact distances, angles and factors per handle), Blender-style modal move, rotate and scale
  (`GizmoModal`: Shift+W/E/R, G/R/S in the Blender keymap; X/Y/Z constrain to world axes, again to local axes,
  Shift to planes; Enter or a click confirms, Esc or a right click cancels; one undo step), the bounds-centre
  pivot, drop to ground on End (render bounds from `ViewportHost::meshBounds()`, the ground plane when nothing is
  below; physics raycasts from M7w), align to view (Ctrl+Shift+F), align and distribute (GPU-free math in
  `Arrange.hpp`), `gizmo.modal`, `transform.drop_to_ground`, `transform.align_to_view`, `transform.align` and
  `transform.distribute` (126 commands). Unit tests for typed input, modal constraints, snapping and the
  arranging math, command tests with fake mesh bounds, a headless UI test of Shift+W, X, typed numbers, Enter,
  Esc, a confirming click and End, a GPU test that drop to ground lands on a floor, and the `gizmo/modal-update`
  (500 ns budget; 80 ns measured) and `arrange/drop-to-ground` (10 µs budget for 1,000 surfaces; 1,091 ns
  measured) Benchmarks
- M7p-3: ✅ Gizmos, third part: vertex snap (hold V during a move: the vertex nearest to the cursor within 12
  points, searched on the CPU in a `VertexSnapGrid` per Mesh, a uniform grid in the Mesh's own space visited
  slice by slice along the cursor ray), surface snap (Ctrl+Shift: the depth of an 8x8 probe render around the
  cursor without the selection, the normal rebuilt from neighbouring depths) with align to normal (the active
  entity's +y onto the normal), snapped moves constrained to the dragged axis or plane, the Measure tool (M:
  a ruler between two snapped points or on the ground plane, distance, x/y/z and elevation; Esc removes it),
  `ViewportHost::requestSnap()`/`snap()`, `viewport.snap` (async, read-only), `gizmo.measure` and
  `gizmo.clear_ruler` (129 commands). Unit tests for the grid against a brute-force search, the depth patch
  normal, align to normal, constraints and the ruler, command tests with a fake host, a headless UI test of V
  and M, a GPU test that a surface snap lands on a tilted plane with its normal and a vertex snap on a cube
  corner, and the `snap/vertex-search` Benchmark (80 µs budget for 100,000 vertices; 11,278 ns measured)
- M7q: Module API v3: lifecycle, multi-world host, host service interfaces (scripting API) (ADR 0041), in two
  parts:
- M7q-1: ✅ Module API v3, first part: `kModuleApiVersion = 3` with `start`/`stop` once per Simulation World
  (hot reloads call neither) and `load`/`unload` without a World (`ModuleContext::world()` asserts there),
  `ModuleCallbacks`; the multi-world `ModuleHost(JobSystem&)` with `attachWorld(World&, WorldRole{Edit,
  Simulation, Preview}, ServiceSet)`, `detachWorld`, `update(World&, f64)`, stripping over every attached World
  and services looked up per World, then on the host; the header-only `illusion::{physics,audio,input,render}_api`
  targets and `illusion_add_module(... USES ...)`, with `LINK` of engine libraries rejected for `SHARED`
  modules at configure time. Tests of the lifecycle on edit, play and preview Worlds, stripping from the edit
  and the play World on reload and unload, per-World services, the `world()` assert during `load`, the two
  configure fixtures, and the `modules/update` Benchmark (25 ns budget per module and World; 1.85 ns measured)
- M7q-2: ✅ Module API v3, second part: the host service interfaces `PhysicsService` (`physics_api`),
  `AudioService` (`audio_api`), `InputService` (`input_api`) and `SceneService` (the new header-only
  `illusion::runtime_api`), abstract classes with host adapters `PhysicsWorldService`, `AudioWorldService`,
  `InputActionsService` and `SceneLoaderService` (the new `illusion::runtime`); `USES runtime`; the plain
  query, event and playback types moved to `PhysicsTypes.hpp`, `AudioTypes.hpp` and `core/AssetState.hpp`,
  and the `InputBinding` factories became `constexpr`, so modules use them without engine code. Counting
  proxies of all four interfaces with a shared test module that calls every function once, forwarding tests
  of every adapter, the `PhysicsService::createMesh` regression (a mesh body built by a module survives a hot
  reload and the unload; meant for ASan), the `services/virtual-call` Benchmark (10 ns budget; 2.16 ns
  measured), `reference/scripting-api.md` (Doxygen group "Scripting API") and the `game-modules` skill
- M7r: Play mode and the `GameSession` runtime (ADR 0042), in three parts:
- M7r-1: ✅ The game runtime: `GameSession` (`illusion::runtime`) owns a World with its `PhysicsWorld`,
  `AudioWorld`, `InputActions`, physics and render asset resolvers, hierarchy index, gamepad routing and the
  `ModuleHost` attachment with the four host services, and steps input, modules, physics, hierarchy, audio
  and resolvers in that order; Play and Simulate modes, a deterministic fixed-step mode with seeded
  EntityGuids for runtime spawns, input focus with `InputSettings::gamepadsInBackground`. Project settings
  `PhysicsSettings`, `AudioSettings`, `InputSettings` and `RenderSettings` in `settings:/*.json` through
  struct reflection (`loadProjectSettings()`), with the settings file format moved from the editor to core
  (`SettingsFile.hpp`). The `.input` input actions asset (`readInputActions()`, `writeInputActions()`,
  `loadInputActions()`) and `InputActions::removeAction`, `renameAction`, `clear` and `isValidActionName`.
  Tests of the step order, modules and services, Simulate, fixed steps, the same reflected-state hash after
  600 steps for the same seed, injected keys and virtual gamepads with and without focus, settings, and the
  asset's errors; the `runtime/session-step-empty` Benchmark (50 µs budget; 2,326 ns measured); `manual/runtime.md`
- M7r-2: ✅ Editor Play mode on `GameSession`: `illusion::editor_play` / `PlaySession` implementing the
  core's `PlayHost` (in-memory scene snapshot with ids kept into a play World, Play, Simulate, Pause, Step N,
  time scale, fixed steps with a seed), the edit World attached to the editor's `ModuleHost` as
  `WorldRole::Edit`; `CommandMode::Any` commands routed to the play World while playing (own entity index,
  never undoable) with the `"world": "edit"` override, edit hooks (physics rebuilds, restarted sounds);
  commands `play.start`, `stop`, `pause`, `step`, `set_time_scale`, `status`, `run_until`, `keep_changes`
  (one undoable step), `module.load`, `unload`, `list`, `input.actions`, `action_define`, `action_remove`,
  `action_rename`, `bind`, `unbind` (undoable `.input` edits), `input.inject` and `input.read`; the runtime
  project settings in the editor's settings catalogue; the F5/F6/F7/Shift+F5 UI actions; tests of the
  byte-identical edit scene after Stop, routing, keep_changes, edit hooks, input and modules with a
  `FakePlay` in the core fixture; the `play/snapshot` Benchmark (20 µs per entity budget; 3.0 µs measured);
  `manual/editor/play-mode.md`
- M7r-3: Game view rendering of the play World with input routing and cursor capture (Shift+F1), the Play
  toolbar, tint and Maximize on Play, autosave before Play, the Tetris reference module and the play
  scenarios of the acceptance transcript (C5)
- M7s: Project modules built from the editor (ADR 0043); opening sources with the External Tools code editor
  (from M7l)
- M7t: Component layout migration on hot reload, the M6 follow-up (ADR 0044)
- M7u: Asset catalog, live reload and the asset browser (ADR 0045); GPU eviction of unused
  `RenderAssetCache` entries (from M7h); `#` asset search in the command palette (from M7l)
- M7v: Prefabs with overrides and nesting (ADR 0046)
- M7w: Engine feature panels: rendering, physics, audio mixer, input actions, gamepad tester, profiler,
  diagnostics; gamepad navigation of the editor window through `GamepadSystem` (from M7l)
- M7x: Showcase scene covering every Component and the scripting API; Tetris built by an agent end to end

## M8 — Release pipeline

- Export: clean runtime build, packed assets, no editor code
- CPack installers: Windows (signing-ready), macOS `.app` (notarization-ready), Linux (Steam Runtime
  "sniper" compatible)
- Optional Steamworks module (achievements, cloud saves, Steam Input); SDK supplied by the user
- Release and Steam guide (EN/TR)

## M9 — Upscaling and frame generation

- Render resolution decoupled from output resolution; one upscaler interface (quality modes, jitter,
  motion vectors, depth, exposure, reactive mask), with the existing TAA as the fallback on every GPU
- AMD FidelityFX FSR (MIT): temporal upscaling and frame generation on every vendor through Vulkan
- NVIDIA DLSS (Super Resolution, Ray Reconstruction, Frame Generation) as an optional module, like
  Steamworks: the SDK is supplied by the user under NVIDIA's licence and never committed; an ADR records
  the exception to the permissive-licence rule
- Optional Intel XeSS module on the same interface
- GPU tests and benchmarks per upscaler where the CI hardware allows it; documentation (EN/TR)

## M10 — Hybrid ray tracing

- Acceleration structures (BLAS/TLAS) built and refit from the shared geometry pools and the ECS, through
  NVRHI on Vulkan ray queries; raster rendering stays the fallback where they are unavailable (MoltenVK)
- Ray-traced shadows (sun and local lights) and ambient occlusion
- Ray-traced reflections
- Ray-traced global illumination, denoised (DLSS Ray Reconstruction where the module is present)
- Verify ray query support of Mesa lavapipe for CI GPU tests; ADRs for the acceleration structure and
  denoising design

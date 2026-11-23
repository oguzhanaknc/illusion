# Roadmap

Milestones are delivered in order; each one ends with tests, benchmarks, bilingual documentation and
ADRs for its significant decisions. Items marked ✅ are done.

## Order after DEMO

1. TEX (texture compression), 2. M8 (release pipeline), 3. UI (game interface), 4. SAVE (saved games),
5. ANIM (skeletal animation), 6. NAV (navigation and AI), 7. MAT (custom shaders and materials),
8. CAM (camera system), 9. ANIM-5 (retargeting and aim layers), 10. DECAL, 11. POST (cinematic post
effects), 12. PLAY (gameplay foundations), 13. SHOOTER (third-person shooter demo), 14. PROBE (reflection
and light probes, screen-space reflections, contact shadows), 15. TERRAIN (with GPU grass and impostors),
16. WATER, 17. GFX-3 (lit smoke, clouds), 18. M9 (upscaling and frame generation), 19. M10 (hybrid ray
tracing).

The release pipeline comes early so that every later milestone is exercised by an exported build in CI.
PLAY comes right before SHOOTER: the demo is its first real user, it builds on the cameras of CAM (the
character a camera follows) and the animation events of ANIM-5, and nothing before SHOOTER needs it.
MAT comes before the render features that add shader paths (decals, grass, impostors, water), so they are
written once on the material system.

CAM, ANIM-5, DECAL and POST are the engine features the SHOOTER demo needs; each is a general engine
system (the demo only configures it), designed in its own ADR and finished before the demo starts.

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

## M5 — Gameplay services ✅

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
- ✅ Reflection-based migration of module Components whose layout changed, so that such a change survives a
  hot reload instead of needing a restart (M7t, ADR 0044)

## M7 — Editor and AI control ✅

Plan: `docs/plans/m7-editor.md` (sub-milestones, command API, localization, theme, acceptance test).

Status: every sub-milestone is done. The nightly live-agent workflow of the plan is deferred: the owner chose
to record the agent session from a Claude Code development session instead of through the API (2026-11-07).

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
  reference, `SceneLoadResult::missingAssets` (ADR 0035). Freeing GPU memory of cache entries no
  resolver uses came with live reload (M7u-2)
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
  commands, `preferences.get/set` and `editor.screenshot` (91 commands). Cut to later: follow-agent (done in
  M7o-3), launching External Tools (M7s), `#` asset search in the palette (M7u); gamepad navigation of the
  window was dropped as unnecessary
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
- M7r-3: split in two parts:
- M7r-3a: ✅ The play World in the editor's viewports: `ViewportService` renders, picks, samples and bounds
  the play World while playing (its own `RenderAssetResolver` and hierarchy index per play session, through
  `EditorContext::playSessions()`), viewport commands report play-World entities and refuse
  `"world": "edit"` while playing; input routing from the Game view (`GameInput`: a click captures the
  keyboard, mouse and cursor through `ui::requestCursorCapture()`, Shift+F1, Pause and Stop release and the
  game sees held keys released, shortcuts wait); the Play toolbar (Play turns into Stop, Pause toggles and
  stays pressed, Pause and Step off while stopped, Playing/Paused in the status bar); the play tint
  (`playModeTint`) and Maximize on Play (`maximizeOnPlay`, the Game view's toolbar); autosave of an unsaved
  scene before Play (`EditorContext::autosaveNow()`). Tests of autosave, play-World viewport commands, the
  toolbar, capture and release with Maximize on Play, a GPU test of rendering and picking the play World
  across sessions, and the golden images `shell-dark-100-play` and `shell-dark-100-play-maximized`
- M7r-3b: ✅ The Tetris reference module (`tests/acceptance/tetris/module/Tetris.cpp`, built as
  `illusion_tetris_module` with `USES input render runtime`: the SPEC's Components, SRS rotation and wall
  kicks, soft and hard drop, line clears and scoring, gravity by level, game over, forced pieces and seeded
  7-bags) and the play scenarios of the acceptance transcript: the input asset in `scripted.jsonl`, C1 checks
  of its actions and C5 checks that load the module through `--var tetrisModule`, start Play mode with
  `fixedStep` and play spawn, walls, rotation, drops, gravity and game over through `input.inject` and
  `play.step`; `editor/tests/play/TestTetris.cpp` tests the rules through `PlaySession`
- M7s: Project modules built from the editor (ADR 0043) and the External Tools code editor (from M7l), in two
  parts:
- M7s-1: ✅ The module SDK: every editor build tree writes a CMake package to `<build>/sdk`
  (`illusionConfig.cmake` importing `illusion::core` and the five API targets, with `illusion_add_module()` and
  `illusion_project_modules()`) and a toolchain description per configuration (`illusion-sdk.json`: CMake,
  generator, configuration, compiler and flags, module library prefix and suffix), read through
  `EditorContextOptions::moduleSdk`; `ModuleBuild` (configure when there is no cache, then `cmake --build`, as
  child processes read line by line) with `BuildOutputParser` for GCC, Clang, MSVC, clang-cl, MSVC linker and
  CMake diagnostics; commands `module.create` (undoable template files), `module.build` (async, diagnostics
  with virtual paths logged to the Console, loads new modules and hot-reloads loaded ones) and `module.reload`;
  `PlayHost::reloadModule` and `reloadChangedModules`; the Build Modules (Ctrl+B) and Reload Modules (Ctrl+R)
  actions enabled. Tests of the parser, the toolchain description and the commands against a stand-in for
  CMake (`illusion_fake_cmake`), and one real build with the build tree's SDK that compiles a module, reports a
  compile error at its line, hot-reloads the fix and plays it; `manual/editor/project-modules.md`
- M7s-2: ✅ `editor.open_source`: opens a project file (virtual or absolute path) at a line and column in the
  External Tools code editor (`codeEditor`, a command line with `{file}`, `{line}` and `{column}`) or with the
  system's default application (`explorer`, `open`, `xdg-open`), started as a detached process
  (`ProcessOptions::detached`); Console source links of project files, such as build diagnostics, open it.
  Tests of the command line splitting, the command with a recording stand-in editor and its errors, and a
  detached process that outlives its `Process`
- M7t-1: ✅ Component layout migration on hot reload, the M6 follow-up (ADR 0044): a reloaded module's
  registrations are staged (`ComponentRegistry::commitStaged`/`discardStaged`, archetype use counts,
  `World::purgeArchetypesWith`); Components of a type whose size, alignment or reflected fields changed are
  written with the old reflection and read back by field name with the new one (renames, version migrations,
  scene file conversions; values that do not fit are reset), reported in `ModuleInfo::migration`;
  unreflected types in use, Worlds outside the host and unwritable values roll back. Test module variants
  with a widened, a reshaped and an unreflected type, 50 migrating reloads; `modules/migrate` benchmark
- M7t-2: ✅ `module.reload` and `module.build` return the migration report (`modules[].migrated`, the
  `editor.ComponentMigration` struct) and post the `module.migrated` event; tests with the stand-in play host;
  `manual/editor/project-modules.md`
- M7u-1: ✅ Asset catalog and file watching (ADR 0045): `FileWatcher` in `engine/platform`
  (ReadDirectoryChangesW, inotify, FSEvents, a stat-polling backend and automatic fallback; coalesced
  delivery at `poll()`), `DirectoryEntry::modified`; `AssetCatalog` in the editor core (kinds, sizes, stamps,
  xxHash64, scene/material/glTF dependencies, reverse index, incremental `refresh()`/`apply()`, rename
  detection by content hash); `EditorContext::assets()`, the `asset.changed` event and `asset.find_missing`;
  `assets/catalog-scan`, `assets/catalog-first-scan` and `assets/reference-index` benchmarks
- M7u-2: ✅ Live reload and GPU resource lifetime (ADR 0045 addendum): generation-checked `RenderResources`
  handles, release deferred until the GPU is done (`collect()`, event queries), free ranges in the geometry
  pools (`RangeAllocator` in core), `updateMaterial()`, `isValid()`, `statistics()`;
  `AssetStreamer::invalidate()`/`reload()` and `AssetReloadedEvent`; the `RenderAssetCache` reloads models,
  textures, material files (in place) and environments, keeps the old version until the new one is up and
  re-resolves renderers; `RenderAssetResolver::releaseUnused()` and `RenderAssetCache::evictUnused()` (GPU
  eviction from M7h); the viewports reload files of `asset.changed` and their catalog referrers; 100-cycle
  reimport GPU test; `memory/range-allocator`, `render/asset-cache-steady`, `render/update-material`
  benchmarks
- M7u-3: ✅ Import settings sidecars and reimport (ADR 0045 addendum): versioned, validated
  `<file>.import.json` sidecars written only for non-default settings (texture `colorSpace`, `generateMips`,
  `maxSize`; model `scale`; `ImportSettings.hpp`, `limitImageSize()`, `scaleModel()`), honoured by
  `AssetDatabase`, `AssetStreamer` and `cookAssets` as a cache dependency (importers `image:2`/`gltf:2`;
  orphans reported by the cook); `AssetReloadMode::Reimport`; the catalog keeps sidecars on their asset
  (`importSettingsHash`, owner reported as modified), `asset.list` hides them, `asset.move`/`asset.delete`
  carry them with undo; `asset.set_import_settings`, `asset.reimport` (`reimported` in `asset.changed`),
  `importSettings` in `asset.info`; GPU test of sidecar live reload and reimport; fix of
  `AssetCatalog::removeTree()` for siblings that sort inside a folder
- M7u-4a: ✅ Environment bake cache and live reload of physics and audio (ADR 0045 addendum):
  `AssetStreamer::loadCooked()`/`storeCooked()` for assets the caller makes itself (verified by source
  hashes, `AssetReloadMode::Reimport` skips the entry; `AssetReloadedEvent::mode`); `AssetType::Environment`
  with `BakedEnvironment` and `serializeEnvironment()`; `RenderResources::requestEnvironmentReadback()`,
  `readEnvironment()` and `createEnvironment(const BakedEnvironment&)`; the `RenderAssetCache` stores bakes
  of image files keyed by the image, its sidecar and the `EnvironmentDesc`, and uploads them after a restart
  without baking (`bakes`, `bakeCacheHits`); the `PhysicsAssetResolver` cooks the geometry of reloaded
  models again (`reloads`) and the `AudioWorld` reloads streamed clips under the same handle
  (`clipReloads`); GPU tests of the readback and of the cache across restarts, edits and reimports;
  `assets/environment-load` and `physics/asset-resolve-steady` benchmarks
- M7u-4b: ✅ Thumbnails (ADR 0045 addendum): `ThumbnailHost` and `ThumbnailCache` in the editor core (PNGs in
  the new `thumbnails:` mount, `.illusion/thumbnails`, named after a content key of the asset and its
  dependencies, deleted on `asset.changed` with those of their referrers); the async `asset.thumbnail`
  command (cache hits at once, `refresh`, made again when the asset changes meanwhile); `ThumbnailRenderer`
  in `editor/viewport` (a preview World and a small `SceneRenderer` at 256 px sharing the viewports' asset
  cache, at most one render or texture scale per frame, non-blocking readback) for models, material
  spheres, textures (`textureThumbnail()` on the CPU) and HDRIs (through the bake cache); GPU golden images
  of each kind, frame budget, live reload and failure tests; `thumbnail/key` and `thumbnail/texture`
  benchmarks
- M7u-5: ✅ The asset browser panel (ADR 0045): the project's `assets:` mount as a grid or list with
  thumbnails (the `asset.thumbnail` PNGs shown through the new `ImageHost`), a folder tree and favourites
  (the `assetFavourites` preference), breadcrumbs with back/forward/up, search, sorting and a kind filter,
  multi-select, dragging an asset onto an Inspector asset field, and a context menu whose operations run the
  asset commands (Open, Reimport, Rename, Move, Delete, Reveal, Copy path) plus New folder; the import settings inspector edits a texture's color space,
  mipmaps and max size or a model's scale with Apply/Revert; `#` in the command palette searches assets
  (`PaletteMode::Assets`); the `editor.reveal` command and the `fileBrowser` preference reveal a file in the
  system's file manager
- M7v: ✅ Prefabs with overrides and nesting (ADR 0046), in parts:
  - M7v-1: ✅ Prefab format and engine support: `.prefab` files (`"format": "illusion.prefab"`, one root),
    instance records in scenes with field-level overrides computed at save time, ids derived with
    `derivePrefabGuid()`, nesting (depth 16, cycles kept unresolved) and variants, `instantiatePrefab()`,
    the runtime Components `PrefabInstance`/`PrefabMember`/`UnresolvedPrefab` (missing prefabs pass through),
    `PrefabSaveMode::Expanded` snapshots for Play mode and the clipboard, `SceneService::instantiate` of
    `.prefab` files, the editor opening and saving scenes with instances, undo keeping prefab links,
    `asset.references`/`asset.move` seeing record prefabs; benchmarks `prefab/instantiate` and
    `prefab/save-records`
  - M7v-2: ✅ `prefab.create` (replaces the entities with an instance and redirects references to it),
    `prefab.instantiate`, `prefab.unpack`, `prefab.overrides` and `prefab.revert`; the `Prefab` asset kind in
    the catalog, `asset.list` and the browser; `.prefab` files in the reference index; members a changed
    prefab no longer has saved as plain entities; `manual/editor/prefabs.md`
  - M7v-2b: ✅ `prefab.apply` (one Component, an entity or the whole instance; other open instances follow
    and keep their overrides), linked paste of whole instances (`entity.duplicate` makes plain copies), and a
    prefab-aware reference index: scenes refer through their instances, and `asset.move` rewrites prefab
    files and the scenes instancing them without adding overrides
  - M7v-3a: ✅ prefab edit mode (`prefab.open`, `prefab.close`, an Outliner breadcrumb, scene.save writing
    the prefab), prefab file changes propagating into the open scene as one undoable step that keeps
    overrides (`prefab.refresh`, run by the editor when a tracked prefab changes), and the Inspector's prefab
    bar (edit, apply all, revert, unpack) with overridden Components marked bold with an accent bar and
    applied or reverted from their menu
  - M7v-3b: ✅ create a prefab by dragging an entity from the Outliner into the Asset Browser, instantiate by
    dragging a prefab into the viewport (on the ground, `prefab.instantiate` with `position`) or onto the
    Outliner, and per-field override marks with per-field apply and revert (`field`)
- M7w: Engine feature panels: rendering, physics, audio mixer, input actions, gamepad tester, profiler,
  diagnostics, in parts:
- M7w-1: ✅ Profiler and Diagnostics: `FrameProfiler` in the core (nested named CPU scopes of the last frames
  in a fixed ring, allocation-free recording and `summarize()`), the editor's frame loops record their phases
  (events, requests, ui with one scope per drawn panel, render, play, editor), GPU pass timings per viewport
  in `render.stats`, the Profiler panel (CPU graph and places, GPU passes per viewport, Diagnostics tab that
  opens what an issue is about), and `profiler.get`, `profiler.set` and `diagnostics.get` (render validation,
  log errors and warnings, missing assets, dropped lights, unresolved prefabs; 173 commands). Unit tests for
  the recorder, command tests with a fake viewport host, a headless UI test of the panel, and the
  `profiler/scope` Benchmark (400 ns budget per scope; 50 ns measured)
- M7w-2a: ✅ Project Settings panel: a page per project settings type of the `SettingsCatalog` (the
  runtime's Physics, Audio, Input and Render and the game's own) drawn by the Inspector's widgets in a
  settings mode (`Inspector::drawSettings()`, edits update a working value and send the changed top-level
  field to `project.settings_set`, one undo step per drag), reset to defaults, re-read after undo and redo,
  the physics layers as a table of editable names and a collision-matrix triangle, and named audio buses.
  A headless UI test edits a game setting with undo, the matrix, a layer name and resets the page
- M7w-2b: ✅ Rendering panel (the last placeholder): the `viewportQuality` preference (shadows with their
  cascades, resolution and distance, ambient occlusion, bloom, temporal anti-aliasing, occlusion culling;
  validated, applied by `ViewportService` through the GPU-free `shadowSettingsOf()`/`postProcessSettingsOf()`,
  secondary Scene views keep reduced shadows), the GPU and each viewport's last frame. Unit tests for the
  mapping and the preference, a headless UI test and a GPU test that turning shadows off stops shadow casters
- M7w-2c: ✅ Physics and Audio Mixer windows for the running game: `PlayHost::diagnostics()` (bodies,
  characters, joints, physics steps, contacts of the last step, voices, clips, the mixer), a session-only mixer
  (`setMasterVolume()`, `setBus()`), `play.diagnostics` and `play.set_mix` (175 commands), and a button that
  shows every collision shape in the Scene view. Command tests on a real play session and a headless UI test
- M7w-3a: ✅ Input Actions window: the actions of an input actions asset with their types and bindings,
  rebinding by capturing the next key press or typing a binding, unbinding, adding and removing actions, all
  through the `input.*` commands (one undo step each). A headless UI test adds, binds, unbinds and removes
- M7w-3b: ✅ Gamepad Tester window and `gamepad.*` commands: the editor now owns a `GamepadSystem` (Play mode
  reads it; before, games in the editor saw no gamepads), `GamepadHost` in the core with `EditorGamepads` in
  `editor_play`, the live state (buttons, sticks, triggers, touchpad, gyroscope, accelerometer) and the outputs
  (rumble, trigger rumble, light, DualSense adaptive triggers, motion sensors) as `gamepad.list`,
  `gamepad.rumble`, `gamepad.set_led`, `gamepad.set_trigger_effect` and `gamepad.set_motion` (180 commands).
  Command tests with a fake host, a test over a virtual DualSense and a headless UI test
- M7w-3c: ✅ Drop to ground with physics: `transform.drop_to_ground` `"onto": "colliders"` and the Entity menu's
  Drop onto colliders. `PhysicsQueryHost` in the core, `EditorPhysicsQueries` in `editor_play`: a scratch Jolt
  world of static bodies from every Collider (sensors and inactive entities left out) and the ground plane;
  the falling Colliders are shape-cast down, entities without one cast a ray from the bottom of their box. Tests
  with Jolt (boxes, a scaled sphere, a capsule, a sensor, a disabled floor, a child following its parent)
- M7w-3d: ✅ Mesh asset colliders: drop onto colliders loads and cooks ConvexHull and Mesh Colliders made from
  mesh assets (an `AssetStreamer` and a `PhysicsAssetResolver` of `EditorPhysicsQueries`, geometry kept between
  drops and cooked again when the model file changes) and sweeps falling convex hulls; a scaled
  `PhysicsWorld::shapeCast()` overload. Play mode in the editor now gets an `AssetStreamer` too: before, mesh
  colliders had no shape there and audio clips loaded on the editor thread (fixed-step sessions and steps wait
  for the loads, so they stay deterministic)
- M7w-4: ✅ Multiple Inspectors: Inspector windows 2 to 4 (Window menu, `ui.show_panel` with
  `window.inspector_2` to `window.inspector_4`), each locked to the selection of the moment it opens and with its
  own stable ids (`inspector_2.` ...); a headless UI test edits two entities in two Inspectors
- M7x-1: ✅ Showcase project (`samples/showcase`): `everything.scene` with every saved engine Component and every
  field set to a non-default value (cameras, lights, sky, Meshes and a submesh, hierarchy, disabled entity, every
  collider shape including asset meshes, every body kind, a character, every joint type, audio on every bus with
  every attenuation, prefab instances with overrides), built by Editor commands (`build.jsonl`) from generated
  assets (`tools/make_assets.py`); a gate test fails on any saved Component or field left out or at its default
  and on missing assets or unresolved prefabs
- M7x-2: ✅ Showcase module (`samples/showcase/modules/showcase`): plays the everything scene through every
  function of `ModuleContext` and the four host services (a floor and a gem from mesh geometry, a walking and
  jumping character, pushed bodies, rays, sweeps, overlaps, contacts and trigger visits, chimes on impacts, a
  prefab spawned at frame 120, moon gravity, layer changes); `tools/check_api_coverage.py` (CI) over the
  Doxygen "Scripting API" group (now with `ModuleContext`) and the counting proxies, which forward to real
  services; a 300-frame run on the real services that must reach every function; two fixed-step sessions with
  the same input that must end in the same state hash; a GPU golden of the scene through its main Camera.
  Fixed: `SceneService::instantiate()` in a fixed-step session took EntityGuids from the process-wide
  generator instead of the session's seed, so runs that spawned a scene or prefab never repeated
- M7x-3: ✅ Recorded agent transcript: Claude Code built the SPEC.md game through the MCP tools alone (its own
  module, compiled with `module.build`, materials, scene, input actions, rules tested in Play mode);
  `tools/acceptance/normalize_transcript.py` turned the `illusion_mcp --record` output into
  `tests/acceptance/tetris/recorded.jsonl` with the sources in `files/` (`{"$file": ...}` transcript values);
  CTest replays it with the checks (`acceptance.tetris.recorded`, `.gpu`, `.mcp`), the build compiles its
  module and the play tests check every rule on it and on the reference module; C2 (module loaded, schemas)
  is checked for both transcripts. Deferred: the nightly live-agent workflow (needs an API key and budget)
- M7x-4: ✅ Tutorials (Tetris with an agent, Tetris by hand, whose C++ blocks the test build compiles and
  loads as a module); editor manual screenshots taken by `docs/screenshots/manual.jsonl` in a headless editor on the
  showcase project and compared in the GPU job (`docs.screenshots`); string freeze: `illusion_textc freeze`
  writes `en.freeze.json` and the locale check fails when a frozen string changes or disappears (editor and
  input tables frozen as M7); the M7 status above
- M7x-5: ✅ Showcase playground: every station in view of the main Camera, child meshes for the bodies whose
  collider had none, a door opened and closed by its hinge motor, a swinging pendulum and rope, a sliding
  drawer, a rising platform, a trigger pad that lights a lamp and greets the Player, a Player that walks by
  itself until it gets input, and five generated sounds instead of one tone. Fixed: changing
  `Joint::motorSpeed` did not wake a sleeping body, so a motor never moved a door that rested at its limit

## PFX — Particle effects ✅

Before M8. Design: ADR 0047 (a saved emitter Component, a deterministic CPU simulation, GPU billboards).

- PFX-1: ✅ `illusion::particles`: the saved `ParticleEmitter` (rate, burst, looping and duration, lifetime
  and speed ranges, point/sphere/box shapes with a spread angle, gravity factor and drag, size and HDR
  colour over life, additive or alpha blending, world or local space, texture, seed) and the runtime
  `ParticlePool` (structure-of-arrays, allocated once per capacity); `ParticleWorld` emits, moves and
  retires particles as parallel Jobs, deterministically and without per-frame allocations, with
  play/stop/clear; `GameSession` advances it after physics with the physics gravity; benchmark
  `particles/update`; the showcase has a spark fountain over the trigger pad and a dust emitter
- PFX-2: ✅ Rendering: `SceneRenderer` draws the live particles as instanced camera-facing quads after the
  blended geometry in the HDR target (bloom), additive and premultiplied-alpha blending, alpha emitters and
  their particles sorted back to front, the scene depth read in the shader for the depth test and a 30 cm
  soft fade (no depth copy), sprite textures resolved into `RenderParticles` by `RenderAssetResolver` (held
  against eviction, re-resolved on reload), `RenderStats::particles`, GPU tests and the GPU benchmark
  `render/100k-particles`; the showcase's spark fountain glows under the lamp
- PFX-3: ✅ Editor and scripting: `EditorContext` previews the edit World's emitters every frame (the
  scene and its hash do not change), the `particles.play`, `stop`, `clear`, `emit` and `stats` commands act on
  the active World (and are MCP tools), `ParticleService` (`USES particles`, `ParticleWorldService`, provided
  by `GameSession`) with `emit` for bursts; the showcase module raises dust on the first hard landing,
  showers sparks on the pad and stops and restarts the fountain, so the API coverage includes the service
- PFX-4: ✅ GPU simulation: `ParticleEmitter::simulation = Gpu` keeps the particles on the GPU. Compute
  shaders (reset, simulate, spawn, finish) ping-pong two state buffers with atomic live counts and write the
  indirect draw arguments, and a 3-slot readback brings the live count to `ParticlePool::count`.
  `ParticleWorld` publishes numbered steps (births, seconds, gravity) that every view simulates once, and
  the step seeds the births, so all views agree. The showcase has a GPU firefly field. GPU benchmark
  `render/1m-gpu-particles`: 1.4 ns per particle per full frame on an RTX 4070 SUPER. Fixed on the way: the
  dispatches of one binding set need explicit UAV barriers in NVRHI

## DEMO — Tech demo scene

Before M8. A first-person night-and-day forest clearing (`samples/techdemo`) that shows what the renderer,
particles and physics can do together, and the engine features it needs.

- DEMO-1: ✅ Mouse look: `ActionType::Delta` (unbounded, bindings add up) and `BindingSource::MouseMotion`
  (`Mouse.Motion`, `Mouse.Scroll`, scaled), captured by `BindingCapture` for Delta actions, with glyphs
- DEMO-2: ✅ Wind (ADR 0048): the saved singleton `Wind` (direction, speed, travelling gusts, turbulence)
  with its runtime clock `WindState` (`advanceWind()`, called by `GameSession` and the editor preview);
  `MaterialData::windSway`/`windFlutter` displace vertices in the forward pass, prepass (IDs, motion vectors
  with the previous frame's wind) and shadow casters (batched by Material when they sway); cooked model
  format 2; `ParticleEmitter::windFactor` on the CPU and GPU paths; GPU test for both material paths
- DEMO-3: ✅ Day and night (ADR 0049): the saved singleton `SkyAtmosphere` (time of day, sun path, moon
  offset, haze, sun/moon/star brightness); an analytic Rayleigh + Mie + ozone sky (`assets/Atmosphere`, the
  same model in `Atmosphere.slang`) drawn per pixel with the sun disc, a phased moon, twinkling stars and a
  Milky Way; its sun or moon is the first shadowed directional light, fading through dusk; a per-renderer
  cube map redrawn in place (`createAtmosphereEnvironment()`/`updateAtmosphereEnvironment()`) only when the
  sky changes, CPU spherical harmonics (`atmosphereIrradiance()`, `ShAccumulator`); GPU and CPU tests
- DEMO-4: ✅ Assets: `samples/techdemo/tools/fetch_assets.py` downloads eight CC0 Poly Haven assets (43
  files pinned by size and SHA-256, git-ignored) and merges the plants' alpha maps into RGBA leaf textures
  with a standard-library PNG decoder; `make_assets.py` generates the terrain (a flat clearing in a bowl of
  hills), three branching trees with leaf cards, neon rings and bars, a log, bark and leaf textures, flame
  and smoke sprites and looping ambient sounds; `test_tools.py` runs in CI
- DEMO-5: ✅ The tech demo project (`samples/techdemo`, manual page "Tech Demo Project"): `make_scene.py`
  writes `build.jsonl` (32 materials, about 5,500 entities on the terrain: 46 trees, 5,000 grass tufts, ferns,
  shrubs, rocks, boulders, a campfire with flame/ember/smoke emitters and a light, GPU fireflies, lanterns, a
  neon gate, hoops and bars, pushable orbs, ambience); the `techdemo` module walks a CharacterController with
  mouse/stick look, sprint and jump, runs the day clock (T speeds it up), lets the fireflies, lanterns and
  crickets out at night, flickers the fire and holds props kinematic until the terrain collider is ready
- DEMO-6: ✅ A Poly Haven fir forest: `fetch_assets.py` pins 34 CC0 assets (234 files, about 900 MB: firs,
  young firs, saplings, broad-leaved trees, grass, ferns, bushes, flowers, rocks, cliffs, dead wood, props,
  pine needle litter), merges 16 alpha maps and writes lighter tree copies (`thin_gltf()`: evenly spread
  foliage cards kept and enlarged, `_far` copies for the hills, texture transforms baked into the UVs);
  `make_scene.py` builds about 14,700 entities (110 firs, 80 young firs, dense undergrowth, a richer camp,
  the sun setting behind the neon gate); the procedural trees and bark textures are gone

## GFX — AAA look

After DEMO. Features that bring the renderer and the tech demo to a modern AAA look.

- GFX-1a: ✅ `PostEffects` (saved singleton): automatic exposure (centre-weighted log-luminance metering
  on the GPU, adaptation in stops, limits, compensation; `RenderView::deltaSeconds`), ACES or AgX
  tonemapping, white balance, contrast, saturation, vignette, film grain, chromatic aberration
- GFX-1b: ✅ `VolumetricFog` (saved singleton): froxel fog with height falloff and wind-carried noise,
  light shafts from the shadowed sun or moon, glow around point lights, temporal reprojection; applied to
  opaque surfaces, the sky, blended surfaces and particles
- GFX-1c: ✅ Point light shadows: `PointLight::castShadows` with `sourceRadius`, `shadowBias` and
  `shadowNearPlane`; cube shadow maps for up to 4 lights nearest the view (`ShadowSettings::pointLightCount`,
  `pointResolution`), drawn with the caster path (wind, alpha masks), PCSS in the forward shading and one tap
  per froxel in the volumetric fog (ADR 0051); the tech demo's campfire and lanterns cast them
- GFX-2: ✅ Foliage and level of detail (GPU grass and impostors moved to TERRAIN; screen-space reflections
  and contact shadows moved to PROBE):
  - GFX-2a: ✅ Levels of detail for every Mesh (ADR 0052): up to three coarser index ranges per submesh over
    the same vertices (`Submesh::lods`, cooked model format 3), generated on import with meshoptimizer 1.3
    (MIT; simplifier with normals and locked borders, vertex cache order; sidecar `lodCount`,
    `lodReduction`; `generateLods()`/`clearLods()`), chosen per instance by projected error
    (`RenderView::lodPixelError`, 1 pixel) in the culling Jobs and drawn at the same level into every
    shadow view; batches keyed by level; `RenderStats::lodInstances`; import settings in the Asset Browser;
    triangle-mesh colliders keep the full-detail triangles (`fullDetailIndices()`)
  - GFX-2b: ✅ Dithered cross-fades between levels (ADR 0052 addendum): the renderer keeps each Entity's
    level and fade between frames (`RenderView::lodFadeSeconds`, 0.25 s; reversed when it turns back), draws
    a fading instance at both levels with complementary interleaved-gradient-noise dithering in the
    prepass, shading, Entity IDs and shadow casters (through the alpha-tested pipelines, so opaque draws
    keep early depth testing), `RenderStats::lodFades`; `RenderDebugView::LevelOfDetail` and the
    viewport's Level of detail shading mode
  - GFX-2c: ✅ Card thinning for foliage levels of detail (ADR 0052 addendum): submeshes of many small
    vertex-disjoint parts keep an evenly spread half of their cards per level, enlarged to keep the leaf
    area; `MeshRenderer::lodBias` scales the pixel allowance per renderer; the tech demo drops its hand-made
    `_far` tree copies for a bias of 2
  - GFX-2d: ✅ Leaf translucency (ADR 0053): `MaterialData::diffuseTransmission` and its colour (glTF
    `KHR_materials_diffuse_transmission`) send part of the diffuse light to the other side; the shadow test
    of translucent surfaces offsets towards the lit side; the tech demo's foliage glows against the sun
- GFX-3: Lit smoke and clouds (after WATER)

## TEX — Texture compression

Every platform: textures reach the GPU uncompressed (RGBA8), using 4 to 8 times the memory they need.

BC7 (colour, colour + alpha), BC5 (normal maps), BC4 (masks) and BC6H (HDR environments), encoded on import
on the Job System, one format family on Windows, Linux and macOS (Apple Silicon supports BC through
MoltenVK); design in ADR 0054, public API in `illusion/assets/TextureCompression.hpp`.

- TEX-1: ✅ Encoders. `engine/assets`: `ImageFormat` BC formats, `compressImage()`/`decompressImage()`
  (bc7enc_rdo for BC7/BC5/BC4, own BC6H encoder in `Bc6hEncoder.cpp`, bcdec to decode), dependencies pinned
  and recorded. Done: `tests/assets/TestTextureCompression.cpp` (round trips, error bounds per format and
  quality, odd sizes, same bytes with and without jobs), `benchmarks/BenchTextureCompression.cpp` (encode
  speed per format) within budget.
- TEX-2: ✅ Import. `ImageImportSettings::compression`, sidecar version 2 (`compression`, `quality`; model
  `compressTextures`, `textureQuality`), cooked image version 2 and model version 5, compression by
  material use (`materialTextureUses()`, images of glTF models), `cookAssets()`; the editor's import
  settings (commands, Asset Browser, thumbnails). Done: tests of sidecars, cache keys, cooked round trips
  and the cooker pass; command reference and goldens updated.
- TEX-3: ✅ GPU upload. `RenderResources::createTexture()` uploads BC levels (CPU decode without
  `textureCompressionBC`), the render asset cache asks for compressed material textures, shaders rebuild
  normal z, `RenderResourceStatistics` reports texture bytes. Done: GPU tests draw every BC format within
  an error bound and check the memory ratio; validation stays clean.
- TEX-4: ✅ Environments. `compressEnvironment()`, cooked environment version 2, the bake cache stores BC6H
  and later sessions upload it. Done: tests of the round trip and error bound, GPU test of a BC6H
  environment, statistics report environment bytes.
- TEX-5: ✅ Mip streaming. `RenderResources::setTextureMips()` (resident range per texture) and the budget
  policy of `RenderAssetCache` fed by `TextureStreamingView`. Done: GPU tests of raising and dropping
  levels with stable Handles, deterministic policy tests (budget, cooldown), a benchmark of the policy
  update, no allocation in an idle update.
- TEX-6: ✅ Documentation (EN/TR): manual pages for assets and rendering, the editor's import settings, the
  showcase with a non-default texture sidecar; in step with each pull request.
- TEX-7 (later): cooked container with separately addressed mip levels, so raising a texture reads only the
  missing levels, and streaming of the images inside glTF models.

## M8 — Release pipeline

"Export" turns a project into a clean runtime build: static modules, packed assets, no editor code, with
packages for Windows (signing-ready), macOS (`.app`, notarization-ready) and Linux (Steam Runtime "sniper"
compatible). Design in ADR 0055; public API in `illusion/player/Player.hpp` and
`illusion/editor/core/Export.hpp`.

- M8-1: ✅ Player. `engine/player` (`illusion::player`, `runPlayer()`), `WindowSettings`
  (`settings:/window.json`) in the project settings and the Project Settings window, the `user:` mount, the
  log file and the failure dialog; `illusion_add_game()` in `cmake/IllusionGame.cmake`, used in-tree to build
  `showcase_game` from the showcase project. Done: unit tests of the command line and of the mounts from
  archives; a GPU test runs `showcase_game --fixed-step --frames 300 --screenshot` on packed data and
  matches the golden image of the editor's Play mode after the same 300 steps (`showcase-play-300.png`); the
  showcase sets non-default window settings; the failure dialog's text comes from the player's string table.
  Open: a way for a module to end the game (with UI), an allocation test of a steady player frame,
  and GPU particle emitters whose state depends on which steps were rendered (a renderer that skips frames
  shows different particles; PFX follow-up).
- M8-2: ✅ Player SDK. The `player` component of the CMake package (every engine and third-party static
  library behind `illusion::player`), `illusion-sdk.json` version 2, install rules that write a relocatable
  SDK (headers, libraries per configuration, package). Done: a CTest configures and builds a game outside
  the engine tree against the build-tree SDK and against an installed copy moved to another directory, and
  runs it for one frame; the boundary check covers the package (no editor target reachable).
- M8-3: ✅ Export. `ExportSettings`, `ProjectExport` and the async command `export.build` (with
  `export.settings`/`export.set_settings` if the generic settings commands do not cover the file), headless
  and windowed; cooking into a fresh cache, the three archives, exclude patterns, the report. Done: tests of
  generation, exclusion and the report on a temporary project; the fuzz test covers the commands; a CTest
  exports the showcase and the acceptance Tetris project and runs each for 300 frames; a symbol scan of the
  exported executable finds no editor or Dear ImGui symbol; command reference and goldens updated.
  Open: image sources still ship in `assets.illpak` next to their cooked form (the runtime loads some
  images, such as environment maps, with settings the cook does not cover; only glTF sources are dropped);
  the allocation test of a steady player frame needs a player that tests can drive without a window.
- M8-4: ✅ Packaging and signing. Icon and version resources (Windows `.ico`/`.rc`, macOS `.icns` and
  `Info.plist`, Linux PNG and `.desktop`), the macOS bundle with MoltenVK, CPack packages (ZIP, NSIS, DMG,
  `.tar.gz`), the signing hooks and notarization. Done: per-platform CTests check the layout and that the
  packaged game starts; the Windows hook is tested with a recording command, macOS with an ad hoc identity
  (`codesign --verify`); a missing tool fails with its name. Open: signing with a real certificate and
  notarization are a manual check before a release (CI has no certificate); a failed Package stage keeps
  the finished game directory.
- M8-5: ✅ Export window. The editor's Export window (settings, target, progress, diagnostics with source
  links, "Show in folder"), string table entries, UiDriver tests and a golden image. Done: File > Export…
  (also in the palette) opens the window, which edits `settings:/export.json` through `project.settings_*`
  with the command's error under a field, offers the host's package kinds and signing fields, runs
  `export.build` with its stage, progress and Cancel, and shows the report, diagnostics that select project
  files in the Asset Browser, the tool output and "Show in folder" (`editor.reveal`); UiDriver tests export a
  temporary project with the stand-in SDK, and an idle frame of the open window allocates nothing (which
  fixed an Asset Browser refresh every frame). Open: validation problems that `export.build` reports in one
  message (product name, identifier, organization, exclude patterns) appear in the result, not next to
  their field.
- M8-6: Steamworks (ADR 0056): optional module built only when the user supplies the SDK path;
  achievements, cloud saves, Steam Input glyphs and the overlay behind a service interface with a null
  implementation that is always built and tested; `linuxRuntime: "sniper"` check and a CI job that exports
  inside the sniper SDK container and runs the game in the sniper platform container.
  Done: `StoreService` (`USES store`) with `NullStoreService` in `engine/store` (achievements, progress and
  stats in memory, cloud files in `user:/cloud` with a quota), unit tests, `GameSessionDesc::store`, the player's
  `PlayerDesc::store` factory with the null fallback and `update()` per frame, Play mode on a null store in
  `cache:/store/cloud`, `SteamStoreService` in the optional `engine/steam` (`ILLUSION_STEAMWORKS_SDK`, the
  package's `steam` component builds it from shipped sources), `illusion_add_game(... STORE steam STEAM_APP_ID)`,
  the export settings' `store` section with validation naming the variable and `steam_appid.txt` for
  development exports only; the showcase module calls the whole API.
  Open: `SteamStoreService` and the `steam` package component are unbuilt and untested (no SDK here or in CI;
  needs a CI job with the SDK as a secret); Steam Input glyphs (`glyphPath()` returns nothing on Steam: Steam
  Input next to SDL's gamepads is its own decision); `SteamAPI_RestartAppIfNecessary()`; the store fields in the
  Export window; the sniper check (`linuxRuntime` is still rejected) and the CI job in the sniper containers.
- M8-7: ✅ Engine releases. Install rules and CPack packages of the editor, tools, data and SDK; a `release`
  workflow on tags (three platforms, checksums, the Linux build in the sniper container) that exports the
  showcase from the installed package and runs it; semver and changelog rules. The installed SDK carries its
  debug information (MSVC compile PDBs, without which a moved SDK links with LNK4099).
  Done: install components `editor` (`illusion_editor` with `editor-data/`) and `tools` (`illusion_ctl`,
  `illusion_mcp`, `illusion_textc`) next to `sdk` and `player`, all in `bin/<Config>/`; `illusion_package_engine()`
  (`cmake/IllusionRelease.cmake`) makes one archive `illusion-<version>-<platform>-<arch>` (ZIP on Windows,
  `.tar.gz` on Linux and macOS) with its `.sha256`; an installed editor uses the SDK installed beside it
  (`ILLUSION_INSTALLED_MODULE_SDK`, relative to the executable); MSVC compile PDBs are written and installed
  next to each static library, and the moved-SDK CTest fails on LNK4099; CTests `sdk.release.*` install every
  component, move the copy, export the showcase from its headless editor with the installed SDK and run the
  game for one frame (`gpu`); `.github/workflows/release.yml` (tag equals `v<project version>`, Release build
  and tests, `cpack`, checksum and layout check, the installed-package tests, artifacts, a draft release on
  tags); `CHANGELOG.md` and "Versions and releases" in the contributor guide (EN/TR).
  Open: the workflow has never run; the Linux build in the sniper container; the package was only made and
  tested on Windows; macOS ships a plain directory (no `.app` for the editor, no installer, the Vulkan loader
  comes from the system) and no installer exists on any platform; the acceptance Tetris is not exported from
  the package; the editor binary still carries the build tree's SDK path as its fallback.
- M8-8: ✅ Documentation (EN/TR): manual page "Exporting a game", the release and Steam guide (signing,
  notarization, depots, the sniper container), the runtime page's player section, tutorials end with an
  export; in step with each pull request.
  Done: the manual page `manual/editor/export.md` (settings, the Export window, `export.build`, packages,
  signing, limits), the player section of `manual/runtime.md`, the guide `guides/release.md` (export settings,
  signing on Windows, signing and notarizing on macOS, Linux and the sniper option, Steam: SDK, `store`
  setting, depots, `StoreService` example, a "Before you ship" list and "Known limits") and an "Export it"
  section in the two Tetris tutorials, all EN/TR.
  Open: the guide states what is unverified (real-certificate signing and notarization, the Steam backend,
  Steam Input glyphs, the sniper container check and job) and must be revised when M8-6 and M8-7 close those;
  `first-agent-session.md` ends without a running game and has no export section.

## UI — Game interface ✅

Games have no way to draw a HUD or a menu: the editor's Dear ImGui is not shipped in games. Design in
ADR 0057: UI elements are Entities with saved Components under a `UiCanvas`, text is drawn from signed
distance field fonts, and a batched pass draws the UI after tonemapping. Public API in
`illusion/ui/UiGeometry.hpp`, `illusion/ui/UiDrawList.hpp`, `illusion/ui/UiComponents.hpp`,
`illusion/assets/Font.hpp` and `illusion/render/UiRenderer.hpp`.

- UI-1a: CPU foundation. `Font` in `engine/assets` (`bake()` with `stb_truetype`, `serializeFont()`); the new
  library `engine/ui` (`illusion::ui`, `illusion::ui_api`) with `UiGeometry` (placement, canvas scale, safe
  area, transforms) and `UiDrawList` (rectangles, images, nine-slice, text with wrapping, alignment and
  kerning, clip stack, batching). Done: unit tests of baking (determinism, missing code points, a full
  atlas, round trip of the cooked form), of the geometry at several aspect ratios and of every primitive
  (vertex counts, batching, clipping, a full list drops whole primitives); allocation tests of recording and
  measuring; benchmarks `ui/text-layout` and `ui/draw-list-quads` with budgets; `UiComponents.hpp` compiles
  in the library.
  ✅ Done as described: `stb_truetype` is compiled privately in `illusion::assets` behind a bounds check of
  the font's tables; `illusion::ui_api` (`USES ui`) is in the module SDK and carries no `Font`; the tests use
  the Roboto file the build already downloads (`ILLUSION_TEST_FONT`); `ui/text-layout` 120 ns per glyph
  (budget 1,000), `ui/draw-list-quads` 34 ns per quad (budget 250).
  Open: built and tested only with MSVC and clang-cl on Windows before CI; a bake gives the same bytes within
  one build, not across compilers (the field is computed in floating point), so golden images of text need a
  tolerance; the bounds check covers the tables `stb_truetype` indexes, not composite glyph data, so fonts
  are trusted project files, baked at cook time; a carriage return draws the fallback glyph; trailing spaces
  count in a line's width.
- UI-1b: The pass. `UiRenderer` in `engine/render` (`Ui.slang`, one pipeline for images and distance fields,
  premultiplied blending, scissor per command), `AssetType::Font` with cooking and import settings
  (ranges, pixel size, spread); the player and the Game view draw a session's UI in UI-2a, when a session
  has one. Done: GPU golden images of text at three
  sizes, a nine-slice panel and clipping, at two target sizes with the same canvas; zero validation errors; a
  steady frame of the pass allocates nothing; benchmark of `render()` for a HUD-sized list.
  ✅ Done: six golden images (`ui-{text,panel,clip}-{1280x720,1920x1080}.png`); fonts are an asset kind
  with `FontImportSettings` sidecars, cooked by `cookAssets()` and loaded by `AssetStreamer::loadFont()`; the
  Asset Browser shows pixel size and spread; `ui/render-hud` 67 µs for 195 quads with submit and wait (budget
  500 µs). Open: the pass's own code allocates nothing, but NVRHI allocates a constant handful per frame for
  the two buffer uploads (the test asserts the count does not grow; zero needs persistently mapped buffers);
  made and verified on an RTX 4070 only before CI (lavapipe and MoltenVK first run there); the Asset
  Browser has no editor for code point ranges; images registered with `addImage()` must not be streamed
  textures (their GPU texture is replaced when levels change).
- UI-2a: Elements and layout. The saved Components of `UiComponents.hpp` reflected as `illusion.Ui*` with
  metadata and icons, the UI System in `GameSession` (layout only when something changed, draw list per
  session), font and image resolution through `RenderAssetCache`, the session's `Localization` from
  `assets:/locales` for `UiText::key`, the player and the editor's Game view draw the session's UI. Done:
  layout tests (anchors, stacks, grids, nested transforms, `Inactive`, canvas order), an allocation test of
  a steady frame, benchmark `ui/layout` for 1,000 elements, the showcase scene has a HUD with every
  Component and non-default fields, the golden image of the showcase game is updated, a project template
  ships a font.
  ✅ Done: `registerUiComponents()`, `UiWorld` (`illusion/ui/UiWorld.hpp`), `UiRenderAssets` in
  `engine/render` (fonts through `AssetStreamer::loadFont()`, images uploaded as sRGB textures),
  `GameSession::ui()`, `setUiFrame()` and `localization()`, the player and the Game view draw the UI;
  `ui/layout` 544 ns per element (budget 5,000), `ui/layout-reused` 408 ns (budget 3,000).
  Open: code that writes a UI Component through a reference must call `world.markChanged<T>()` (the
  `UiService` of UI-3a does it for modules); a font or image that fails to load re-runs the layout every
  frame; canvases with different scales share the draw list in the first canvas's units, and only that
  canvas snaps glyphs to pixels; the player draws the UI into the view at the render scale, so a render
  scale below 1 blurs it (it should be drawn after the upscale; M9); the session's locale stays English
  until something calls `setLocale()` (a language setting comes with UI-3a); the Scene view does not draw
  the UI (UI-3b).
- UI-2b: Interaction. `UiWidgets.hpp`: `UiInteractable` (hover, pressed, focus, disabled, state tints,
  explicit neighbours), `UiButton`, `UiToggle`, `UiSlider`, the runtime `UiInteraction`; `UiInput` and
  `UiWorld::update(frame, input)`, pointer hit tests (topmost element, transforms and clips respected), focus
  and navigation through the actions `ui.navigate`, `ui.submit`, `ui.cancel` (default bindings added to input
  assets, repeat while held), modal canvases (the game's other actions see no input), `UiWorld::events()`.
  Done: tests drive a menu with a virtual gamepad, the keyboard and the mouse through a `GameSession`; a
  headless session handles input without a renderer; the showcase HUD has a button, a toggle and a slider;
  allocation test of a steady frame with input; benchmark `ui/hit-test`.
  ✅ Done as described, with `InputActions::setExclusivePrefix()` for the modal mask, the 2D gamepad input
  `Stick.DPad` (`ui.navigate` needs the D-pad as a vector) and `GameSession::setUiFrame(frame, pointerScale)`;
  `ui/hit-test` 58 µs for 1,000 buttons (budget 400 µs).
  The project templates ship `assets/input/game.input` with the three `ui.*` actions (UI follow-up; a test
  keeps them equal to the defaults `GameSession::create()` still adds to older projects at run time).
  Open: the Game view's pointer mapping is untested in the running editor (only through `GameSession` in
  tests).
- UI-2c: Text fields, scrolling and glyphs. `UiTextField` (UTF-8 editing from `InputState::text()`, caret,
  selection by keyboard, maximum length), `UiScrollView` (wheel, drag, navigation keeps the focus in view;
  virtualised lists for long content), `UiGlyph` (the current binding of an action for the device used
  last, as its label until glyph images exist). Done: tests of editing with multi-byte text, scrolling
  with clipping, and glyph labels that change with the device.
  ✅ Done: `UiTextField`, `UiScrollView`, `UiGlyph` (`UiGlyphSource` over the session's `InputActions`),
  `InputActions::gamepadVector()` and `bindingsVersion()`; while a field has the focus the game's actions
  are masked and typed keys go to the field; `ui/scroll-1000` 43 µs (budget 300 µs).
  Open: no text selection or clipboard; lists are not recycled (rows outside a scroll view are skipped for
  hit tests and drawing but exist and are laid out); glyphs are labels, not images, and English unless the
  project translates the `input.*` keys; no IME composition display.
- UI-3a: Scripting. `UiService` (`USES ui`) with its adapter: find elements, set text (literal or key with
  arguments), values, visibility and focus, instantiate a UI prefab, poll events; a way for a module to end
  the game (open from M8-1). Done: counting proxy and the showcase module call the whole API;
  `check_api_coverage.py` passes; the Tetris acceptance game shows its score and a game-over menu.
  ✅ Done: `UiService` (17 functions) with `UiWorldService`, keyed texts with arguments (`UiTextArguments`),
  `setLanguage()`, `SceneService::requestExit()` (the player exits, Play mode stops), module API version 4,
  the showcase module drives its HUD; `ui/set-text-key` 2.6 µs (budget 20 µs).
  Open: the Tetris acceptance game still has no score display or game-over menu (its recorded transcripts
  need a new agent session); UI prefabs are instantiated with `SceneService::instantiate()`, there is no
  UI-specific function.
- UI-3b: Editor commands and preview. Commands `gameui.create` (a canvas or an element of a kind with
  sensible defaults: panel, text, button, toggle, slider, text field, scroll view, stack, grid; undoable),
  `gameui.layout` (computed rectangles of the edit World or the play World at a target size), `gameui.click`
  and `gameui.focus` (Play mode: act on an element as the pointer or navigation would), with reference and
  goldens; the Game view draws the edit World's UI while not playing; Add menu entries. Done: command cases
  and the fuzz test cover the commands; a GPU golden of the Game view showing the showcase HUD outside Play
  mode; an agent transcript builds a menu, enters Play mode, clicks a button and reads the result.
  ✅ Done: 190 commands; `gameui.click` steps the session twice (press, release) and returns the UI's
  events; the edit World is laid out by a preview `UiWorld` that writes no saved Component
  (`UiWorldDesc::preview`, `UiWorld::layout()`, `hitTest()`); `GameSession::setUiPointer()`;
  `loadGameLocalization()` shared by the session and the editor's preview; transcript
  `tests/acceptance/game-ui/menu.jsonl` (CTest `acceptance.game_ui.menu`).
  Open: `gameui.create` does not select what it creates (as `entity.create`); the Scene view draws no UI.
- UI-3d: Canvas editing. Selection outlines and placement handles for UI Entities in the Game view (move,
  resize, anchors), anchor presets and placement drawers in the Inspector, picking and framing of UI
  Entities. Done: UiDriver tests drag an element and change its anchors; a golden image.
  ✅ Done: 193 commands (`gameui.pick`, `gameui.place`, `gameui.set_anchors`); `UiPlacementEdit.hpp` (anchor
  presets, picking, handle hit tests and drags, GPU-free); the Game view selects the topmost element, draws
  the outline, eight resize handles and anchor markers and drags as one transaction (snap 1 unit, 8 with Ctrl,
  none with Alt; Esc or right click cancels; children of a stack or grid only resize); the Inspector has anchor
  presets and placement fields; theme tokens `canvas.*`; `gameui/pick-and-handles` 3.3 µs (budget 20 µs).
  Open: zoom and pan and framing of UI Entities in the Game view; the Scene view draws no UI.
- UI-3c: Transitions and text effects. `UiTransition` (offset, scale, opacity and tint eased between rest
  and a changed state on show, hover, press, focus or by `UiService::playTransition()`), text outline and
  drop shadow from the distance field (`UiText::outlineWidth`, `shadowOffset`). Done: deterministic
  transition tests with fixed steps; GPU golden of outlined and shadowed text; the showcase HUD uses both;
  module API version 5.
  ✅ Done: `UiTransition` with the runtime `UiTransitionState`, `UiService::playTransition()` and
  `transitionProgress()`, outlines as push constants of a draw command and the shadow as a second text draw
  (ADR 0057 addendum); `ui/transitions-1000` measures 81 µs for 1,000 running transitions (budget 500 µs).
- UI-3e: The demo. The tech demo gets a HUD and a pause menu (Resume, a volume slider, Quit) driven by its
  module through `UiService`; the Tetris acceptance game shows its score and a game-over menu. Done: golden
  image of the demo's pause menu; the acceptance transcripts cover the menu.
  ✅ Done: the tech demo's HUD and modal pause menu are `assets/scenes/ui.scene`, instantiated by its module
  (Resume, master volume, language, Quit; frame-time readout by key); `TestTechDemo.cpp` and the golden
  `techdemo-pause-menu.png` run from committed files only; the Tetris reference module shows points, lines
  and level and a game-over menu, checked by `checks-ui.jsonl` (C9) on the scripted runs.
  Open: the pause menu has not been played over the real forest scene in CI (its assets are external); the
  recorded Tetris agent session predates the UI contract: it still passes `checks.jsonl` (whose entity
  counts became minimums) but is not run against C9 until an agent session is recorded again. The headless
  editor without a GPU now derives the edit World's Inactive tags after every change (UI follow-up), so
  `gameui.layout` and `gameui.pick` skip disabled canvases there too.
- UI-4: Documentation (EN/TR), in step with each pull request: manual page "Game UI", the scripting API
  reference, a tutorial that adds a HUD and a pause menu, the architecture page.
  ✅ Done: manual page `manual/ui.md`, the tutorial `tutorials/hud-and-pause-menu.md` (its module code is
  built and loaded by the tests), the architecture page `architecture/game-ui.md`, the scripting API
  reference, the manual's Play mode screenshot with the showcase HUD.
  Open: the tutorial's editor steps and its agent commands were checked against the code, not replayed; the
  tutorial module's UI logic is compiled and loaded but runs without a `UiService` in its test.

Open (recorded by ADR 0057): right-to-left text and shaping, CJK with dynamic atlas pages, multi-channel
distance fields, world-space UI, HDR output of the UI pass (M9).

## SAVE — Saved games ✅

- Save slots written to `user:/` through the VFS: chosen Components and module state serialized with the
  reflection, versioned with migrations, checksummed, written atomically in the background; screenshots and
  metadata per slot
- `SaveService` for game modules; Steam Cloud through the Steamworks module when present (M8); the UI's
  pause menu gets Save and Load; tests for corruption, versions and interrupted writes

Design: [ADR 0058](docs/adr/0058-saved-games.md). Work packages, one pull request each, in order:

- SAVE-1 (`implementer`): The file and the delta, without threads. `SceneSaveOptions::externalReferences`,
  `SceneLoadOptions::replace` and `resolveExternal` in `SceneSerializer`; `VirtualFileSystem::writeFileAtomic()`
  (platform code behind one function); the library `engine/save` (`illusion::save`) with the `Saved` tag
  (`SaveComponents.hpp`, `registerSaveComponents()`, a value in `samples/showcase/build.jsonl`), the
  container (`encodeSaveFile()`, `decodeSaveFile()`, metadata) and `captureSave()` / `applySave()`.
  Done: round trips of tagged subtrees with unsaved parents, spawned and destroyed entities and prefab
  instances; a failed apply changes nothing; every flipped byte and every truncation of a file is `Corrupt`;
  a newer container is `NewerVersion`; a type migration runs on apply; benchmarks `save/capture-1000` and
  `save/apply-1000`.
  ✅ Done: the three serializer options (defaults keep every existing file byte for byte); `writeFileAtomic()`
  (`CreateFileW` + `FlushFileBuffers` + `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on
  Windows, `fsync` (`F_FULLFSYNC` on macOS) + `rename` + `fsync` of the directory elsewhere); `engine/save`
  with `SaveFile.hpp` (24-byte header, payload = u32 metadata size, metadata JSON, World JSON;
  `isValidSaveMetadata()`, `decodeSaveMetadata()`) and `SaveCapture.hpp` (`collectSavedEntities()`,
  `captureSave()`, `applySave()` returning `Expected<..., SaveFailure>` with the serializer's message); the
  showcase Player is `Saved`. A flipped byte of the version field reads as `NewerVersion` (or `Corrupt` when
  it becomes 0), since a newer header cannot be checked; every other flipped byte and every truncation is
  `Corrupt`. `save/capture-1000` 1.9 ms and `save/apply-1000` 1.8 ms for 1,000 saved entities in a World of
  2,000 (budgets 15 ms each).
  Open: the `Saved` tag's manual anchor points at "Scene deltas" in `manual/scenes.md` until SAVE-4 writes
  the "Saved games" page; `writeFileAtomic()` was run on Windows only (the POSIX path is built by CI); a
  non-saved entity whose Entity field points at a saved one keeps a dead handle after `applySave()`.
- SAVE-2 (`implementer`): The service. `SaveSystem` (save thread, queue, slot list, `.bak` fallback,
  `waitIdle()`), `SaveService` with its adapter and `USES save` (module API version 6), the apply point in
  `GameSession::update()`, `GameSessionDesc::saves`; the player uses `user:/saves`, Play mode `cache:/saves`;
  the showcase module calls the whole API. Done: tests stop a write after each step (temporary file, flush,
  backup, rename) through a failing file layer and find the previous save; requests keep their order; no
  test sleeps; `slots()` and `pollEvents()` do not allocate.
  ✅ Done: `SaveService` (`USES save`, header-only `illusion::save_api`, module API version 6) with
  `SaveEvent`, `SaveSlotInfo`, `SaveSlotState`; `SaveSystem` (`SaveSystem.hpp`): one save thread started on
  first use, FIFO queue with tickets, slot list scanned once and updated by every request (`copySlots()`,
  `slotsVersion()`), `<slot>.save.bak` written atomically before a valid file is replaced, fallback to it for a
  missing or corrupt file (never for `NewerVersion`), `waitIdle()`, a destructor that finishes queued writes
  and removals and drops queued loads; file operations behind `SaveStorage` (default `VfsSaveStorage`), the
  clock in `SaveSystemDesc::clock`. `SaveSystemService` is the adapter: requests are kept until the Saves step
  of `GameSession::update()` (after Modules, before Physics), which applies finished loads, turns finished
  requests into the next update's events, then captures and sends this update's requests in call order; with
  `fixedStep` it waits for the thread first, so results always arrive one update later and fixed-step runs
  repeat. `GameSession::stop()` drops unsent requests and ignores the results of sent ones (writes still
  finish, no load is applied); `GameSession::saves()` gives host tools the service. The player saves in
  `user:/saves` and waits for the thread before it exits; Play mode uses `cache:/saves` (one `SaveSystem` per
  session). The showcase writes, removes and then loads a slot at frame 240 (the load always fails with
  `NotFound`, so the World is never replaced; events and the slot list only go into `showcase.Runtime`); the
  showcase determinism test now runs with a `SaveSystem`. Tests stop a replacing write after each of six steps
  (backup temporary file, flush, rename; slot temporary file, flush, rename) and load the previous save after
  a restart.
  Open: `SaveEvent` does not say which request failed (save, load or remove) beyond the slot; stale
  `*.tmp-*` files of a write interrupted by a crash are ignored but never deleted; `SaveSystem` creates its
  directory on the constructing thread; the save thread was run on Windows only (TSan runs in CI); no
  benchmark of the thread hand-off (the SAVE-1 benchmarks measure capture and apply, the rest is file I/O).
- SAVE-3 (`implementer`): Screenshots and cloud. `SaveSystemDesc::thumbnail` supplied by the player and by
  `PlaySession`, the sidecar `<slot>.png` and `SaveSlotInfo::screenshot` shown by a `UiImage`;
  `SaveSettings` (`settings:/save.json`) and the cloud mirror through `StoreService`. Done: a GPU test saves
  a slot with its image; cloud tests with a test store cover newer-local, newer-cloud, a corrupt cloud file
  and no quota.
  ✅ Done: `SaveSystem::write()` calls `SaveSystemDesc::thumbnail` on the calling thread (its buffer reused,
  nothing without a function) and the save thread writes the PNG sidecar `<slot>.png` after the slot file
  (`encodePng`, `illusion::assets` linked privately); a failed image is logged, drops the previous save's image
  and never fails the save; the scan and every request keep `SaveSlotRecord::screenshot` current, `remove()`
  and `importFile()` delete the image. `FrameCapture` (`illusion/render/FrameCapture.hpp`: a blocking
  `ReadbackRing` read and `downscaleRgba8Srgb()`, centre crop to 16:9 and a box filter in linear light) is
  shared by the player's `--screenshot` and its slot images, and by Play mode
  (`PlaySession::setSaveThumbnail()`, which the editor sets to the Game view's target, windowed and headless
  with a GPU); only a save reads the frame back. `SaveSettings` (`settings:/save.json`, `cloud`, default on) is
  reflected and loaded with the other project settings and registered in the editor's settings catalogue; the
  showcase turns it off. `StoreBackend::cloudIsRemote()` (null store false, Steam true) and
  `GameSessionDesc::storeCloudIsRemote` gate the cloud mirror in `SaveSystemService`: the first update imports
  each valid `<slot>.save` cloud file (`SaveSystem::importFile()`: newer `savedAt` wins, the local file becomes
  the backup, equal or older changes nothing, a local `NewerVersion` file is kept), saved writes (with
  `keepFile`) go to `writeCloudFile()` and removals to `removeCloudFile()`; imports make no events and do not
  count for `busy()`, cloud failures are only logged; with a cloud, `GameSession::stop()` (`cancel()`) waits for
  the save thread and mirrors the last writes and removals, so "save and quit" reaches the cloud. `SaveCompletion` says which call made it
  (`SaveRequestType`). Tests: the image sidecar (written, listed after a restart, dropped by a save without
  one, removed, a failing image step, a wrong size), a write stopped after each of nine steps (six of the slot,
  three of the image), cloud newer/older/equal/corrupt/only in the cloud, a full cloud, mirrored removals,
  `cancel()` during imports, the session's gating (remote, setting, availability, null store), the CPU scaling,
  and a GPU test that saves a slot from a rendered frame and decodes a 320 x 180 PNG.
  Open: a local slot that is newer than its cloud file or absent from the cloud is not uploaded at start; stale
  `*.tmp-*` files are still never deleted; `UiRenderAssets` caches an image by its path for good, so a `UiImage`
  showing `SaveSlotInfo::screenshot` keeps the first image of a slot after the slot is saved again (it needs a
  reload of the file through `AssetStreamer::reload()` and a cache that drops reloaded images); the player's
  streamer trusts its packed cache (`verifySources` off), which `user:` images are not part of, untested; the
  Game view image in Play mode is the last one the editor rendered, stale while the Game panel is hidden.
- SAVE-4 (`mechanic`): Editor and documentation. Commands `saves.list`, `saves.write`, `saves.load`,
  `saves.remove` (reference, goldens, fuzz); Save and Load in the tech demo's pause menu; EN/TR manual page
  "Saved games", the scripting API reference, a section in the HUD tutorial, the architecture page,
  `CHANGELOG.md`.
  ✅ Done: `saves.list` (Play mode, sync) and the async `saves.write`, `saves.load`, `saves.remove`
  (`SavesCommands.cpp`; `CommandMode::Play`, so they fail with `wrong_mode` without a session and with
  `precondition_failed` in `simulate` or without a save directory). A task queues its request in its first
  update and finishes with the event of that slot; `PlayHost` gained `saves()`, `saveEventCount()` and
  `saveEvents()`, which `PlaySession` fills after every update it runs (a log of the last 256 events), so steps
  and several updates per frame lose no event. Validation per field (slot name, title, custom, playSeconds);
  the fake play host of the core tests has an in-memory `SaveService`; `TestSavesCommands.cpp` covers write and
  list, load restoring a changed Saved entity, remove, the mode errors, an ended session and invalid values.
  Tech demo: Save game and Load game in the pause menu (Load dimmed while no slot exists) and a status line,
  one slot `slot1`; the Player, the Sky, the orbs and the lanterns are `Saved` in `tools/make_scene.py` (scenes
  rebuilt), the module finds them again after `Loaded` and `LoadedBackup`, strings in both locales, a play test
  that saves, moves, loads and checks; the golden `techdemo-pause-menu` was regenerated because the menu has
  three more rows. Docs: the manual page "Saved games" (EN/TR, nav, the `Saved` tag's `docsAnchor` now
  `manual/saved-games.md#the-saved-tag`), a section in the Play mode and Tech Demo pages and the architecture
  overview, "Save and load" in the HUD tutorial (its module now saves a `Progress` Component), `CHANGELOG.md`.
  Open: no acceptance transcript for the `saves.*` commands (the editor tests cover them); `saves.write` and the
  others wait for the game to update, so a paused game needs `play.step`; the tech demo does not save the
  walker's yaw and pitch (runtime `techdemo.Player`) and the golden image was made on a desktop GPU, not on
  lavapipe.

Open (recorded by ADR 0058): velocities of dynamic bodies, live particles and playing sounds are not saved;
compression and a binary World section; several level deltas in one slot; the Steam cloud path is untested
until a CI job has the SDK.

## ANIM — Skeletal animation ✅

After DEMO, independent of GFX. Characters, creatures and machines that move with bones, at AAA quality.
Nothing of it exists yet: glTF skins and animations are skipped on import and the vertex has no bone data.

- ✅ ANIM-1: Import and playback, designed in ADR 0059 (skins and clips in the cooked model, poses in an
  `AnimationWorld` outside the ECS, skinning once per frame in a compute pass). Three work packages:
- ANIM-1a (`implementer`): Assets and pose math. `loadGltf()` imports skins (`Skeleton`), animations
  (`AnimationClip`, channels by joint name, Step/Linear/CubicSpline) and the `SkinVertex` stream with
  `jointBounds` (`illusion/assets/Animation.hpp`); model format 6; fragments `#skin/N`, `#animation/<name>`
  and `AssetKind::Skeleton`/`AnimationClip`; library `engine/animation` with the pure functions of
  `illusion/animation/Pose.hpp`. Done: import tests on a small rigged glTF in `tests/data` (no third-party
  asset needed), sampling tests for every interpolation and for clip ends and loops, the round trip through
  the cooked format, benchmark `animation/evaluate-300x60` (sample + compose + skin matrices, budget from the
  measurement as AGENTS.md says).
  ✅ Done: `loadGltf()` imports every skin of up to `kMaxJoints` joints as a `Skeleton` (joints sorted by depth so
  parents come first, parents remapped to the nearest joint ancestor, rest TRS from the node or its matrix,
  inverse bind matrices or identity, `rootTransform` from the world matrix of the root joints' common
  ancestor), JOINTS_0/1 and WEIGHTS_0/1 (u8/u16, float/unorm) into `MeshData::skin` for the skin of the first
  node that draws the mesh with one (the four largest influences, renormalized and quantized to sum to 65535;
  invalid data leaves the mesh rigid with a warning), `jointBounds` and `skeleton`; animations keep their
  translation, rotation and scale channels on joints (other nodes and morph weights are skipped with a log
  line, channels with mismatched or non-increasing keys with a warning), sorted by joint name then path, with
  unique names (`<name>.2` for repeats, `animation<N>` for none). `scaleModel()` scales joint translations,
  inverse binds, `rootTransform`, `jointBounds` and translation keys; skinned meshes are never card-thinned
  and `clearLods()` keeps `skin` parallel. Model format 6 rejects joint indices out of range, weights that do
  not sum to 65535, parents at or after their joint, more than `kMaxJoints` joints, empty or non-increasing
  times, value counts that do not match and clips shorter than their keys. `AssetFragment::Skin` and
  `Animation`, `AssetKind::Skeleton` and `AnimationClip`; mesh resolvers (render, physics, viewport snapping)
  reject the new fragments. `engine/animation` (`illusion::animation`): `bindClip()` (by name; inconsistent
  channels stay unbound), `clipTime()`, `restPose()`, `sampleClip()` (binary search, Step, Linear with
  shortest-arc slerp, glTF CubicSpline), `localToModel()`, `skinMatrices()`, `skinnedBounds()`; none allocates.
  Test data `tests/data/gltf/rigged.gltf` (from `generate.py`). `animation/evaluate-300x60`: 1.5 ms on one
  thread (budget 10 ms).
  Open: the physics Joint Component already owns `illusion::Joint`, so a skeleton's joint is `SkeletonJoint`;
  a mesh drawn by nodes with different skins is skinned for the first only; nodes between two joints are
  ignored with a warning; no `_api` target or SDK export for `engine/animation` until a module API needs it
  (ANIM-2), and the player links it with ANIM-1b.
- ANIM-1b (`implementer`): Runtime. Saved `Animator` (`illusion.Animator`) and runtime `AnimationPose`, the
  resolver, `AnimationWorld` (pose slots, cached clip bindings, double-buffered skin matrices, world bounds)
  with its step in `GameSession::update()` after Saves; the editor evaluates the saved `time` without
  advancing it; a value in the showcase's `build.jsonl`. Done: determinism with `fixedStep`, a saved game
  restores `time`, a mismatched skeleton draws the bind pose, no allocation per update.
  ✅ Done: saved `Animator` (`illusion.Animator`: `skeleton`, `clip`, `speed`, `time`, `loop`, `playing`; asset kinds
  Skeleton and AnimationClip, `time` in seconds) and runtime `AnimationPose` (`registerAnimationComponents()`,
  registered by the player and the editor). `AnimationWorld` (`illusion/animation/AnimationWorld.hpp`) loads each
  model file once through the `AssetStreamer` and keeps copies of its skeletons, clips and the `jointBounds` of
  its meshes, binds each (skeleton, clip) pair once, gives each Animator a slot (reused through a free list, with
  a joint range in contiguous local, model and double-buffered skin matrix arrays), advances `time` when asked
  (wrapped or clamped; a clip that ends stops `playing`), poses every slot with `parallelFor` and writes world
  bounds from `skinnedBounds()` and the Entity's `Transform` (the joints' box without a mesh). The mesh is
  `MeshRenderer::mesh`, read through reflection so that `engine/render` can link `engine/animation`. Missing
  assets are counted and logged once; a skeleton that does not fit the mesh (another skin, joint count or a
  rigid mesh) is logged once and gets no pose; Inactive entities keep pose and time; a reloaded model binds its
  Animators again (a failed reload keeps the data). `GameSession` owns one with a streamer (`animation()`), steps
  it after Saves and before Physics with `advance` while started; the editor's viewport poses the edit World at
  the saved `time`. `engine/runtime` and the player link `illusion::animation`. Showcase: a hidden skinned
  `Column` with an `Animator` (`assets/models/column.gltf` from `make_assets.py`). Tests in
  `tests/animation/TestAnimationWorld.cpp`; benchmark `animation/world-update-300x60`.
  Open: an Animator's pose is withdrawn (bind pose) while a changed clip or skeleton loads; time does not
  advance until the model is loaded, so a fixed-step run waits for its models (`waitIdle()`) to repeat; the
  edit World is posed only by the GPU viewport (not by a headless editor); the showcase `Column` is hidden until
  ANIM-1c draws skinned meshes (its golden images are unchanged).
- ANIM-1c (`implementer`): GPU skinning. The compute pass, pooled per-instance vertex buffers in the
  `Vertex` layout with previous positions, every pass (prepass, shadows, forward, motion vectors) drawing
  skinned instances, culling by the skinned bounds. Done: GPU golden test of a posed character (lavapipe),
  motion vectors of a moving joint, validation errors zero, a GPU benchmark of 300 skinned instances, the
  manual page "Animation" (EN/TR) and the architecture page.
  ✅ Done: `RenderResources::createMesh()` uploads `MeshData::skin` into a shared skin pool (`GpuMesh::skinOffset`,
  `skinCount`; the vertex pool gains raw views for the pass). `RenderView::animation` gives `SceneRenderer` the
  poses (null: bind poses); the player, Play mode, the editor's viewports (edit World, snap probe, camera
  preview) and the glTF viewer (which now loops a model's first clip) pass theirs. Culling takes entities with
  an `AnimationPose` on the render thread: a fresh pose (`frame` equals `AnimationWorld::frame()`, skin matrices
  of `jointCount`) on a skinned mesh is culled by `AnimationPose::bounds` and, when it reaches the camera or a
  shadow view, gets a range of the renderer's skinned vertex buffers (`RangeAllocator`, kept while the pose
  lasts, returned when it goes) and a dispatch of `Skinning.slang` at the start of the frame: bind-pose vertices
  from the vertex pool, `SkinVertex` from the skin pool, this frame's and the previous frame's skin matrices
  from one per-frame buffer, out to the `Vertex` layout plus a `Vec4` previous position per vertex (no UAV
  barriers between instances). Skinned opaque items follow the GPU-culled region of the camera order and are
  drawn one by one in the prepass (before the phase-2 Hi-Z), shading and wireframe with the per-material
  pipelines; blended ones, shadow casters and entity IDs draw from the range too (vertex buffer bound at the
  range, base vertex 0); the forward vertex shader reads previous positions (`t10`, push constant
  `kForwardSkinned`) for motion vectors. `RenderStats::skinnedInstances`/`skinnedVertices`,
  `RenderGpuTimings::skinning`, `SceneRenderer::skinnedVertices()` (range, for tools and tests). Tests in
  `tests/gpu/TestSkinning.cpp` (goldens `skinned-rigged-0s`/`-1s`, GPU equals CPU skinning, motion of a moving
  and a still joint, frustum culling and range lifetime, posed shadows, steady allocations independent of the
  instance count, 300 x 10,240 vertices: 0.66 ms on an RTX 4070 SUPER, budget 3 ms). The showcase `Column` is
  visible (goldens `showcase-play-300`, `showcase-game`); manual page "Animation", the scene frame on the
  architecture overview; the Animator's docs anchor is `manual/animation.md#the-animator`.
  Open: skinned instances are neither occlusion culled nor GPU-instanced (each is one draw per pass); a mesh of
  several submeshes is skinned whole for each submesh's Entity; the editor's "pick behind" and frame-selection
  bounds still use the bind-pose bounds; the steady allocation test shows NVRHI's own allocations per frame,
  equal with one and four instances.
- ✅ ANIM-2: Blending and control, designed in ADR 0060 (layered state machines as `.animgraph` assets driven by
  parameters, evaluated per Animator; root motion into the CharacterController; `AnimationService`). Three
  work packages:
- ANIM-2a (`implementer`): Graph data and evaluation without a World. `AnimationGraph.hpp`
  (`parseAnimationGraph()` with validation and messages naming the JSON path, Delaunay triangulation of 2D
  blend spaces at load) and `AnimationGraphState.hpp` (`AnimationGraphState`, `GraphContext`, `stepGraph()`,
  `evaluateGraph()`: transitions with conditions, exit times and cross-fades, triggers, 1D/2D blend spaces in
  sync, layers with masks and additive mode, events, root motion extraction), the blend functions added to
  `Pose.hpp`. Done: parser tests for every error, evaluation tests per feature against hand-computed poses,
  determinism, no allocation in a steady step and evaluation, benchmark `animation/graph-300` single-threaded.
  ✅ Done: `parseAnimationGraph()` reads `illusion.animgraph` version 1 (unknown keys are errors; every message
  names its JSON path, e.g. `layers[0].transitions[1].conditions[0].parameter: unknown parameter 'sped'`):
  unique non-empty names (`*` is reserved), references by name, finite values, weights and normalized times
  in [0, 1], durations and speeds >= 0, compares by type (Float: Greater/Less; Int: all four; Bool:
  Equal/NotEqual with true/false; a Trigger takes no compare or value), Int values whole and within 2^24, clip
  paths with an `#animation/` fragment, Blend1D points sorted and distinct, Blend2D points distinct and not
  collinear, events sorted by time; Blend2D points are triangulated at load (Bowyer-Watson in f64, insertion
  in file order, counter-clockwise triangles sorted, checked to cover the hull); `writeAnimationGraph()`
  omits defaults and round-trips exactly. `bindGraph()` resolves clips (in file order), per-joint layer masks
  (each entry sets its subtree, a later entry wins) and the root motion joint, failing with the path of the
  missing clip or joint. `stepGraph()`: a running cross-fade advances and ends, otherwise the first
  transition in priority order whose conditions hold fires (an any-state transition never targets the
  current state; an exit time holds until the source's normalized time reaches it or a looping source
  wraps), consuming its triggers; unconsumed triggers clear at the end; normalized times advance by speed
  times the speed parameter (never backwards) over the clip's or blend space's weighted duration; events of
  states with weight > 0 crossed in [before, after) incl. one loop wrap, written in layer order and counted
  beyond the span. `evaluateGraph()`: 1D neighbours, 2D barycentric (nearest triangulation edge outside the
  hull), blend clips synchronized on normalized time, cross-fades by weight, `kMaxActiveClips` (lightest by
  weight times layer weight dropped, their layers renormalized), incremental nlerp blending, Override layers
  through `blendPoses()` and Additive ones through `addPose()` against each clip's first frame, root motion
  per clip (wrap: end - before + after - start; translation x/z and yaw of the joint in model space,
  weighted and layered like the pose) and its removal (the joint is put back to its rest pose's horizontal
  position and yaw in model space); scratch from the caller's `LinearAllocator`, rewound on return.
  `Pose.hpp` gains `blendPoses()` and `addPose()`. Tests in `tests/animation/TestAnimationGraph.cpp` and
  `TestAnimationGraphState.cpp`; `animation/graph-300`: 6.5 ms on one thread (budget 50 ms).
  Open: no `GraphContext` (the header's `GraphBinding` plays its part); `AnimationGraphState::triggers` marks
  non-trigger parameters with `kNotTrigger` so that `setValue()` can reject triggers without the graph;
  playing backwards (negative speed) holds the state; a step longer than one loop fires each event once and
  takes one loop's root motion; a zero-length clip advances one loop per step; an instant transition
  (duration 0) drops the old state's root motion of the step it fires in; the new state of a transition
  starts moving on the next step.
- ANIM-2b (`implementer`): Runtime and API. `Animator::graph`, graph states in `AnimationWorld` (per-worker
  scratch from a `LinearAllocator`, events published per update, `AnimationPose::rootDelta`), root motion
  applied in `GameSession` (CharacterController velocity or Transform), `AnimationService` with its adapter
  `AnimationWorldService` (`USES animation`, `illusion::animation_api`, module API version 7, SDK export), the
  showcase module calls the whole API and `build.jsonl` sets `graph`, `AssetKind::AnimationGraph`, cooking or
  packing of `.animgraph` files for export. Done: fixed-step determinism, root motion moves a character on a
  slope test scene, events reach a module, live reload of a graph file restarts its Animators.
  ✅ Done: `Animator::graph` (`AssetKind::AnimationGraph`; the editor's asset browser lists `.animgraph` files
  as `AnimationGraph`, exports ship them as they are) drives the Animator instead of its clip: `AnimationWorld`
  loads each graph once through the `AssetStreamer` (decoder `illusion.animgraph`), binds it once per skeleton
  (`GraphBinding`, bound again after a model reload), keeps one `AnimationGraphState` per slot that starts in
  the entry states at `Animator::time` (seconds, through `graphStateDuration()`), steps it by `speed` times the
  seconds while advancing and `playing` (held otherwise; without advancing it shows the entry states, so the
  editor scrubs `time`) and evaluates it in the existing `parallelFor` with per-worker `LinearAllocator`s
  (sized with `graphScratchBytes()` when a graph is bound); the events of an update are gathered per slot
  (room for every event a step can cross, reserved at binding) and published in update order (`events()`);
  root motion is turned into world space by the Entity's Transform (rotation, scale, mirroring flips the yaw)
  into `AnimationPose::rootTranslation`/`rootYaw`. A changed graph file restarts its Animators; a broken one
  keeps the loaded graph; a graph that fails to load or bind is reported and shows the rest pose. Animators
  without a graph cross-fade after `crossFade()` (the previous clip, its time and the fade in the slot); a
  loaded model keeps all its clips, so `play()` to another clip of it needs no load. `GameSession` applies
  root motion after the Animation step while started (a `CharacterController` gets the horizontal velocity and
  keeps its vertical one; other Entities move, through `LocalTransform` under a parent; both turn by the yaw)
  and provides `AnimationService` (`AnimationWorldService`) with a streamer; `USES animation`
  (`illusion::animation_api`, in the SDK), module API version 7. The showcase `Column` plays
  `assets/animations/column.animgraph` (two states, a 1D blend of `sway` and the new clip `lean`, a trigger
  transition, an event) and the module calls the whole `AnimationService`. Tests in
  `tests/animation/TestAnimationRuntime.cpp`; `animation/world-graph-300`: 0.65 ms with Jobs (budget 5 ms).
  Open: `AnimationGraphState.hpp` gained `graphStateDuration()` and `graphScratchBytes()`; root motion is
  applied once per update, so a non-fixed update that runs several physics steps spreads it as one velocity;
  `play()` while a cross-fade runs drops the older clip; the graph's live state is neither saved nor shown in
  the editor; `.animgraph` files are not validated by the cook.
- ANIM-2c (`mechanic`): Documentation and sample. The manual page "Animation" gets graphs, blend spaces,
  layers, events and root motion with a complete `.animgraph` example; the scripting API reference; a tutorial
  section (a character that walks, runs and jumps); the rigged test model gets walk/run clips if needed;
  `CHANGELOG.md`. Done: `mkdocs build --strict`, the tutorial's code builds (`ExtractCppBlocks`).
  ✅ Done: the manual page "Animation" has the part "Animation graphs" (the file key by key, blend spaces,
  transitions and cross-fades, events, root motion, what is saved, limits and errors) with the complete example
  `tests/data/animation/character.animgraph`, which `TestAnimationGraphExample.cpp` parses and finds unchanged
  in both language pages; the `AnimationService` reference lists every error and the events' lifetime; the
  tutorial "Animate a character" (`tutorials/animated-character.md`, graph `hero.animgraph` tested the same
  way, its C++ built by `ExtractCppBlocks` as `illusion_tutorial_animation_module` and loaded by
  `tests/docs/TestTutorials.cpp`); `CHANGELOG.md` (ANIM-1, ANIM-2, module API version 7).
  Open: the tutorial needs the user's own rigged model (clips `idle`, `walk`, `run`, `jump`); the test models
  were not extended.
- ANIM-2 (scope): cross-fades, additive and masked (per-bone) layers, 1D/2D blend spaces,
  a data-driven state machine with parameters and transitions, animation events (footsteps, effects),
  root motion into the CharacterController; `AnimationService` for game modules
- ANIM-3: Sockets and procedural animation, designed in ADR 0061 (post-process stages of the pose after the
  graph: spring bones, look-at, two-bone IK, ragdoll blend; sockets; foot placement raycasts in GameSession).
  Four work packages:
- ANIM-3a (`implementer`): Sockets. Saved `BoneAttachment`, the stage hook in `AnimationWorld` after
  evaluation, `AnimationService::jointTransform()` (module API 8), showcase coverage. Done: a child follows a
  joint through clips, cross-fades and Inactive parents; physics bodies on it follow kinematically; no
  allocation per update.
  ✅ Done: `BoneAttachment` (`illusion.BoneAttachment`: `joint`, offset `translation`/`rotation`/`scale`, saved,
  manual anchor `#sockets`); `AnimationWorld::update()` evaluates the poses with a marked place for the per-pose
  stages of ADR 0061 (on the model-space matrices, before skin matrices and bounds), then places the sockets on
  the World's thread: for an active child whose parent has a posed Animator, `LocalTransform` = joint
  model-space matrix x offset and `Transform` from the parent's current one; the joint index is cached per
  attached Entity and looked up again when the skeleton, a model reload (revision) or `joint` changes; a
  parent without an Animator (or no parent) and a missing joint are logged once per Entity and leave it alone.
  `GameSession` re-places attached children from their parents after root motion, so kinematic bodies on them
  reach this update's socket in the physics step; the editor's edit World gets them from
  `update(0, ..., false)`. `AnimationService::jointTransform()` (Entity `Transform` x joint model matrix of the
  last update; `NoAnimator`, `NoGraph` while the pose loads, `UnknownName`), module API version 8. Showcase:
  `Column Lamp` (a sphere on the Column's `top` joint) and the module reads `jointTransform()`. Tests in
  `tests/animation/TestAnimationSockets.cpp` (the walker assets moved to `WalkerAssets.hpp`);
  `animation/sockets-300`: 0.30 ms with Jobs for 300 Animators and 600 sockets (budget 2.5 ms).
  Open: a `Dynamic` body on an attached Entity is not supported (teleported every update, documented); a socket
  under an attached Animator may reach physics one update late; sockets are placed on one thread.
- ANIM-3b (`implementer`): Look-at and two-bone IK. Pure solvers, saved `LookAt` and `TwoBoneIk`,
  `FootPlacement` raycasting in `GameSession`, `setLookAtTarget`/`setIkTarget`/`setIkWeight`. Done: solver
  tests (reach, out of reach, pole, clamp), feet on a slope and steps scene, benchmark within the ADR budget.
  ✅ Done: `InverseKinematics.hpp` (`solveLookAt()`: a clamped, weighted shortest-arc turn shared evenly along up
  to four joints, fading out for a target behind the head instead of flipping; `solveTwoBoneIk()`: law of
  cosines, pole plane, straight stretch out of reach, optional end alignment, weight blending, no NaN for
  degenerate input; both allocation-free); saved `LookAt`, `TwoBoneIk` and `FootPlacement` (`illusion.<Name>`,
  manual anchors `#look-at`, `#two-bone-ik`, `#foot-placement`) and the runtime `FootPlacementPose`.
  `AnimationWorld::update()` now runs the stages on the local pose before the model-space matrices, per
  Animator in the parallel evaluation, with a marked place for spring bones first: foot placement's pelvis
  offset and foot targets, look-at (`speed` smoothing state in the pose slot, the full turn at once when not
  advancing), then the limbs (targets and poles turned into model space through the Animator's `Transform`).
  A `TwoBoneIk` acts on its own Entity's Animator, else its parent's (one Component per Entity, so further limbs
  go on children; the header's "several on one Entity" could not work); the feet of a `FootPlacement` are the
  limbs whose chain hangs from the pelvis joint (up to four). `GameSession` raycasts below each foot before the
  Animation step (skipping the character's carrier body and passing below its other bodies), writes lifts,
  ground tilts, eased weights and the pelvis drop (clamped by `maxStep`, smoothed by `speed`) into
  `FootPlacementPose`. `AnimationService::setLookAtTarget()`, `setIkTarget()`, `setIkWeight()` (module API 9).
  Showcase: the `Strider` (a two-legged rig from `make_assets.py`, out of the camera's view) with all three
  Components, and the module calls the three functions. Tests in `tests/animation/TestInverseKinematics.cpp`
  and `TestAnimationIk.cpp`; `animation/ik-300`: 0.36 ms with Jobs for 300 Animators with a three-joint look-at
  and two limbs (0.10 ms over `animation/world-update-300x60`; budget 2.5 ms).
  Open: IK chains of more than two bones; foot placement for legs that do not hang from the pelvis (front legs
  of quadrupeds); own bodies with mesh or hull shapes stop the foot rays; feet follow the ground one update late.
- ✅ ANIM-3c (`implementer`): Spring bones and ragdolls. Saved `SpringBone` and `Ragdoll`, generated and
  `.ragdoll` profiles, Jolt ragdolls in `PhysicsWorld`, blend in and out, `setRagdoll()`. Done: deterministic
  spring chains with fixed steps, a ragdoll falls down stairs and settles, blend continuity, benchmark of 20
  ragdolls.
  Open: the Entity's Transform stays where it was while its ragdoll runs (game code moves it before turning
  the ragdoll off); spring chains collide only with their own spheres; powered ragdolls.
- ✅ ANIM-3d (`mechanic`): Documentation and sample: manual sections for each stage (EN/TR), the scripting API
  reference, the tutorial "Animate a character" gets a lantern socket, look-at and foot placement,
  `CHANGELOG.md`.
- ANIM-3 (scope): attach entities to bones (a lantern in a hand), look-at and
  two-bone IK (feet on uneven ground), ragdolls on Jolt from the skeleton, physics-driven secondary motion
- ✅ ANIM-4: Editor and agents, in three work packages (no new ADR: it builds on ADR 0059-0061 and the editor
  command rules of ADR 0036/0039):
- ✅ ANIM-4a (`mechanic`): Commands. `animation.list` (the skins and clips of a model with joint counts and
  durations), `animation.status` (an Animator's clip or graph, time, current state per layer, parameters, the
  last events; edit or play World), `animation.set_parameter` and `animation.trigger` (play World, through
  `AnimationService`), `animation.scrub` (sets `Animator::time` through `WorldEditor`, undoable, edit World),
  `animation.validate` (a `.animgraph` or `.ragdoll` file's errors with JSON paths); every param validated,
  fuzz cases, reference and `catalog.json`, agent summaries with examples.
  Done: all six commands with `PlayHost::animation()` (PlaySession and the test FakePlay), command tests for
  every command and error, the reference and `catalog.json`, the acceptance transcript
  `tests/acceptance/animation/scrub.jsonl` (CTest `acceptance.animation.scrub`, on a copy of the showcase)
  that lists the column's clips, scrubs and undoes it, then plays, sets a parameter and fires a trigger,
  and the "From the editor and agents" section of `manual/animation.md` (EN/TR).
  Open: `animation.status` in the edit World reports the saved Animator fields only (the editor viewport's
  `AnimationWorld` is not reachable without a GPU viewport); `animation.set_parameter` and `animation.trigger`
  need the graph loaded (steps load it), they do not wait for it, because a non-advancing update would
  restart every graph.
- ✅ ANIM-4b (`implementer`): Inspector and viewport. In the Animator's Inspector: a clip and graph summary, a
  timeline scrubber over `Animator::time` (edit World, undoable through `animation.scrub`), the current state
  per layer, live parameters and events in Play mode; a "Skeleton" viewport visualiser drawing the joints of
  selected Animators from `AnimationWorld::jointMatrices()` (and IK targets, spring chains and ragdoll parts
  when present); the tech demo creature is the manual's screenshot subject. Panels change state only through
  the ANIM-4a commands.
  Done: the Animator card's Preview/Live section (`AnimatorSection` in `editor/shell`: joints, clip or graph
  summary, a scrubber that runs `animation.scrub` with one undo step per drag, each layer's state and
  progress, the parameters, which Play mode sets through `animation.set_parameter` and `animation.trigger`,
  the last eight events), the edit World's poses reachable as `ViewportHost::editAnimation()`,
  `AnimationWorld::clip()` and `graphBinding()`, the Skeletons visualiser (`ViewportVisualisers::skeleton`,
  drawn over the scene in the theme's `info`, `text.primary`, `warning`, `success` and `error` colours;
  ragdoll profiles read once and cached until the assets change), UiDriver tests
  (`editor/tests/shell/TestAnimatorInspector.cpp`), the CPU test of the visualiser, the goldens
  `shell-dark-100-animator` and `viewport-skeleton`, an idle-frame allocation test with an Animator selected,
  the EN/TR manual sections "In the Inspector" and "The skeleton view" (`manual/animation.md`), the Inspector
  and Visualisers sections of `manual/editor/`, the command reference and `CHANGELOG.md`.
  On the way the Inspector stopped reporting parts of files (`#mesh/0`) as missing assets and stopped
  touching the file system for asset fields in every frame (that made every idle frame with a model allocate).
  Open (closed by ANIM-4c, except the last): an editor frame that runs several game updates shows the events of
  its last update only.
- ✅ ANIM-4c (`mechanic`): Closing. `animation.status` in the edit World reads `ViewportHost::editAnimation()`:
  with a Scene view posing the World it is `live` with `posed`, `joints`, the loaded graph's layers and
  parameters at the saved time (the result's shape is unchanged, so the command's version stays); without a
  viewport it keeps `live: false` and the saved fields. The tech demo's creature is the animation manual's
  screenshot (`images/editor/animation-preview.png`: Preview section and Skeletons visualiser), taken by
  `docs/screenshots/animation.jsonl` (CTest `docs.screenshots.animation`, label `gpu`) on a copy of
  `samples/techdemo` without its downloaded assets, with a scene built from committed files only.
- ANIM-4 (scope): clip and state-machine preview in the Inspector, a timeline scrubber,
  `animation.*` commands (MCP tools), skeleton debug drawing; the tech demo gets an animated creature;
  documentation (EN/TR)

## NAV — Navigation and AI ✅

After ANIM. Designed in ADR 0062: tiled Recast navmeshes (Recast Navigation v1.6.0, private to `engine/nav`)
baked from the colliders inside a `NavMeshSurface` by one pure bake function, in the editor into `.navmesh`
files and at run time for missing or changed tiles; a crowd of our own built from Detour's parts that runs
as Jobs and repeats with fixed steps for any worker count; `NavAgent`s that steer CharacterControllers
through their velocity and read back where physics put them; behaviour trees as `.behavior` assets with
typed blackboards and named actions that modules poll; sight and hearing perception; `NavService` and
`AiService`. Two libraries: `engine/nav` (`illusion::nav`) and `engine/ai` (`illusion::ai`); the public
headers are in their `include/` directories.

- ✅ NAV-1: Navmeshes and crowds. Five work packages:
- ✅ NAV-1a (`implementer`): Dependency and bake. Recast Navigation v1.6.0 in `cmake/Dependencies.cmake`
  (exact URL and SHA-256; our own static targets of Recast, Detour, DetourCrowd and DebugUtils with
  `DT_POLYREF64`, private to `illusion::nav`), `THIRD_PARTY_NOTICES.md`; the library `engine/nav` with
  `illusion::nav_api`; `NavMeshData.hpp` (`encodeNavMeshData()`, `decodeNavMeshData()`), `NavBuild.hpp`
  (`collectNavGeometry()`: every Collider shape as triangles, Mesh and ConvexHull through the lookup, areas,
  volumes, links, carving obstacles; `navTileInputHash()`, `bakeNavMesh()`, `rebakeStaleTiles()`),
  `registerNavComponents()` for the saved surface, area, link and obstacle Components, `AssetKind::NavMesh`,
  Recast and Detour allocations through the engine heap. Done: tests for every shape's triangles and scale,
  layer masks, Static/Kinematic/Dynamic filtering, Inactive sources, areas and volumes, link input; the same
  bytes from 1 and 4 workers and from two bakes; encode/decode round trip and every `NavDataError`
  (truncated, checksum, versions, duplicate tile); a moved box changes only the hashes of its tiles and
  `rebakeStaleTiles()` rebuilds only those; benchmark `nav/bake-tile` (one 64 x 64-cell tile of a
  10,000-triangle scene, budget from the measurement, ADR target 20 ms) and `nav/bake-256m` with Jobs.
- ✅ NAV-1b (`implementer`): `NavWorld` surfaces and queries. Loading `NavMeshSurface::data` through the
  streamer (decoder `illusion.navmesh`), baking surfaces without data or with rejected files in the
  background, stale tiles (rebuilt with `rebuildAtRuntime`, otherwise counted and logged once), the rebuild
  of tiles touched by changed sources and settled carving obstacles, `invalidate()`, live reload of
  `.navmesh` files, one surface per agent type; `findPath()`, `nearestPoint()`, `raycast()`, `randomPoint()`
  with area masks and costs; `drawDebug()` (navmesh, tiles, links, obstacles). Showcase: a `NavMeshSurface`,
  a `NavArea`, a `NavLink` and a `NavObstacle` with non-default values in `build.jsonl`. Done: a path around
  a wall, over a ramp and through a jump link; area costs change the path, an area mask blocks it; partial
  and failed paths; carving cuts a path and the rebuild touches only its tiles; tiles swapped in only at the
  start of an update; `waitIdle()` then the same queries give the same results; `randomPoint()` repeats for a
  seed; no allocation per query (`test::ScopedAllocationCounter`); benchmark `nav/find-path-1000` (1,000 paths
  of about 100 m on a 256 m tiled navmesh, one thread, ADR target 50 ms).
- ✅ NAV-1c (`implementer`): The crowd. `NavAgent`/`NavAgentState`, crowd slots, the six phases of ADR 0062
  with per-worker Detour queries and avoidance, the four drives, link traversal with `LinkEntered`/
  `LinkExited` events, `Arrived`/`PathFailed`, `OffMesh` and replanning, `setDestination()`, `stop()`,
  `warp()`, `pathCorners()`; `GameSession` gets a `NavWorld` (`navigation()`) and the Navigation step after
  Saves (Character velocities into `PhysicsWorld::setCharacterVelocity()` keeping the vertical velocity,
  `speedParameter` into the Animator's graph, root motion translation skipped for Character and Transform
  agents); the player and the editor register the Components. Showcase: a `NavAgent` with non-default
  values. Done: a Character agent walks up a ramp and a step to its destination through physics; two agents
  crossing do not touch; a crowd of 50 through a doorway arrives; a Transform agent stays on the navmesh; a
  jump link moves the agent along its parabola with both events; the same World after 600 fixed steps with
  1 and 4 workers; Inactive agents do not move; a saved game restores `destination`; no allocation in a
  steady update; benchmark `nav/crowd-500` with Jobs (ADR target 2 ms).
- ✅ NAV-1d (`mechanic`): `NavService`. The interface joins the scripting API here (`@ingroup scripting_api` on the class, so
  `tools/check_api_coverage.py` requires the showcase to call it). `NavWorldService`, `USES nav` (`illusion::nav_api` in the SDK and
  `cmake/IllusionModules.cmake`), `GameSession` provides it, module API version raised by one, the showcase
  module calls the whole `NavService` (`tools/check_api_coverage.py`). Done: a module test sends an agent to a
  point and reads its `Arrived` event; every `NavError` from the service; the SDK test builds a module that
  uses it.
- ✅ NAV-1e (`mechanic`): Editor and documentation. Commands `nav.bake` (async: one or every surface, changed
  tiles only, writes the `.navmesh`, sets `NavMeshSurface::data` through `executeStep()`), `nav.status`,
  `nav.path`, `nav.debug`; the edit World's `NavWorld` (never advanced); a viewport visualiser over
  `drawDebug()`; a Bake button in the Inspector of `NavMeshSurface`; the asset browser lists `.navmesh` as
  `NavMesh`; exports ship `.navmesh` files; the command reference (`illusion_editor_command_reference
  --write docs/content`) and `catalog.json` (`ILLUSION_UPDATE_GOLDENS=1`); the manual page "Navigation"
  (EN/TR: surfaces, settings, areas, links, obstacles, agents and drives, run-time rebuilds, limits), the
  `NavService` reference, `CHANGELOG.md`. Done: command tests (bake twice writes the same file and changes
  nothing the second time, a moved wall rebakes its tiles only, `nav.path` around a wall, every invalid
  param), the fuzz test, a GPU golden image of the visualiser, `mkdocs build --strict`, docs parity.
  Open: the Inspector's Bake button and the `.navmesh` items of the Asset Browser and the export; the `nav.*`
  commands and the debug view act on the edit World only.
- ✅ NAV-2: Behaviour trees and perception. Four work packages:
- ✅ NAV-2a (`implementer`): Trees without a World. The library `engine/ai` with `illusion::ai_api`;
  `Blackboard.hpp` (`compareBlackboard()`), `BehaviorTree.hpp` (`parseBehaviorTree()`,
  `writeBehaviorTree()`, `findKey()`), `BehaviorTreeState.hpp` (`makeBehaviorTreeState()`,
  `resetBehaviorTree()`, `tickBehaviorTree()`, `runningNode()`), `Perception.hpp` (`inSightCone()`,
  `hearsNoise()`), `AssetKind::BehaviorTree`. Done: a parser test for every error (messages with the JSON
  path) and the round trip; per node type a test against a fake `BehaviorContext` (sequence and selector
  order, parallel thresholds, every decorator, guards with each abort mode, aborted moves and actions,
  Wait deviation from the seed); the same statuses for the same seed; no allocation in `tickBehaviorTree()`;
  cone and hearing edge cases; benchmark `ai/tick-1000` (1,000 trees of 30 nodes, one thread, ADR target
  1 ms).
- ✅ NAV-2b (`implementer`): `AiWorld` and perception. `AiComponents.hpp` (`registerAiComponents()`), `AiWorld`
  (tree loading through the streamer, decoder `illusion.behavior`, slots, blackboard overrides, the context
  over `NavWorld`, perception with the raycast budget, blackboard keys from perception, actions, staggered
  ticks, live reload), `GameSession` gets an `AiWorld` (`ai()`) and the AI step before Navigation; the player
  and the editor register the Components. Showcase: a `BehaviorAgent` (with a small `.behavior` file and an
  override), an `AiPerception` and an `AiStimulus` with non-default values. Done: sight within the cone and
  radius, blocked by a wall, outside the cone; hearing within the scaled radius; memory forgets after
  `memorySeconds`; the raycast budget serves every perceiver in turn; a guard with `LowerPriority` interrupts
  wandering when a target is seen; the same World after 600 fixed steps with 1 and 4 workers; Inactive agents
  do not tick; a reloaded tree restarts its agents; no allocation in a steady update; benchmark
  `ai/perception-200` (200 perceivers, 50 stimuli, 256 rays, ADR target 1 ms) and `ai/world-1000`.
  Done: every item above in `tests/ai/TestAiWorld.cpp`, `TestAiPerceptionWorld.cpp` and `TestAiComponents.cpp`;
  `ai/perception-200` 0.13 ms (budget 0.75 ms), `ai/world-1000` 0.19 ms (budget 1.3 ms); the manual page "AI"
  gained Agents, Actions, Live reload, Perception, Stimuli and the game session step (EN/TR). Open: the
  `AiWorldService` adapter and the scripting API (NAV-2c); the `ai.*` commands and the visualiser over
  `drawDebug()` (NAV-2d).
- ✅ NAV-2c (`mechanic`): `AiService`. The interface joins the scripting API here (`@ingroup scripting_api` on the class, so
  `tools/check_api_coverage.py` requires the showcase to call it). `AiWorldService`, `USES ai` (`illusion::ai_api` in the SDK), `GameSession`
  provides it, module API version raised by one, the showcase module calls the whole `AiService`. Done: a
  module test finishes an action and sees its node succeed, sees an abort, writes a key the tree reads,
  reports a noise that a perceiver hears; every `AiError` from the service; the SDK test builds a module
  that uses it.
  Done: `AiWorldService` (`engine/ai/src/AiWorldService.cpp`), `USES ai` and `illusion_ai_api` in the SDK, `GameSession`
  provides the service, `kModuleApiVersion` 12; `tests/ai/TestAiService.cpp` (every function and every `AiError`),
  a module test in `tests/modules/TestModules.cpp` (finishes an action, sees an abort, writes a key the tree
  reads, reports a noise a listener hears), `CountingAi`, the showcase module calls all 13 functions (the guard
  tree gained the keys `alert`, `greetings`, `boldness`), the SDK test module uses it; the scripting API
  reference gained "AiService" (EN/TR). Open: the `ai.*` commands and the visualiser (NAV-2d).
- ✅ NAV-2d (`mechanic`): Editor and documentation. Commands `ai.status` (running node, blackboard, perceived),
  `ai.setValue`, `ai.noise` (Play mode) and `ai.validate` (a `.behavior` file's errors), a perception
  visualiser over `AiWorld::drawDebug()`; the asset browser lists `.behavior` as `BehaviorTree`; exports
  ship them; the command reference and `catalog.json`; the manual page "AI" (EN/TR: the `.behavior` file
  node by node with a complete example tested like `character.animgraph`, blackboards, actions, perception),
  the `AiService` reference, the tutorial "A creature that wanders and chases" (its C++ built by
  `ExtractCppBlocks`, its tree parsed by a test), `CHANGELOG.md`. Done: command tests and the fuzz test,
  `mkdocs build --strict`, docs parity, the tutorial module loads in `tests/docs/TestTutorials.cpp`.
  Done: `ai.status`, `ai.setValue`, `ai.noise` (CommandMode::Any on the play World through `PlayHost::ai()`) and
  `ai.validate` (`editor/core/src/commands/AiCommands.cpp`, tests in `editor/tests/core/TestAiCommands.cpp`: a
  running node and the blackboard, every key type written and cleared, a noise heard at the next step, every
  invalid param, the fuzz test; the test play host runs a real `AiWorld`); the Perception visualiser
  (`ViewportVisualisers::perception`, on by default, in the Visualisers menu and `viewport.set_overlays`) over
  `AiWorld::drawDebug()`; the Asset Browser kind `BehaviorTree` (exports ship `.behavior` files like every file of
  `assets:/`); the command reference and `catalog.json`; the manual page "AI" with the complete example
  `tests/data/ai/watchman.behavior` (every node and key type; `tests/ai/TestBehaviorTreeExample.cpp` parses it
  and checks both languages show it unchanged), "In the Editor" and the perception view; the `AiService` reference
  checked; the tutorial "A creature that wanders and chases" (`tutorials/creature-ai.md`, module
  `illusion_tutorial_creature_module` built by `ExtractCppBlocks`, its tree `creature.behavior` parsed by the same
  test); `CHANGELOG.md`. Open: a GPU golden image of the perception view; `ai.*` act on the play World only; the
  `.navmesh` kind of the Asset Browser and the Inspector's Bake button (NAV-1e).
- ✅ NAV-3 (`implementer`): The tech demo creature. A `NavMeshSurface` over the clearing baked by `build.jsonl`
  (`nav.bake`, the `.navmesh` committed), a creature (the ANIM-4 creature, or until it lands a simple rigged
  model from `make_assets.py`) with a `NavAgent` (Character drive, `speedParameter` into its graph), an
  `AiPerception` and a `BehaviorAgent` whose tree wanders around its den, follows the player it sees or
  hears (footsteps reported by the module as noises), keeps away from the campfire (a high-cost `NavArea`)
  and returns home when it loses the player; the pause menu stops it. Done: `TestTechDemo.cpp` runs the
  module on the scene for 600 fixed steps and sees the creature wander, notice a scripted player and reach
  it, with the same result twice; the techdemo manual page (EN/TR) and its README describe the creature.
  Done: `make_assets.py` writes the creature (`creature.gltf`: 13 joints of boxes, clips `idle`, `walk`, `run`),
  `creature.animgraph` (a 1D blend by `speed` at 0, 1.4 and 4.2 m/s) and `creature.behavior` (chase and the
  action `growl`, return: search then go home, wander around `home`); `make_scene.py` adds after the UI (every
  other id stays) the Player's `AiStimulus`, *Clearing Navigation* (40 x 40 m, `nav.bake` into
  `assets/navigation/clearing.navmesh`), *Campfire Area* (area 1, cost 25) and *Creature* (`NavAgent` with the
  Character drive and `speedParameter`, `AiPerception`, `BehaviorAgent` with `home`), and to `ui.scene` the
  hidden HUD line *Creature Alert* (`techdemo.hud.creatureAlert`, EN/TR); the module (`USES ai nav`) reports
  footsteps as noises (7 m walking, 15 m running), sets the walk or run speed, answers `growl`, shows the alert
  while it chases, and holds the tree and the navigation until the ground exists and while the pause menu
  shows; tests in `editor/tests/play/TestTechDemo.cpp` (600 fixed steps twice: wanders, notices at step 244,
  reaches at 363, identical; the pause menu stops it) and `tools/test_tools.py`; the techdemo manual page and
  README. Fixed on the way: async commands over RPC kept a pointer to a freed `PermissionGate`, so `nav.bake`
  crashed the editor (regression test in `editor/tests/rpc/TestRpcAsync.cpp`). Goldens unchanged (the alert is
  hidden in the pause-menu image). Open: the creature is not `Saved` (a load restarts nothing of it); its
  perception's memory keeps running while the game is paused; the ANIM-4 creature may replace the box model.
- NAV (scope): navigation meshes baked from colliders and walkable surfaces with Recast (zlib), in the
  editor and at run time for changing levels, tiled for large worlds; path queries and crowds with Detour on
  the Job System; off-mesh links (jumps, ladders)
- NAV (scope): a `NavAgent` Component steering CharacterControllers, avoidance, debug drawing; behaviour
  trees as saved assets with blackboards, perception (sight, hearing) and an `AiService`; `nav.*` and `ai.*`
  commands; a creature that wanders the tech demo

## MAT — Custom shaders and materials

Today games can only set the parameters of the built-in PBR material.

- MAT is designed in ADR 0063 (Slang surface functions in `.shader` assets, compiled by the pinned `slangc`
  at cook time and on hot reload, cooked SPIR-V in the player). Four work packages:
- ✅ MAT-1 (`implementer`): Shader assets and the compiler. `illusion/Surface.slang` (the surface API),
  `.shader` files with declared parameters, `ShaderCompiler` running `slangc` per variant with JSON
  reflection, `AssetType::Shader` cooked with a cache keyed by source, engine shader and compiler versions,
  errors with file/line/column; the pinned `slangc` in the editor CPack component and the SDK. Done: compile,
  cache and error tests on small shaders, a malformed shader never crashes the cooker, export fails with the
  compiler's message.
- MAT-2 (`implementer`): The renderer. The standard PBR material rewritten as `builtin:/shader/standard` on
  the surface API (golden images unchanged), pipelines per (shader, variant, state) built lazily and cached,
  per-material parameter buffers on the bindless path, `worldOffset` in prepass, shadows and motion vectors,
  the fallback/error material, hot reload swapping pipelines. Done: GPU goldens of a custom emissive and a
  wind-offset shader, zero validation errors, benchmark of 1,000 materials over 20 shaders.
- MAT-3 (`implementer`): Shading models `Unlit`, `Subsurface`, `TwoSidedFoliage`, `ClearCoat` in the
  lighting code. Done: a golden per model, the tech demo foliage on `TwoSidedFoliage` with unchanged images.
- MAT-4 (`mechanic`): Editor and documentation. `.material` `shader` and typed parameters in the Inspector,
  `material.compile`/`material.validate` commands (diagnostics for agents), the asset browser kind `Shader`,
  the showcase (a custom shader with every parameter type), a tech demo example, the manual "Material
  shaders" (EN/TR, the surface API reference), a tutorial, `CHANGELOG.md`.
- Material templates in Slang (scope): a surface function (base colour, normal, roughness, emission, opacity,
  world offset) compiled into the forward, prepass, shadow and bindless variants, with typed parameters and
  textures exposed to `.material` files and the Inspector; shader permutations cached and hot-reloaded
- Shading models (default, subsurface, two-sided foliage, unlit, clear coat); a node-based material editor
  later; documentation of the surface API

## CAM — Camera system

Games have only the `Camera` Component; every sample moves it by hand. A general camera system in the spirit
of virtual-camera rigs, so that a third-person, first-person, orbit, rail or cutscene camera is data, not
game code.

- CAM is designed in ADR 0064 (virtual cameras as saved rigs of body/aim/noise stages, a `CameraBrain` that
  blends to the live one, `CameraWorld` in a new GPU-free `engine/camera` after Hierarchy). Four work packages:
- ✅ CAM-1 (`implementer`): Stages without a World. `engine/camera` (`illusion::camera`, core only) with the
  pure stage functions over plain settings and state structs: `CameraPose.hpp` (`CameraPose`,
  `CameraTarget`, `CameraPlacement`), `CameraDamping.hpp` (exact critically damped `smoothDamp()`,
  `dampFactor()`, `dampRotation()`), `CameraBody.hpp` (`stepCameraFollow()`, `stepCameraOrbit()`,
  `stepCameraSpringArm()` against `CameraCollisionQuery`, `stepCameraDolly()` on Hermite paths),
  `CameraAim.hpp` (`stepCameraLookAt()`, `stepCameraComposer()` with dead and soft zones in screen space),
  `CameraNoise.hpp` (two seeded sines per channel), `CameraBlend.hpp` (cut, linear, ease in/out, cubic
  Bezier; `blendCameraPoses()`). Done: `tests/camera` (every stage and the damping against hand-computed
  values, composer zones, blend curves, bit-identical repeated runs, no allocation), benchmark
  `camera/stages-100`. Open: no SDK target yet (the library joins the player closure when
  `engine/runtime` links it in CAM-2; a `camera_api` for modules comes with CAM-3).
- CAM-2 (`implementer`): Runtime. The saved Components, `CameraBrain`, `CameraWorld` in `GameSession` after
  Hierarchy, spring-arm sphere casts through `PhysicsWorld`, impulses and shake, the showcase covering every
  Component. Done: a spring arm keeps out of a wall, a blend between two cameras is continuous, fixed-step
  runs repeat, benchmark of 100 virtual cameras.
- CAM-3 (`mechanic`): `CameraService` (`USES camera`, module API version raised by one), the showcase module
  calls it; the tech demo walker uses a first-person virtual camera with unchanged screenshots. Done: module
  tests for every function and error.
- CAM-4 (`mechanic`): Editor and documentation: `camera.*` commands, frusta in the Scene view, Game view
  preview of a virtual camera, manual page "Cameras" (EN/TR), a tutorial (a third-person camera with
  shoulder swap), `CHANGELOG.md`.
- Virtual cameras (scope) as saved Components with priorities; the camera brain on the real `Camera` picks the
  highest live one and blends to it (cut, ease, custom curve, per-pair overrides)
- Body and aim stages as data: follow with offset and damping, orbit, spring arm with collision against
  physics layers, framing composer (dead and soft zones), look-at with damping, path/dolly tracks
- Impulses and noise: camera shake from game events with falloff, handheld noise profiles
- Input binding through `InputActions` (look, zoom, shoulder swap) and a `CameraService` for modules
- `camera.*` editor commands, a preview of the live and blending cameras in the Scene view, EN/TR docs
- Determinism with fixed steps; no per-frame allocation; benchmarks of 100 virtual cameras

## ANIM-5 — Retargeting and aim layers

- Humanoid retargeting: a joint map per skeleton (generated by name and topology, editable), clips played
  on any mapped skeleton with rest-pose correction and proportional root motion
- Aim layers: additive aim offsets from a pitch/yaw blend space on an upper-body mask, so a character aims
  while it walks, runs or crouches; `AnimationService` calls to drive them
- Tests on two CC0 rigs with different joint names and proportions; benchmarks

## PROBE — Reflection and light probes

Correct indirect light indoors and in shadow until ray tracing (M10) is available.

- Reflection probes (box-projected, blended between volumes), baked in the editor and refreshed at run time
  on demand; irradiance probes (spherical harmonics) on a grid or placed volumes with visibility, baked
  from the scene with the existing renderer, interpolated per pixel; specular occlusion
- Probe Components, `probe.*` commands, a bake panel; tests and benchmarks
- Screen-space reflections that fall back to the reflection probes off screen, and contact shadows
  (formerly part of GFX-3)

## DECAL — Decals

- Deferred-style decals projected onto opaque surfaces in a box (base colour, normal, roughness, emission),
  sorted, layered and culled by mask; decal materials on the MAT system; footprints and impacts from game
  code through a pooled `DecalService`

## POST — Cinematic post effects

- Depth of field (physically based circle of confusion from focus distance and aperture, bokeh), driven
  by `PostEffects` and by virtual cameras (CAM)
- Per-object motion blur from the existing motion vectors; lens effects (lens dirt on bloom, flares)
- Quality levels in `RenderSettings`; GPU goldens and budgets on lavapipe

## PLAY — Gameplay foundations

Found by reading the tech demo and showcase modules (2026-11-22): game code there does work that every game
needs and that belongs in the engine. Each item replaces code in `samples/techdemo/modules/techdemo` and
keeps its behaviour (the tech demo tests must not change).

- Pause and time scale: `SceneService::setPaused()` / `setTimeScale()` honoured by `GameSession` for
  physics, animation, AI, navigation, particles and audio buses (UI keeps running); the tech demo pause menu
  stops its own clock and holds the creature by hand today, while physics, particles and animation run on
- Level readiness: the session holds dynamic bodies, characters, navigation and AI until the static
  colliders and navmeshes of the loaded scene are cooked and loaded (`SceneService::isReady()` and a loading
  phase the UI can show); the tech demo checks for ground with a raycast and switches props named `Orb ...` /
  `Lantern ...` from kinematic to dynamic once the terrain collider exists
- Finding Entities by name or tag from modules (`SceneService::find()` over an index of `Name`), instead of
  the `findNamed()` scan that every sample module repeats
- A saved `CharacterMovement` Component (walk, run, crouch, jump, air control, slopes) driven by input
  actions on a `CharacterController`, with first-person head bob; the cameras that follow it come with CAM
- Light animation: a saved `LightAnimator` (flicker, pulse, noise on intensity and position) for fires,
  neon and torches, done by hand in `updateLights()` today
- Time of day: a saved `DayNightCycle` on the `SkyAtmosphere` (speed, start time) with day/night events and
  intensity curves for lights tagged to follow it, replacing `updateTime()`
- Footsteps: animation events or stride distance turned into `AiStimulus` noises and footstep sounds by a
  saved `Footsteps` Component, replacing the stride counter in the module
- A shared, pinned asset fetcher (`tools/fetch_assets.py` of the tech demo generalised for SHOOTER: a
  manifest of URLs, sizes and SHA-256, a local override folder, never committed)

## SHOOTER — Third-person shooter demo

A playable game in `samples/shooter` (beside the forest tech demo) that shows the engine at its best; it is
built only from engine features and game-module code, and exported with `export.build`.

- Player: walk, run, sprint, crouch, roll, aim, strafe, two weapons with hitscan fire and reload, hit
  reactions, ragdoll on death; gamepad and keyboard/mouse with glyphs
- Enemies: soldiers (patrol, hear, chase, take cover, shoot), a heavy mech, a melee creature, all on
  behaviour trees, with ragdolls; a friendly NPC who gives the mission, with subtitled lines
- Two scenes as missions with objectives, markers, triggers and checkpoints: an outpost at sunset and a
  facility at night with the mech fight and the extraction
- UI: main menu, settings (graphics, audio, rebinding), HUD (health, ammo, crosshair, objective, damage
  direction, compass), pause, save slots with images, loading screen, mission complete
- Sky, fog and light shafts; muzzle flashes, tracers, sparks, smoke, explosions, decals; weapons, footsteps,
  ambience and music
- Assets: CC0 packs pinned by size and SHA-256 and fetched by a script (never committed), replaceable by
  the owner's own packs from a local folder that is never committed either

## TERRAIN — Terrain

- Heightmap terrain in tiles with GPU tessellation-free LOD (CDLOD), layered materials painted in the
  editor (splat maps, triplanar on slopes), holes, Jolt height-field colliders, vegetation painted onto it
  with the GFX-2 instancing; streaming of tiles for large worlds
- GPU-generated dense grass placed from the terrain's density maps, and impostors for distant trees
  (formerly GFX-2e and GFX-2f)

## WATER — Water

- Lakes and rivers (flow maps, shoreline foam, refraction, depth colour, caustics) and an ocean (FFT
  waves, foam, LOD grid), lit by the sky, probes and fog; buoyancy for physics bodies; water as MAT
  materials

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

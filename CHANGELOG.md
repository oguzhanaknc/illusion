# Changelog

All notable changes to illusion are recorded in this file. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the engine follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html); the rules that decide which part of the
version changes are in the contributor guide ("Versions and releases"). The version comes from the
`project()` call of the root `CMakeLists.txt`; `ROADMAP.md` has the details of each milestone.

## [Unreleased]

### Added

- Foundation (M0): CMake presets, pinned dependencies, CI on Windows, Linux and macOS, the core library,
  memory allocators, the Job System, the archetype ECS, Tracy profiling, benchmarks with budgets and the
  English/Turkish documentation site.
- Platform (M1): windows and keyboard/mouse input (GLFW), timers, the virtual file system, the event bus,
  the Vulkan 1.3 render device through NVRHI and the Slang shader pipeline.
- Input (M2): Xbox and DualSense gamepads through SDL3 (rumble, LED, gyro, touchpad, adaptive triggers),
  action mapping, rebinding and per-device glyphs.
- Renderer (M3): glTF import, PBR, image-based lighting, cascaded soft shadows, GTAO, the HDR pipeline with
  bloom, tonemapping and TAA, GPU occlusion culling and bindless materials.
- Assets (M4): binary serialization, stable asset ids, the import cache, asynchronous streaming and packed
  archives (`.illpak`, `illusion_pack`).
- Gameplay services (M5): component reflection and JSON scenes, Jolt Physics (bodies, colliders, character
  controller, joints, queries) and audio through miniaudio (3D sound, buses, filters, reverb).
- Game modules (M6): hot-reloadable C++ game modules with semver, service injection and Component
  migration across reloads.
- Editor and AI control (M7): the editor with its panels, undo, Play mode and gizmos, the command catalogue
  over JSON-RPC and MCP, `illusion_ctl`, `illusion_mcp`, and the acceptance test in which an agent builds a
  Tetris-like game.
- Particle effects (PFX): saved emitters, a deterministic CPU simulation, GPU billboards and a GPU
  simulation path.
- Tech demo (DEMO): mouse look, wind, a day and night sky and the forest clearing sample.
- AAA look (GFX): automatic exposure and colour grading, volumetric fog, point light shadows, levels of
  detail with cross-fades and leaf translucency.
- Texture compression (TEX): BC4, BC5, BC6H and BC7 encoding on import, compressed GPU upload,
  compressed environments and mip streaming.
- Release pipeline (M8): the player (`illusion::player`), the player SDK, `export.build` with packages and
  signing hooks, the Export window, `StoreService` with an optional Steamworks backend, and the engine
  package (editor, tools and SDK in one archive per platform) with its release workflow.
- Game UI (UI): fonts as an Asset kind with baked SDF glyphs, the saved UI Components (canvas, element,
  image, text, stack, grid), anchored layout and canvas scaling, `UiRenderer`, string table keys for text,
  interaction with the pointer, the keyboard and gamepads (buttons, toggles, sliders, focus, modal canvases),
  `UiService` for game modules (find, text, visibility, values, focus, events, language) and
  `SceneService::requestExit()`, with which a module ends the game in the player and in Play mode; text
  fields, scroll views and input glyphs (`UiTextField`, `UiScrollView`, `UiGlyph`); the Editor commands
  `gameui.create`, `gameui.layout`, `gameui.click` and `gameui.focus` with the Entity > Game UI menu, and the
  Game view showing the scene's UI also outside Play mode; transitions (`UiTransition`), text outlines and
  shadows, canvas editing in the Game view (`gameui.pick`, `gameui.place`, `gameui.set_anchors`), a HUD and a
  pause menu in the tech demo and the Tetris acceptance game, the manual page, the architecture page and the
  tutorial "A HUD and a pause menu".
- Saved games (SAVE): the `Saved` tag, checksummed slot files with a backup and a slot image, the capture and
  application of the tagged Entities, `SaveService` for game modules (`USES save`) with its save thread, the
  cloud mirror through `StoreService`, `settings:/save.json`, the Editor commands `saves.list`, `saves.write`,
  `saves.load` and `saves.remove`, Save and Load in the tech demo's pause menu, the manual page "Saved games"
  and the HUD tutorial's section "Save and load".
- Skeletal animation (ANIM-1): skins and clips imported from glTF, the saved `Animator` Component and the
  runtime `AnimationPose`, pose evaluation on the Job System and GPU skinning with motion vectors, shadows
  and picking, scrubbing in the editor and the manual page "Animation".
- Animation graphs (ANIM-2): `.animgraph` files (layered state machines with 1D and 2D blend spaces,
  cross-fading transitions on parameters and triggers, masked and additive layers, state events and root
  motion), `Animator::graph`, root motion applied to the `CharacterController` or the Transform,
  `AnimationService` for game modules (`USES animation`: parameters, triggers, states, events and `play()`),
  the manual section "Animation graphs" with a complete example and the tutorial "Animate a character".
- Sockets (ANIM-3a): the saved `BoneAttachment` Component makes a child Entity follow a joint of its parent's
  Animator with an offset (kinematic bodies on it follow too), and `AnimationService::jointTransform()` gives
  game modules a joint's world-space placement; the manual section "Sockets".
- Look-at and IK (ANIM-3b): the saved `LookAt` (a head and up to three ancestors turned towards an Entity or a
  point, clamped and smoothed), `TwoBoneIk` (analytic two-bone IK with a pole, on the Animator's Entity or its
  children) and `FootPlacement` (feet kept on the ground by raycasts in the game session, the pelvis lowered
  for the lower foot) Components, `AnimationService::setLookAtTarget()`, `setIkTarget()` and `setIkWeight()`,
  and the manual sections "Look-at", "Two-bone IK" and "Foot placement".
- Navigation meshes (NAV-1a): the Recast Navigation 1.6.0 dependency, the library `illusion::nav`, the saved
  `NavMeshSurface`, `NavArea`, `NavLink`, `NavObstacle` and `NavAgent` Components, a tiled navmesh bake from
  colliders on the Job System with identical bytes for any worker count, per-tile input hashes with
  `rebakeStaleTiles()`, the checksummed `.navmesh` file and the manual page "Navigation".
- Run-time navigation (NAV-1b): `NavWorld` loads the `.navmesh` files of the NavMeshSurfaces or bakes them,
  rebuilds the tiles of changed sources in the background, and answers `findPath()`, `nearestPoint()`,
  `raycast()` and `randomPoint()` queries with `NavQueryFilter` and area costs; `drawDebug()`, the Nav yard
  of the showcase and the manual sections "Run time" and "Queries".
- Navigation in the editor (NAV-1e): the Editor commands `nav.bake` (changed tiles only, writes the `.navmesh`
  and sets `NavMeshSurface::data`), `nav.status`, `nav.path` and `nav.debug`, the edit World's `NavWorld`, the
  navigation shown in the Scene views, a `nav.bake` step in the showcase build and the manual section "In the
  editor".
- Behaviour trees (NAV-2a): the library `illusion::ai` with `.behavior` files (blackboard keys, composites,
  decorators, guards with abort modes and leaves for movement and game actions), `parseBehaviorTree()`, the
  tick `tickBehaviorTree()` over a `BehaviorContext`, the geometry of sight and hearing, the Benchmark
  `ai/tick-1000` and the manual page "AI".
- AI in the game and for modules (NAV-2b, NAV-2c): `AiWorld` with perception (sight with line-of-sight
  raycasts, hearing, memory), the AI step of the game session, and `AiService` (blackboard writes and reads,
  game actions, noises, what an agent perceives) with the adapter `AiWorldService`.

### Changed

- The module API version is 12 (`AiService` joined in version 12, `NavService` in 11, `AnimationService` gained `setLookAtTarget()`, `setIkTarget()` and
  `setIkWeight()`; version 8 added `jointTransform()`, version 7
  `AnimationService`, version 6 `UiService`, `SceneService::requestExit()` and `SaveService`, and `UiService`
  gained `playTransition()` and `transitionProgress()`): game modules built for an earlier version must be
  rebuilt.
- New projects ship the input actions asset `assets/input/game.input` with the actions `ui.navigate`,
  `ui.submit` and `ui.cancel` and their default bindings, named by `settings/input.json`; older projects still
  get the actions at run time.

### Fixed

- The editor without a GPU (`--headless --no-gpu`) now makes disabled Entities and their descendants Inactive
  after every change, so `gameui.layout`, `gameui.pick` and the physics queries no longer report disabled
  canvases, elements and bodies there.

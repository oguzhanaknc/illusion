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
  Game view showing the scene's UI also outside Play mode.

### Changed

- The module API version is 4 (`UiService` and `SceneService::requestExit()` are new interface functions):
  game modules built for an earlier version must be rebuilt.

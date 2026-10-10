# illusion — 3D Game Engine

Working rules for agents: @AGENTS.md

The project brief below is the product definition. `ROADMAP.md` tracks what is done and what is next.

---

Production-grade, performance-critical 3D game engine for Windows, macOS, Linux (Ubuntu 24+).
Commercial-quality: stable, fully tested, shippable on Steam and similar platforms. No hacks.

## Dev Workflow
- Work autonomously; ask only when absolutely necessary
- IMPORTANT: do NOT look outside the working directory or use other code on this computer as reference
- Heavy unit testing (Catch2/doctest), automated runs, CI (GitHub Actions: Win/macOS/Linux matrix)
- Test scene exercising every component and the entire scripting API
- Create AGENTS.md + skills with concrete development guidelines
- Git repo -> https://github.com/oguzhanaknc/illusion. Code review before every commit; style + tests must pass

## Tech stack
- C++20, CMake (presets), static core library
- GLFW (window, keyboard, mouse) + SDL3 gamepad subsystem only (Xbox + DualSense: rumble, LED, gyro, touchpad, adaptive triggers)
- nvrhi + Vulkan (MoltenVK on macOS), glm, miniaudio, Jolt Physics
- Game code in C++ as a game module: hot-reloadable shared lib in editor, statically linked in export

## Performance (hard requirement)
- Data-oriented ECS (archetype/SoA), job system, frustum/occlusion culling, instancing,
  bindless/GPU-driven where possible, async asset streaming, no per-frame allocations
- Tracy profiling integration; perf budgets + benchmark tests in CI

## Renderer
- glTF import (meshes/materials/textures), PBR, IBL (Poly Haven HDRIs), soft shadows (CSM+PCSS),
  GTAO, HDR pipeline + tonemapping (bloom, optional TAA), editor gizmos

## Input
- Keyboard/mouse, Xbox, DualSense; action-mapping layer, rebinding, hot-plug, per-device glyphs

## Editor / AI control
- Editor fully controllable by AI agents (command/tool API, e.g. MCP/JSON-RPC): build a Tetris-like game without human intervention

## Release
- "Export" produces a clean runtime build: packed assets, no editor code
- CPack/installers, Windows (signing-ready), macOS (.app, notarization-ready), Linux (Steam Runtime sniper compatible)
- Optional Steamworks module (achievements, cloud, Steam Input); SDK supplied by user, not committed

## Documentation (commercial quality)
- English + Turkish, kept in sync (CI parity check); Turkish keeps technical terms untranslated
  (Entity, Component, Shader, Render Pass...)
- Getting started, tutorials, manual, scripting/API reference (Doxygen), architecture, release/Steam guide
- MkDocs Material + i18n

## Extensibility
- Module/plugin system, component reflection, event bus, semver, ADRs for design decisions

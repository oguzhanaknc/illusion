# Architecture Overview

This page describes how illusion is structured and the principles every module follows.

## Layers

illusion is built in layers. A layer may depend only on layers below it.

| Layer | Contents | Status |
| --- | --- | --- |
| Core | Platform detection, assertions, logging (console, memory and ring Sinks), memory, Job System, ECS, reflection and scene files, event bus, time, math, semantic versions, JSON, `Expected`, deterministic and secure random numbers | Implemented |
| Platform | Window, keyboard and mouse (GLFW), virtual file system, packed archives | Implemented |
| Text | UTF-8, search folding, fuzzy matching, CLDR plural rules, MessageFormat, string tables and localization | Implemented |
| Input | Gamepads through SDL3 (rumble, LED, motion, touchpad, adaptive triggers), action mapping, rebinding, glyphs and their translatable labels | Implemented |
| Assets | Images, Meshes, glTF models, environments, cooked formats, an import cache and asynchronous streaming | Implemented |
| Physics | Rigid bodies, primitive, convex hull and mesh colliders, characters, joints, raycasts, shape casts and contacts with Jolt Physics on the Job System | Implemented |
| Audio | 3D sound with cones, streaming, background loading and a mixer with buses, filters and reverb with miniaudio, mixed on its own thread | Implemented |
| Renderer: foundation | Vulkan 1.3 device through NVRHI, swapchains, Slang shader pipeline | Implemented |
| Renderer: scene | Meshes, PBR Materials, direct and image-based lighting, soft shadows, GTAO, HDR post-processing (TAA, bloom, tonemapping), frustum culling, GPU occlusion culling with indirect draws, instancing, bindless multi-draw | Implemented |
| Editor | Separate tree with a checked export boundary; UI foundation (Dear ImGui backends, theme, fonts, icons, widget kit, gallery) done; scene editing, gizmos and the AI command API in progress ([Editor Architecture](editor.md)) | In progress |
| Game modules | The game's own code: hot-reloadable shared libraries in development, linked statically into exported games ([Game Modules](../manual/modules.md)) | Implemented |

The core is a static library. The editor and exported games link it; a game module never contains a
second copy of engine state (see "Process-wide services" below).

## Principles

**Data-oriented design.** Data is laid out for the way it is processed. The ECS stores Components in
structure-of-arrays Chunks so that systems read memory sequentially.

**No per-frame heap allocations.** Per-frame data lives in arenas and pools. Jobs, log messages, ECS
Queries and command buffers do not allocate in steady state. Benchmarks guard the hot paths.

**Explicit threading model.** Work runs as Jobs on the Job System. Structural ECS changes happen on one
thread at defined points; parallel code only touches the Components it is given.

**Fail loudly on programmer errors.** Invariants are checked with `ILL_ASSERT` (Debug and CI) and
`ILL_CHECK` (always). Engine code does not use exceptions for control flow; an exception escaping a Job
or a Component constructor terminates the process.

**Every decision is recorded.** Significant design decisions are written down as Architecture Decision
Records (ADRs); see [Decisions](decisions.md).

## Process-wide services

A few services are process-wide: the Logger, the Component registry, the assert handler and the thread
and worker indices. Each is reached through an instance pointer or function that can be replaced
(`Logger::setInstance()`, `ComponentRegistry::setInstance()`, `setAssertHandler()`).

This matters for hot reload. In the editor the game module is a shared library that links its own copy
of the static core library. When the module loads, the `ModuleHost` injects its own services into the
module, so both sides share one Logger and one Component registry. Component types are identified by the
hash of their fully qualified name, so a Component gets the same id on both sides. The registry records
which module's code each type uses, so that a reload can hand the type over to the new code (ADR 0027).
Only the core library may exist twice: a shared module links the other engine libraries' headers alone
(`USES`) and calls their code through the host, and the host runs modules on every World attached to it
(ADR 0041).

## Repository layout

| Path | Contents |
| --- | --- |
| `engine/core` | Core library: `include/illusion/{core,ecs}` and `src/` |
| `engine/text` | Text library: `include/illusion/text` and `src/` |
| `engine/platform` | Platform library: `include/illusion/platform` and `src/` |
| `engine/input` | Input library: `include/illusion/input` and `src/` |
| `engine/assets` | Assets library: `include/illusion/assets` and `src/` |
| `engine/physics` | Physics library: `include/illusion/physics` and `src/` |
| `engine/audio` | Audio library: `include/illusion/audio` and `src/` |
| `engine/modules` | Module host: `include/illusion/modules` and `src/` |
| `engine/render` | Render library: `include/illusion/render`, `src/` and `shaders/` |
| `samples` | Small applications that exercise engine features |
| `tests` | Unit tests (doctest), one file per feature |
| `tools/pack` | `illusion_pack`: archive packer and asset cooker |
| `tools/textc` | `illusion_textc`: string table generator and locale file check |
| `benchmarks` | Benchmarks with performance budgets (nanobench) |
| `cmake` | Build options, compiler settings, pinned dependencies |
| `docs/content/{en,tr}` | This documentation, in English and Turkish |
| `docs/adr` | Architecture Decision Records |
| `tools` | Format, documentation and platform helper scripts |
| `.claude/skills` | Step-by-step guides for AI agents and humans |

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

## M1 — Platform and first pixels

- Window and keyboard/mouse input (GLFW), high-resolution timer, frame loop
- File system abstraction (virtual paths, platform user-data directories)
- Event bus
- NVRHI device creation on Vulkan (Windows, Linux) and MoltenVK (macOS); swapchain; clear and triangle
- Vulkan validation in Debug; GPU smoke test in CI where a software Vulkan driver is available (lavapipe)

## M2 — Input

- SDL3 gamepad subsystem: Xbox and DualSense, hot-plug, rumble, LED, gyro, touchpad, adaptive triggers
- Action-mapping layer, rebinding with persistence, per-device glyphs

## M3 — Renderer core

- Bindless resources, GPU-driven draw submission where supported, instancing
- glTF 2.0 import (meshes, materials, textures) through the asset pipeline
- PBR shading, IBL from HDRIs, CSM + PCSS soft shadows, GTAO
- HDR pipeline, bloom, tonemapping, optional TAA
- Frustum and occlusion culling (Job System parallel)
- Render benchmarks with frame-time budgets

## M4 — Assets and streaming

- Asset database, import cache, async streaming on the Job System and dedicated I/O threads
- Packed asset archives for export

## M5 — Gameplay services

- Jolt Physics integration (ECS components, fixed-step simulation)
- miniaudio integration (3D sound, mixer)
- Component reflection with stable, explicit names; serialization; semver for modules

## M6 — Game modules and hot reload

- Module/plugin system with semver; game module as hot-reloadable shared library in the editor and
  statically linked in exports
- Service injection into modules (ADR 0003): Logger, ComponentRegistry, assert handler; re-registration
  of module-owned component types on reload

## M7 — Editor and AI control

- Editor UI, scene editing, gizmos
- Command/tool API (JSON-RPC and MCP) covering every editor operation
- Acceptance test: an agent builds a Tetris-like game end to end without human input
- Test scene exercising every component and the whole scripting API

## M8 — Release pipeline

- Export: clean runtime build, packed assets, no editor code
- CPack installers: Windows (signing-ready), macOS `.app` (notarization-ready), Linux (Steam Runtime
  "sniper" compatible)
- Optional Steamworks module (achievements, cloud saves, Steam Input); SDK supplied by the user
- Release and Steam guide (EN/TR)

# illusion

illusion is a performance-focused 3D game engine written in C++20 for Windows, macOS and Linux. It is
designed to ship commercial games on Steam and similar stores.

!!! warning "Early development"
    illusion is in its foundation phase. The core library (logging, memory, Job System, ECS) is
    implemented and tested. Rendering, input, audio, physics, the editor and the export pipeline are
    planned; see the [roadmap](https://github.com/oguzhanaknc/illusion/blob/main/ROADMAP.md).

## Design goals

- **Data-oriented.** Entities and Components live in an archetype-based ECS with structure-of-arrays
  storage, so systems stream through tightly packed memory.
- **Parallel by default.** A Job System with an allocation-free queue runs work on every core; ECS
  queries can iterate in parallel.
- **No per-frame allocations.** Steady-state frames use arenas and pools instead of the general heap.
- **Measured.** Hot paths have benchmarks with performance budgets that run in CI, and the engine is
  instrumented for the Tracy profiler.
- **Tested.** Every feature ships with unit tests that run on all three platforms, plus sanitizer builds.
- **Controllable by tools and AI agents.** The editor will expose a command API so that agents can build
  games without human intervention.

## Platforms

| Platform | Compilers | Graphics (planned) |
| --- | --- | --- |
| Windows 10/11 x64 | MSVC 19.40+, clang-cl 18+ | Vulkan |
| macOS 13.3+ | Apple Clang 15+ | Vulkan via MoltenVK |
| Linux (Ubuntu 24.04+, Steam Runtime "sniper") | GCC 13+, Clang 18+ | Vulkan |

## Where to go next

- [Getting Started](getting-started.md): build the engine and run the tests.
- [Manual](manual/ecs.md): learn the core systems.
- [Architecture](architecture/overview.md): how the engine is put together and why.
- [Contributing](contributing.md): workflow, code style and documentation rules.

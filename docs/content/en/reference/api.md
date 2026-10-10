# API Reference

The API reference is generated from the comments in the public headers with Doxygen. Every public
declaration is documented; CI fails if one is not.

## Generating the reference locally

Install [Doxygen](https://www.doxygen.nl/) 1.9 or newer, then run from the repository root:

```bash
doxygen docs/Doxyfile
```

Open `build/api/index.html` in a browser.

## Public headers

| Header | Contents |
| --- | --- |
| `illusion/core/Base.hpp` | Fixed-width types, platform macros, alignment helpers |
| `illusion/core/Assert.hpp` | `ILL_ASSERT`, `ILL_VERIFY`, `ILL_CHECK`, assert handlers |
| `illusion/core/Log.hpp` | `Logger`, `LogSink`, `ILL_LOG_*` macros |
| `illusion/core/Memory.hpp` | `LinearAllocator`, `PoolAllocator`, aligned allocation |
| `illusion/core/JobSystem.hpp` | `JobSystem`, `JobCounter`, `Job` |
| `illusion/core/InplaceFunction.hpp` | Allocation-free type-erased callable |
| `illusion/core/Thread.hpp` | Thread names, thread indices, spin-wait hint |
| `illusion/core/Hash.hpp` | Stable FNV-1a hashing |
| `illusion/core/TypeName.hpp` | Compile-time type names |
| `illusion/core/Profiler.hpp` | Tracy profiling macros |
| `illusion/ecs/World.hpp` | `World`, `Query`, `ChunkView` |
| `illusion/ecs/CommandBuffer.hpp` | Deferred structural changes |
| `illusion/ecs/Component.hpp` | Component registration and `ComponentRegistry` |
| `illusion/ecs/Entity.hpp` | `Entity` handle |
| `illusion/ecs/Archetype.hpp` | Archetype storage (engine internal) |

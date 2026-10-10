# Memory

illusion avoids the general-purpose heap in steady-state frames. Allocations made every frame cause
contention between threads, fragment memory and make frame times unpredictable. The core library
provides two allocators for those cases.

Header: `#include <illusion/core/Memory.hpp>`

## LinearAllocator

A linear allocator (arena) hands out memory by bumping an offset. Individual frees are not supported:
you rewind to a marker or reset the whole arena. Use one per thread for transient per-frame data:

```cpp
illusion::LinearAllocator frameArena(4 * 1024 * 1024);

void update()
{
    frameArena.reset();
    auto* visible = frameArena.allocateArray<illusion::Entity>(1024);
    // ... fill and use `visible` during this frame only ...
}
```

`create<T>()` constructs an object in the arena; `T` must be trivially destructible because the arena
never runs destructors. `peak()` reports the high-water mark, which helps to size arenas.

## PoolAllocator

A pool allocator manages blocks of one fixed size. It reserves memory in pages and reuses freed blocks,
so allocation and deallocation are a few instructions:

```cpp
illusion::PoolAllocator pool(sizeof(Particle), alignof(Particle), 256);
void* memory = pool.allocate();
// ...
pool.deallocate(memory);
```

The ECS uses a pool of 16 KiB Chunks per World.

## Aligned allocation

`alignedAlloc(size, alignment)` and `alignedFree(ptr)` allocate memory with any power-of-two alignment
on every platform.

## Rules

- Do not call `new`, `malloc`, or grow containers in per-frame code paths. Reserve capacity up front or
  use an arena.
- Allocators are not thread-safe. Give each thread or Job its own arena.
- Out-of-memory conditions are fatal: they trigger `ILL_CHECK` and terminate with a message.

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
`reallocate(ptr, oldSize, newSize)` resizes a block: the most recent allocation grows in place, any
other block is copied. Libraries that need a `realloc`, such as the JSON parser, can work on an arena
this way.

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

## RangeAllocator

A range allocator hands out ranges of elements inside a buffer that you own, such as a GPU vertex pool. It
takes the first freed range that is large enough and otherwise extends the used part of the buffer
(`end()`), which the caller grows its buffer to. Freed ranges merge with their neighbours, and a free
range at the end shrinks `end()`:

```cpp
illusion::RangeAllocator ranges;
const illusion::u32 a = ranges.allocate(1000); // offset 0
const illusion::u32 b = ranges.allocate(500);  // offset 1000
ranges.free(a, 1000);
const illusion::u32 c = ranges.allocate(800);  // offset 0: reuses the freed range
// grow the buffer to ranges.end() elements before writing
```

`RenderResources` keeps its vertex and index pools with two of them, so meshes that are released and
created again reuse their memory.

## Aligned allocation

`alignedAlloc(size, alignment)` and `alignedFree(ptr)` allocate memory with any power-of-two alignment
on every platform.

## Rules

- Do not call `new`, `malloc`, or grow containers in per-frame code paths. Reserve capacity up front or
  use an arena.
- Allocators are not thread-safe. Give each thread or Job its own arena.
- Running out of heap memory is fatal: it triggers `ILL_CHECK` and terminates with a message. A full
  arena is not: `allocate()` returns `nullptr`, and APIs that take an arena (such as
  [JSON](json.md#memory)) report the failure as a return value.

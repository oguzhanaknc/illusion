#include <illusion/core/Memory.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <set>

using namespace illusion;

namespace
{

bool isAligned(const void* ptr, usize alignment)
{
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

} // namespace

TEST_CASE("alignedAlloc honours the requested alignment")
{
    for (const usize alignment : {usize{8}, usize{16}, usize{64}, usize{256}, usize{4096}})
    {
        void* ptr = alignedAlloc(100, alignment);
        REQUIRE(ptr != nullptr);
        CHECK(isAligned(ptr, alignment));
        alignedFree(ptr);
    }
    alignedFree(nullptr);
}

TEST_CASE("LinearAllocator allocates aligned memory until full")
{
    LinearAllocator arena(1024);
    CHECK(arena.capacity() == 1024);

    void* a = arena.allocate(10, 1);
    void* b = arena.allocate(16, 64);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(isAligned(b, 64));
    CHECK(arena.used() >= 26);

    CHECK(arena.allocate(2048) == nullptr); // larger than the arena
    CHECK(arena.allocate(arena.capacity() - arena.used(), 1) != nullptr);
    CHECK(arena.allocate(1, 1) == nullptr);
}

TEST_CASE("LinearAllocator mark/rewind/reset reuse memory and track the peak")
{
    LinearAllocator arena(256);
    void* first = arena.allocate(32);
    const LinearAllocator::Marker marker = arena.mark();
    void* second = arena.allocate(64);
    arena.rewind(marker);
    CHECK(arena.allocate(64) == second);
    const usize peak = arena.peak();
    arena.reset();
    CHECK(arena.used() == 0);
    CHECK(arena.peak() == peak);
    CHECK(arena.allocate(32) == first);
}

TEST_CASE("LinearAllocator typed helpers")
{
    struct Pod
    {
        int a;
        float b;
    };
    LinearAllocator arena(1024);
    Pod* pod = arena.create<Pod>(Pod{1, 2.0f});
    REQUIRE(pod != nullptr);
    CHECK(pod->a == 1);
    CHECK(isAligned(pod, alignof(Pod)));

    double* values = arena.allocateArray<double>(8);
    REQUIRE(values != nullptr);
    CHECK(isAligned(values, alignof(double)));
}

TEST_CASE("PoolAllocator hands out distinct aligned blocks and recycles them")
{
    PoolAllocator pool(40, 32, 4);
    CHECK(pool.blockSize() == 64); // rounded up to the alignment

    std::set<void*> blocks;
    for (int i = 0; i < 10; ++i)
    {
        void* block = pool.allocate();
        REQUIRE(block != nullptr);
        CHECK(isAligned(block, 32));
        blocks.insert(block);
    }
    CHECK(blocks.size() == 10);
    CHECK(pool.allocatedBlocks() == 10);
    CHECK(pool.capacityBlocks() == 12); // three pages of four

    void* recycled = *blocks.begin();
    pool.deallocate(recycled);
    CHECK(pool.allocate() == recycled);

    for (void* block : blocks)
    {
        pool.deallocate(block);
    }
    CHECK(pool.allocatedBlocks() == 0);
}

TEST_CASE("PoolAllocator::reserve pre-allocates pages")
{
    PoolAllocator pool(16, 16, 8);
    pool.reserve(20);
    CHECK(pool.capacityBlocks() >= 20);
    const usize capacity = pool.capacityBlocks();
    void* blocks[20];
    for (void*& block : blocks)
    {
        block = pool.allocate();
    }
    CHECK(pool.capacityBlocks() == capacity);
    for (void* block : blocks)
    {
        pool.deallocate(block);
    }
}

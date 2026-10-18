#include "TestSupport.hpp"

#include <illusion/core/Memory.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <thread>
#include <vector>

using namespace illusion;

namespace
{

bool isAligned(const void* ptr, usize alignment)
{
    return reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0;
}

// Publishing a pointer through a volatile global keeps the compiler from eliding its allocation (C++14
// allows removing new/delete pairs whose result does not escape).
const void* volatile g_escaped = nullptr;

void escape(const void* ptr)
{
    g_escaped = ptr;
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

TEST_CASE("LinearAllocator::reallocate grows the last block in place and copies others")
{
    LinearAllocator arena(1024);
    auto* first = static_cast<char*>(arena.allocate(16, 16));
    REQUIRE(first != nullptr);
    first[0] = 'a';
    first[15] = 'z';
    // The most recent allocation grows and shrinks in place.
    CHECK(arena.reallocate(first, 16, 64) == first);
    CHECK(arena.used() == 64);
    CHECK(arena.reallocate(first, 64, 32) == first);
    CHECK(arena.used() == 32);

    auto* second = static_cast<char*>(arena.allocate(8, 8));
    REQUIRE(second != nullptr);
    // An older block moves, keeping its contents.
    auto* moved = static_cast<char*>(arena.reallocate(first, 32, 100));
    REQUIRE(moved != nullptr);
    CHECK(moved != first);
    CHECK(moved[0] == 'a');
    CHECK(moved[15] == 'z');

    // Without room the old block stays as it is.
    const usize used = arena.used();
    CHECK(arena.reallocate(moved, 100, 4096) == nullptr);
    CHECK(arena.used() == used);
    CHECK(moved[0] == 'a');

    // A null block is a plain allocation.
    CHECK(arena.reallocate(nullptr, 0, 8) != nullptr);
    CHECK(arena.owns(moved));
    int outside = 0;
    CHECK_FALSE(arena.owns(&outside));
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

TEST_CASE("ScopedAllocationCounter sees heap allocations on its thread only")
{
    struct alignas(64) OverAligned
    {
        char bytes[64];
    };
    std::atomic<bool> go{false};
    std::atomic<bool> done{false};
    std::thread other([&] {
        while (!go.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
        auto elsewhere = std::make_unique<int>(2);
        escape(elsewhere.get());
        done.store(true, std::memory_order_release);
    });

    test::ScopedAllocationCounter allocations;
    CHECK(allocations.count() == 0);
    auto single = std::make_unique<int>(1);
    auto array = std::make_unique<int[]>(16);
    auto aligned = std::make_unique<OverAligned>();
    escape(single.get());
    escape(array.get());
    escape(aligned.get());
    CHECK(allocations.count() == 3);

    go.store(true, std::memory_order_release);
    while (!done.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }
    CHECK(allocations.count() == 3); // the other thread's allocation is not counted here
    {
        test::ScopedAllocationCounter nested;
        std::vector<double> growing;
        growing.reserve(8);
        escape(growing.data());
        CHECK(nested.count() == 1);
    }
    CHECK(allocations.count() == 4);
    other.join();
}

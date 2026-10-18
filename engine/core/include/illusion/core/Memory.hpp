#pragma once

/// @file
/// Memory primitives: aligned allocation, linear (arena) allocator and fixed-size block pool.
///
/// Engine rule: steady-state frames must not touch the general-purpose heap. Use a LinearAllocator
/// reset once per frame for transient data and PoolAllocator for fixed-size objects with churn.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace illusion
{

/// Allocates @p size bytes aligned to @p alignment (a power of two). Returns nullptr on failure.
/// Free with alignedFree().
[[nodiscard]] void* alignedAlloc(usize size, usize alignment) noexcept;

/// Frees memory returned by alignedAlloc(). Accepts nullptr.
void alignedFree(void* ptr) noexcept;

/// Bump allocator over a single fixed buffer. Individual frees are not supported: rewind to a
/// marker or reset() the whole arena. Not thread-safe; use one per thread.
class LinearAllocator : NonCopyable
{
public:
    /// Position in the arena returned by mark() and accepted by rewind().
    using Marker = usize;

    /// Creates an arena owning a buffer of @p capacity bytes.
    explicit LinearAllocator(usize capacity);
    ~LinearAllocator();

    /// Returns @p size bytes aligned to @p alignment, or nullptr when the arena is full.
    [[nodiscard]] void* allocate(usize size, usize alignment = alignof(std::max_align_t)) noexcept;

    /// Resizes the block @p ptr of @p oldSize bytes (from this arena, aligned to @p alignment) to
    /// @p newSize bytes and returns its new address, or nullptr when the arena is full (the old block is
    /// then unchanged). The most recent allocation grows or shrinks in place; any other block is copied
    /// to a new allocation and its old bytes stay used until rewind() or reset(). A null @p ptr allocates.
    [[nodiscard]] void* reallocate(void* ptr, usize oldSize, usize newSize,
                                   usize alignment = alignof(std::max_align_t)) noexcept;

    /// Returns true if @p ptr points into this arena's buffer.
    [[nodiscard]] bool owns(const void* ptr) const noexcept
    {
        const auto* byte = static_cast<const std::byte*>(ptr);
        return byte >= m_buffer && byte < m_buffer + m_capacity;
    }

    /// Allocates uninitialized storage for @p count objects of type T, or nullptr when full.
    template <class T>
    [[nodiscard]] T* allocateArray(usize count) noexcept
    {
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    /// Constructs a T in the arena. T must be trivially destructible because the arena never runs
    /// destructors. Returns nullptr when full.
    template <class T, class... Args>
    [[nodiscard]] T* create(Args&&... args)
    {
        static_assert(std::is_trivially_destructible_v<T>, "LinearAllocator never calls destructors");
        void* memory = allocate(sizeof(T), alignof(T));
        return memory != nullptr ? ::new (memory) T(std::forward<Args>(args)...) : nullptr;
    }

    /// Returns the current position.
    [[nodiscard]] Marker mark() const noexcept { return m_offset; }

    /// Frees everything allocated after @p marker.
    void rewind(Marker marker) noexcept
    {
        ILL_ASSERT(marker <= m_offset, "rewinding forward");
        m_offset = marker;
    }

    /// Frees everything.
    void reset() noexcept { m_offset = 0; }

    /// Bytes currently allocated (including alignment padding).
    [[nodiscard]] usize used() const noexcept { return m_offset; }

    /// Total size of the arena in bytes.
    [[nodiscard]] usize capacity() const noexcept { return m_capacity; }

    /// Highest value used() has reached since construction. Useful for sizing arenas.
    [[nodiscard]] usize peak() const noexcept { return m_peak; }

private:
    std::byte* m_buffer;
    usize m_capacity;
    usize m_offset = 0;
    usize m_peak = 0;
};

/// Pool of fixed-size, fixed-alignment blocks. Memory is reserved in pages of several blocks and is
/// only returned to the system when the pool is destroyed. Not thread-safe.
class PoolAllocator : NonCopyable
{
public:
    /// Creates a pool of @p blockSize-byte blocks aligned to @p blockAlignment (a power of two),
    /// allocating @p blocksPerPage blocks at a time.
    PoolAllocator(usize blockSize, usize blockAlignment, usize blocksPerPage);
    ~PoolAllocator();

    /// Returns a block, allocating a new page if needed. Returns nullptr if the system is out of memory.
    [[nodiscard]] void* allocate() noexcept;

    /// Returns @p block (from this pool) to the free list. Accepts nullptr.
    void deallocate(void* block) noexcept;

    /// Ensures at least @p blockCount blocks can be allocated without allocating pages.
    void reserve(usize blockCount);

    /// Size of each block in bytes (rounded up to the alignment).
    [[nodiscard]] usize blockSize() const noexcept { return m_blockSize; }

    /// Number of blocks currently handed out.
    [[nodiscard]] usize allocatedBlocks() const noexcept { return m_allocated; }

    /// Number of blocks owned by the pool (allocated + free).
    [[nodiscard]] usize capacityBlocks() const noexcept { return m_pages.size() * m_blocksPerPage; }

private:
    bool addPage() noexcept;

    struct FreeBlock
    {
        FreeBlock* next;
    };

    usize m_blockSize;
    usize m_blockAlignment;
    usize m_blocksPerPage;
    usize m_allocated = 0;
    usize m_free = 0;
    FreeBlock* m_freeList = nullptr;
    std::vector<void*> m_pages;
};

} // namespace illusion

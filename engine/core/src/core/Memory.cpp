#include <illusion/core/Memory.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace illusion
{

void* alignedAlloc(usize size, usize alignment) noexcept
{
    ILL_ASSERT(isPowerOfTwo(alignment), "alignment must be a power of two");
    alignment = std::max(alignment, alignof(void*));
#if defined(ILL_PLATFORM_WINDOWS)
    return _aligned_malloc(size, alignment);
#else
    // std::aligned_alloc requires size to be a multiple of alignment.
    return std::aligned_alloc(alignment, alignUp(std::max<usize>(size, 1), alignment));
#endif
}

void alignedFree(void* ptr) noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

// --- LinearAllocator ----------------------------------------------------------------------------------

LinearAllocator::LinearAllocator(usize capacity)
    : m_buffer(static_cast<std::byte*>(alignedAlloc(capacity, kCacheLineSize)))
    , m_capacity(capacity)
{
    ILL_CHECK(m_buffer != nullptr, "LinearAllocator: out of memory reserving {} bytes", capacity);
}

LinearAllocator::~LinearAllocator()
{
    alignedFree(m_buffer);
}

void* LinearAllocator::allocate(usize size, usize alignment) noexcept
{
    ILL_ASSERT(isPowerOfTwo(alignment), "alignment must be a power of two");
    // Align the absolute address, not just the offset, so alignments above the buffer's own work.
    const auto base = reinterpret_cast<std::uintptr_t>(m_buffer);
    const usize alignedOffset = alignUp(base + m_offset, alignment) - base;
    if (alignedOffset > m_capacity || size > m_capacity - alignedOffset)
    {
        return nullptr;
    }
    m_offset = alignedOffset + size;
    m_peak = std::max(m_peak, m_offset);
    return m_buffer + alignedOffset;
}

void* LinearAllocator::reallocate(void* ptr, usize oldSize, usize newSize, usize alignment) noexcept
{
    if (ptr == nullptr)
    {
        return allocate(newSize, alignment);
    }
    ILL_ASSERT(owns(ptr), "reallocate() of a block from another allocator");
    auto* block = static_cast<std::byte*>(ptr);
    const auto offset = static_cast<usize>(block - m_buffer);
    if (offset + oldSize == m_offset)
    {
        // The last allocation: resize in place.
        if (newSize > m_capacity - offset)
        {
            return nullptr;
        }
        m_offset = offset + newSize;
        m_peak = std::max(m_peak, m_offset);
        return ptr;
    }
    void* moved = allocate(newSize, alignment);
    if (moved != nullptr)
    {
        std::memcpy(moved, ptr, std::min(oldSize, newSize));
    }
    return moved;
}

// --- PoolAllocator ------------------------------------------------------------------------------------

PoolAllocator::PoolAllocator(usize blockSize, usize blockAlignment, usize blocksPerPage)
    : m_blockSize(
          alignUp(std::max(blockSize, sizeof(FreeBlock)), std::max(blockAlignment, alignof(FreeBlock))))
    , m_blockAlignment(std::max(blockAlignment, alignof(FreeBlock)))
    , m_blocksPerPage(blocksPerPage)
{
    ILL_CHECK(isPowerOfTwo(blockAlignment), "PoolAllocator: alignment must be a power of two");
    ILL_CHECK(blocksPerPage > 0, "PoolAllocator: blocksPerPage must be > 0");
}

PoolAllocator::~PoolAllocator()
{
    ILL_ASSERT(m_allocated == 0, "PoolAllocator destroyed with {} blocks still allocated", m_allocated);
    for (void* page : m_pages)
    {
        alignedFree(page);
    }
}

bool PoolAllocator::addPage() noexcept
{
    auto* page = static_cast<std::byte*>(alignedAlloc(m_blockSize * m_blocksPerPage, m_blockAlignment));
    if (page == nullptr)
    {
        return false;
    }
    // Reserving the page slot can throw; treat that as out of memory.
    try
    {
        m_pages.push_back(page);
    }
    catch (...)
    {
        alignedFree(page);
        return false;
    }
    // Push blocks in reverse so allocation order follows address order within the page.
    for (usize i = m_blocksPerPage; i-- > 0;)
    {
        auto* block = reinterpret_cast<FreeBlock*>(page + i * m_blockSize);
        block->next = m_freeList;
        m_freeList = block;
    }
    m_free += m_blocksPerPage;
    return true;
}

void* PoolAllocator::allocate() noexcept
{
    if (m_freeList == nullptr && !addPage())
    {
        return nullptr;
    }
    FreeBlock* block = m_freeList;
    m_freeList = block->next;
    --m_free;
    ++m_allocated;
    return block;
}

void PoolAllocator::deallocate(void* block) noexcept
{
    if (block == nullptr)
    {
        return;
    }
    ILL_ASSERT(m_allocated > 0, "PoolAllocator: double free or foreign block");
    auto* freeBlock = static_cast<FreeBlock*>(block);
    freeBlock->next = m_freeList;
    m_freeList = freeBlock;
    ++m_free;
    --m_allocated;
}

void PoolAllocator::reserve(usize blockCount)
{
    while (m_free < blockCount)
    {
        ILL_CHECK(addPage(), "PoolAllocator: out of memory");
    }
}

} // namespace illusion

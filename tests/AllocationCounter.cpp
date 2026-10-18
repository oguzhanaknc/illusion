// Counts the heap allocations a piece of code makes on the calling thread (see
// test::ScopedAllocationCounter). Sanitizer runtimes define the global allocation functions themselves, so
// sanitizer builds count through the sanitizer's allocator hooks (which also see malloc()); other builds
// replace the global allocation functions of the test executable.

#include "TestSupport.hpp"

#include <illusion/core/Memory.hpp>

#include <cstdlib>
#include <new>

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define ILL_TEST_SANITIZER_HOOKS 1
#endif
#endif
#if !defined(ILL_TEST_SANITIZER_HOOKS) && !defined(_MSC_VER) &&                                              \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define ILL_TEST_SANITIZER_HOOKS 1
#endif

#if defined(ILL_TEST_SANITIZER_HOOKS)
#include <sanitizer/allocator_interface.h>
#endif

namespace
{

thread_local int t_countingDepth = 0;
thread_local illusion::usize t_allocations = 0;

#if defined(ILL_TEST_SANITIZER_HOOKS)

void onAllocate(const volatile void*, std::size_t) noexcept
{
    if (t_countingDepth > 0)
    {
        ++t_allocations;
    }
}

void onFree(const volatile void*) noexcept
{
}

// Installed before main() runs; the hooks stay installed for the life of the process.
[[maybe_unused]] const int g_hooksInstalled = __sanitizer_install_malloc_and_free_hooks(&onAllocate, &onFree);

#else

void* allocate(std::size_t size) noexcept
{
    if (t_countingDepth > 0)
    {
        ++t_allocations;
    }
    return std::malloc(size != 0 ? size : 1);
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) noexcept
{
    if (t_countingDepth > 0)
    {
        ++t_allocations;
    }
    return illusion::alignedAlloc(size != 0 ? size : 1, static_cast<std::size_t>(alignment));
}

#endif

} // namespace

namespace illusion::test
{

ScopedAllocationCounter::ScopedAllocationCounter() noexcept
    : m_start(t_allocations)
{
    ++t_countingDepth;
}

ScopedAllocationCounter::~ScopedAllocationCounter()
{
    --t_countingDepth;
}

usize ScopedAllocationCounter::count() const noexcept
{
    return t_allocations - m_start;
}

} // namespace illusion::test

#if !defined(ILL_TEST_SANITIZER_HOOKS)

void* operator new(std::size_t size)
{
    if (void* memory = allocate(size))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    if (void* memory = allocate(size))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (void* memory = allocateAligned(size, alignment))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    if (void* memory = allocateAligned(size, alignment))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateAligned(size, alignment);
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
    illusion::alignedFree(memory);
}

void operator delete[](void* memory, std::align_val_t) noexcept
{
    illusion::alignedFree(memory);
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    illusion::alignedFree(memory);
}

void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    illusion::alignedFree(memory);
}

void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    illusion::alignedFree(memory);
}

void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    illusion::alignedFree(memory);
}

#endif

#include "NavAllocator.hpp"

#include <illusion/core/Memory.hpp>

#include <DetourAlloc.h>
#include <RecastAlloc.h>
#include <algorithm>
#include <cstddef>

namespace illusion::detail
{
namespace
{

void* allocate(usize size) noexcept
{
    return alignedAlloc(std::max<usize>(size, 1), alignof(std::max_align_t));
}

void* recastAlloc(size_t size, rcAllocHint) noexcept
{
    return allocate(size);
}

void* detourAlloc(size_t size, dtAllocHint) noexcept
{
    return allocate(size);
}

void release(void* pointer) noexcept
{
    alignedFree(pointer);
}

} // namespace

void installNavAllocators() noexcept
{
    static const bool installed = [] {
        rcAllocSetCustom(recastAlloc, release);
        dtAllocSetCustom(detourAlloc, release);
        return true;
    }();
    static_cast<void>(installed);
}

} // namespace illusion::detail

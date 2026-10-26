#include <illusion/core/Profiler.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <cstring>
#include <vector>

namespace illusion
{

struct ReadbackRing::Impl
{
    struct Slot
    {
        nvrhi::StagingTextureHandle staging;
        nvrhi::CommandListHandle commands;
        nvrhi::EventQueryHandle done;
        u64 ticket = 0;
        usize rowBytes = 0;
        u32 height = 0;
    };
    nvrhi::IDevice* device = nullptr;
    std::vector<Slot> slots;
    u64 nextTicket = 1;
};

ReadbackRing::ReadbackRing()
    : m_impl(std::make_unique<Impl>())
{
}
ReadbackRing::~ReadbackRing() = default;

std::unique_ptr<ReadbackRing> ReadbackRing::create(RenderDevice& device, u32 capacity)
{
    if (capacity == 0 || capacity > 64)
    {
        return nullptr;
    }
    std::unique_ptr<ReadbackRing> ring(new ReadbackRing());
    ring->m_impl->device = device.device();
    ring->m_impl->slots.resize(capacity);
    for (Impl::Slot& slot : ring->m_impl->slots)
    {
        slot.commands = device.device()->createCommandList();
        slot.done = device.device()->createEventQuery();
        if (!slot.commands || !slot.done)
        {
            return nullptr;
        }
    }
    return ring;
}

std::optional<u64> ReadbackRing::request(nvrhi::ITexture* source, UVec2 origin, UVec2 extent)
{
    ILL_PROFILE_SCOPE();
    if (source == nullptr)
    {
        return std::nullopt;
    }
    const auto& desc = source->getDesc();
    const auto& format = nvrhi::getFormatInfo(desc.format);
    if (desc.dimension != nvrhi::TextureDimension::Texture2D || desc.sampleCount != 1 ||
        format.blockSize != 1 || format.bytesPerBlock == 0 || format.hasStencil || origin.x >= desc.width ||
        origin.y >= desc.height)
    {
        return std::nullopt;
    }
    if (extent == UVec2(0))
    {
        extent = UVec2(desc.width, desc.height) - origin;
    }
    if (extent.x == 0 || extent.y == 0 || extent.x > desc.width - origin.x ||
        extent.y > desc.height - origin.y)
    {
        return std::nullopt;
    }
    Impl& impl = *m_impl;
    for (Impl::Slot& slot : impl.slots)
    {
        if (slot.ticket != 0)
        {
            continue;
        }
        if (!slot.staging || slot.staging->getDesc().width != extent.x ||
            slot.staging->getDesc().height != extent.y || slot.staging->getDesc().format != desc.format)
        {
            nvrhi::TextureDesc staging;
            staging.width = extent.x;
            staging.height = extent.y;
            staging.format = desc.format;
            staging.debugName = "Readback ring";
            slot.staging = impl.device->createStagingTexture(staging, nvrhi::CpuAccessMode::Read);
            if (!slot.staging)
            {
                return std::nullopt;
            }
        }
        impl.device->resetEventQuery(slot.done);
        slot.commands->open();
        slot.commands->copyTexture(
            slot.staging, nvrhi::TextureSlice().setSize(extent.x, extent.y, 1), source,
            nvrhi::TextureSlice().setOrigin(origin.x, origin.y).setSize(extent.x, extent.y, 1));
        slot.commands->close();
        impl.device->executeCommandList(slot.commands);
        impl.device->setEventQuery(slot.done, nvrhi::CommandQueue::Graphics);
        slot.ticket = impl.nextTicket++;
        if (impl.nextTicket == 0)
        {
            ++impl.nextTicket;
        }
        slot.rowBytes = static_cast<usize>(extent.x) * format.bytesPerBlock;
        slot.height = extent.y;
        return slot.ticket;
    }
    return std::nullopt;
}

ReadbackStatus ReadbackRing::read(u64 ticket, std::span<std::byte> destination, bool block)
{
    ILL_PROFILE_SCOPE();
    if (ticket == 0)
    {
        return ReadbackStatus::Invalid;
    }
    Impl& impl = *m_impl;
    for (Impl::Slot& slot : impl.slots)
    {
        if (slot.ticket != ticket)
        {
            continue;
        }
        if (destination.size() < slot.rowBytes * slot.height)
        {
            return ReadbackStatus::Invalid;
        }
        if (block)
        {
            impl.device->waitEventQuery(slot.done);
        }
        if (!impl.device->pollEventQuery(slot.done))
        {
            return ReadbackStatus::Pending;
        }
        usize pitch = 0;
        const auto* source = static_cast<const std::byte*>(impl.device->mapStagingTexture(
            slot.staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &pitch));
        if (source == nullptr)
        {
            return ReadbackStatus::Invalid;
        }
        for (u32 y = 0; y < slot.height; ++y)
        {
            std::memcpy(destination.data() + y * slot.rowBytes, source + y * pitch, slot.rowBytes);
        }
        impl.device->unmapStagingTexture(slot.staging);
        slot.ticket = 0;
        return ReadbackStatus::Ready;
    }
    return ReadbackStatus::Invalid;
}

} // namespace illusion

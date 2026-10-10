#include <illusion/core/Assert.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/CommandBuffer.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>

namespace illusion
{

CommandBuffer::CommandBuffer(usize blockSize)
    : m_blockSize(std::max<usize>(blockSize, 1024))
{
}

CommandBuffer::~CommandBuffer()
{
    clear();
    for (const Block& block : m_blocks)
    {
        alignedFree(block.data);
    }
}

template <class Visitor>
void CommandBuffer::forEachCommand(Visitor&& visitor)
{
    for (usize b = 0; b < m_blocks.size() && b <= m_currentBlock; ++b)
    {
        Block& block = m_blocks[b];
        usize offset = 0;
        while (offset < block.used)
        {
            auto* header = reinterpret_cast<Header*>(block.data + offset);
            void* payload = header->payloadOffset != 0 ? block.data + header->payloadOffset : nullptr;
            visitor(*header, payload);
            offset = header->next;
        }
    }
}

void* CommandBuffer::record(CommandType type, Entity entity, ComponentId component, u32 payloadSize,
                            usize payloadAlignment)
{
    ILL_ASSERT(payloadAlignment <= kChunkAlignment, "payload alignment too large");

    // Worst case space for this command in an empty block.
    const usize worstCase = sizeof(Header) + payloadAlignment + payloadSize;

    for (;;)
    {
        if (m_currentBlock == m_blocks.size())
        {
            Block block;
            block.capacity = std::max(m_blockSize, worstCase);
            block.data = static_cast<std::byte*>(alignedAlloc(block.capacity, kChunkAlignment));
            ILL_CHECK(block.data != nullptr, "CommandBuffer: out of memory");
            m_blocks.push_back(block);
        }

        Block& block = m_blocks[m_currentBlock];
        if (block.used == 0 && block.capacity < worstCase)
        {
            // Reused block too small for an oversized payload: replace it.
            alignedFree(block.data);
            block.capacity = std::max(m_blockSize, worstCase);
            block.data = static_cast<std::byte*>(alignedAlloc(block.capacity, kChunkAlignment));
            ILL_CHECK(block.data != nullptr, "CommandBuffer: out of memory");
        }

        const usize headerOffset = alignUp(block.used, alignof(Header));
        const usize payloadOffset = payloadSize != 0
                                        ? alignUp(headerOffset + sizeof(Header), payloadAlignment)
                                        : headerOffset + sizeof(Header);
        const usize end = payloadOffset + payloadSize;
        if (end > block.capacity)
        {
            ++m_currentBlock;
            continue;
        }

        ILL_CHECK(end <= 0xFFFFFFFFu, "CommandBuffer block too large");
        auto* header = ::new (block.data + headerOffset) Header{};
        header->type = type;
        header->component = component;
        header->payloadOffset = payloadSize != 0 ? static_cast<u32>(payloadOffset) : 0u;
        header->next = static_cast<u32>(end);
        header->entity = entity;
        block.used = end;
        ++m_commandCount;
        return payloadSize != 0 ? block.data + payloadOffset : nullptr;
    }
}

void CommandBuffer::destroy(Entity entity)
{
    record(CommandType::Destroy, entity, kInvalidComponentId, 0, 1);
}

void CommandBuffer::apply(World& world)
{
    ILL_PROFILE_SCOPE();
    world.assertNotIterating();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    Entity created = kNullEntity;

    forEachCommand([&](const Header& header, void* payload) {
        switch (header.type)
        {
        case CommandType::Destroy:
            if (world.isAlive(header.entity))
            {
                world.destroy(header.entity);
            }
            break;
        case CommandType::Create:
            created = world.create();
            break;
        case CommandType::Remove:
            if (world.isAlive(header.entity))
            {
                world.removeComponentRaw(header.entity, header.component);
            }
            break;
        case CommandType::Set:
        case CommandType::SetOnCreated:
        {
            const Entity target = header.type == CommandType::Set ? header.entity : created;
            const ComponentInfo& info = registry.info(header.component);
            if (!world.isAlive(target))
            {
                if (payload != nullptr)
                {
                    info.destroy(payload);
                }
                break;
            }
            if (world.m_slots[target.index].archetype->has(header.component))
            {
                if (payload != nullptr)
                {
                    // Replace: destroy the current value, then move the recorded one in.
                    void* destination = world.componentRaw(target, header.component);
                    info.destroy(destination);
                    info.relocate(destination, payload);
                }
            }
            else
            {
                void* destination = world.addComponentRaw(target, header.component);
                if (payload != nullptr)
                {
                    info.relocate(destination, payload);
                }
            }
            break;
        }
        }
    });

    // Every payload has been consumed (relocated or destroyed).
    reset();
}

void CommandBuffer::clear() noexcept
{
    const ComponentRegistry& registry = ComponentRegistry::instance();
    forEachCommand([&](const Header& header, void* payload) {
        if (payload != nullptr)
        {
            registry.info(header.component).destroy(payload);
        }
    });
    reset();
}

void CommandBuffer::reset() noexcept
{
    for (Block& block : m_blocks)
    {
        block.used = 0;
    }
    m_currentBlock = 0;
    m_commandCount = 0;
}

} // namespace illusion

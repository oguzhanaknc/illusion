#include <illusion/core/Assert.hpp>
#include <illusion/ecs/Archetype.hpp>

#include <algorithm>
#include <cstring>

namespace illusion
{

Archetype::Archetype(u32 index, std::span<const ComponentId> signature, const ComponentRegistry& registry,
                     PoolAllocator& chunkPool)
    : m_index(index)
    , m_signature(signature.begin(), signature.end())
    , m_registry(registry)
    , m_chunkPool(chunkPool)
{
    ILL_ASSERT(std::is_sorted(m_signature.begin(), m_signature.end()), "archetype signature must be sorted");
    ILL_ASSERT(std::adjacent_find(m_signature.begin(), m_signature.end()) == m_signature.end(),
               "archetype signature has duplicates");
    ILL_ASSERT(chunkPool.blockSize() == kChunkSize, "chunk pool block size must be kChunkSize");

    m_columnOf.fill(kNoColumn);
    usize bytesPerRow = sizeof(Entity);
    for (const ComponentId id : m_signature)
    {
        m_mask.set(id);
        registry.addArchetypeUse(id, 1);
        const ComponentInfo& info = registry.info(id);
        if (info.isTag())
        {
            continue;
        }
        ILL_CHECK(info.alignment <= kChunkAlignment,
                  "component '{}' alignment {} exceeds the chunk alignment", info.name, info.alignment);
        m_columnOf[id] = static_cast<u16>(m_columns.size());
        m_columns.push_back(Column{id, info.size, 0, &info});
        bytesPerRow += info.size;
    }

    // Start from the upper bound and shrink until all aligned arrays fit in one chunk.
    u32 capacity = static_cast<u32>(kChunkSize / bytesPerRow);
    for (; capacity > 0; --capacity)
    {
        usize offset = sizeof(Entity) * capacity;
        for (Column& column : m_columns)
        {
            offset = alignUp(offset, column.info->alignment);
            column.offset = static_cast<u32>(offset);
            offset += static_cast<usize>(column.size) * capacity;
        }
        if (offset <= kChunkSize)
        {
            break;
        }
    }
    ILL_CHECK(capacity > 0, "archetype row of {} bytes does not fit in a {} byte chunk", bytesPerRow,
              kChunkSize);
    m_chunkCapacity = capacity;
}

Archetype::~Archetype()
{
    for (u32 row = 0; row < m_size; ++row)
    {
        for (u16 c = 0; c < m_columns.size(); ++c)
        {
            if (!m_columns[c].info->trivial)
            {
                m_columns[c].info->destroy(componentAt(row, c));
            }
        }
    }
    for (std::byte* chunk : m_chunks)
    {
        m_chunkPool.deallocate(chunk);
    }
    m_chunkPool.deallocate(m_spareChunk);
    for (const ComponentId id : m_signature)
    {
        m_registry.addArchetypeUse(id, -1);
    }
}

void Archetype::relocate(const Column& column, void* dst, void* src) noexcept
{
    if (column.info->trivial)
    {
        std::memcpy(dst, src, column.size);
    }
    else
    {
        column.info->relocate(dst, src);
    }
}

u32 Archetype::allocateRow(Entity entity)
{
    const u32 row = m_size;
    if (row == static_cast<u32>(m_chunks.size()) * m_chunkCapacity)
    {
        std::byte* chunk = m_spareChunk;
        m_spareChunk = nullptr;
        if (chunk == nullptr)
        {
            chunk = static_cast<std::byte*>(m_chunkPool.allocate());
            ILL_CHECK(chunk != nullptr, "out of memory allocating an archetype chunk");
        }
        m_chunks.push_back(chunk);
    }
    ++m_size;
    reinterpret_cast<Entity*>(m_chunks[row / m_chunkCapacity])[row % m_chunkCapacity] = entity;
    return row;
}

Entity Archetype::removeRow(u32 row, bool destroyComponents) noexcept
{
    ILL_ASSERT(row < m_size, "row {} out of range ({})", row, m_size);
    const u32 last = m_size - 1;

    if (destroyComponents)
    {
        for (u16 c = 0; c < m_columns.size(); ++c)
        {
            if (!m_columns[c].info->trivial)
            {
                m_columns[c].info->destroy(componentAt(row, c));
            }
        }
    }

    Entity moved = kNullEntity;
    if (row != last)
    {
        for (u16 c = 0; c < m_columns.size(); ++c)
        {
            relocate(m_columns[c], componentAt(row, c), componentAt(last, c));
        }
        moved = entityAt(last);
        reinterpret_cast<Entity*>(m_chunks[row / m_chunkCapacity])[row % m_chunkCapacity] = moved;
    }

    m_size = last;
    if (m_size == (static_cast<u32>(m_chunks.size()) - 1) * m_chunkCapacity)
    {
        // The last chunk became empty. Keep one spare so an entity oscillating across the chunk
        // boundary does not hit the pool every time.
        std::byte* chunk = m_chunks.back();
        m_chunks.pop_back();
        if (m_spareChunk == nullptr)
        {
            m_spareChunk = chunk;
        }
        else
        {
            m_chunkPool.deallocate(chunk);
        }
    }
    return moved;
}

Archetype* Archetype::addEdge(ComponentId id) const noexcept
{
    const auto it = m_addEdges.find(id);
    return it != m_addEdges.end() ? it->second : nullptr;
}

Archetype* Archetype::removeEdge(ComponentId id) const noexcept
{
    const auto it = m_removeEdges.find(id);
    return it != m_removeEdges.end() ? it->second : nullptr;
}

void Archetype::setAddEdge(ComponentId id, Archetype* target)
{
    m_addEdges[id] = target;
}

void Archetype::setRemoveEdge(ComponentId id, Archetype* target)
{
    m_removeEdges[id] = target;
}

void Archetype::clearEdges() noexcept
{
    m_addEdges.clear();
    m_removeEdges.clear();
}

} // namespace illusion

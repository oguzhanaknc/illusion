#pragma once

/// @file
/// Archetype: storage for all entities that have exactly the same set of component types.
///
/// Entities are stored in fixed-size chunks (kChunkSize bytes). Inside a chunk the data is laid out as
/// structure-of-arrays: first the Entity handles, then one tightly packed array per component type.
/// Rows are kept dense: removing a row moves the archetype's last row into the hole, so every chunk
/// except the last is full.
///
/// This is an engine-internal type used by World and Query; game code should not need it.

#include <illusion/core/Base.hpp>
#include <illusion/core/Memory.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>

#include <array>
#include <span>
#include <unordered_map>
#include <vector>

namespace illusion
{

/// Size of one archetype chunk in bytes.
inline constexpr usize kChunkSize = 16 * 1024;

/// Alignment of archetype chunks; also the maximum supported component alignment.
inline constexpr usize kChunkAlignment = 64;

/// Storage for entities with one specific set of component types. Not thread-safe.
class Archetype : NonCopyable
{
public:
    /// Value of columnIndex() for components without a column.
    static constexpr u16 kNoColumn = 0xFFFF;

    /// One component array inside each chunk.
    struct Column
    {
        ComponentId id;            ///< Component type.
        u32 size;                  ///< Size of one element.
        u32 offset;                ///< Byte offset of the array from the start of the chunk.
        const ComponentInfo* info; ///< Type description (owned by the registry).
    };

    /// Creates an archetype for the sorted, duplicate-free @p signature. Chunks come from @p chunkPool,
    /// whose block size must be kChunkSize.
    Archetype(u32 index, std::span<const ComponentId> signature, const ComponentRegistry& registry,
              PoolAllocator& chunkPool);

    /// Destroys every component still stored and returns the chunks to the pool.
    ~Archetype();

    /// Position of this archetype in its world's archetype list.
    [[nodiscard]] u32 index() const noexcept { return m_index; }

    /// Sorted component ids, including tags.
    [[nodiscard]] std::span<const ComponentId> signature() const noexcept { return m_signature; }

    /// Signature as a bit mask.
    [[nodiscard]] const ComponentMask& mask() const noexcept { return m_mask; }

    /// Returns true if the signature contains @p id.
    [[nodiscard]] bool has(ComponentId id) const noexcept { return m_mask.test(id); }

    /// Returns the column storing @p id, or kNoColumn for absent and tag components.
    [[nodiscard]] u16 columnIndex(ComponentId id) const noexcept { return m_columnOf[id]; }

    /// Component columns (tags excluded).
    [[nodiscard]] std::span<const Column> columns() const noexcept { return m_columns; }

    /// Number of entities stored.
    [[nodiscard]] u32 size() const noexcept { return m_size; }

    /// Maximum number of entities per chunk.
    [[nodiscard]] u32 chunkCapacity() const noexcept { return m_chunkCapacity; }

    /// Number of chunks that hold at least one entity.
    [[nodiscard]] u32 chunkCount() const noexcept { return static_cast<u32>(m_chunks.size()); }

    /// Number of entities in chunk @p chunk.
    [[nodiscard]] u32 chunkSize(u32 chunk) const noexcept
    {
        const u32 begin = chunk * m_chunkCapacity;
        return m_size - begin < m_chunkCapacity ? m_size - begin : m_chunkCapacity;
    }

    /// Entity handles of chunk @p chunk.
    [[nodiscard]] Entity* entities(u32 chunk) noexcept { return reinterpret_cast<Entity*>(m_chunks[chunk]); }

    /// Start of column @p column in chunk @p chunk.
    [[nodiscard]] void* columnData(u32 chunk, u16 column) noexcept
    {
        return m_chunks[chunk] + m_columns[column].offset;
    }

    /// Address of the component in @p column for @p row.
    [[nodiscard]] void* componentAt(u32 row, u16 column) noexcept
    {
        const Column& c = m_columns[column];
        return m_chunks[row / m_chunkCapacity] + c.offset +
               static_cast<usize>(row % m_chunkCapacity) * c.size;
    }

    /// Entity stored at @p row.
    [[nodiscard]] Entity entityAt(u32 row) const noexcept
    {
        return reinterpret_cast<const Entity*>(m_chunks[row / m_chunkCapacity])[row % m_chunkCapacity];
    }

    /// Appends a row for @p entity and returns its index. Component storage is left uninitialized.
    u32 allocateRow(Entity entity);

    /// Removes @p row by relocating the last row into it. If @p destroyComponents is false the caller
    /// has already destroyed or relocated the row's components. Returns the entity that moved into
    /// @p row, or the null entity if @p row was the last row.
    Entity removeRow(u32 row, bool destroyComponents) noexcept;

    /// Cached archetype reached by adding @p id (nullptr if not computed yet).
    [[nodiscard]] Archetype* addEdge(ComponentId id) const noexcept;

    /// Cached archetype reached by removing @p id (nullptr if not computed yet).
    [[nodiscard]] Archetype* removeEdge(ComponentId id) const noexcept;

    /// Caches the archetype reached by adding @p id.
    void setAddEdge(ComponentId id, Archetype* target);

    /// Caches the archetype reached by removing @p id.
    void setRemoveEdge(ComponentId id, Archetype* target);

private:
    static void relocate(const Column& column, void* dst, void* src) noexcept;

    u32 m_index;
    std::vector<ComponentId> m_signature;
    ComponentMask m_mask;
    std::vector<Column> m_columns;
    std::array<u16, kMaxComponentTypes> m_columnOf;
    u32 m_chunkCapacity = 0;
    u32 m_size = 0;
    std::vector<std::byte*> m_chunks;
    std::byte* m_spareChunk = nullptr;
    PoolAllocator& m_chunkPool;
    std::unordered_map<ComponentId, Archetype*> m_addEdges;
    std::unordered_map<ComponentId, Archetype*> m_removeEdges;
};

} // namespace illusion

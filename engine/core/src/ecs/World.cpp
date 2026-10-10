#include <illusion/core/Hash.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <cstring>

namespace illusion
{
namespace
{

// Chunks are pooled per world; a page holds this many chunks (1 MiB).
constexpr usize kChunksPerPage = 64;

u64 hashSignature(std::span<const ComponentId> signature) noexcept
{
    u64 hash = 0x9ae16a3b2f90404full;
    for (const ComponentId id : signature)
    {
        hash = hashCombine(hash, id);
    }
    return hash;
}

} // namespace

usize World::QueryKeyHash::operator()(const QueryKey& key) const noexcept
{
    const usize a = std::hash<ComponentMask>{}(key.include);
    const usize b = std::hash<ComponentMask>{}(key.exclude);
    return static_cast<usize>(hashCombine(a, b));
}

World::World()
    : m_registry(ComponentRegistry::instance())
    , m_chunkPool(kChunkSize, kChunkAlignment, kChunksPerPage)
{
    m_root = findOrCreateArchetype({});
}

World::~World()
{
    ILL_ASSERT(!isIterating(), "World destroyed while iterating");
    // Archetypes return their chunks to m_chunkPool, which is destroyed after them.
    m_archetypes.clear();
}

Entity World::allocateEntity()
{
    ++m_aliveCount;
    if (!m_freeSlots.empty())
    {
        const u32 index = m_freeSlots.back();
        m_freeSlots.pop_back();
        return Entity{index, m_slots[index].generation};
    }
    ILL_CHECK(m_slots.size() < Entity::kInvalidIndex, "entity limit reached");
    m_slots.emplace_back();
    return Entity{static_cast<u32>(m_slots.size() - 1), 0};
}

Entity World::create()
{
    assertNotIterating();
    const Entity entity = allocateEntity();
    EntitySlot& slot = m_slots[entity.index];
    slot.archetype = m_root;
    slot.row = m_root->allocateRow(entity);
    return entity;
}

void World::destroy(Entity entity)
{
    assertNotIterating();
    ILL_ASSERT(isAlive(entity), "destroying a dead entity");
    if (!isAlive(entity))
    {
        return;
    }
    EntitySlot& slot = m_slots[entity.index];
    const Entity moved = slot.archetype->removeRow(slot.row, true);
    if (!moved.isNull())
    {
        m_slots[moved.index].row = slot.row;
    }
    slot.archetype = nullptr;
    slot.row = 0;
    ++slot.generation;
    m_freeSlots.push_back(entity.index);
    --m_aliveCount;
}

void World::clear()
{
    assertNotIterating();
    std::scoped_lock lock(m_queryMutex);
    m_queryCache.clear();
    m_archetypeLookup.clear();
    m_archetypes.clear();
    m_freeSlots.clear();
    // Invalidate every handle but keep the slots so generations keep increasing.
    for (u32 i = static_cast<u32>(m_slots.size()); i-- > 0;)
    {
        EntitySlot& slot = m_slots[i];
        if (slot.archetype != nullptr)
        {
            ++slot.generation;
        }
        slot.archetype = nullptr;
        slot.row = 0;
        m_freeSlots.push_back(i);
    }
    m_aliveCount = 0;
    m_root = nullptr;
    m_root = findOrCreateArchetype({});
}

Archetype* World::findOrCreateArchetype(std::span<const ComponentId> signature)
{
    const u64 hash = hashSignature(signature);
    const auto [begin, end] = m_archetypeLookup.equal_range(hash);
    for (auto it = begin; it != end; ++it)
    {
        const std::span<const ComponentId> existing = it->second->signature();
        if (std::equal(existing.begin(), existing.end(), signature.begin(), signature.end()))
        {
            return it->second;
        }
    }

    ILL_PROFILE_SCOPE_N("World::createArchetype");
    const auto index = static_cast<u32>(m_archetypes.size());
    m_archetypes.push_back(std::make_unique<Archetype>(index, signature, m_registry, m_chunkPool));
    Archetype* archetype = m_archetypes.back().get();
    m_archetypeLookup.emplace(hash, archetype);
    return archetype;
}

Archetype* World::archetypeWith(Archetype* from, ComponentId id)
{
    if (from->has(id))
    {
        return from;
    }
    if (Archetype* cached = from->addEdge(id))
    {
        return cached;
    }
    std::vector<ComponentId> signature(from->signature().begin(), from->signature().end());
    signature.insert(std::upper_bound(signature.begin(), signature.end(), id), id);
    Archetype* target = findOrCreateArchetype(signature);
    from->setAddEdge(id, target);
    target->setRemoveEdge(id, from);
    return target;
}

Archetype* World::archetypeWithout(Archetype* from, ComponentId id)
{
    if (!from->has(id))
    {
        return from;
    }
    if (Archetype* cached = from->removeEdge(id))
    {
        return cached;
    }
    std::vector<ComponentId> signature;
    signature.reserve(from->signature().size() - 1);
    for (const ComponentId existing : from->signature())
    {
        if (existing != id)
        {
            signature.push_back(existing);
        }
    }
    Archetype* target = findOrCreateArchetype(signature);
    from->setRemoveEdge(id, target);
    target->setAddEdge(id, from);
    return target;
}

void World::moveEntity(Entity entity, Archetype* to)
{
    EntitySlot& slot = m_slots[entity.index];
    Archetype* from = slot.archetype;
    const u32 fromRow = slot.row;
    const u32 toRow = to->allocateRow(entity);

    // Relocate shared components; destroy the ones the target does not have. Components only in the
    // target stay uninitialized for the caller to construct.
    for (u16 c = 0; c < from->columns().size(); ++c)
    {
        const Archetype::Column& column = from->columns()[c];
        void* source = from->componentAt(fromRow, c);
        const u16 targetColumn = to->columnIndex(column.id);
        if (targetColumn != Archetype::kNoColumn)
        {
            void* destination = to->componentAt(toRow, targetColumn);
            if (column.info->trivial)
            {
                std::memcpy(destination, source, column.size);
            }
            else
            {
                column.info->relocate(destination, source);
            }
        }
        else if (!column.info->trivial)
        {
            column.info->destroy(source);
        }
    }

    const Entity moved = from->removeRow(fromRow, false);
    if (!moved.isNull())
    {
        m_slots[moved.index].row = fromRow;
    }
    slot.archetype = to;
    slot.row = toRow;
}

void* World::addComponentRaw(Entity entity, ComponentId id)
{
    assertNotIterating();
    ILL_CHECK(isAlive(entity), "adding a component to a dead entity");
    Archetype* from = m_slots[entity.index].archetype;
    ILL_CHECK(!from->has(id), "entity already has component '{}'", m_registry.info(id).name);
    Archetype* to = archetypeWith(from, id);
    moveEntity(entity, to);
    const u16 column = to->columnIndex(id);
    return column == Archetype::kNoColumn ? nullptr : to->componentAt(m_slots[entity.index].row, column);
}

bool World::removeComponentRaw(Entity entity, ComponentId id)
{
    assertNotIterating();
    ILL_ASSERT(isAlive(entity), "removing a component from a dead entity");
    if (!isAlive(entity))
    {
        return false;
    }
    Archetype* from = m_slots[entity.index].archetype;
    if (!from->has(id))
    {
        return false;
    }
    moveEntity(entity, archetypeWithout(from, id));
    return true;
}

void* World::componentRaw(Entity entity, ComponentId id) noexcept
{
    ILL_ASSERT(isAlive(entity), "accessing a component of a dead entity");
    if (!isAlive(entity))
    {
        return nullptr;
    }
    const EntitySlot& slot = m_slots[entity.index];
    const u16 column = slot.archetype->columnIndex(id);
    return column == Archetype::kNoColumn ? nullptr : slot.archetype->componentAt(slot.row, column);
}

const std::vector<Archetype*>& World::matchArchetypes(const ComponentMask& include,
                                                      const ComponentMask& exclude)
{
    std::scoped_lock lock(m_queryMutex);
    QueryCache& cache = m_queryCache[QueryKey{include, exclude}];
    const auto archetypeCount = static_cast<u32>(m_archetypes.size());
    for (; cache.scanned < archetypeCount; ++cache.scanned)
    {
        Archetype* archetype = m_archetypes[cache.scanned].get();
        const ComponentMask& mask = archetype->mask();
        if ((mask & include) == include && (mask & exclude).none())
        {
            cache.archetypes.push_back(archetype);
        }
    }
    return cache.archetypes;
}

} // namespace illusion

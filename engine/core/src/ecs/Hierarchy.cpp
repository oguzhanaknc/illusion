#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/CommandBuffer.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <atomic>
#include <tuple>

namespace illusion
{

namespace
{

// Sort key of roots that have no Hierarchy: after every ordered root.
constexpr u32 kNoOrder = ~0u;

// Levels with fewer entities are processed on the calling thread.
constexpr u32 kParallelLevelSize = 2048;
constexpr u32 kParallelBatch = 512;

struct Sibling
{
    Entity entity;
    u32 order = 0;
    u64 guid = 0;
};

[[nodiscard]] bool siblingLess(u32 orderA, u64 guidA, Entity a, u32 orderB, u64 guidB, Entity b) noexcept
{
    return std::tie(orderA, guidA, a.index) < std::tie(orderB, guidB, b.index);
}

[[nodiscard]] u64 guidOf(const World& world, Entity entity) noexcept
{
    const EntityGuid* guid = world.tryGet<EntityGuid>(entity);
    return guid != nullptr ? guid->value : 0;
}

// The parent of @p hierarchy if it is alive, else the null entity.
[[nodiscard]] Entity liveParent(const World& world, const Hierarchy& hierarchy) noexcept
{
    return !hierarchy.parent.isNull() && world.isAlive(hierarchy.parent) ? hierarchy.parent : Entity{};
}

// Replaces @p out with the children of @p parent (the null entity: the roots that have a Hierarchy) in
// order, leaving out @p skip.
void collectChildren(World& world, Entity parent, Entity skip, std::vector<Entity>& out)
{
    std::vector<Sibling> siblings;
    world.query<const Hierarchy>().each([&](Entity entity, const Hierarchy& hierarchy) {
        if (entity != skip && liveParent(world, hierarchy) == parent)
        {
            siblings.push_back(Sibling{entity, hierarchy.order, guidOf(world, entity)});
        }
    });
    std::sort(siblings.begin(), siblings.end(), [](const Sibling& a, const Sibling& b) {
        return siblingLess(a.order, a.guid, a.entity, b.order, b.guid, b.entity);
    });
    out.clear();
    for (const Sibling& sibling : siblings)
    {
        out.push_back(sibling.entity);
    }
}

// Gives @p siblings (all with a Hierarchy) the orders 0, 1, 2, ...
void renumber(World& world, std::span<const Entity> siblings) noexcept
{
    for (usize i = 0; i < siblings.size(); ++i)
    {
        world.get<Hierarchy>(siblings[i]).order = static_cast<u32>(i);
    }
}

[[nodiscard]] Transform worldTransformOf(const World& world, Entity entity) noexcept
{
    if (entity.isNull())
    {
        return Transform{};
    }
    const Transform* transform = world.tryGet<Transform>(entity);
    return transform != nullptr ? *transform : Transform{};
}

[[nodiscard]] ComponentMask maskOf(ComponentId id) noexcept
{
    ComponentMask mask;
    mask.set(id);
    return mask;
}

} // namespace

std::string_view hierarchyErrorMessage(HierarchyError error) noexcept
{
    switch (error)
    {
    case HierarchyError::DeadEntity:
        return "the entity is not alive";
    case HierarchyError::Cycle:
        return "an entity cannot become a child of itself or of one of its descendants";
    }
    return "unknown hierarchy error";
}

Expected<void, HierarchyError> setParent(World& world, Entity child, Entity parent, ParentMode mode,
                                         u32 index)
{
    if (!world.isAlive(child) || (!parent.isNull() && !world.isAlive(parent)))
    {
        return unexpected(HierarchyError::DeadEntity);
    }
    // Walk up from the new parent: reaching the child means a cycle. Bounded in case Hierarchy values that
    // were written directly already form one.
    Entity current = parent;
    for (u32 steps = 0; !current.isNull() && steps <= world.aliveCount(); ++steps)
    {
        if (current == child)
        {
            return unexpected(HierarchyError::Cycle);
        }
        const Hierarchy* hierarchy = world.tryGet<Hierarchy>(current);
        current = hierarchy != nullptr ? liveParent(world, *hierarchy) : Entity{};
    }

    const Transform* oldTransform = world.tryGet<Transform>(child);
    const bool keepWorld = mode == ParentMode::KeepWorld && oldTransform != nullptr;
    const Transform worldBefore = oldTransform != nullptr ? *oldTransform : Transform{};
    const LocalTransform* oldLocal = world.tryGet<LocalTransform>(child);
    Transform local = oldLocal != nullptr ? oldLocal->asTransform() : worldBefore;

    std::vector<Entity> siblings;
    if (!parent.isNull() && !world.has<Hierarchy>(parent))
    {
        collectChildren(world, Entity{}, Entity{}, siblings);
        world.emplace<Hierarchy>(parent, Hierarchy{Entity{}, static_cast<u32>(siblings.size())});
    }
    if (const Hierarchy* old = world.tryGet<Hierarchy>(child))
    {
        collectChildren(world, liveParent(world, *old), child, siblings);
        renumber(world, siblings);
    }
    collectChildren(world, parent, child, siblings);
    const auto position = static_cast<u32>(std::min<usize>(index, siblings.size()));
    siblings.insert(siblings.begin() + position, child);
    world.set(child, Hierarchy{parent, position});
    renumber(world, siblings);

    const Transform parentWorld = worldTransformOf(world, parent);
    if (keepWorld)
    {
        local = relativeTo(worldBefore, parentWorld);
    }
    world.set(child, LocalTransform::from(local));
    if (!keepWorld)
    {
        world.set(child, combine(parentWorld, local));
    }
    world.markChanged<Hierarchy>();
    return {};
}

Expected<void, HierarchyError> detach(World& world, Entity entity, ParentMode mode)
{
    return setParent(world, entity, Entity{}, mode, kLastSibling);
}

Expected<void, HierarchyError> reorder(World& world, Entity entity, u32 index)
{
    if (!world.isAlive(entity))
    {
        return unexpected(HierarchyError::DeadEntity);
    }
    std::vector<Entity> siblings;
    if (!world.has<Hierarchy>(entity))
    {
        collectChildren(world, Entity{}, Entity{}, siblings);
        world.emplace<Hierarchy>(entity, Hierarchy{Entity{}, static_cast<u32>(siblings.size())});
    }
    Hierarchy& hierarchy = world.get<Hierarchy>(entity);
    hierarchy.parent = liveParent(world, hierarchy); // a dead parent becomes null
    const Entity parent = hierarchy.parent;
    collectChildren(world, parent, entity, siblings);
    const usize position = std::min<usize>(index, siblings.size());
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(position), entity);
    renumber(world, siblings);
    world.markChanged<Hierarchy>();
    return {};
}

u32 destroyRecursive(World& world, Entity entity)
{
    if (!world.isAlive(entity))
    {
        return 0;
    }
    // Every parent link, sorted by parent, to find children by binary search.
    std::vector<std::pair<u64, Entity>> links;
    world.query<const Hierarchy>().each([&](Entity child, const Hierarchy& hierarchy) {
        if (!hierarchy.parent.isNull())
        {
            links.emplace_back(hierarchy.parent.toBits(), child);
        }
    });
    std::sort(links.begin(), links.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Entity> doomed{entity};
    for (usize i = 0; i < doomed.size(); ++i)
    {
        const u64 key = doomed[i].toBits();
        auto it = std::lower_bound(links.begin(), links.end(), key,
                                   [](const auto& link, u64 value) { return link.first < value; });
        for (; it != links.end() && it->first == key; ++it)
        {
            // Every entity has one parent, so only a cycle through @p entity could revisit one.
            if (it->second != entity)
            {
                doomed.push_back(it->second);
            }
        }
    }
    const Hierarchy* hierarchy = world.tryGet<Hierarchy>(entity);
    const bool hadHierarchy = hierarchy != nullptr;
    const Entity parent = hadHierarchy ? liveParent(world, *hierarchy) : Entity{};
    for (const Entity doomedEntity : doomed)
    {
        world.destroy(doomedEntity);
    }
    if (hadHierarchy)
    {
        std::vector<Entity> siblings;
        collectChildren(world, parent, Entity{}, siblings);
        renumber(world, siblings);
        world.markChanged<Hierarchy>();
    }
    return static_cast<u32>(doomed.size());
}

void setEnabled(World& world, Entity entity, bool enabled)
{
    if (enabled)
    {
        world.remove<Disabled>(entity);
    }
    else if (!world.has<Disabled>(entity))
    {
        world.emplace<Disabled>(entity);
    }
}

bool isEnabledInHierarchy(const World& world, Entity entity)
{
    Entity current = entity;
    for (u32 steps = 0; !current.isNull() && steps <= world.aliveCount(); ++steps)
    {
        if (world.has<Disabled>(current))
        {
            return false;
        }
        const Hierarchy* hierarchy = world.tryGet<Hierarchy>(current);
        current = hierarchy != nullptr ? liveParent(world, *hierarchy) : Entity{};
    }
    return true;
}

Entity parentOf(const World& world, Entity entity)
{
    const Hierarchy* hierarchy = world.tryGet<Hierarchy>(entity);
    return hierarchy != nullptr ? liveParent(world, *hierarchy) : Entity{};
}

// --- HierarchyIndex ---

bool HierarchyIndex::update(const World& world)
{
    if (isCurrent(world))
    {
        return false;
    }
    rebuild(world);
    return true;
}

bool HierarchyIndex::isCurrent(const World& world) const noexcept
{
    return m_world == &world && m_hierarchyVersion == world.componentVersion<Hierarchy>() &&
           implicitRootsAlive(world);
}

bool HierarchyIndex::implicitRootsAlive(const World& world) const noexcept
{
    return std::all_of(m_implicitRoots.begin(), m_implicitRoots.end(),
                       [&world](Entity root) { return world.isAlive(root); });
}

u32 HierarchyIndex::nodeOf(Entity entity) const noexcept
{
    if (entity.index >= m_nodeOfSlot.size())
    {
        return kNoNode;
    }
    const u32 node = m_nodeOfSlot[entity.index];
    return node != kNoNode && m_nodes[node].entity == entity ? node : kNoNode;
}

std::span<const Entity> HierarchyIndex::children(Entity parent) const noexcept
{
    const u32 node = nodeOf(parent);
    if (node == kNoNode)
    {
        return {};
    }
    return {m_children.data() + m_nodes[node].firstChild, m_nodes[node].childCount};
}

Entity HierarchyIndex::parent(Entity entity) const noexcept
{
    const u32 node = nodeOf(entity);
    return node == kNoNode || m_nodes[node].parent == kNoNode ? Entity{}
                                                              : m_nodes[m_nodes[node].parent].entity;
}

u32 HierarchyIndex::depth(Entity entity) const noexcept
{
    const u32 node = nodeOf(entity);
    return node == kNoNode ? 0u : m_nodes[node].depth;
}

std::span<const Entity> HierarchyIndex::subtree(Entity root) const noexcept
{
    const u32 node = nodeOf(root);
    if (node == kNoNode)
    {
        return {};
    }
    return {m_preOrder.data() + node, m_nodes[node].subtreeEnd - node};
}

std::span<const Entity> HierarchyIndex::level(u32 index) const noexcept
{
    ILL_ASSERT(index < levelCount(), "hierarchy level {} out of range", index);
    return {m_levelEntities.data() + m_levelOffsets[index],
            m_levelOffsets[index + 1] - m_levelOffsets[index]};
}

void HierarchyIndex::rebuild(const World& world)
{
    ILL_PROFILE_SCOPE();
    m_world = &world;
    m_hierarchyVersion = world.componentVersion<Hierarchy>();
    m_activityVersion = ~0ull; // the Inactive pass runs again
    m_records.clear();
    m_implicitRoots.clear();

    const ComponentId hierarchyId = componentId<Hierarchy>();
    u32 maxIndex = 0;
    world.forEachMatching(maskOf(hierarchyId), ComponentMask{}, [&](const RawChunk& chunk) {
        const auto* hierarchies = static_cast<const Hierarchy*>(chunk.column(hierarchyId));
        const std::span<const Entity> entities = chunk.entities();
        for (u32 i = 0; i < chunk.size(); ++i)
        {
            const Entity parent = liveParent(world, hierarchies[i]);
            m_records.push_back(
                Record{.entity = entities[i], .parent = parent, .order = hierarchies[i].order});
            maxIndex = std::max(maxIndex, entities[i].index);
            if (!parent.isNull())
            {
                maxIndex = std::max(maxIndex, parent.index);
            }
        }
    });

    m_recordOfSlot.assign(m_records.empty() ? 0u : maxIndex + 1, kNoNode);
    for (u32 i = 0; i < m_records.size(); ++i)
    {
        m_recordOfSlot[m_records[i].entity.index] = i;
    }
    // Link parents; a live parent without a Hierarchy becomes an extra root record.
    const auto withHierarchy = static_cast<u32>(m_records.size());
    for (u32 i = 0; i < withHierarchy; ++i)
    {
        const Entity parent = m_records[i].parent;
        if (parent.isNull())
        {
            continue;
        }
        if (m_recordOfSlot[parent.index] == kNoNode)
        {
            m_recordOfSlot[parent.index] = static_cast<u32>(m_records.size());
            m_records.push_back(Record{.entity = parent, .order = kNoOrder});
            m_implicitRoots.push_back(parent);
        }
        m_records[i].parentRecord = m_recordOfSlot[parent.index];
    }
    for (Record& record : m_records)
    {
        record.guid = guidOf(world, record.entity);
    }

    // Child lists (CSR) and the root list, each sorted.
    m_rootRecords.clear();
    for (u32 i = 0; i < m_records.size(); ++i)
    {
        const u32 parent = m_records[i].parentRecord;
        if (parent == kNoNode)
        {
            m_rootRecords.push_back(i);
        }
        else
        {
            ++m_records[parent].childCount;
        }
    }
    u32 offset = 0;
    for (Record& record : m_records)
    {
        record.firstChild = offset;
        offset += record.childCount;
        record.childCount = 0;
    }
    m_childRecords.resize(offset);
    for (u32 i = 0; i < m_records.size(); ++i)
    {
        const u32 parent = m_records[i].parentRecord;
        if (parent != kNoNode)
        {
            Record& parentRecord = m_records[parent];
            m_childRecords[parentRecord.firstChild + parentRecord.childCount++] = i;
        }
    }
    const auto recordLess = [this](u32 a, u32 b) {
        const Record& x = m_records[a];
        const Record& y = m_records[b];
        return siblingLess(x.order, x.guid, x.entity, y.order, y.guid, y.entity);
    };
    for (const Record& record : m_records)
    {
        const auto begin = m_childRecords.begin() + record.firstChild;
        std::sort(begin, begin + record.childCount, recordLess);
    }
    std::sort(m_rootRecords.begin(), m_rootRecords.end(), recordLess);

    // Depth-first numbering from the roots.
    m_nodes.clear();
    m_children.clear();
    m_roots.clear();
    for (const u32 root : m_rootRecords)
    {
        m_records[root].claimed = true;
        m_roots.push_back(m_records[root].entity);
        visit(root);
    }
    if (m_nodes.size() != m_records.size())
    {
        // The rest have parents that form cycles: each first unvisited one becomes a root.
        m_scratch.clear();
        m_rootRecords.clear();
        for (u32 i = 0; i < m_records.size(); ++i)
        {
            if (!m_records[i].claimed)
            {
                m_rootRecords.push_back(i);
            }
        }
        ILL_LOG_WARN("ECS", "{} entities have Hierarchy parents that form cycles; they are treated as roots",
                     m_rootRecords.size());
        std::sort(m_rootRecords.begin(), m_rootRecords.end(), recordLess);
        for (const u32 root : m_rootRecords)
        {
            if (!m_records[root].claimed)
            {
                m_records[root].claimed = true;
                m_roots.push_back(m_records[root].entity);
                visit(root);
            }
        }
    }

    // Subtree ends (children follow their parents in pre-order), entity lookup and levels.
    const auto nodeCount = static_cast<u32>(m_nodes.size());
    for (u32 n = nodeCount; n-- > 0;)
    {
        const u32 parent = m_nodes[n].parent;
        if (parent != kNoNode)
        {
            m_nodes[parent].subtreeEnd = std::max(m_nodes[parent].subtreeEnd, m_nodes[n].subtreeEnd);
        }
    }
    m_preOrder.resize(nodeCount);
    m_nodeOfSlot.assign(m_recordOfSlot.size(), kNoNode);
    u32 levels = 0;
    for (u32 n = 0; n < nodeCount; ++n)
    {
        m_preOrder[n] = m_nodes[n].entity;
        m_nodeOfSlot[m_nodes[n].entity.index] = n;
        levels = std::max(levels, m_nodes[n].depth + 1);
    }
    m_levelOffsets.assign(levels + 1, 0u);
    for (const Node& node : m_nodes)
    {
        ++m_levelOffsets[node.depth + 1];
    }
    for (u32 l = 0; l < levels; ++l)
    {
        m_levelOffsets[l + 1] += m_levelOffsets[l];
    }
    m_levelNodes.resize(nodeCount);
    m_levelEntities.resize(nodeCount);
    m_stack.assign(m_levelOffsets.begin(), m_levelOffsets.end()); // next free slot per level
    for (u32 n = 0; n < nodeCount; ++n)
    {
        const u32 slot = m_stack[m_nodes[n].depth]++;
        m_levelNodes[slot] = n;
        m_levelEntities[slot] = m_nodes[n].entity;
    }
}

void HierarchyIndex::visit(u32 root)
{
    m_stack.clear();
    m_stack.push_back(root);
    while (!m_stack.empty())
    {
        const u32 r = m_stack.back();
        m_stack.pop_back();
        Record& record = m_records[r];
        const auto node = static_cast<u32>(m_nodes.size());
        record.node = node;
        Node& added = m_nodes.emplace_back();
        added.entity = record.entity;
        added.parent = record.parentNode;
        added.depth = record.parentNode == kNoNode ? 0u : m_nodes[record.parentNode].depth + 1;
        added.subtreeEnd = node + 1;
        added.firstChild = static_cast<u32>(m_children.size());
        // Claim the unvisited children in order; push them reversed so the first is visited first.
        const auto stackBase = static_cast<u32>(m_stack.size());
        for (u32 c = 0; c < record.childCount; ++c)
        {
            Record& child = m_records[m_childRecords[record.firstChild + c]];
            if (child.claimed)
            {
                continue;
            }
            child.claimed = true;
            child.parentNode = node;
            m_children.push_back(child.entity);
            m_stack.push_back(m_childRecords[record.firstChild + c]);
        }
        m_nodes[node].childCount = static_cast<u32>(m_children.size()) - m_nodes[node].firstChild;
        std::reverse(m_stack.begin() + stackBase, m_stack.end());
    }
}

// --- updateTransformHierarchy ---

namespace
{

// Adds or removes Inactive so that exactly the Disabled entities and their descendants have it.
void updateActivity(World& world, const HierarchyIndex& index, std::vector<Entity>& inactive)
{
    ILL_PROFILE_SCOPE();
    inactive.clear();
    world.forEachMatching(maskOf(componentId<Disabled>()), ComponentMask{}, [&](const RawChunk& chunk) {
        for (const Entity entity : chunk.entities())
        {
            const std::span<const Entity> subtree = index.subtree(entity);
            if (subtree.empty())
            {
                inactive.push_back(entity);
            }
            else
            {
                inactive.insert(inactive.end(), subtree.begin(), subtree.end());
            }
        }
    });
    const auto bitsLess = [](Entity a, Entity b) {
        return a.toBits() < b.toBits();
    };
    std::sort(inactive.begin(), inactive.end(), bitsLess);
    inactive.erase(std::unique(inactive.begin(), inactive.end()), inactive.end());

    CommandBuffer commands;
    bool changed = false;
    world.forEachMatching(maskOf(componentId<Inactive>()), ComponentMask{}, [&](const RawChunk& chunk) {
        for (const Entity entity : chunk.entities())
        {
            if (!std::binary_search(inactive.begin(), inactive.end(), entity, bitsLess))
            {
                commands.remove<Inactive>(entity);
                changed = true;
            }
        }
    });
    for (const Entity entity : inactive)
    {
        if (!world.has<Inactive>(entity))
        {
            commands.set(entity, Inactive{});
            changed = true;
        }
    }
    if (changed)
    {
        commands.apply(world);
    }
}

[[nodiscard]] u64 activityVersion(const World& world) noexcept
{
    return world.componentVersion<Hierarchy>() + world.componentVersion<Disabled>() +
           world.componentVersion<Inactive>();
}

} // namespace

void updateTransformHierarchy(World& world, HierarchyIndex& index, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    index.update(world);
    if (index.m_activityVersion != activityVersion(world))
    {
        updateActivity(world, index, index.m_scratch);
        index.m_activityVersion = activityVersion(world);
    }
    const u32 levels = index.levelCount();
    if (levels == 0)
    {
        return;
    }

    // Roots: Transform is the truth.
    for (const Entity root : index.level(0))
    {
        LocalTransform* local = world.tryGet<LocalTransform>(root);
        const Transform* transform = local != nullptr ? world.tryGet<Transform>(root) : nullptr;
        if (transform != nullptr)
        {
            *local = LocalTransform::from(*transform);
        }
    }

    const std::vector<HierarchyIndex::Node>& nodes = index.m_nodes;
    std::atomic<bool> missingTransform{false};
    // Derives the Transform of node @p n from its parent's (or its LocalTransform from its Transform).
    const auto propagate = [&](u32 n, bool addMissing) {
        const HierarchyIndex::Node& node = nodes[n];
        LocalTransform* local = world.tryGet<LocalTransform>(node.entity);
        if (local == nullptr)
        {
            return; // placed in world space
        }
        const Transform* parentTransform = world.tryGet<Transform>(nodes[node.parent].entity);
        const Transform parentWorld = parentTransform != nullptr ? *parentTransform : Transform{};
        Transform* transform = world.tryGet<Transform>(node.entity);
        if (transform == nullptr)
        {
            if (addMissing)
            {
                world.emplace<Transform>(node.entity, combine(parentWorld, local->asTransform()));
            }
            else
            {
                missingTransform.store(true, std::memory_order_relaxed);
            }
            return;
        }
        if (world.has<WorldSpaceAuthority>(node.entity))
        {
            *local = LocalTransform::from(relativeTo(*transform, parentWorld));
        }
        else
        {
            *transform = combine(parentWorld, local->asTransform());
        }
    };

    for (u32 l = 1; l < levels; ++l)
    {
        const u32 begin = index.m_levelOffsets[l];
        const u32 count = index.m_levelOffsets[l + 1] - begin;
        const u32* levelNodes = index.m_levelNodes.data() + begin;
        if (jobs != nullptr && count >= kParallelLevelSize)
        {
            jobs->parallelFor(count, kParallelBatch, [&](u32 first, u32 last) {
                for (u32 i = first; i < last; ++i)
                {
                    propagate(levelNodes[i], false);
                }
            });
        }
        else
        {
            for (u32 i = 0; i < count; ++i)
            {
                propagate(levelNodes[i], false);
            }
        }
    }

    if (missingTransform.load(std::memory_order_relaxed))
    {
        // Rare: add the missing Transforms and derive every level again in order.
        for (u32 l = 1; l < levels; ++l)
        {
            for (u32 i = index.m_levelOffsets[l]; i < index.m_levelOffsets[l + 1]; ++i)
            {
                propagate(index.m_levelNodes[i], true);
            }
        }
    }
}

} // namespace illusion

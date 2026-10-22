#pragma once

/// @file
/// Transform hierarchy: parents and children, local placement, enabling and disabling subtrees
/// (docs/adr/0034-transform-hierarchy.md).
///
/// An Entity with a Hierarchy Component has a parent (or is a root) and a position among its siblings. Its
/// LocalTransform places it relative to its parent, and updateTransformHierarchy() derives the world-space
/// Transform from it every frame, so every System keeps reading Transform. Disabled switches an Entity and
/// all its descendants off: updateTransformHierarchy() gives each of them the Inactive tag, which the
/// renderer, physics and audio skip.
///
/// @code
/// illusion::World world;
/// const illusion::Entity car = world.create(illusion::Transform{});
/// const illusion::Entity wheel = world.create(illusion::Transform{});
/// (void)illusion::setParent(world, wheel, car); // keeps the wheel where it is in the world
///
/// illusion::HierarchyIndex index;
/// world.get<illusion::Transform>(car).position.x += 1.0f;
/// illusion::updateTransformHierarchy(world, index); // the wheel follows the car
///
/// illusion::setEnabled(world, car, false); // car and wheel become Inactive at the next update
/// @endcode
///
/// Write Hierarchy Components through the functions of this header. Code that writes Hierarchy values
/// directly (a tool, a loader) must call `world.markChanged<Hierarchy>()` afterwards so that
/// HierarchyIndex notices.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Transform.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace illusion
{

class JobSystem;
class World;

/// Place of an Entity in the transform hierarchy (reflected as "illusion.Hierarchy"). A root has a null
/// parent. Siblings are ordered by @p order, which the functions of this header keep dense (0, 1, 2, ...).
/// An Entity whose parent is dead counts as a root.
struct Hierarchy
{
    Entity parent; ///< Parent, or the null entity for a root.
    u32 order = 0; ///< Position among the parent's children (or among the roots).
};

/// Placement of an Entity relative to its parent (reflected as "illusion.LocalTransform"). For an Entity
/// with a parent, updateTransformHierarchy() derives Transform from it; for a root it is kept equal to
/// Transform.
struct LocalTransform
{
    Vec3 position{0.0f};                   ///< Position in the parent's space.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Rotation relative to the parent.
    Vec3 scale{1.0f};                      ///< Per-axis scale relative to the parent.

    /// The same placement as a Transform.
    [[nodiscard]] Transform asTransform() const noexcept { return Transform{position, rotation, scale}; }

    /// @p transform as a LocalTransform.
    [[nodiscard]] static LocalTransform from(const Transform& transform) noexcept
    {
        return LocalTransform{transform.position, transform.rotation, transform.scale};
    }
};

/// Tag that switches an Entity and its descendants off (reflected as "illusion.Disabled", saved in scenes).
/// Use setEnabled() to change it.
struct Disabled
{
};

/// Tag of an Entity that is Disabled or has a Disabled ancestor (runtime state "illusion.Inactive", never
/// saved). updateTransformHierarchy() adds and removes it; engine Systems skip Inactive entities.
struct Inactive
{
};

/// Tag of an Entity whose Transform a System writes in world space, such as a dynamic physics body
/// (runtime state "illusion.WorldSpaceAuthority"). updateTransformHierarchy() does not overwrite its
/// Transform; it writes its LocalTransform from it instead, so that the Entity's children follow it.
struct WorldSpaceAuthority
{
};

/// What setParent() keeps when an Entity moves to another parent.
enum class ParentMode : u8
{
    KeepWorld, ///< The Entity stays where it is in the world; its LocalTransform changes.
    KeepLocal, ///< The Entity keeps its LocalTransform and moves with its new parent.
};

/// Why a hierarchy change was rejected.
enum class HierarchyError : u8
{
    DeadEntity, ///< An Entity is not alive.
    Cycle,      ///< The new parent is the Entity itself or one of its descendants.
};

/// English description of @p error, for logs and editor commands.
[[nodiscard]] std::string_view hierarchyErrorMessage(HierarchyError error) noexcept;

/// Index argument of setParent() and reorder() that means "after the last sibling".
inline constexpr u32 kLastSibling = ~0u;

/// Makes @p child a child of @p parent (the null entity makes it a root), at position @p index among its
/// new siblings (clamped; kLastSibling appends). @p child gets a Hierarchy, a LocalTransform and a
/// Transform if it lacks them, a @p parent without a Hierarchy becomes the last root, and the orders of the
/// old and new siblings stay dense. With ParentMode::KeepWorld the child's Transform stays and its
/// LocalTransform is computed (within the shear limit of combine()); with KeepLocal its LocalTransform stays
/// and its Transform is computed. The child's descendants follow at the next updateTransformHierarchy().
/// Rejects dead entities and cycles, changing nothing. Takes time linear in the number of entities with a
/// Hierarchy; a structural change, so not allowed while a query iterates.
Expected<void, HierarchyError> setParent(World& world, Entity child, Entity parent,
                                         ParentMode mode = ParentMode::KeepWorld, u32 index = kLastSibling);

/// Makes @p entity a root: setParent() with the null parent, appended after the last root.
Expected<void, HierarchyError> detach(World& world, Entity entity, ParentMode mode = ParentMode::KeepWorld);

/// Moves @p entity to position @p index among its siblings (clamped; kLastSibling moves it last), keeping
/// its parent. An Entity without a Hierarchy gets one as a root. Rejects a dead entity.
Expected<void, HierarchyError> reorder(World& world, Entity entity, u32 index);

/// Destroys @p entity and all its descendants, keeps the orders of its siblings dense, and returns how many
/// entities were destroyed (0 if @p entity is dead).
u32 destroyRecursive(World& world, Entity entity);

/// Enables or disables @p entity by removing or adding its Disabled tag. Inactive tags follow at the next
/// updateTransformHierarchy().
void setEnabled(World& world, Entity entity, bool enabled);

/// True if neither @p entity nor any of its ancestors is Disabled: what the Inactive tag will say after the
/// next updateTransformHierarchy(). Walks the parent chain.
[[nodiscard]] bool isEnabledInHierarchy(const World& world, Entity entity);

/// Returns the parent of @p entity, or the null entity for a root (also when the parent is dead).
[[nodiscard]] Entity parentOf(const World& world, Entity entity);

/// Child lists and depth order of a World's hierarchy, built from its Hierarchy Components. update()
/// rebuilds the index when it is out of date: when an entity gained or lost a Hierarchy, Hierarchy values
/// were announced with World::markChanged(), or a parent that has no Hierarchy died. Otherwise update()
/// costs O(1) and the index allocates nothing.
///
/// The index holds every Entity with a Hierarchy plus every live parent without one. Children are ordered
/// by Hierarchy::order, then by EntityGuid, then by handle index; roots likewise, with roots that have no
/// Hierarchy last. Entities whose parents form a cycle (possible only by writing Hierarchy directly) are
/// treated as roots, starting with the first of them in that order.
class HierarchyIndex : NonCopyable
{
public:
    HierarchyIndex() = default;

    /// Brings the index up to date with @p world and returns true if it was rebuilt. Using the index with
    /// another World rebuilds it.
    bool update(const World& world);

    /// True if the index describes the current hierarchy of @p world.
    [[nodiscard]] bool isCurrent(const World& world) const noexcept;

    /// Number of indexed entities.
    [[nodiscard]] u32 size() const noexcept { return static_cast<u32>(m_preOrder.size()); }

    /// True if @p entity is indexed (it has a Hierarchy or children).
    [[nodiscard]] bool contains(Entity entity) const noexcept { return nodeOf(entity) != kNoNode; }

    /// Roots in order.
    [[nodiscard]] std::span<const Entity> roots() const noexcept { return m_roots; }

    /// Children of @p parent in order (empty for an entity that is not indexed or has none).
    [[nodiscard]] std::span<const Entity> children(Entity parent) const noexcept;

    /// Parent of @p entity in the index: the null entity for roots and entities that are not indexed.
    [[nodiscard]] Entity parent(Entity entity) const noexcept;

    /// Depth of @p entity: 0 for roots and entities that are not indexed.
    [[nodiscard]] u32 depth(Entity entity) const noexcept;

    /// Every indexed entity in depth-first pre-order (parents before children, siblings in order).
    [[nodiscard]] std::span<const Entity> preOrder() const noexcept { return m_preOrder; }

    /// @p root followed by all its descendants in pre-order: a contiguous part of preOrder(). Empty if @p
    /// root is not indexed.
    [[nodiscard]] std::span<const Entity> subtree(Entity root) const noexcept;

    /// Number of depth levels (0 for an empty index).
    [[nodiscard]] u32 levelCount() const noexcept
    {
        return m_levelOffsets.empty() ? 0u : static_cast<u32>(m_levelOffsets.size() - 1);
    }

    /// Entities of depth @p index, in pre-order. Every parent is in an earlier level than its children.
    [[nodiscard]] std::span<const Entity> level(u32 index) const noexcept;

    /// Calls `fn(Entity child)` for every child of @p parent in order.
    template <class F>
    void forEachChild(Entity parent, F&& fn) const
    {
        for (const Entity child : children(parent))
        {
            fn(child);
        }
    }

    /// Calls `fn(Entity entity, u32 depth)` for @p root and its descendants in pre-order; depth is relative
    /// to @p root (0 for @p root).
    template <class F>
    void depthFirst(Entity root, F&& fn) const
    {
        const std::span<const Entity> entities = subtree(root);
        if (entities.empty())
        {
            return;
        }
        const u32 base = depth(root);
        for (const Entity entity : entities)
        {
            fn(entity, depth(entity) - base);
        }
    }

private:
    friend void updateTransformHierarchy(World& world, HierarchyIndex& index, JobSystem* jobs);

    static constexpr u32 kNoNode = ~0u;

    struct Node
    {
        Entity entity;
        u32 parent = kNoNode; // node id
        u32 depth = 0;
        u32 subtreeEnd = 0; // one past the last descendant's node id
        u32 firstChild = 0; // offset into m_children
        u32 childCount = 0;
    };

    // One entity while building: its sort key and links.
    struct Record
    {
        Entity entity{};
        Entity parent{};
        u32 order = 0;
        u64 guid = 0;
        u32 parentRecord = kNoNode;
        u32 firstChild = 0; // offset into m_childRecords
        u32 childCount = 0;
        u32 node = kNoNode;       // node id once visited
        u32 parentNode = kNoNode; // node id of the parent that claimed it
        bool claimed = false;
    };

    [[nodiscard]] u32 nodeOf(Entity entity) const noexcept;
    void rebuild(const World& world);
    void visit(u32 root);
    [[nodiscard]] bool implicitRootsAlive(const World& world) const noexcept;

    const World* m_world = nullptr;
    u64 m_hierarchyVersion = ~0ull;
    std::vector<Node> m_nodes;      // in pre-order
    std::vector<Entity> m_preOrder; // entity of each node
    std::vector<Entity> m_children; // CSR child lists
    std::vector<Entity> m_roots;
    std::vector<Entity> m_implicitRoots; // indexed parents without a Hierarchy
    std::vector<u32> m_nodeOfSlot;       // entity index -> node id
    std::vector<Entity> m_levelEntities;
    std::vector<u32> m_levelNodes;
    std::vector<u32> m_levelOffsets;
    // Scratch of rebuild() and of the Inactive pass, kept to reuse its capacity.
    std::vector<Record> m_records;
    std::vector<u32> m_recordOfSlot;
    std::vector<u32> m_childRecords;
    std::vector<u32> m_rootRecords;
    std::vector<u32> m_stack;
    std::vector<Entity> m_scratch;
    u64 m_activityVersion = ~0ull; // sum of the Hierarchy, Disabled and Inactive versions at the last pass
};

/// Brings the hierarchy of @p world up to date; call it once per frame after game code and physics moved
/// entities and before rendering:
/// - Updates @p index (see HierarchyIndex::update()).
/// - Adds or removes the Inactive tag where Disabled says so (only after a change to Hierarchy, Disabled or
///   Inactive Components).
/// - Derives the Transform of every Entity with a parent and a LocalTransform, level by level from the roots
///   (on @p jobs in parallel for large levels when @p jobs is not null), adding a missing Transform. A parent
///   without a Transform counts as the identity.
/// - For roots and WorldSpaceAuthority entities, writes LocalTransform from Transform instead.
///
/// Entities without a Hierarchy cost nothing. A structural change when tags or Transforms are added, so not
/// allowed while a query iterates.
void updateTransformHierarchy(World& world, HierarchyIndex& index, JobSystem* jobs = nullptr);

} // namespace illusion

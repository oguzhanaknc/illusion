#pragma once

/// @file
/// Finds the entities of the edited World by id, hierarchy path or name, and resolves the entity
/// references of editor commands.

#include <illusion/core/Expected.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/editor/core/CommandError.hpp>

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace illusion
{
class World;
} // namespace illusion

namespace illusion::editor
{

class Selection;

/// How commands return an entity: never as raw Entity bits.
struct EntitySummary
{
    std::string id;   ///< The EntityGuid as 16 hex digits.
    std::string name; ///< The Name ("" if it has none).
    std::string path; ///< Hierarchy path of names, e.g. "/Game/Board" (see EntityIndex::pathOf()).
};

/// Maps entity ids to the entities of one World, and builds and resolves references to them.
///
/// Entity references are strings in one of these forms:
///
/// | Form | Example |
/// | --- | --- |
/// | 16 hex digits: an id | `9f2c41aa0b7d3e15` |
/// | A hierarchy path of names | `/Game/Board/Cell 3` |
/// | `name:` and a name that only one entity has | `name:Board` |
/// | The selection, when exactly one entity is selected | `$selection` |
///
/// A path segment of an entity without a Name is `#` and its id. The index follows changes made through
/// WorldEditor at once, and rebuilds itself when it finds an id it does not know or an entity that has
/// changed (structural changes made outside the editor).
class EntityIndex : NonCopyable
{
public:
    /// Indexes @p world, which must outlive the index.
    explicit EntityIndex(World& world);

    /// The entity with @p guid, or the null entity.
    [[nodiscard]] Entity find(EntityGuid guid);

    /// The entity with @p guid if the index knows it, without rebuilding (WorldEditor keeps the index
    /// current), or the null entity.
    [[nodiscard]] Entity lookup(EntityGuid guid) const noexcept;

    /// The id of @p entity (null if it has no EntityGuid or is dead).
    [[nodiscard]] EntityGuid guidOf(Entity entity) const noexcept;

    /// Records that @p entity now has @p guid (WorldEditor calls it).
    void insert(EntityGuid guid, Entity entity);

    /// Forgets @p guid (WorldEditor calls it before destroying the entity).
    void erase(EntityGuid guid);

    /// Re-reads every EntityGuid of the World.
    void rebuild();

    /// The World's hierarchy, brought up to date first.
    [[nodiscard]] const HierarchyIndex& hierarchy();

    /// Derives the world-space Transform of every child with a LocalTransform from its parent's Transform, as
    /// illusion::updateTransformHierarchy() does for children; with @p addMissing, children without a
    /// Transform get one (a scene was loaded: saved scenes leave out derived Transforms). Saved values (root
    /// Transforms, LocalTransforms) are never written, so undo and the crash journal stay exact. Costs
    /// nothing when no Transform, LocalTransform or Hierarchy changed since the last call. EditorContext
    /// calls it after every change, so that world placements are current when the next command reads them.
    void updateTransforms(bool addMissing = false);

    /// Appends the children of @p parent to @p out in order; for the null entity, the roots: entities with
    /// a Hierarchy and no parent in order, then entities without a Hierarchy by id.
    void children(Entity parent, std::vector<Entity>& out);

    /// The hierarchy path of @p entity, e.g. "/Game/Board".
    [[nodiscard]] std::string pathOf(Entity entity);

    /// Id, name and path of @p entity.
    [[nodiscard]] EntitySummary summary(Entity entity);

    /// Resolves one entity reference (see the class description). @p pointer is the JSON Pointer of the
    /// reference in the command's params, used in errors (NotFound, Ambiguous or ValidationFailed).
    [[nodiscard]] Expected<Entity, CommandError>
    resolve(std::string_view reference, const Selection& selection, std::string_view pointer = "");

    /// Resolves a list of references into @p out. `$selection` stands for every selected entity here;
    /// duplicates are dropped and the order of first appearance is kept.
    [[nodiscard]] Expected<void, CommandError> resolveAll(std::span<const std::string> references,
                                                          const Selection& selection,
                                                          std::string_view pointer, std::vector<Entity>& out);

    /// The World.
    [[nodiscard]] World& world() noexcept { return *m_world; }

private:
    [[nodiscard]] Expected<Entity, CommandError> resolvePath(std::string_view path, std::string_view pointer);
    [[nodiscard]] Expected<Entity, CommandError> resolveName(std::string_view name, std::string_view pointer);
    [[nodiscard]] std::string segmentOf(Entity entity) const;

    World* m_world;
    std::unordered_map<u64, Entity> m_entities;
    HierarchyIndex m_hierarchy;
    std::vector<Entity> m_scratch;
    u64 m_placementVersions[3] = {~u64{0}, ~u64{0}, ~u64{0}}; // Transform, LocalTransform, Hierarchy
};

} // namespace illusion::editor

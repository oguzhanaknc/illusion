#pragma once

/// @file
/// Prefabs: groups of entities saved as `.prefab` assets and placed in scenes as instances
/// (docs/adr/0046-prefabs-as-instance-records.md).
///
/// A prefab file has the scene layout of SceneSerializer.hpp with `"format": "illusion.prefab"` and exactly
/// one root. A scene stores an instance as a record, `{"prefab": path, "id": instance, "overrides": {...}}`;
/// loadScene() expands it (SceneLoadOptions::prefabs reads the prefab files) and saveScene() writes it back
/// with the overrides it finds by comparing the instance with a fresh expansion of its prefab. The entities
/// of an instance carry PrefabMember, and its root PrefabInstance.
///
/// @code
/// illusion::PrefabData crate{"assets:/props/crate.prefab", files.readText("assets:/props/crate.prefab")};
/// const illusion::PrefabInstantiateResult placed =
///     illusion::instantiatePrefab(world, crate, illusion::Entity{}, guids.next());
/// if (placed.ok())
/// {
///     world.get<illusion::LocalTransform>(placed.root).position = {2.0f, 0.0f, 0.0f};
/// }
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/EntityGuid.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class World;

/// Deepest nesting of prefab instances that loadScene() expands; deeper records are kept unresolved.
inline constexpr u32 kMaxPrefabDepth = 16;

/// The root of a prefab instance (runtime state "illusion.PrefabInstance", written as the instance's record
/// by saveScene()). The root of a nested instance keeps its own; where the roots of nested instances are
/// the same entity (a variant), it names the outermost.
struct PrefabInstance
{
    AssetRef prefab; ///< The prefab asset.
    EntityGuid
        instance; ///< Id of the instance (the record's "id"), which the ids of its entities derive from.
};

/// An entity created by a prefab instance (runtime state "illusion.PrefabMember"). Of nested instances, it
/// names the outermost one.
struct PrefabMember
{
    EntityGuid instance; ///< Id of the instance (PrefabInstance::instance of its root).
    EntityGuid local;    ///< Id of the entity inside the prefab file: the key of its overrides.
};

/// An instance whose prefab could not be loaded (runtime state "illusion.UnresolvedPrefab"): one entity, with
/// the instance id as its EntityGuid, that saveScene() writes back as the same record.
struct UnresolvedPrefab
{
    std::string prefab;    ///< Path of the prefab asset.
    std::string overrides; ///< The record's "overrides" as compact JSON (empty if it had none).
};

/// The EntityGuid of the entity with id @p local in a prefab file, in the instance @p instance: a 64-bit mix
/// of both, never zero. Instantiating the same prefab with the same instance id always gives the same ids.
[[nodiscard]] EntityGuid derivePrefabGuid(EntityGuid instance, EntityGuid local) noexcept;

/// Reads the prefab file at a virtual path (for example through VirtualFileSystem::readText()); nullopt if
/// it cannot be read.
using PrefabSource = std::function<std::optional<std::string>(std::string_view path)>;

/// A prefab file in memory.
struct PrefabData
{
    std::string path; ///< Its virtual path, recorded in the instance (for example "assets:/crate.prefab").
    std::string json; ///< Its contents.
};

/// Result of instantiatePrefab().
struct PrefabInstantiateResult
{
    Entity root;                       ///< The instance's root (null on failure).
    std::vector<Entity> entities;      ///< Every entity created, root first (empty on failure).
    std::vector<std::string> warnings; ///< As SceneLoadResult::warnings.
    std::string error;                 ///< Why the instance could not be created (empty on success).

    /// True if the instance was created.
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Creates an instance of @p prefab in @p world with the id @p instance, as a child of @p parent (appended
/// after its last child) or a root if @p parent is null. Nested instances read their prefabs through
/// @p nested. Fails, adding nothing, if the prefab cannot be loaded or an entity id it derives already
/// exists in the world.
[[nodiscard]] PrefabInstantiateResult instantiatePrefab(World& world, const PrefabData& prefab, Entity parent,
                                                        EntityGuid instance, const PrefabSource& nested = {});

namespace detail
{

/// A scene file with one record: an instance of @p prefab with the id @p instance and no overrides.
[[nodiscard]] std::string instanceRecordScene(std::string_view prefab, EntityGuid instance);

} // namespace detail

} // namespace illusion

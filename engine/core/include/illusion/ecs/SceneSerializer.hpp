#pragma once

/// @file
/// Saving and loading entities as JSON scene files.
///
/// Scene files store reflected components (see Reflection.hpp) by stable type and field name, so they
/// survive code changes: fields missing from a file keep their default values, and unknown component
/// types and fields are skipped with a warning. Components that are not reflected, or are runtime state
/// (TypeMeta::transient), are not saved. Values use the JSON forms of ComponentJson.hpp, except that U64
/// values are written as numbers in version 1 files; older type versions are migrated on load.
///
/// @code
/// const illusion::SceneSaveResult saved = illusion::saveScene(world);
/// files.writeText("game:/scenes/level1.scene", saved.json);
///
/// const illusion::SceneLoadResult loaded = illusion::loadScene(otherWorld, saved.json);
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class World;

/// Version of the scene file format written by saveScene().
inline constexpr u32 kSceneFormatVersion = 1;

/// Result of saveScene().
struct SceneSaveResult
{
    std::string json;          ///< The scene file; empty on failure.
    std::string error;         ///< Why saving failed (empty on success).
    u32 entities = 0;          ///< Number of saved entities.
    u32 skippedComponents = 0; ///< Components not saved: their type is not reflected or is runtime state.

    /// True if the scene was saved.
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Result of loadScene().
struct SceneLoadResult
{
    std::vector<Entity> entities;      ///< Created entities, in file order (empty on failure).
    std::string error;                 ///< Why loading failed (empty on success).
    std::vector<std::string> warnings; ///< Skipped unknown component types and fields.

    /// True if the scene was loaded.
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Saves every live entity of @p world, ordered by entity index. Fails if a floating-point field is
/// not finite (JSON cannot store it).
[[nodiscard]] SceneSaveResult saveScene(const World& world);

/// Saves @p entities (which must be alive) in the given order. Entity fields that refer to entities
/// outside the list are saved as null.
[[nodiscard]] SceneSaveResult saveScene(const World& world, std::span<const Entity> entities);

/// Creates the entities of the scene @p json in @p world, which keeps its existing entities. Entity
/// fields are remapped to the new entities. On failure, nothing is added to the world.
[[nodiscard]] SceneLoadResult loadScene(World& world, std::string_view json);

} // namespace illusion

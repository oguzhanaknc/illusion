#pragma once

/// @file
/// Saving and loading entities as JSON scene files.
///
/// Scene files store reflected components (see Reflection.hpp) by stable type and field name, so they
/// survive code changes: fields missing from a file keep their default values, unknown fields are skipped
/// with a warning, and Components of unknown types are preserved (UnknownComponents) and saved again
/// unchanged. Components that are not reflected, or are runtime state (TypeMeta::transient), are not saved.
/// Every saved entity needs an EntityGuid (see assignMissingGuids()), its "id" in the file; Entity fields
/// are written as ids. Values use the JSON forms of ComponentJson.hpp. Version 1 files, which refer to
/// entities by index, still load (docs/adr/0033-entity-ids-names-scene-format-v2.md).
///
/// @code
/// illusion::assignMissingGuids(world);
/// const illusion::SceneSaveResult saved = illusion::saveScene(world);
/// files.writeText("game:/scenes/level1.scene", saved.json);
///
/// const illusion::SceneLoadResult loaded = illusion::loadScene(otherWorld, saved.json);
/// @endcode

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class World;

/// Version of the scene file format written by saveScene(). loadScene() also reads version 1.
inline constexpr u32 kSceneFormatVersion = 2;

/// A value of a type this program does not know, kept as JSON so that it can be saved again unchanged.
struct PreservedValue
{
    std::string type;           ///< Stable name of the type.
    std::optional<u32> version; ///< Version from the file's "types" table (nullopt if it lists none).
    JsonText value;             ///< The value as compact JSON text.

    /// Compares every member.
    friend bool operator==(const PreservedValue&, const PreservedValue&) = default;
};

/// Components of an entity whose types were not registered when its scene was loaded (reflected as the
/// runtime state "illusion.UnknownComponents"). saveScene() writes them back unchanged; no engine System
/// queries them. Entity references inside them are not remapped (their types are unknown).
struct UnknownComponents
{
    std::vector<PreservedValue> components; ///< In file order.
};

/// A reflected struct saved in a scene's "settings" (for example the scene's render settings).
struct SceneSettings
{
    const StructReflection* type = nullptr; ///< Its reflection.
    const void* value = nullptr;            ///< The value, of the reflected C++ type.
};

/// A reflected struct that loadScene() reads from the scene's "settings".
struct SceneSettingsTarget
{
    const StructReflection* type = nullptr; ///< Its reflection.
    void* value = nullptr;                  ///< Where to read it, a value of the reflected C++ type.
};

/// Returns the settings entry for @p value, whose type must be reflected with reflectStruct().
template <class T>
[[nodiscard]] SceneSettings
sceneSettings(const T& value, const StructRegistry& structs = ComponentRegistry::instance().structs())
{
    const StructReflection* type = structs.reflectionOf(typeHash<T>());
    ILL_ASSERT(type != nullptr, "sceneSettings() of {}, which is not reflected", typeName<T>());
    return SceneSettings{type, &value};
}

/// Returns the settings target for @p value, whose type must be reflected with reflectStruct().
template <class T>
[[nodiscard]] SceneSettingsTarget
sceneSettingsTarget(T& value, const StructRegistry& structs = ComponentRegistry::instance().structs())
{
    const StructReflection* type = structs.reflectionOf(typeHash<T>());
    ILL_ASSERT(type != nullptr, "sceneSettingsTarget() of {}, which is not reflected", typeName<T>());
    return SceneSettingsTarget{type, &value};
}

/// Order of the entities in a saved scene.
enum class SceneOrder : u8
{
    /// Depth-first pre-order of the transform hierarchy (Hierarchy.hpp): every parent before its children,
    /// siblings by Hierarchy::order. Roots (including entities whose parent is not saved) come by order,
    /// roots without a Hierarchy last, ties by EntityGuid. The same scene always gives the same file.
    Hierarchy,
    ById, ///< By EntityGuid value: the same scene always gives the same file.
    /// In the order of SceneSaveOptions::subset, or without one in the order of World::entities() (by
    /// handle index): for tools that match saved entities to the World by position.
    AsGiven,
};

/// Options of saveScene().
struct SceneSaveOptions
{
    /// Entities to save (they must be alive). nullopt saves every live entity. Entity fields that refer to
    /// entities outside the saved ones are written as null.
    std::optional<std::span<const Entity>> subset{};
    SceneOrder order = SceneOrder::Hierarchy;  ///< Order of the entities.
    std::span<const SceneSettings> settings{}; ///< Settings to save.
    /// Settings of unknown types to save again (SceneLoadResult::preservedSettings).
    std::span<const PreservedValue> preservedSettings{};
};

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

/// What loadScene() does with the ids in a file.
enum class IdPolicy : u8
{
    /// Entities get the ids of the file (to open a scene). An id that an entity of the World already has
    /// is a load error. Version 1 files have no ids: their entities get new ones.
    Keep,
    /// Entities get new ids (to paste or instance a scene several times); SceneLoadResult::fileIds maps the
    /// file's ids to the new entities.
    Regenerate,
};

/// Options of loadScene().
struct SceneLoadOptions
{
    IdPolicy ids = IdPolicy::Keep; ///< Ids of the loaded entities.
    /// Settings to read. Settings in the file that no target asks for are returned in
    /// SceneLoadResult::preservedSettings. Targets change only if the whole scene loads.
    std::span<const SceneSettingsTarget> settings{};
    GuidGenerator* guids = nullptr; ///< Makes new ids; nullptr uses GuidGenerator::shared().
};

/// Result of loadScene().
struct SceneLoadResult
{
    std::vector<Entity> entities; ///< Created entities, in file order (empty on failure).
    /// The id of each entity in the file, in the same order as entities. Empty for version 1 files.
    std::vector<EntityGuid> fileIds;
    /// Settings that no SceneLoadOptions::settings target read, to pass to
    /// SceneSaveOptions::preservedSettings.
    std::vector<PreservedValue> preservedSettings;
    std::string error;                 ///< Why loading failed (empty on success).
    std::vector<std::string> warnings; ///< Skipped fields and types, preserved types, version changes.
    u32 version = 0;                   ///< Format version of the file (0 if it could not be read).

    /// True if the scene was loaded.
    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

/// Saves the entities of @p world selected by @p options. The Transform of an entity with a LocalTransform
/// whose parent is saved too is not written: updateTransformHierarchy() derives it after loading. Fails if a
/// saved entity has no EntityGuid or shares one with another saved entity, or if a floating-point field is
/// not finite (JSON cannot store it).
[[nodiscard]] SceneSaveResult saveScene(const World& world, const SceneSaveOptions& options = {});

/// Creates the entities of the scene @p json in @p world, which keeps its existing entities. Entity
/// fields are remapped to the new entities. On failure, nothing is added to the world and no settings
/// target changes.
[[nodiscard]] SceneLoadResult loadScene(World& world, std::string_view json,
                                        const SceneLoadOptions& options = {});

} // namespace illusion

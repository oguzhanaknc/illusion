#pragma once

/// @file
/// Asset references in scenes, materials and project settings files: which files refer to an asset, what a
/// file depends on, and rewriting references when an asset moves. References are found through reflection
/// (FieldType::Asset fields), so Components and settings whose types are not registered are not searched.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class VirtualFileSystem;
class World;
} // namespace illusion

namespace illusion::editor
{

class EntityIndex;

/// One asset reference: where it is and what it points to.
struct AssetReference
{
    std::string asset; ///< The reference as stored, e.g. "assets:/models/ship.glb#mesh/0".
    std::string
        source; ///< Scene or material file that holds it (for the open scene: its path, "" if untitled).
    std::string entity; ///< Id of the entity that holds it (16 hex digits); "" in materials and settings.
    std::string owner;  ///< Component or settings type, e.g. "illusion.MeshRenderer" ("" in materials and the
                        ///< manifest).
    std::string field;  ///< JSON Pointer of the field in the owner's value (or the material), e.g. "/mesh".
};

/// The mount whose scenes and materials hold asset references.
inline constexpr std::string_view kAssetsRoot = "assets:/";

/// The mount whose `.json` files hold project settings.
inline constexpr std::string_view kSettingsRoot = "settings:/";

/// File extension of scene files.
inline constexpr std::string_view kSceneExtension = ".scene";

/// The extension of @p path in lower case, with its dot (".scene"), or "" if it has none.
[[nodiscard]] std::string lowerExtension(std::string_view path);

/// True if @p reference (an asset path, possibly with a `#` fragment) refers to @p path: the asset itself, a
/// sub-asset of it, or for a folder anything inside it. Paths compare as written (they are normalized).
[[nodiscard]] bool refersTo(std::string_view reference, std::string_view path) noexcept;

/// @p reference with its prefix @p from replaced by @p to; @p reference must refer to @p from (refersTo()).
[[nodiscard]] std::string movedReference(std::string_view reference, std::string_view from,
                                         std::string_view to);

namespace detail
{

/// forEachAssetRef() for one value of @p type at @p data, whose JSON Pointer is @p pointer.
template <class F>
void forEachAssetRefIn(const ValueType& type, void* data, std::string& pointer, F& fn)
{
    const usize length = pointer.size();
    switch (type.type)
    {
    case FieldType::Asset:
        fn(*static_cast<AssetRef*>(data), std::string_view(pointer));
        break;
    case FieldType::Struct:
        for (const FieldInfo& field : type.structType->fields)
        {
            pointer += '/';
            pointer += field.name;
            forEachAssetRefIn(field, fieldData(data, field), pointer, fn);
            pointer.resize(length);
        }
        break;
    case FieldType::FixedArray:
        for (u32 i = 0; i < type.count; ++i)
        {
            pointer += '/';
            pointer += std::to_string(i);
            forEachAssetRefIn(*type.element, static_cast<std::byte*>(data) + usize{i} * type.element->size,
                              pointer, fn);
            pointer.resize(length);
        }
        break;
    case FieldType::Array:
        for (usize i = 0, n = type.container->size(data); i < n; ++i)
        {
            pointer += '/';
            pointer += std::to_string(i);
            forEachAssetRefIn(*type.element, type.container->element(data, i), pointer, fn);
            pointer.resize(length);
        }
        break;
    case FieldType::Optional:
        if (type.container->size(data) > 0)
        {
            forEachAssetRefIn(*type.element, type.container->element(data, 0), pointer, fn);
        }
        break;
    default:
        break;
    }
}

/// True if values of @p type can hold an AssetRef.
[[nodiscard]] bool typeHasAssets(const ValueType& type) noexcept;

} // namespace detail

/// Calls @p fn(AssetRef& reference, std::string_view pointer) for every AssetRef in the object of the
/// reflected @p type at @p object, with the JSON Pointer of each in the object's JSON value.
template <class F>
void forEachAssetRef(const TypeReflection& type, void* object, F&& fn)
{
    std::string pointer;
    for (const FieldInfo& field : type.fields)
    {
        pointer = "/";
        pointer += field.name;
        detail::forEachAssetRefIn(field, fieldData(object, field), pointer, fn);
    }
}

/// True if objects of the reflected @p type can hold an AssetRef.
[[nodiscard]] bool hasAssetFields(const TypeReflection& type) noexcept;

/// Appends the references of the scene file @p json to @p out with @p source as their source.
[[nodiscard]] Expected<void, std::string> sceneReferences(std::string_view json, std::string_view source,
                                                          std::vector<AssetReference>& out);

/// Appends the references of the material file @p json to @p out with @p source as their source.
[[nodiscard]] Expected<void, std::string> materialReferences(std::string_view json, std::string_view source,
                                                             std::vector<AssetReference>& out);

/// Appends the references of the open scene: the saved Components of @p world (not runtime state) and the
/// scene's @p settings, with @p source as their source.
void worldReferences(World& world, const EntityIndex& index, std::span<const PreservedValue> settings,
                     std::string_view source, std::vector<AssetReference>& out);

/// Moves the references to @p from in the scene setting @p setting to @p to, rewriting its value. Returns
/// how many moved (0 if it holds none, or its type is not registered).
[[nodiscard]] u32 rewriteSettingReferences(PreservedValue& setting, std::string_view from,
                                           std::string_view to);

/// The scene file @p json with every reference to @p from moved to @p to, or std::nullopt if it has none.
[[nodiscard]] Expected<std::optional<std::string>, std::string>
rewriteSceneReferences(std::string_view json, std::string_view from, std::string_view to);

/// The material file @p json with every reference to @p from moved to @p to, or std::nullopt if it has none.
[[nodiscard]] Expected<std::optional<std::string>, std::string>
rewriteMaterialReferences(std::string_view json, std::string_view from, std::string_view to);

/// Appends the references of the project settings file @p json (see SettingsCatalog.hpp) to @p out with
/// @p source as their source. Returns false for JSON that is not a settings file, which holds none.
[[nodiscard]] Expected<bool, std::string>
settingsFileReferences(std::string_view json, std::string_view source, std::vector<AssetReference>& out);

/// The project settings file @p json with every reference to @p from moved to @p to, or std::nullopt if it
/// has none.
[[nodiscard]] Expected<std::optional<std::string>, std::string>
rewriteSettingsFileReferences(std::string_view json, std::string_view from, std::string_view to);

/// The asset references held by the scene, material and project settings files of a project.
///
/// refresh() reads every `.scene` and `.material` file under `assets:/` and every `.json` file under
/// `settings:/`, and keeps what it found per file; on later calls it parses again only files whose contents
/// changed (by hash). Extensions match in any case. A file that cannot be read is kept with its error (see
/// problems()).
class ReferenceIndex
{
public:
    /// Brings the index up to date with the files under `assets:/` and `settings:/` on @p files.
    void refresh(const VirtualFileSystem& files);

    /// Forgets every file (a project was opened or closed).
    void clear() noexcept { m_files.clear(); }

    /// Appends the references to @p path (an asset, or a folder and everything in it) held by indexed files
    /// other than @p skip (the open scene's file, whose content is the edited World).
    void referencesTo(std::string_view path, std::string_view skip, std::vector<AssetReference>& out) const;

    /// The references held by @p file, or nullptr if it is not indexed.
    [[nodiscard]] const std::vector<AssetReference>* referencesIn(std::string_view file) const;

    /// Files of the index that hold at least one reference to @p path, other than @p skip, sorted.
    [[nodiscard]] std::vector<std::string> filesReferring(std::string_view path, std::string_view skip) const;

    /// Indexed files that could not be read, with the reason ("<path>: <why>").
    [[nodiscard]] std::vector<std::string> problems() const;

    /// Number of indexed files.
    [[nodiscard]] usize size() const noexcept { return m_files.size(); }

private:
    struct FileEntry
    {
        u64 hash = 0;
        std::vector<AssetReference> references;
        std::string error;
    };

    std::map<std::string, FileEntry, std::less<>> m_files;
};

} // namespace illusion::editor

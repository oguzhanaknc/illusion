#include "ReflectedObject.hpp"

#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/EntityIndex.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/SettingsCatalog.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <cctype>
#include <memory>

namespace illusion::editor
{
namespace
{

std::string guidText(EntityGuid guid)
{
    const auto text = formatGuid(guid);
    return std::string(text.data(), text.size());
}

// "assets:/dir/" -> "assets:/dir"; a mount root keeps its slash.
std::string_view trimmed(std::string_view path) noexcept
{
    while (path.size() > 1 && path.ends_with('/') && !path.ends_with(":/"))
    {
        path.remove_suffix(1);
    }
    return path;
}

// Reads entity ids as stand-in handles and writes the same ids back, so that a settings value keeps its
// entity references while its asset references are read and rewritten.
class IdKeepingResolver final : public EntityResolver
{
public:
    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        if (entity.isNull() || entity.index >= m_ids.size())
        {
            return doc.makeNull();
        }
        return doc.makeString(guidText(m_ids[entity.index]));
    }

    Expected<Entity, std::string> readEntity(JsonValue value) const override
    {
        if (value.isNull())
        {
            return Entity{};
        }
        const std::optional<std::string_view> text = value.asString();
        const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
        if (!guid)
        {
            return unexpected(std::string("must be null or an entity id of 16 hex digits"));
        }
        m_ids.push_back(*guid);
        return Entity{static_cast<u32>(m_ids.size() - 1), 0};
    }

private:
    mutable std::vector<EntityGuid> m_ids;
};

const StructReflection* settingsType(std::string_view name)
{
    const StructRegistry& structs = ComponentRegistry::instance().structs();
    return structs.reflection(structs.findByStableName(name));
}

// Calls fn(AssetRef&, pointer) for the references of a settings value; with rewrite, writes the value back
// when fn returns true for any of them.
template <class F>
bool visitSetting(PreservedValue& setting, bool rewrite, F&& fn)
{
    const StructReflection* type = settingsType(setting.type);
    if (type == nullptr || !hasAssetFields(*type))
    {
        return false;
    }
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(setting.value.text);
    if (!parsed)
    {
        return false;
    }
    detail::ReflectedObject object(type);
    IdKeepingResolver resolver;
    ValueReadOptions options;
    options.mode = JsonReadMode::Replace;
    options.entities = &resolver;
    options.savedVersion = setting.version;
    if (!readValue(*type, object.data(), parsed->root(), options).ok())
    {
        return false;
    }
    bool changed = false;
    forEachAssetRef(*type, object.data(), [&](AssetRef& reference, std::string_view pointer) {
        changed = fn(reference, pointer) || changed;
    });
    if (!rewrite || !changed)
    {
        return false;
    }
    JsonMutDocument doc;
    Expected<JsonMutValue, ValueError> written =
        writeValue(doc, *type, object.data(), {.entities = &resolver});
    if (!written)
    {
        return false;
    }
    doc.setRoot(*written);
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return false;
    }
    setting.value.text = std::move(text);
    setting.version = type->version;
    return true;
}

// Calls fn(AssetRef&, pointer, entityId, owner) for every reference held by the saved Components of the
// entities.
template <class F>
void visitEntities(World& world, std::span<const Entity> entities, std::span<const EntityGuid> ids, F&& fn)
{
    const ComponentRegistry& registry = world.registry();
    for (usize i = 0; i < entities.size(); ++i)
    {
        const std::string id = guidText(i < ids.size() ? ids[i] : world.get<EntityGuid>(entities[i]));
        for (const ComponentId type : world.componentsOf(entities[i]))
        {
            const TypeReflection* reflection = registry.reflection(type);
            if (reflection == nullptr || reflection->meta.transient || reflection->size == 0 ||
                !hasAssetFields(*reflection))
            {
                continue;
            }
            forEachAssetRef(*reflection, world.componentData(entities[i], type),
                            [&](AssetRef& reference, std::string_view pointer) {
                                fn(reference, pointer, id, reflection->name);
                            });
        }
    }
}

struct LoadedScene
{
    World world;
    SceneLoadResult result;
};

Expected<std::unique_ptr<LoadedScene>, std::string> loadSceneFile(std::string_view json)
{
    auto scene = std::make_unique<LoadedScene>();
    scene->result = loadScene(scene->world, json, {});
    if (!scene->result.ok())
    {
        return unexpected("not a scene: " + scene->result.error);
    }
    return scene;
}

const StructReflection& materialType()
{
    const StructReflection* type =
        ComponentRegistry::instance().structs().reflectionOf(typeHash<MaterialAsset>());
    ILL_ASSERT(type != nullptr, "the material types are not reflected (call reflectAssetStructs())");
    return *type;
}

// The reflection of the settings struct that the settings file @p root names (nullptr if it is not
// registered);
// @p isSettings tells whether @p root is a settings file at all.
const StructReflection* settingsFileType(JsonValue root, bool& isSettings)
{
    isSettings = root.isObject() && root.find("format").asString() == kSettingsFormat;
    const std::optional<std::string_view> name = root.find("type").asString();
    return isSettings && name ? settingsType(*name) : nullptr;
}

// Lists the files under @p root on @p files whose extension (in lower case) is @p extension.
void filesWithExtension(const VirtualFileSystem& files, std::string_view root, std::string_view extension,
                        std::vector<std::string>& out)
{
    std::vector<DirectoryEntry> entries;
    if (files.listRecursive(root, entries) != FileError::None)
    {
        return;
    }
    for (const DirectoryEntry& entry : entries)
    {
        if (!entry.isDirectory && lowerExtension(entry.name) == extension)
        {
            out.push_back(std::string(root) + entry.name);
        }
    }
}

} // namespace

namespace detail
{

bool typeHasAssets(const ValueType& type) noexcept
{
    switch (type.type)
    {
    case FieldType::Asset:
        return true;
    case FieldType::Struct:
        return hasAssetFields(*type.structType);
    case FieldType::FixedArray:
    case FieldType::Array:
    case FieldType::Optional:
        return typeHasAssets(*type.element);
    default:
        return false;
    }
}

} // namespace detail

bool hasAssetFields(const TypeReflection& type) noexcept
{
    return std::any_of(type.fields.begin(), type.fields.end(),
                       [](const FieldInfo& field) { return detail::typeHasAssets(field); });
}

std::string lowerExtension(std::string_view path)
{
    const usize slash = path.rfind('/');
    const usize dot = path.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
    {
        return {};
    }
    std::string extension(path.substr(dot));
    for (char& c : extension)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension;
}

bool refersTo(std::string_view reference, std::string_view path) noexcept
{
    path = trimmed(path);
    if (path.empty() || !reference.starts_with(path))
    {
        return false;
    }
    if (reference.size() == path.size() || path.ends_with('/'))
    {
        return true;
    }
    const char next = reference[path.size()];
    return next == '#' || next == '/';
}

std::string movedReference(std::string_view reference, std::string_view from, std::string_view to)
{
    from = trimmed(from);
    to = trimmed(to);
    std::string moved(to);
    std::string_view rest = reference.substr(from.size());
    if (moved.ends_with('/') && rest.starts_with('/'))
    {
        rest.remove_prefix(1);
    }
    moved += rest;
    return moved;
}

Expected<void, std::string> sceneReferences(std::string_view json, std::string_view source,
                                            std::vector<AssetReference>& out)
{
    Expected<std::unique_ptr<LoadedScene>, std::string> loaded = loadSceneFile(json);
    if (!loaded)
    {
        return unexpected(std::move(loaded).error());
    }
    LoadedScene& scene = **loaded;
    visitEntities(
        scene.world, scene.result.entities, scene.result.fileIds,
        [&](AssetRef& reference, std::string_view pointer, const std::string& id, std::string_view owner) {
            if (!reference.empty())
            {
                out.push_back(AssetReference{std::string(reference.path()), std::string(source), id,
                                             std::string(owner), std::string(pointer)});
            }
        });
    for (PreservedValue& setting : scene.result.preservedSettings)
    {
        (void)visitSetting(setting, false, [&](AssetRef& reference, std::string_view pointer) {
            if (!reference.empty())
            {
                out.push_back(AssetReference{std::string(reference.path()),
                                             std::string(source),
                                             {},
                                             setting.type,
                                             std::string(pointer)});
            }
            return false;
        });
    }
    return {};
}

Expected<void, std::string> materialReferences(std::string_view json, std::string_view source,
                                               std::vector<AssetReference>& out)
{
    Expected<MaterialAsset, std::string> material = parseMaterialAsset(json);
    if (!material)
    {
        return unexpected(std::move(material).error());
    }
    forEachAssetRef(materialType(), &*material, [&](AssetRef& reference, std::string_view pointer) {
        if (!reference.empty())
        {
            out.push_back(AssetReference{
                std::string(reference.path()), std::string(source), {}, {}, std::string(pointer)});
        }
    });
    return {};
}

void worldReferences(World& world, const EntityIndex& index, std::span<const PreservedValue> settings,
                     std::string_view source, std::vector<AssetReference>& out)
{
    std::vector<Entity> entities;
    world.entities(entities);
    std::vector<EntityGuid> ids;
    ids.reserve(entities.size());
    for (const Entity entity : entities)
    {
        ids.push_back(index.guidOf(entity));
    }
    visitEntities(
        world, entities, ids,
        [&](AssetRef& reference, std::string_view pointer, const std::string& id, std::string_view owner) {
            if (!reference.empty())
            {
                out.push_back(AssetReference{std::string(reference.path()), std::string(source), id,
                                             std::string(owner), std::string(pointer)});
            }
        });
    for (const PreservedValue& value : settings)
    {
        PreservedValue setting = value;
        (void)visitSetting(setting, false, [&](AssetRef& reference, std::string_view pointer) {
            if (!reference.empty())
            {
                out.push_back(AssetReference{std::string(reference.path()),
                                             std::string(source),
                                             {},
                                             setting.type,
                                             std::string(pointer)});
            }
            return false;
        });
    }
}

u32 rewriteSettingReferences(PreservedValue& setting, std::string_view from, std::string_view to)
{
    u32 moved = 0;
    const bool rewritten = visitSetting(setting, true, [&](AssetRef& reference, std::string_view) {
        if (reference.empty() || !refersTo(reference.path(), from))
        {
            return false;
        }
        reference = AssetRef(movedReference(reference.path(), from, to));
        ++moved;
        return true;
    });
    return rewritten ? moved : 0;
}

Expected<std::optional<std::string>, std::string>
rewriteSceneReferences(std::string_view json, std::string_view from, std::string_view to)
{
    Expected<std::unique_ptr<LoadedScene>, std::string> loaded = loadSceneFile(json);
    if (!loaded)
    {
        return unexpected(std::move(loaded).error());
    }
    LoadedScene& scene = **loaded;
    bool changed = false;
    const auto move = [&](AssetRef& reference) {
        if (reference.empty() || !refersTo(reference.path(), from))
        {
            return false;
        }
        reference = AssetRef(movedReference(reference.path(), from, to));
        return true;
    };
    visitEntities(scene.world, scene.result.entities, scene.result.fileIds,
                  [&](AssetRef& reference, std::string_view, const std::string&, std::string_view) {
                      changed = move(reference) || changed;
                  });
    for (PreservedValue& setting : scene.result.preservedSettings)
    {
        changed = visitSetting(setting, true,
                               [&](AssetRef& reference, std::string_view) { return move(reference); }) ||
                  changed;
    }
    if (!changed)
    {
        return std::optional<std::string>{};
    }
    SceneSaveOptions options;
    options.preservedSettings = scene.result.preservedSettings;
    SceneSaveResult saved = saveScene(scene.world, options);
    if (!saved.ok())
    {
        return unexpected("cannot write the scene: " + saved.error);
    }
    return std::optional<std::string>(std::move(saved.json));
}

Expected<std::optional<std::string>, std::string>
rewriteMaterialReferences(std::string_view json, std::string_view from, std::string_view to)
{
    Expected<MaterialAsset, std::string> material = parseMaterialAsset(json);
    if (!material)
    {
        return unexpected(std::move(material).error());
    }
    bool changed = false;
    forEachAssetRef(materialType(), &*material, [&](AssetRef& reference, std::string_view) {
        if (!reference.empty() && refersTo(reference.path(), from))
        {
            reference = AssetRef(movedReference(reference.path(), from, to));
            changed = true;
        }
    });
    if (!changed)
    {
        return std::optional<std::string>{};
    }
    Expected<std::string, std::string> written = writeMaterialAsset(*material);
    if (!written)
    {
        return unexpected("cannot write the material: " + written.error());
    }
    return std::optional<std::string>(std::move(written).value());
}

Expected<bool, std::string> settingsFileReferences(std::string_view json, std::string_view source,
                                                   std::vector<AssetReference>& out)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return false;
    }
    bool isSettings = false;
    const StructReflection* type = settingsFileType(parsed->root(), isSettings);
    if (type == nullptr || !hasAssetFields(*type))
    {
        return isSettings;
    }
    detail::ReflectedObject value(type);
    if (Expected<void, std::string> read = readSettingsFile(*type, json, value.data()); !read)
    {
        return unexpected(std::move(read).error());
    }
    forEachAssetRef(*type, value.data(), [&](AssetRef& reference, std::string_view pointer) {
        if (!reference.empty())
        {
            out.push_back(AssetReference{std::string(reference.path()),
                                         std::string(source),
                                         {},
                                         std::string(type->name),
                                         std::string(pointer)});
        }
    });
    return true;
}

Expected<std::optional<std::string>, std::string>
rewriteSettingsFileReferences(std::string_view json, std::string_view from, std::string_view to)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    bool isSettings = false;
    const StructReflection* type = parsed ? settingsFileType(parsed->root(), isSettings) : nullptr;
    if (type == nullptr || !hasAssetFields(*type))
    {
        return std::optional<std::string>{};
    }
    detail::ReflectedObject value(type);
    if (Expected<void, std::string> read = readSettingsFile(*type, json, value.data()); !read)
    {
        return unexpected(std::move(read).error());
    }
    bool changed = false;
    forEachAssetRef(*type, value.data(), [&](AssetRef& reference, std::string_view) {
        if (!reference.empty() && refersTo(reference.path(), from))
        {
            reference = AssetRef(movedReference(reference.path(), from, to));
            changed = true;
        }
    });
    if (!changed)
    {
        return std::optional<std::string>{};
    }
    Expected<std::string, std::string> written = writeSettingsFile(*type, value.data());
    if (!written)
    {
        return unexpected("cannot write the settings: " + written.error());
    }
    return std::optional<std::string>(std::move(written).value());
}

void ReferenceIndex::refresh(const VirtualFileSystem& files)
{
    ILL_PROFILE_SCOPE();
    std::vector<std::string> paths;
    filesWithExtension(files, kAssetsRoot, kSceneExtension, paths);
    filesWithExtension(files, kAssetsRoot, kMaterialAssetExtension, paths);
    filesWithExtension(files, kSettingsRoot, ".json", paths);
    std::map<std::string, FileEntry, std::less<>> next;
    std::string text;
    for (std::string& path : paths)
    {
        FileEntry file;
        if (const FileError error = files.readText(path, text); error != FileError::None)
        {
            file.error = "cannot read it (" + std::string(fileErrorName(error)) + ")";
            next.emplace(std::move(path), std::move(file));
            continue;
        }
        file.hash = fnv1a64(text);
        if (const auto known = m_files.find(path); known != m_files.end() && known->second.hash == file.hash)
        {
            next.emplace(std::move(path), std::move(known->second));
            continue;
        }
        const std::string extension = lowerExtension(path);
        Expected<void, std::string> scanned;
        if (extension == kSceneExtension)
        {
            scanned = sceneReferences(text, path, file.references);
        }
        else if (extension == kMaterialAssetExtension)
        {
            scanned = materialReferences(text, path, file.references);
        }
        else if (Expected<bool, std::string> settings = settingsFileReferences(text, path, file.references);
                 !settings)
        {
            scanned = unexpected(std::move(settings).error());
        }
        if (!scanned)
        {
            file.references.clear();
            file.error = std::move(scanned).error();
        }
        next.emplace(std::move(path), std::move(file));
    }
    m_files = std::move(next);
}

void ReferenceIndex::referencesTo(std::string_view path, std::string_view skip,
                                  std::vector<AssetReference>& out) const
{
    for (const auto& [file, entry] : m_files)
    {
        if (file == skip)
        {
            continue;
        }
        for (const AssetReference& reference : entry.references)
        {
            if (refersTo(reference.asset, path))
            {
                out.push_back(reference);
            }
        }
    }
}

const std::vector<AssetReference>* ReferenceIndex::referencesIn(std::string_view file) const
{
    const auto it = m_files.find(file);
    return it != m_files.end() ? &it->second.references : nullptr;
}

std::vector<std::string> ReferenceIndex::filesReferring(std::string_view path, std::string_view skip) const
{
    std::vector<std::string> out;
    for (const auto& [file, entry] : m_files)
    {
        if (file != skip &&
            std::any_of(entry.references.begin(), entry.references.end(),
                        [&](const AssetReference& reference) { return refersTo(reference.asset, path); }))
        {
            out.push_back(file);
        }
    }
    return out;
}

std::vector<std::string> ReferenceIndex::problems() const
{
    std::vector<std::string> out;
    for (const auto& [file, entry] : m_files)
    {
        if (!entry.error.empty())
        {
            out.push_back(file + ": " + entry.error);
        }
    }
    return out;
}

} // namespace illusion::editor

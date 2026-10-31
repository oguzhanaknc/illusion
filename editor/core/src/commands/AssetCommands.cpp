#include "../ReflectedObject.hpp"
#include "CommandSupport.hpp"

#include <illusion/assets/ImportSettings.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>

#include <algorithm>
#include <iterator>
#include <tuple>

namespace illusion::editor::detail
{
namespace
{

// Checks that @p path is under `assets:/` (and, with @p allowRoot false, is not the mount itself).
Expected<void, CommandError> checkAssetPath(CommandContext& context, std::string_view path,
                                            std::string_view pointer, bool allowRoot)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return open;
    }
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            std::string(pointer), "must be a path in the assets: mount, e.g. \"assets:/models/ship.glb\""));
    }
    if (!allowRoot && path == kAssetsRoot)
    {
        return unexpected(
            CommandError::invalid(std::string(pointer), "must not be the assets: mount itself"));
    }
    return {};
}

Expected<FileStat, CommandError> existing(CommandContext& context, std::string_view path)
{
    FileStat stat;
    const FileError error = context.editor().files().stat(path, stat);
    if (error == FileError::NotFound)
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist"));
    }
    if (error != FileError::None)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                                     std::string(fileErrorName(error)) +
                                                                     ")"));
    }
    return stat;
}

// Sidecars are edited through asset.set_import_settings and travel with their asset, never on their own.
Expected<void, CommandError> notImportSettings(std::string_view path, std::string_view pointer)
{
    if (isImportSettingsPath(path))
    {
        return unexpected(
            CommandError::invalid(std::string(pointer), "must not be an import settings file (" +
                                                            std::string(kImportSettingsSuffix) +
                                                            "); use asset.set_import_settings on its asset"));
    }
    return {};
}

// The text of the sidecar of @p asset, or std::nullopt if it has none.
Expected<std::optional<std::string>, CommandError> readSidecar(CommandContext& context,
                                                               std::string_view asset)
{
    const std::string path = importSettingsPath(asset);
    std::string text;
    const FileError error = context.editor().files().readText(path, text);
    if (error == FileError::NotFound)
    {
        return std::optional<std::string>();
    }
    if (error != FileError::None)
    {
        return unexpected(CommandError::make(
            ErrorKind::IoError, "cannot read '" + path + "' (" + std::string(fileErrorName(error)) + ")"));
    }
    return std::optional<std::string>(std::move(text));
}

void sortReferences(std::vector<AssetReference>& references)
{
    std::sort(references.begin(), references.end(), [](const AssetReference& a, const AssetReference& b) {
        return std::tie(a.source, a.entity, a.owner, a.field, a.asset) <
               std::tie(b.source, b.entity, b.owner, b.field, b.asset);
    });
}

// Warns about the files the reference index could not read: references in them are not known.
void warnProblems(CommandContext& context, const ReferenceIndex& index)
{
    for (const std::string& problem : index.problems())
    {
        context.warn("references in " + problem + " are not known");
    }
}

// The manifest's startup scene as a reference, if it refers to @p path.
std::optional<AssetReference> startupReference(CommandContext& context, std::string_view path)
{
    const Project* project = context.editor().project();
    const std::string& startup = project != nullptr ? project->manifest().startupScene : std::string();
    if (startup.empty() || !refersTo(startup, path))
    {
        return std::nullopt;
    }
    return AssetReference{startup, std::string(kProjectFileName), {}, {}, "/startupScene"};
}

// References to @p path in the project files (except the open scene's), in the open scene and in the
// manifest.
std::vector<AssetReference> referencesTo(CommandContext& context, std::string_view path)
{
    EditorContext& editor = context.editor();
    const std::string& open = editor.document().path();
    std::vector<AssetReference> references;
    if (std::optional<AssetReference> startup = startupReference(context, path))
    {
        references.push_back(std::move(*startup));
    }
    const ReferenceIndex& index = editor.references();
    warnProblems(context, index);
    index.referencesTo(path, open, references);
    std::vector<AssetReference> live;
    worldReferences(editor.world(), editor.entities(), editor.sceneSettings(), open, live);
    for (AssetReference& reference : live)
    {
        if (refersTo(reference.asset, path))
        {
            references.push_back(std::move(reference));
        }
    }
    sortReferences(references);
    return references;
}

// The open scene's path must not be @p path or inside it: its file is what the edited World is saved to.
Expected<void, CommandError> notOpenScene(CommandContext& context, std::string_view path,
                                          std::string_view what)
{
    const std::string& open = context.editor().document().path();
    if (!open.empty() && refersTo(open, path))
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed,
            "the open scene " + open + " cannot be " + std::string(what) +
                "; save it under another name (scene.save_as) or open another scene first"));
    }
    return {};
}

// Moves the references to @p from in the open scene to @p to. Returns how many changed.
Expected<u32, CommandError> moveWorldReferences(CommandContext& context, std::string_view from,
                                                std::string_view to)
{
    World& world = context.world();
    const ComponentRegistry& registry = world.registry();
    std::vector<Entity> entities;
    world.entities(entities);
    u32 moved = 0;
    for (const Entity entity : entities)
    {
        // Assigning values is not a structural change, so the Component list stays valid.
        for (const ComponentId type : world.componentsOf(entity))
        {
            const TypeReflection* reflection = registry.reflection(type);
            if (reflection == nullptr || reflection->meta.transient || reflection->size == 0 ||
                !hasAssetFields(*reflection))
            {
                continue;
            }
            bool refers = false;
            forEachAssetRef(*reflection, world.componentData(entity, type),
                            [&](AssetRef& reference, std::string_view) {
                                refers = refers || (!reference.empty() && refersTo(reference.path(), from));
                            });
            if (!refers)
            {
                continue;
            }
            ReflectedObject value(reflection);
            reflection->ops->assign(value.data(), world.componentData(entity, type));
            u32 changed = 0;
            forEachAssetRef(*reflection, value.data(), [&](AssetRef& reference, std::string_view) {
                if (!reference.empty() && refersTo(reference.path(), from))
                {
                    reference = AssetRef(movedReference(reference.path(), from, to));
                    ++changed;
                }
            });
            if (changed == 0)
            {
                continue;
            }
            Expected<bool, CommandError> assigned =
                context.edit().assignComponent(entity, type, value.data());
            if (!assigned)
            {
                return unexpected(std::move(assigned).error());
            }
            moved += changed;
        }
    }
    std::vector<PreservedValue> settings(context.editor().sceneSettings().begin(),
                                         context.editor().sceneSettings().end());
    for (PreservedValue& setting : settings)
    {
        const u32 changed = rewriteSettingReferences(setting, from, to);
        if (changed == 0)
        {
            continue;
        }
        Expected<bool, CommandError> set =
            context.edit().setSetting(setting.type, setting.value.text, setting.version);
        if (!set)
        {
            return unexpected(std::move(set).error());
        }
        moved += changed;
    }
    return moved;
}

Expected<AssetListResult, CommandError> assetList(CommandContext& context, const AssetListParams& params)
{
    const std::string path = params.path.value_or(std::string(kAssetsRoot));
    if (Expected<void, CommandError> valid = checkAssetPath(context, path, "/path", true); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (params.kind && std::find(kAssetKinds.begin(), kAssetKinds.end(), *params.kind) == kAssetKinds.end())
    {
        CommandError error = CommandError::invalid(
            "/kind", "must be Folder, Scene, Material, Model, Texture, Environment, AudioClip or Other");
        error.suggestions = closestMatches(
            *params.kind, std::vector<std::string_view>(kAssetKinds.begin(), kAssetKinds.end()));
        return unexpected(std::move(error));
    }
    if (Expected<FileStat, CommandError> stat = existing(context, path); !stat)
    {
        return unexpected(std::move(stat).error());
    }
    std::vector<DirectoryEntry> entries;
    VirtualFileSystem& files = context.editor().files();
    const FileError error =
        params.recursive.value_or(false) ? files.listRecursive(path, entries) : files.list(path, entries);
    if (error != FileError::None)
    {
        return unexpected(CommandError::make(
            ErrorKind::IoError, "cannot list '" + path + "' (" + std::string(fileErrorName(error)) + ")"));
    }
    AssetListResult result;
    for (const DirectoryEntry& entry : entries)
    {
        std::string entryPath = joinPath(path, entry.name);
        if (!entry.isDirectory && isImportSettingsPath(entryPath))
        {
            continue; // Part of its asset (see asset.info), not an asset of its own.
        }
        const std::string_view kind = assetKindOf(entryPath, entry.isDirectory);
        if (!params.kind || *params.kind == kind)
        {
            result.assets.push_back(AssetEntry{std::move(entryPath), std::string(kind), entry.size});
        }
    }
    std::sort(result.assets.begin(), result.assets.end(),
              [](const AssetEntry& a, const AssetEntry& b) { return a.path < b.path; });
    return result;
}

Expected<std::vector<std::string>, CommandError> dependenciesOf(CommandContext& context,
                                                                std::string_view path)
{
    EditorContext& editor = context.editor();
    std::vector<AssetReference> references;
    if (path == editor.document().path())
    {
        worldReferences(editor.world(), editor.entities(), editor.sceneSettings(), path, references);
    }
    else
    {
        const std::string extension = lowerExtension(path);
        if (extension == kSceneExtension || extension == kMaterialAssetExtension)
        {
            std::string text;
            if (const FileError error = editor.files().readText(path, text); error != FileError::None)
            {
                return unexpected(
                    CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                               std::string(fileErrorName(error)) + ")"));
            }
            Expected<void, std::string> scanned = extension == kSceneExtension
                                                      ? sceneReferences(text, path, references)
                                                      : materialReferences(text, path, references);
            if (!scanned)
            {
                return unexpected(CommandError::invalid("/path", "cannot be read: " + scanned.error()));
            }
        }
    }
    std::vector<std::string> dependencies;
    for (AssetReference& reference : references)
    {
        dependencies.push_back(std::move(reference.asset));
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    return dependencies;
}

Expected<AssetInfoResult, CommandError> assetInfo(CommandContext& context, const FilePathParams& params)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.path, "/path", true); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = notImportSettings(params.path, "/path"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<FileStat, CommandError> stat = existing(context, params.path);
    if (!stat)
    {
        return unexpected(std::move(stat).error());
    }
    AssetInfoResult result;
    result.path = params.path;
    result.kind = std::string(assetKindOf(params.path, stat->isDirectory));
    result.size = stat->size;
    result.references = static_cast<u32>(referencesTo(context, params.path).size());
    if (!stat->isDirectory)
    {
        Expected<std::vector<std::string>, CommandError> dependencies = dependenciesOf(context, params.path);
        if (!dependencies)
        {
            return unexpected(std::move(dependencies).error());
        }
        result.dependencies = static_cast<u32>(dependencies->size());
        const ImportSettingsKind kind = importSettingsKindOf(params.path);
        if (kind != ImportSettingsKind::None)
        {
            Expected<std::optional<std::string>, CommandError> sidecar = readSidecar(context, params.path);
            if (!sidecar)
            {
                return unexpected(std::move(sidecar).error());
            }
            Expected<ImportSettings, ImportSettingsError> settings =
                *sidecar ? parseImportSettings(**sidecar, kind)
                         : Expected<ImportSettings, ImportSettingsError>();
            if (settings)
            {
                result.importSettings = JsonText{importSettingsValue(*settings, kind)};
            }
            else
            {
                context.warn(importSettingsPath(params.path) + settings.error().pointer +
                             " is invalid: " + settings.error().message);
            }
        }
    }
    return result;
}

Expected<FilePathResult, CommandError> assetCreateFolder(CommandContext& context,
                                                         const FilePathParams& params)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.path, "/path", false); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> created = context.edit().createDirectory(params.path); !created)
    {
        return unexpected(std::move(created).error());
    }
    return FilePathResult{params.path};
}

Expected<AssetMoveResult, CommandError> assetMove(CommandContext& context, const AssetMoveParams& params)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.from, "/from", false); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.to, "/to", false); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<FileStat, CommandError> stat = existing(context, params.from); !stat)
    {
        return unexpected(std::move(stat).error());
    }
    if (refersTo(params.to, params.from))
    {
        return unexpected(CommandError::invalid("/to", "must not be inside the moved folder"));
    }
    if (context.editor().files().exists(params.to))
    {
        return unexpected(CommandError::make(ErrorKind::AlreadyExists, "'" + params.to + "' exists already"));
    }
    for (const auto& [path, pointer] :
         {std::pair{std::string_view(params.from), "/from"}, std::pair{std::string_view(params.to), "/to"}})
    {
        if (Expected<void, CommandError> valid = notImportSettings(path, pointer); !valid)
        {
            return unexpected(std::move(valid).error());
        }
    }
    // A file's sidecar moves with it (a folder's sidecars are inside it).
    const std::string fromSidecar = importSettingsPath(params.from);
    const std::string toSidecar = importSettingsPath(params.to);
    const bool moveSidecar = context.editor().files().exists(fromSidecar);
    if (moveSidecar)
    {
        if (importSettingsKindOf(params.to) != importSettingsKindOf(params.from))
        {
            return unexpected(CommandError::make(
                ErrorKind::PreconditionFailed,
                "'" + params.from + "' has import settings that '" + params.to +
                    "' could not use; reset them first with asset.set_import_settings {\"value\": null}"));
        }
        if (context.editor().files().exists(toSidecar))
        {
            return unexpected(
                CommandError::make(ErrorKind::AlreadyExists, "'" + toSidecar + "' exists already"));
        }
    }
    if (Expected<void, CommandError> allowed = notOpenScene(context, params.from, "moved"); !allowed)
    {
        return unexpected(std::move(allowed).error());
    }
    if (std::optional<AssetReference> startup = startupReference(context, params.from))
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the project's startup scene " + startup->asset + " (named in " +
                                                 std::string(kProjectFileName) + ") cannot be moved"));
    }
    EditorContext& editor = context.editor();
    const ReferenceIndex& index = editor.references();
    warnProblems(context, index);
    const std::vector<std::string> referring = index.filesReferring(params.from, editor.document().path());
    AssetMoveResult result;
    result.from = params.from;
    result.to = params.to;
    for (const std::string& file : referring)
    {
        const std::vector<AssetReference>* held = index.referencesIn(file);
        result.references +=
            static_cast<u32>(std::count_if(held->begin(), held->end(), [&](const AssetReference& r) {
                return refersTo(r.asset, params.from);
            }));
    }

    if (Expected<void, CommandError> moved = context.edit().moveFile(params.from, params.to); !moved)
    {
        return unexpected(std::move(moved).error());
    }
    if (moveSidecar)
    {
        if (Expected<void, CommandError> moved = context.edit().moveFile(fromSidecar, toSidecar); !moved)
        {
            return unexpected(std::move(moved).error());
        }
    }
    for (const std::string& file : referring)
    {
        // A referring file inside a moved folder moved with it.
        const std::string current =
            refersTo(file, params.from) ? movedReference(file, params.from, params.to) : file;
        std::string text;
        if (const FileError error = editor.files().readText(current, text); error != FileError::None)
        {
            return unexpected(CommandError::make(ErrorKind::IoError, "cannot read '" + current + "' (" +
                                                                         std::string(fileErrorName(error)) +
                                                                         ")"));
        }
        const std::string extension = lowerExtension(current);
        Expected<std::optional<std::string>, std::string> rewritten =
            extension == kSceneExtension ? rewriteSceneReferences(text, params.from, params.to)
            : extension == kMaterialAssetExtension
                ? rewriteMaterialReferences(text, params.from, params.to)
                : rewriteSettingsFileReferences(text, params.from, params.to);
        if (!rewritten)
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "cannot update the references in '" +
                                                                          current +
                                                                          "': " + rewritten.error()));
        }
        if (!*rewritten)
        {
            continue;
        }
        if (Expected<bool, CommandError> written = context.edit().writeFile(current, **rewritten); !written)
        {
            return unexpected(std::move(written).error());
        }
        result.updated.push_back(current);
    }
    Expected<u32, CommandError> open = moveWorldReferences(context, params.from, params.to);
    if (!open)
    {
        return unexpected(std::move(open).error());
    }
    result.references += *open;
    std::sort(result.updated.begin(), result.updated.end());
    if (*open > 0)
    {
        context.warn("references in the open scene now point to " + params.to +
                     "; save the scene to keep them");
    }
    return result;
}

Expected<AssetDeleteResult, CommandError> assetDelete(CommandContext& context,
                                                      const AssetDeleteParams& params)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.path, "/path", false); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = notImportSettings(params.path, "/path"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<FileStat, CommandError> stat = existing(context, params.path); !stat)
    {
        return unexpected(std::move(stat).error());
    }
    if (Expected<void, CommandError> allowed = notOpenScene(context, params.path, "deleted"); !allowed)
    {
        return unexpected(std::move(allowed).error());
    }
    std::vector<AssetReference> references = referencesTo(context, params.path);
    // Files inside a deleted folder go with it: their references to each other do not count.
    std::erase_if(references, [&](const AssetReference& reference) {
        return !reference.source.empty() && reference.source != context.editor().document().path() &&
               refersTo(reference.source, params.path);
    });
    if (!references.empty() && !params.force.value_or(false))
    {
        CommandError error = CommandError::make(
            ErrorKind::PreconditionFailed,
            "'" + params.path + "' is referenced " + std::to_string(references.size()) + " times (first in " +
                (references.front().source.empty() ? std::string("the open scene")
                                                   : references.front().source) +
                "); see asset.references, or pass force to delete it anyway");
        return unexpected(std::move(error));
    }
    if (Expected<void, CommandError> deleted = context.edit().deleteFile(params.path); !deleted)
    {
        return unexpected(std::move(deleted).error());
    }
    // The sidecar goes with its asset, in the same undo step.
    if (const std::string sidecar = importSettingsPath(params.path); context.editor().files().exists(sidecar))
    {
        if (Expected<void, CommandError> deleted = context.edit().deleteFile(sidecar); !deleted)
        {
            return unexpected(std::move(deleted).error());
        }
    }
    return AssetDeleteResult{params.path, std::move(references)};
}

Expected<AssetReferencesResult, CommandError> assetReferences(CommandContext& context,
                                                              const FilePathParams& params)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.path, "/path", true); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    return AssetReferencesResult{referencesTo(context, params.path)};
}

Expected<AssetDependenciesResult, CommandError> assetDependencies(CommandContext& context,
                                                                  const FilePathParams& params)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, params.path, "/path", false); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (params.path != context.editor().document().path())
    {
        Expected<FileStat, CommandError> stat = existing(context, params.path);
        if (!stat)
        {
            return unexpected(std::move(stat).error());
        }
        if (stat->isDirectory)
        {
            return unexpected(CommandError::invalid("/path", "must be a file, not a folder"));
        }
    }
    Expected<std::vector<std::string>, CommandError> dependencies = dependenciesOf(context, params.path);
    if (!dependencies)
    {
        return unexpected(std::move(dependencies).error());
    }
    return AssetDependenciesResult{std::move(dependencies).value()};
}

Expected<AssetFindMissingResult, CommandError> assetFindMissing(CommandContext& context, const NoParams&)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    EditorContext& editor = context.editor();
    const AssetCatalog& catalog = editor.assets();
    const std::string& openScene = editor.document().path();
    std::vector<AssetReference> found;
    if (std::optional<AssetReference> startup = startupReference(context, kAssetsRoot))
    {
        found.push_back(std::move(*startup));
    }
    const ReferenceIndex& index = editor.references();
    warnProblems(context, index);
    index.referencesTo(kAssetsRoot, openScene, found);
    std::vector<AssetReference> live;
    worldReferences(editor.world(), editor.entities(), editor.sceneSettings(), openScene, live);
    std::move(live.begin(), live.end(), std::back_inserter(found));
    for (const auto& [path, record] : catalog.records())
    {
        if (lowerExtension(path) == ".gltf")
        {
            for (const std::string& dependency : record.dependencies)
            {
                found.push_back(AssetReference{dependency, path, {}, {}, {}});
            }
        }
    }
    AssetFindMissingResult result;
    for (AssetReference& reference : found)
    {
        const std::string_view path = std::string_view(reference.asset).substr(0, reference.asset.find('#'));
        if (refersTo(path, kAssetsRoot) && path != kAssetsRoot && catalog.find(path) == nullptr)
        {
            result.assets.emplace_back(path);
            result.references.push_back(std::move(reference));
        }
    }
    sortReferences(result.references);
    std::sort(result.assets.begin(), result.assets.end());
    result.assets.erase(std::unique(result.assets.begin(), result.assets.end()), result.assets.end());
    return result;
}

// The settings kind of the existing asset file @p path ("/path"), or why it has no import settings.
Expected<ImportSettingsKind, CommandError> importedAsset(CommandContext& context, std::string_view path,
                                                         bool allowRoot)
{
    if (Expected<void, CommandError> valid = checkAssetPath(context, path, "/path", allowRoot); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<void, CommandError> valid = notImportSettings(path, "/path"); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<FileStat, CommandError> stat = existing(context, path);
    if (!stat)
    {
        return unexpected(std::move(stat).error());
    }
    if (stat->isDirectory)
    {
        return ImportSettingsKind::None;
    }
    const ImportSettingsKind kind = importSettingsKindOf(path);
    if (kind == ImportSettingsKind::None)
    {
        return unexpected(CommandError::invalid(
            "/path",
            "must be a texture (.png, .jpg, .jpeg, .tga, .bmp, .hdr) or a model (.gltf, .glb); other "
            "assets are not imported"));
    }
    return kind;
}

Expected<AssetImportSettingsResult, CommandError>
assetSetImportSettings(CommandContext& context, const AssetSetImportSettingsParams& params)
{
    Expected<ImportSettingsKind, CommandError> kind = importedAsset(context, params.path, false);
    if (!kind)
    {
        return unexpected(std::move(kind).error());
    }
    if (*kind == ImportSettingsKind::None)
    {
        return unexpected(CommandError::invalid("/path", "must be a file, not a folder"));
    }
    Expected<JsonDocument, CommandError> patch = parseParam(params.value, "/value");
    if (!patch)
    {
        return unexpected(std::move(patch).error());
    }
    Expected<std::optional<std::string>, CommandError> before = readSidecar(context, params.path);
    if (!before)
    {
        return unexpected(std::move(before).error());
    }
    const std::string sidecar = importSettingsPath(params.path);
    ImportSettings settings;
    if (*before)
    {
        Expected<ImportSettings, ImportSettingsError> current = parseImportSettings(**before, *kind);
        if (current)
        {
            settings = *current;
        }
        else if (!patch->root().isNull())
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "'" + sidecar + "' is invalid (" + current.error().pointer +
                                                     ": " + current.error().message +
                                                     "); reset it with {\"value\": null} first"));
        }
    }
    if (Expected<void, ImportSettingsError> merged = mergeImportSettings(settings, patch->root(), *kind);
        !merged)
    {
        return unexpected(CommandError::invalid("/value" + merged.error().pointer, merged.error().message));
    }
    AssetImportSettingsResult result;
    result.path = params.path;
    result.value.text = importSettingsValue(settings, *kind);
    const std::string text = writeImportSettings(settings, *kind);
    if (text.empty())
    {
        // Every setting is a default: no sidecar.
        if (*before)
        {
            if (Expected<void, CommandError> deleted = context.edit().deleteFile(sidecar); !deleted)
            {
                return unexpected(std::move(deleted).error());
            }
            result.changed = true;
        }
        return result;
    }
    result.sidecar = sidecar;
    if (*before && **before == text)
    {
        return result;
    }
    if (Expected<bool, CommandError> written = context.edit().writeFile(sidecar, text); !written)
    {
        return unexpected(std::move(written).error());
    }
    result.changed = true;
    return result;
}

Expected<AssetReimportResult, CommandError> assetReimport(CommandContext& context,
                                                          const FilePathParams& params)
{
    Expected<ImportSettingsKind, CommandError> kind = importedAsset(context, params.path, true);
    if (!kind)
    {
        return unexpected(std::move(kind).error());
    }
    AssetReimportResult result;
    if (*kind != ImportSettingsKind::None)
    {
        result.assets.push_back(params.path);
    }
    else
    {
        std::vector<DirectoryEntry> entries;
        if (const FileError error = context.editor().files().listRecursive(params.path, entries);
            error != FileError::None)
        {
            return unexpected(CommandError::make(ErrorKind::IoError, "cannot list '" + params.path + "' (" +
                                                                         std::string(fileErrorName(error)) +
                                                                         ")"));
        }
        for (const DirectoryEntry& entry : entries)
        {
            std::string path = joinPath(params.path, entry.name);
            if (!entry.isDirectory && importSettingsKindOf(path) != ImportSettingsKind::None)
            {
                result.assets.push_back(std::move(path));
            }
        }
        std::sort(result.assets.begin(), result.assets.end());
    }
    for (const std::string& path : result.assets)
    {
        context.editor().requestReimport(path);
    }
    return result;
}

} // namespace

void registerAssetCommands(CommandRegistry& registry)
{
    registry.add<&assetList>({
        .name = "asset.list",
        .title = strings::kCommandAssetListTitle,
        .description = strings::kCommandAssetListDescription,
        .agentSummary = "Lists the assets and folders in an assets: folder with their kinds, optionally "
                        "recursive or of one kind. Example: {\"path\": \"assets:/\", \"recursive\": true, "
                        "\"kind\": \"Material\"}.",
        .category = "Asset",
        .icon = "folder_open",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&assetInfo>({
        .name = "asset.info",
        .title = strings::kCommandAssetInfoTitle,
        .description = strings::kCommandAssetInfoDescription,
        .agentSummary = "Returns an asset's kind, size and how many references it has and makes. Example: "
                        "{\"path\": \"assets:/materials/red.material\"}.",
        .category = "Asset",
        .icon = "info",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&assetCreateFolder>({
        .name = "asset.create_folder",
        .title = strings::kCommandAssetCreateFolderTitle,
        .description = strings::kCommandAssetCreateFolderDescription,
        .agentSummary = "Creates a folder (and missing parents) in assets:. Example: {\"path\": "
                        "\"assets:/materials\"}.",
        .category = "Asset",
        .icon = "create_new_folder",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
        .palette = true,
    });
    registry.add<&assetMove>({
        .name = "asset.move",
        .title = strings::kCommandAssetMoveTitle,
        .description = strings::kCommandAssetMoveDescription,
        .agentSummary =
            "Moves or renames an asset or folder and updates every reference to it in scenes, materials and "
            "the open scene, as one undo step. Example: {\"from\": \"assets:/red.material\", \"to\": "
            "\"assets:/materials/red.material\"}.",
        .category = "Asset",
        .icon = "drive_file_move",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&assetDelete>({
        .name = "asset.delete",
        .title = strings::kCommandAssetDeleteTitle,
        .description = strings::kCommandAssetDeleteDescription,
        .agentSummary = "Deletes an asset or folder unless something refers to it (force deletes anyway and "
                        "returns the dangling references); undo brings it back. Example: {\"path\": "
                        "\"assets:/old.material\"}.",
        .category = "Asset",
        .icon = "delete",
        .flags = {.destructive = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&assetReferences>({
        .name = "asset.references",
        .title = strings::kCommandAssetReferencesTitle,
        .description = strings::kCommandAssetReferencesDescription,
        .agentSummary =
            "Lists the references to an asset (or to anything in a folder) in scenes, materials and "
            "the open scene, with the entity, Component and field of each. Example: {\"path\": "
            "\"assets:/materials/red.material\"}.",
        .category = "Asset",
        .icon = "link",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&assetFindMissing>({
        .name = "asset.find_missing",
        .title = strings::kCommandAssetFindMissingTitle,
        .description = strings::kCommandAssetFindMissingDescription,
        .agentSummary =
            "Lists every reference to an assets: path that does not exist (in scenes, materials, "
            "settings, the open scene, the manifest and glTF buffers and images), with where it is. "
            "Example: {}.",
        .category = "Asset",
        .icon = "link_off",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
        .palette = true,
    });
    registry.add<&assetSetImportSettings>({
        .name = "asset.set_import_settings",
        .title = strings::kCommandAssetSetImportSettingsTitle,
        .description = strings::kCommandAssetSetImportSettingsDescription,
        .agentSummary =
            "Merges import settings into a texture's or model's <file>.import.json sidecar (written only "
            "while a setting differs from its default, deleted when all are defaults; null resets) and "
            "reloads it. Texture: colorSpace auto|srgb|linear, generateMips, maxSize (0 = none). Model: "
            "scale. "
            "Example: {\"path\": \"assets:/textures/ui.png\", \"value\": {\"texture\": {\"colorSpace\": "
            "\"linear\", \"maxSize\": 512}}}.",
        .category = "Asset",
        .icon = "tune",
        .flags = {.idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&assetReimport>({
        .name = "asset.reimport",
        .title = strings::kCommandAssetReimportTitle,
        .description = strings::kCommandAssetReimportDescription,
        .agentSummary = "Imports a texture or model (or every one in a folder) again on the next frame, "
                        "ignoring the import cache even if nothing changed; returns the assets. Example: "
                        "{\"path\": \"assets:/models/ship.glb\"}.",
        .category = "Asset",
        .icon = "refresh",
        .flags = {.idempotent = true},
        .mode = CommandMode::Any,
        .palette = true,
    });
    registry.add<&assetDependencies>({
        .name = "asset.dependencies",
        .title = strings::kCommandAssetDependenciesTitle,
        .description = strings::kCommandAssetDependenciesDescription,
        .agentSummary = "Lists the assets a scene or material refers to (for the open scene, its unsaved "
                        "state). Example: {\"path\": \"assets:/scenes/main.scene\"}.",
        .category = "Asset",
        .icon = "account_tree",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail

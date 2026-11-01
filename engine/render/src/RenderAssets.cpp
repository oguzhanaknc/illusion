#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/render/UploadQueue.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

enum class Status : u8
{
    Loading,
    Ready,
    Failed,
};

constexpr std::string_view kProceduralSky = "builtin:/sky/procedural";
constexpr std::string_view kDefaultMaterial = "builtin:/material/default";

// Entries being reloaded keep their status: a Ready entry keeps its GPU data until the new data is on the GPU
// (and keeps it if the reload fails); a Failed one is Loading again until the reload ends.
struct Reload
{
    bool active = false; // a reload is loading
    bool again = false;  // the file changed again while it loaded: reload once more when it ends
};

// A glTF model on the GPU. Only what meshes and materials of the model need is kept after the upload.
struct ModelEntry
{
    Status status = Status::Loading;
    Reload reload;
    std::string file;                          // the glTF file (its id may have no recorded path)
    AssetHandle<ModelData> model;              // until uploaded
    std::shared_ptr<const ModelUpload> upload; // until uploaded
    ModelHandles handles;
    std::vector<std::vector<i32>> submeshMaterials; // material index of every submesh of every mesh
    std::vector<std::pair<std::string, i32>> nodes; // name and mesh index of every node
    std::string error;
};

struct MeshEntry
{
    Status status = Status::Loading;
    u64 model = 0; // ModelEntry key; 0 for built-in meshes
    AssetFragment fragment = AssetFragment::None;
    u32 index = 0;
    std::string node;
    MeshHandle mesh;
    std::vector<MaterialHandle> materials; // the model's material of every submesh
    u32 references = 0;
};

struct TextureEntry
{
    Status status = Status::Loading;
    Reload reload;
    AssetHandle<Image> image; // until uploaded
    TextureHandle handle;
    AssetId asset;
    bool srgb = true;
};

struct MaterialEntry
{
    Status status = Status::Loading;
    Reload reload;
    u64 model = 0; // ModelEntry key of a `#material/N` reference
    u32 index = 0;
    bool fromFile = false;           // a .material file (owns its GPU material)
    AssetHandle<MaterialAsset> file; // until created
    bool texturesRequested = false;
    std::array<u64, 5> textures{}; // TextureEntry keys; 0 for none
    MaterialDesc desc;             // of a .material file, kept for updateMaterial()
    MaterialHandle handle;
    u32 references = 0;
};

struct EnvironmentEntry
{
    Status status = Status::Loading;
    Reload reload;
    AssetId asset;
    AssetHandle<CookedAsset> cooked;      // the bake cache lookup, until answered
    AssetHandle<Image> image;             // after a cache miss, until baked
    std::vector<AssetDependency> sources; // of a miss, recorded in the bake stored after the readback
    EnvironmentHandle handle;
    u32 references = 0;
};

// A bake being read back from the GPU to be stored in the bake cache.
struct EnvironmentStore
{
    u64 ticket = 0;
    AssetId asset;
    std::string importKey;
    std::vector<AssetDependency> sources;
};

// Bump when the baker's output changes for the same image and EnvironmentDesc (shaders, layout).
constexpr u32 kEnvironmentBakeVersion = 1;

bool endsWithIgnoreCase(std::string_view text, std::string_view suffix) noexcept
{
    if (text.size() < suffix.size())
    {
        return false;
    }
    for (usize i = 0; i < suffix.size(); ++i)
    {
        char c = text[text.size() - suffix.size() + i];
        c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        if (c != suffix[i])
        {
            return false;
        }
    }
    return true;
}

// Removes the keys of entries that are no longer loading; @p advance returns false while one still loads.
template <class Advance>
void advanceAll(std::vector<u64>& loading, Advance advance)
{
    for (usize i = 0; i < loading.size();)
    {
        if (advance(loading[i]))
        {
            loading[i] = loading.back();
            loading.pop_back();
        }
        else
        {
            ++i;
        }
    }
}

// The texture field of @p desc for texture @p slot of materialTextureUses().
TextureHandle& materialTexture(MaterialDesc& desc, usize slot) noexcept
{
    switch (slot)
    {
    case 0:
        return desc.baseColor;
    case 1:
        return desc.metallicRoughness;
    case 2:
        return desc.normal;
    case 3:
        return desc.occlusion;
    default:
        return desc.emissive;
    }
}

u64 skyKey(AssetId environment, const ProceduralSky* sky) noexcept
{
    const u64 parameters =
        sky != nullptr ? fnv1a64(std::as_bytes(std::span<const ProceduralSky, 1>(sky, 1))) : u64{0};
    const u64 key = hashCombine(environment.value, parameters);
    return key == 0 ? 1 : key;
}

} // namespace

struct RenderAssetCache::Impl
{
    Impl(RenderResources& gpuResources, AssetStreamer& assetStreamer, const RenderAssetCacheDesc& cacheDesc)
        : resources(&gpuResources)
        , streamer(&assetStreamer)
        , desc(cacheDesc)
        , uploads(gpuResources)
    {
        reloaded = streamer->events().subscribe<AssetReloadedEvent>(
            [this](const AssetReloadedEvent& event) { reloads.emplace_back(event.asset, event.mode); });
        // The bake cache key holds the EnvironmentDesc the baker actually uses.
        bakeKey = std::format("envbake:{}:{}:{}:{}", kEnvironmentBakeVersion,
                              std::bit_floor(std::max(desc.environment.cubeSize, 1u)),
                              std::bit_floor(std::max(desc.environment.specularSize, 1u)),
                              std::max(desc.environment.sampleCount, 1u));
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    RenderResources* resources;
    AssetStreamer* streamer;
    RenderAssetCacheDesc desc;
    UploadQueue uploads;
    std::unordered_map<u64, ModelEntry> models;
    std::unordered_map<u64, MeshEntry> meshes;
    std::unordered_map<u64, TextureEntry> textures;
    std::unordered_map<u64, MaterialEntry> materials;
    std::unordered_map<u64, EnvironmentEntry> environments;
    std::unordered_map<u64, std::string> errors; // by asset id
    std::vector<u64> loadingModels;
    std::vector<u64> loadingTextures;
    std::vector<u64> loadingMeshes;
    std::vector<u64> loadingMaterials;
    std::vector<u64> loadingEnvironments;
    std::vector<std::pair<AssetId, AssetReloadMode>> reloads; // files reloaded since the last update()
    Subscription reloaded;
    std::string bakeKey; // import key of baked environments in the streamer's cache
    std::vector<EnvironmentStore> stores;
    u32 bakeCacheHits = 0;
    u32 bakes = 0;
    // Increments when handles that resolvers wrote into RenderMesh or RenderSky change, or when an asset
    // that failed is loading again: resolvers then resolve every renderer again.
    u64 epoch = 0;
    u32 reloadsDone = 0;
    u32 evicted = 0;

    void fail(Status& status, AssetId asset, std::string message)
    {
        status = Status::Failed;
        ILL_LOG_WARN("Render", "cannot load asset '{}': {}", AssetPathTable::instance().path(asset), message);
        errors.insert_or_assign(asset.value, std::move(message));
    }

    // Models ---------------------------------------------------------------------------------------------

    u64 requestModel(std::string_view file)
    {
        const u64 key = AssetId::fromPath(file).value;
        const auto [entry, inserted] = models.try_emplace(key);
        if (inserted)
        {
            entry->second.file = file;
            entry->second.model = streamer->loadModel(file);
            entry->second.upload = uploads.upload(entry->second.model);
            loadingModels.push_back(key);
        }
        return key;
    }

    // A load that ended without data: a reload of a Ready entry keeps the previous version.
    void failReload(Status& status, bool replacing, AssetId asset, std::string message)
    {
        if (!replacing)
        {
            fail(status, asset, std::move(message));
            return;
        }
        ILL_LOG_WARN("Render", "cannot reload asset '{}', keeping the previous version: {}",
                     AssetPathTable::instance().path(asset), message);
        errors.insert_or_assign(asset.value, std::move(message));
    }

    bool advanceModel(u64 key)
    {
        ModelEntry& entry = models.at(key);
        const bool replacing = entry.reload.active && entry.status == Status::Ready;
        if (entry.upload->failed)
        {
            failReload(entry.status, replacing, AssetId{key},
                       entry.model.error().empty() ? std::string("the upload failed") : entry.model.error());
            if (!replacing)
            {
                entry.error = errors.at(key);
            }
        }
        else if (entry.upload->uploaded)
        {
            const ModelData& model = entry.model.value();
            ModelHandles previous = std::exchange(entry.handles, entry.upload->handles);
            entry.submeshMaterials.assign(model.meshes.size(), {});
            for (usize m = 0; m < model.meshes.size(); ++m)
            {
                for (const Submesh& submesh : model.meshes[m].submeshes)
                {
                    entry.submeshMaterials[m].push_back(submesh.material);
                }
            }
            entry.nodes.clear();
            for (const ModelNode& node : model.nodes)
            {
                entry.nodes.emplace_back(node.name, node.mesh);
            }
            entry.status = Status::Ready;
            entry.error.clear();
            errors.erase(key);
            if (replacing)
            {
                refreshModelUsers(key);
                resources->releaseModel(previous);
            }
        }
        else
        {
            return false;
        }
        entry.model = {};
        entry.upload.reset();
        if (entry.reload.active)
        {
            entry.reload.active = false;
            ++reloadsDone;
            ++epoch; // new mesh and material handles, or a model that failed before
            if (std::exchange(entry.reload.again, false))
            {
                reloads.emplace_back(AssetId{key}, AssetReloadMode::IfChanged);
            }
        }
        return true;
    }

    // Points the mesh and material entries of a reloaded model at its new handles.
    void refreshModelUsers(u64 key)
    {
        for (auto& [meshKey, mesh] : meshes)
        {
            if (mesh.model == key && mesh.status != Status::Loading)
            {
                mesh.status = Status::Loading;
                mesh.mesh = {};
                mesh.materials.clear();
                advanceMesh(meshKey); // the model is ready, so this ends now
            }
        }
        for (auto& [materialKey, material] : materials)
        {
            if (material.model == key && material.status != Status::Loading)
            {
                material.status = Status::Loading;
                advanceMaterial(materialKey);
            }
        }
    }

    // Meshes ---------------------------------------------------------------------------------------------

    MeshEntry& requestMesh(AssetId id)
    {
        const auto [found, inserted] = meshes.try_emplace(id.value);
        MeshEntry& entry = found->second;
        if (!inserted)
        {
            return entry;
        }
        const std::string_view path = AssetPathTable::instance().path(id);
        const std::optional<AssetPathParts> parts = splitAssetPath(path);
        if (!parts)
        {
            fail(entry.status, id, "the asset id has no recorded path");
        }
        else if (isOnBuiltinMount(parts->file))
        {
            std::optional<MeshData> data =
                parts->fragment == AssetFragment::None ? makeBuiltinMesh(parts->file) : std::nullopt;
            if (!data)
            {
                fail(entry.status, id, "not a built-in mesh (see builtinAssetPaths())");
            }
            else
            {
                entry.mesh = resources->createMesh(*data, std::string(path).c_str());
                entry.materials.assign(data->submeshes.size(), resources->defaultMaterial());
                entry.status = Status::Ready;
            }
        }
        else if (parts->fragment == AssetFragment::Material)
        {
            fail(entry.status, id, "refers to a material, not a mesh");
        }
        else
        {
            entry.model = requestModel(parts->file);
            entry.fragment = parts->fragment;
            entry.index = parts->index;
            entry.node = parts->node;
            if (!advanceMesh(id.value))
            {
                loadingMeshes.push_back(id.value);
            }
        }
        return entry;
    }

    bool advanceMesh(u64 key)
    {
        MeshEntry& entry = meshes.at(key);
        const ModelEntry& model = models.at(entry.model);
        if (model.status == Status::Loading)
        {
            return false;
        }
        if (model.status == Status::Failed)
        {
            fail(entry.status, AssetId{key}, model.error);
            return true;
        }
        i32 mesh = static_cast<i32>(entry.index);
        if (entry.fragment == AssetFragment::Node)
        {
            mesh = -1;
            for (const auto& [name, nodeMesh] : model.nodes)
            {
                if (name == entry.node)
                {
                    mesh = nodeMesh;
                    break;
                }
            }
            if (mesh < 0)
            {
                fail(entry.status, AssetId{key},
                     std::format("the model has no node '{}' with a mesh", entry.node));
                return true;
            }
        }
        const auto index = static_cast<usize>(mesh);
        if (index >= model.handles.meshes.size() || model.handles.meshes[index].index == 0)
        {
            fail(entry.status, AssetId{key}, std::format("the model has no mesh {} with triangles", index));
            return true;
        }
        entry.mesh = model.handles.meshes[index];
        for (const i32 material : model.submeshMaterials[index])
        {
            entry.materials.push_back(material >= 0 &&
                                              static_cast<usize>(material) < model.handles.materials.size()
                                          ? model.handles.materials[static_cast<usize>(material)]
                                          : resources->defaultMaterial());
        }
        entry.status = Status::Ready;
        return true;
    }

    // Textures and materials -----------------------------------------------------------------------------

    u64 requestTexture(AssetId id, bool srgb)
    {
        const u64 key = hashCombine(id.value, srgb ? u64{1} : u64{2});
        const auto [found, inserted] = textures.try_emplace(key);
        if (inserted)
        {
            TextureEntry& entry = found->second;
            entry.asset = id;
            entry.srgb = srgb;
            entry.image = streamer->loadImage(AssetPathTable::instance().path(id), {.srgb = srgb});
            loadingTextures.push_back(key);
        }
        return key;
    }

    bool advanceTexture(u64 key)
    {
        TextureEntry& entry = textures.at(key);
        if (entry.image.state() == AssetState::Loading)
        {
            return false;
        }
        const bool replacing = entry.reload.active && entry.status == Status::Ready;
        if (entry.image.ready())
        {
            const TextureHandle texture = resources->createTexture(
                entry.image.value(), std::string(AssetPathTable::instance().path(entry.asset)).c_str());
            const TextureHandle previous = std::exchange(entry.handle, texture);
            entry.status = Status::Ready;
            errors.erase(entry.asset.value);
            if (replacing)
            {
                // Materials made from files that use it switch to the new texture, then the old one goes.
                for (auto& [materialKey, material] : materials)
                {
                    if (!material.fromFile || material.status != Status::Ready)
                    {
                        continue;
                    }
                    bool uses = false;
                    for (usize t = 0; t < material.textures.size(); ++t)
                    {
                        if (material.textures[t] == key)
                        {
                            materialTexture(material.desc, t) = texture;
                            uses = true;
                        }
                    }
                    if (uses)
                    {
                        resources->updateMaterial(material.handle, material.desc);
                    }
                }
                resources->releaseTexture(previous);
            }
        }
        else
        {
            failReload(entry.status, replacing, entry.asset, entry.image.error());
        }
        entry.image = {};
        if (entry.reload.active)
        {
            entry.reload.active = false;
            ++reloadsDone;
            if (std::exchange(entry.reload.again, false))
            {
                reloads.emplace_back(entry.asset, AssetReloadMode::IfChanged);
            }
        }
        return true;
    }

    MaterialEntry& requestMaterial(AssetId id)
    {
        const auto [found, inserted] = materials.try_emplace(id.value);
        MaterialEntry& entry = found->second;
        if (!inserted)
        {
            return entry;
        }
        const std::string_view path = AssetPathTable::instance().path(id);
        const std::optional<AssetPathParts> parts = splitAssetPath(path);
        if (!parts)
        {
            fail(entry.status, id, "the asset id has no recorded path");
        }
        else if (isOnBuiltinMount(parts->file))
        {
            if (path == kDefaultMaterial)
            {
                entry.handle = resources->defaultMaterial();
                entry.status = Status::Ready;
            }
            else
            {
                fail(entry.status, id, "not a built-in material (see builtinAssetPaths())");
            }
        }
        else if (parts->fragment == AssetFragment::Material)
        {
            entry.model = requestModel(parts->file);
            entry.index = parts->index;
            loadingMaterials.push_back(id.value);
        }
        else if (parts->fragment == AssetFragment::None &&
                 endsWithIgnoreCase(parts->file, kMaterialAssetExtension))
        {
            entry.fromFile = true;
            entry.file =
                streamer->loadFile<MaterialAsset>(parts->file, kMaterialDecoderName, materialAssetDecoder());
            loadingMaterials.push_back(id.value);
        }
        else
        {
            fail(entry.status, id,
                 "not a material (a .material file, model#material/N or builtin:/material/default)");
        }
        return entry;
    }

    bool advanceMaterial(u64 key)
    {
        MaterialEntry& entry = materials.at(key);
        const AssetId id{key};
        if (entry.model != 0)
        {
            const ModelEntry& model = models.at(entry.model);
            if (model.status == Status::Loading)
            {
                return false;
            }
            if (model.status == Status::Failed)
            {
                fail(entry.status, id, model.error);
            }
            else if (entry.index >= model.handles.materials.size())
            {
                fail(entry.status, id, std::format("the model has no material {}", entry.index));
            }
            else
            {
                entry.handle = model.handles.materials[entry.index];
                entry.status = Status::Ready;
            }
            return true;
        }
        if (entry.file.state() == AssetState::Loading)
        {
            return false;
        }
        const bool replacing = entry.reload.active && entry.status == Status::Ready;
        if (!entry.file.ready())
        {
            failReload(entry.status, replacing, id, entry.file.error());
            entry.file = {};
            finishMaterialReload(key, replacing);
            return true;
        }
        const MaterialAsset& asset = entry.file.value();
        const std::array<MaterialTextureUse, 5> uses = materialTextureUses(asset.textures);
        if (!entry.texturesRequested)
        {
            for (usize t = 0; t < uses.size(); ++t)
            {
                entry.textures[t] = uses[t].texture ? requestTexture(uses[t].texture.id, uses[t].srgb) : 0;
            }
            entry.texturesRequested = true;
        }
        std::array<TextureHandle, 5> handles{};
        for (usize t = 0; t < uses.size(); ++t)
        {
            if (entry.textures[t] == 0)
            {
                continue;
            }
            const TextureEntry& texture = textures.at(entry.textures[t]);
            if (texture.status == Status::Loading)
            {
                return false;
            }
            if (texture.status == Status::Failed)
            {
                failReload(entry.status, replacing, id,
                           std::format("texture '{}' failed to load",
                                       AssetPathTable::instance().path(texture.asset)));
                entry.file = {};
                finishMaterialReload(key, replacing);
                return true;
            }
            handles[t] = texture.handle;
        }
        MaterialDesc material;
        material.parameters = asset.material;
        for (usize t = 0; t < handles.size(); ++t)
        {
            materialTexture(material, t) = handles[t];
        }
        if (replacing)
        {
            resources->updateMaterial(entry.handle, material); // the handle stays: nothing to resolve again
        }
        else
        {
            entry.handle = resources->createMaterial(material);
        }
        entry.desc = material;
        entry.status = Status::Ready;
        errors.erase(key);
        entry.file = {};
        finishMaterialReload(key, replacing);
        return true;
    }

    void finishMaterialReload(u64 key, bool replacing)
    {
        MaterialEntry& entry = materials.at(key);
        if (!entry.reload.active)
        {
            return;
        }
        entry.reload.active = false;
        ++reloadsDone;
        epoch += replacing ? 0u : 1u; // a material that failed before resolves to a new handle
        if (std::exchange(entry.reload.again, false))
        {
            reloads.emplace_back(AssetId{key}, AssetReloadMode::IfChanged);
        }
    }

    // Environments ---------------------------------------------------------------------------------------

    std::pair<u64, EnvironmentEntry*> requestEnvironment(AssetId id, const ProceduralSky* sky)
    {
        const std::string_view path = AssetPathTable::instance().path(id);
        const bool procedural = path == kProceduralSky;
        const u64 key = procedural ? skyKey(id, sky) : id.value;
        const auto [found, inserted] = environments.try_emplace(key);
        EnvironmentEntry& entry = found->second;
        if (!inserted)
        {
            return {key, &entry};
        }
        entry.asset = id;
        const std::optional<AssetPathParts> parts = splitAssetPath(path);
        if (procedural)
        {
            const ProceduralSky parameters = sky != nullptr ? *sky : ProceduralSky{};
            const Image image = makeSkyImage(std::max(parameters.resolution, 8u), parameters.desc());
            entry.handle = resources->createEnvironment(image, desc.environment, "Procedural sky");
            entry.status = Status::Ready;
        }
        else if (!parts || isOnBuiltinMount(parts->file) || parts->fragment != AssetFragment::None)
        {
            fail(entry.status, id, "not an environment (an image file or builtin:/sky/procedural)");
        }
        else
        {
            entry.cooked =
                streamer->loadCooked(parts->file, bakeKey, AssetType::Environment, kEnvironmentFormatVersion);
            loadingEnvironments.push_back(key);
        }
        return {key, &entry};
    }

    // The bake cache answered: a hit becomes the environment, a miss loads the image to bake.
    void answerBakeCache(EnvironmentEntry& entry, EnvironmentHandle& created)
    {
        if (entry.cooked.ready() && entry.cooked.value().file)
        {
            if (std::optional<BakedEnvironment> baked =
                    deserializeEnvironment(entry.cooked.value().file->payload))
            {
                created = resources->createEnvironment(
                    *baked, std::string(AssetPathTable::instance().path(entry.asset)).c_str());
                ++bakeCacheHits;
                entry.cooked = {};
                return;
            }
        }
        if (entry.cooked.ready())
        {
            entry.sources = entry.cooked.value().sources;
        }
        entry.cooked = {};
        entry.image = streamer->loadImage(AssetPathTable::instance().path(entry.asset),
                                          {.srgb = true, .generateMips = false});
    }

    // Bakes `entry.image` and, when the bake cache missed, starts reading the bake back to store it.
    EnvironmentHandle bake(EnvironmentEntry& entry)
    {
        const EnvironmentHandle handle =
            resources->createEnvironment(entry.image.value(), desc.environment,
                                         std::string(AssetPathTable::instance().path(entry.asset)).c_str());
        ++bakes;
        if (!entry.sources.empty())
        {
            if (const u64 ticket = resources->requestEnvironmentReadback(handle); ticket != 0)
            {
                stores.push_back(EnvironmentStore{ticket, entry.asset, bakeKey, std::move(entry.sources)});
            }
        }
        entry.sources.clear();
        return handle;
    }

    // Writes the bakes whose readbacks finished into the bake cache (all of them with `block`).
    void storeBakes(bool block)
    {
        std::erase_if(stores, [&](EnvironmentStore& store) {
            BakedEnvironment baked;
            const ReadbackStatus status = resources->readEnvironment(store.ticket, baked, block);
            if (status == ReadbackStatus::Pending)
            {
                return false;
            }
            if (status == ReadbackStatus::Ready)
            {
                AssetFile file;
                file.type = AssetType::Environment;
                file.version = kEnvironmentFormatVersion;
                file.dependencies = std::move(store.sources);
                file.payload = serializeEnvironment(baked);
                streamer->storeCooked(AssetPathTable::instance().path(store.asset), store.importKey, file);
            }
            return true;
        });
    }

    bool advanceEnvironment(u64 key)
    {
        EnvironmentEntry& entry = environments.at(key);
        EnvironmentHandle created;
        if (entry.cooked)
        {
            if (entry.cooked.state() == AssetState::Loading)
            {
                return false;
            }
            answerBakeCache(entry, created);
            if (created.index == 0)
            {
                return false; // the image is loading
            }
        }
        else if (entry.image.state() == AssetState::Loading)
        {
            return false;
        }
        const bool replacing = entry.reload.active && entry.status == Status::Ready;
        if (created.index != 0 || entry.image.ready())
        {
            if (created.index == 0)
            {
                created = bake(entry);
            }
            const EnvironmentHandle previous = std::exchange(entry.handle, created);
            entry.status = Status::Ready;
            errors.erase(entry.asset.value);
            if (replacing)
            {
                resources->releaseEnvironment(previous);
            }
        }
        else
        {
            failReload(entry.status, replacing, entry.asset, entry.image.error());
        }
        entry.image = {};
        if (entry.reload.active)
        {
            entry.reload.active = false;
            ++reloadsDone;
            ++epoch; // a new handle, or an environment that failed before
            if (std::exchange(entry.reload.again, false))
            {
                reloads.emplace_back(entry.asset, AssetReloadMode::IfChanged);
            }
        }
        return true;
    }

    // Reloads -------------------------------------------------------------------------------------------

    // Starts loading @p entry's file again; returns false when a load is already running (it reloads once
    // more when that ends). A Failed entry becomes Loading.
    template <class Entry>
    static bool beginReload(Entry& entry)
    {
        if (entry.status == Status::Loading || entry.reload.active)
        {
            entry.reload.again = true;
            return false;
        }
        entry.reload.active = true;
        if (entry.status == Status::Failed)
        {
            entry.status = Status::Loading;
        }
        return true;
    }

    void reloadModel(u64 key)
    {
        ModelEntry& entry = models.at(key);
        const bool failed = entry.status == Status::Failed;
        if (!beginReload(entry))
        {
            return;
        }
        entry.model = streamer->loadModel(entry.file);
        entry.upload = uploads.upload(entry.model);
        loadingModels.push_back(key);
        if (!failed)
        {
            return;
        }
        // Meshes and materials that failed with the model wait for it again.
        for (auto& [meshKey, mesh] : meshes)
        {
            if (mesh.model == key && mesh.status == Status::Failed)
            {
                mesh.status = Status::Loading;
                mesh.materials.clear();
                loadingMeshes.push_back(meshKey);
            }
        }
        for (auto& [materialKey, material] : materials)
        {
            if (material.model == key && material.status == Status::Failed)
            {
                material.status = Status::Loading;
                loadingMaterials.push_back(materialKey);
            }
        }
    }

    void reloadTexture(u64 key)
    {
        TextureEntry& entry = textures.at(key);
        const bool failed = entry.status == Status::Failed;
        if (!beginReload(entry))
        {
            return;
        }
        entry.image = streamer->loadImage(AssetPathTable::instance().path(entry.asset), {.srgb = entry.srgb});
        loadingTextures.push_back(key);
        if (!failed)
        {
            return;
        }
        // Material files that failed because of this texture load again.
        for (auto& [materialKey, material] : materials)
        {
            if (material.fromFile && material.status == Status::Failed &&
                std::find(material.textures.begin(), material.textures.end(), key) != material.textures.end())
            {
                reloadMaterial(materialKey);
            }
        }
    }

    void reloadMaterial(u64 key)
    {
        MaterialEntry& entry = materials.at(key);
        if (!entry.fromFile || !beginReload(entry))
        {
            return;
        }
        entry.file = streamer->loadFile<MaterialAsset>(AssetPathTable::instance().path(AssetId{key}),
                                                       kMaterialDecoderName, materialAssetDecoder());
        entry.texturesRequested = false;
        loadingMaterials.push_back(key);
    }

    void reloadEnvironment(u64 key, AssetReloadMode mode)
    {
        EnvironmentEntry& entry = environments.at(key);
        if (!beginReload(entry))
        {
            return;
        }
        // Reimport bakes again (and replaces the cache entry) even when the image did not change.
        entry.cooked = streamer->loadCooked(AssetPathTable::instance().path(entry.asset), bakeKey,
                                            AssetType::Environment, kEnvironmentFormatVersion, mode);
        loadingEnvironments.push_back(key);
    }

    // Starts reloading every entry made from a file in `reloads`.
    void startReloads()
    {
        ILL_PROFILE_SCOPE();
        std::vector<std::pair<AssetId, AssetReloadMode>> files;
        files.swap(reloads);
        std::vector<u64> keys;
        for (const auto& [file, mode] : files)
        {
            if (models.contains(file.value))
            {
                reloadModel(file.value);
            }
            keys.clear();
            for (const auto& [key, texture] : textures)
            {
                if (texture.asset == file)
                {
                    keys.push_back(key);
                }
            }
            for (const u64 key : keys)
            {
                reloadTexture(key);
            }
            if (materials.contains(file.value))
            {
                reloadMaterial(file.value);
            }
            // Procedural skies are keyed by their parameters, so a file's environment has the file's id.
            if (environments.contains(file.value))
            {
                reloadEnvironment(file.value, mode);
            }
        }
    }

    // Eviction -------------------------------------------------------------------------------------------

    template <class Entry>
    static bool busy(const Entry& entry) noexcept
    {
        return entry.status == Status::Loading || entry.reload.active;
    }

    u32 evictUnused()
    {
        ILL_PROFILE_SCOPE();
        u32 count = 0;
        std::erase_if(meshes, [&](auto& item) {
            MeshEntry& mesh = item.second;
            if (mesh.references != 0 || mesh.status == Status::Loading)
            {
                return false;
            }
            if (mesh.model == 0 && mesh.status == Status::Ready)
            {
                resources->releaseMesh(mesh.mesh); // built-in meshes own theirs; models own the others
            }
            ++count;
            return true;
        });
        std::erase_if(materials, [&](auto& item) {
            MaterialEntry& material = item.second;
            if (material.references != 0 || busy(material))
            {
                return false;
            }
            if (material.fromFile && material.status == Status::Ready)
            {
                resources->releaseMaterial(material.handle);
            }
            ++count;
            return true;
        });
        std::unordered_set<u64> used;
        for (const auto& [key, mesh] : meshes)
        {
            used.insert(mesh.model);
        }
        for (const auto& [key, material] : materials)
        {
            used.insert(material.model);
        }
        std::erase_if(models, [&](auto& item) {
            ModelEntry& model = item.second;
            if (used.contains(item.first) || busy(model))
            {
                return false;
            }
            if (model.status == Status::Ready)
            {
                resources->releaseModel(model.handles);
            }
            ++count;
            return true;
        });
        used.clear();
        for (const auto& [key, material] : materials)
        {
            used.insert(material.textures.begin(), material.textures.end());
        }
        std::erase_if(textures, [&](auto& item) {
            TextureEntry& texture = item.second;
            if (used.contains(item.first) || busy(texture))
            {
                return false;
            }
            if (texture.status == Status::Ready)
            {
                resources->releaseTexture(texture.handle);
            }
            ++count;
            return true;
        });
        std::erase_if(environments, [&](auto& item) {
            EnvironmentEntry& environment = item.second;
            if (environment.references != 0 || busy(environment))
            {
                return false;
            }
            if (environment.status == Status::Ready)
            {
                resources->releaseEnvironment(environment.handle);
            }
            ++count;
            return true;
        });
        evicted += count;
        // RenderMesh and RenderSky Components written by resolvers that are gone may hold freed handles.
        epoch += count > 0 ? 1u : 0u;
        return count;
    }

    [[nodiscard]] u32 loading() const noexcept
    {
        return static_cast<u32>(loadingModels.size() + loadingTextures.size() + loadingMeshes.size() +
                                loadingMaterials.size() + loadingEnvironments.size() + reloads.size());
    }

    void update()
    {
        ILL_PROFILE_SCOPE_N("RenderAssetCache::update");
        resources->collect();
        if (!stores.empty())
        {
            storeBakes(false);
        }
        if (!reloads.empty())
        {
            startReloads();
        }
        if (loading() == 0)
        {
            return;
        }
        uploads.process(desc.uploadBytesPerUpdate);
        advanceAll(loadingModels, [this](u64 key) { return advanceModel(key); });
        advanceAll(loadingTextures, [this](u64 key) { return advanceTexture(key); });
        advanceAll(loadingMeshes, [this](u64 key) { return advanceMesh(key); });
        advanceAll(loadingMaterials, [this](u64 key) { return advanceMaterial(key); });
        advanceAll(loadingEnvironments, [this](u64 key) { return advanceEnvironment(key); });
    }
};

RenderAssetCache::RenderAssetCache(RenderResources& resources, AssetStreamer& streamer,
                                   const RenderAssetCacheDesc& desc)
    : m_impl(std::make_unique<Impl>(resources, streamer, desc))
{
}

RenderAssetCache::~RenderAssetCache() = default;

void RenderAssetCache::update()
{
    m_impl->update();
}

u32 RenderAssetCache::evictUnused()
{
    return m_impl->evictUnused();
}

void RenderAssetCache::waitIdle()
{
    // Every step starts reloads, publishes loads or uploads at least one model, so this ends.
    while (m_impl->loading() > 0)
    {
        m_impl->streamer->waitIdle();
        m_impl->update();
    }
    m_impl->storeBakes(true); // queued for writing; the streamer writes them before it stops
}

u32 RenderAssetCache::loading() const noexcept
{
    return m_impl->loading();
}

RenderAssetCacheStatistics RenderAssetCache::statistics() const noexcept
{
    RenderAssetCacheStatistics statistics;
    const auto count = [&statistics](const auto& entries, u32& ready) {
        for (const auto& [key, entry] : entries)
        {
            ready += entry.status == Status::Ready ? 1u : 0u;
            statistics.failed += entry.status == Status::Failed ? 1u : 0u;
        }
    };
    count(m_impl->models, statistics.models);
    count(m_impl->meshes, statistics.meshes);
    count(m_impl->materials, statistics.materials);
    count(m_impl->textures, statistics.textures);
    count(m_impl->environments, statistics.environments);
    statistics.loading = m_impl->loading();
    statistics.reloads = m_impl->reloadsDone;
    statistics.evicted = m_impl->evicted;
    statistics.bakeCacheHits = m_impl->bakeCacheHits;
    statistics.bakes = m_impl->bakes;
    return statistics;
}

u32 RenderAssetCache::references(AssetId asset) const noexcept
{
    u32 references = 0;
    if (const auto mesh = m_impl->meshes.find(asset.value); mesh != m_impl->meshes.end())
    {
        references += mesh->second.references;
    }
    if (const auto material = m_impl->materials.find(asset.value); material != m_impl->materials.end())
    {
        references += material->second.references;
    }
    for (const auto& [key, environment] : m_impl->environments)
    {
        references += environment.asset == asset ? environment.references : 0;
    }
    return references;
}

std::string_view RenderAssetCache::error(AssetId asset) const noexcept
{
    const auto found = m_impl->errors.find(asset.value);
    return found != m_impl->errors.end() ? std::string_view(found->second) : std::string_view{};
}

u64 renderSourceKey(const MeshRenderer& renderer) noexcept
{
    u64 key = hashCombine(renderer.mesh.id.value, renderer.material.id.value);
    key = hashCombine(key, (u64{renderer.submesh} << 32) | renderer.layers);
    key = hashCombine(key, (renderer.castShadows ? 1u : 0u) | (renderer.visible ? 2u : 0u));
    return key == 0 ? 1 : key;
}

struct RenderAssetResolver::Impl
{
    explicit Impl(RenderAssetCache& assetCache)
        : owner(&assetCache)
        , cache(&assetCache.impl())
    {
    }

    ~Impl()
    {
        for (const u64 key : heldMeshes)
        {
            --cache->meshes.at(key).references;
        }
        for (const u64 key : heldMaterials)
        {
            --cache->materials.at(key).references;
        }
        for (const u64 key : heldEnvironments)
        {
            --cache->environments.at(key).references;
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    static void hold(std::unordered_set<u64>& held, u64 key, u32& references)
    {
        if (held.insert(key).second)
        {
            ++references;
        }
    }

    // Brings @p target up to date with @p renderer, or leaves it unchanged while assets load.
    void resolve(const MeshRenderer& renderer, RenderMesh& target)
    {
        const u64 key = renderSourceKey(renderer);
        bool failed = false;
        MeshHandle mesh;
        MaterialHandle material = cache->resources->defaultMaterial();
        if (renderer.mesh)
        {
            MeshEntry& meshEntry = cache->requestMesh(renderer.mesh.id);
            hold(heldMeshes, renderer.mesh.id.value, meshEntry.references);
            if (meshEntry.status == Status::Loading)
            {
                ++statistics.pending;
                return;
            }
            failed = meshEntry.status == Status::Failed;
            mesh = meshEntry.mesh;
            if (!failed && renderer.submesh < meshEntry.materials.size())
            {
                material = meshEntry.materials[renderer.submesh];
            }
        }
        if (renderer.material)
        {
            MaterialEntry& materialEntry = cache->requestMaterial(renderer.material.id);
            hold(heldMaterials, renderer.material.id.value, materialEntry.references);
            if (materialEntry.status == Status::Loading)
            {
                ++statistics.pending;
                return;
            }
            failed = failed || materialEntry.status == Status::Failed;
            material = materialEntry.status == Status::Ready ? materialEntry.handle
                                                             : cache->resources->defaultMaterial();
        }
        target.mesh = mesh;
        target.submesh = renderer.submesh;
        target.material = material;
        target.layers = renderer.layers;
        target.flags = (renderer.visible ? RenderMesh::kVisible : 0u) |
                       (renderer.castShadows ? RenderMesh::kCastShadows : 0u);
        target.sourceKey = key;
        ++statistics.resolved;
        statistics.failures += failed ? 1u : 0u;
    }

    void updateMeshes(World& world, JobSystem* jobs, bool everything)
    {
        // Structural changes first: RenderMesh for new renderers, none for removed ones.
        scratch.clear();
        world.query<const MeshRenderer>().without<RenderMesh, Inactive>().each(
            [this](Entity entity, const MeshRenderer&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world.emplace<RenderMesh>(entity, RenderMesh{});
        }
        scratch.clear();
        world.query<const RenderMesh>().without<MeshRenderer>().each(
            [this](Entity entity, const RenderMesh& mesh) {
                if (mesh.sourceKey != 0)
                {
                    scratch.push_back(entity);
                }
            });
        for (const Entity entity : scratch)
        {
            world.remove<RenderMesh>(entity);
        }

        // Find the renderers whose source changed (in parallel), then resolve them.
        auto query = world.query<const MeshRenderer, const RenderMesh>().without<Inactive>();
        const u32 count = query.count();
        if (count > mismatchCapacity)
        {
            mismatches = std::make_unique<Entity[]>(count);
            mismatchCapacity = count;
        }
        std::atomic<u32> found{0};
        Entity* const out = mismatches.get();
        const auto compare = [out, &found, everything](Entity entity, const MeshRenderer& renderer,
                                                       const RenderMesh& mesh) {
            if (everything || renderSourceKey(renderer) != mesh.sourceKey)
            {
                out[found.fetch_add(1, std::memory_order_relaxed)] = entity;
            }
        };
        if (jobs != nullptr)
        {
            query.parallelEach(*jobs, compare);
        }
        else
        {
            query.each(compare);
        }
        const u32 mismatchCount = found.load(std::memory_order_relaxed);
        for (u32 i = 0; i < mismatchCount; ++i)
        {
            const Entity entity = mismatches[i];
            resolve(world.get<MeshRenderer>(entity), world.get<RenderMesh>(entity));
        }
    }

    void updateSkies(World& world, bool everything)
    {
        scratch.clear();
        world.query<const SkyLight>().without<RenderSky, Inactive>().each(
            [this](Entity entity, const SkyLight& sky) {
                if (sky.environment)
                {
                    scratch.push_back(entity);
                }
            });
        for (const Entity entity : scratch)
        {
            world.emplace<RenderSky>(entity, RenderSky{});
        }
        scratch.clear();
        world.query<const RenderSky>().without<SkyLight>().each([this](Entity entity, const RenderSky& sky) {
            if (sky.sourceKey != 0)
            {
                scratch.push_back(entity);
            }
        });
        world.query<const SkyLight, RenderSky>().without<Inactive>().each(
            [this, &world, everything](Entity entity, const SkyLight& sky, RenderSky& target) {
                if (!sky.environment)
                {
                    if (target.sourceKey != 0)
                    {
                        scratch.push_back(entity); // the environment was cleared: the RenderSky was ours
                    }
                    return;
                }
                const ProceduralSky* procedural = world.tryGet<ProceduralSky>(entity);
                const u64 key = skyKey(sky.environment.id, procedural);
                if (key == target.sourceKey && !everything)
                {
                    return;
                }
                const auto [cacheKey, environment] =
                    cache->requestEnvironment(sky.environment.id, procedural);
                hold(heldEnvironments, cacheKey, environment->references);
                if (environment->status == Status::Loading)
                {
                    ++statistics.pending;
                    return;
                }
                target.environment =
                    environment->status == Status::Ready ? environment->handle : EnvironmentHandle{};
                target.sourceKey = key;
                ++statistics.resolved;
                statistics.failures += environment->status == Status::Failed ? 1u : 0u;
            });
        for (const Entity entity : scratch)
        {
            world.remove<RenderSky>(entity);
        }
    }

    u32 releaseUnused(World& world)
    {
        ILL_PROFILE_SCOPE_N("RenderAssetResolver::releaseUnused");
        std::unordered_set<u64> meshesInUse;
        std::unordered_set<u64> materialsInUse;
        std::unordered_set<u64> environmentsInUse;
        world.query<const MeshRenderer>().each([&](Entity, const MeshRenderer& renderer) {
            meshesInUse.insert(renderer.mesh.id.value);
            materialsInUse.insert(renderer.material.id.value);
        });
        world.query<const SkyLight>().each([&](Entity entity, const SkyLight& sky) {
            if (sky.environment)
            {
                const ProceduralSky* procedural = world.tryGet<ProceduralSky>(entity);
                const bool isProcedural =
                    AssetPathTable::instance().path(sky.environment.id) == kProceduralSky;
                environmentsInUse.insert(isProcedural ? skyKey(sky.environment.id, procedural)
                                                      : sky.environment.id.value);
            }
        });
        u32 released = 0;
        const auto release = [&released](std::unordered_set<u64>& held, const std::unordered_set<u64>& inUse,
                                         auto& entries) {
            std::erase_if(held, [&](u64 key) {
                if (inUse.contains(key))
                {
                    return false;
                }
                --entries.at(key).references;
                ++released;
                return true;
            });
        };
        release(heldMeshes, meshesInUse, cache->meshes);
        release(heldMaterials, materialsInUse, cache->materials);
        release(heldEnvironments, environmentsInUse, cache->environments);
        return released;
    }

    RenderAssetCache* owner;
    RenderAssetCache::Impl* cache;
    u64 epoch = 0; // the cache's epoch at the last update()
    RenderAssetResolverStatistics statistics;
    std::vector<Entity> scratch;
    std::unique_ptr<Entity[]> mismatches;
    u32 mismatchCapacity = 0;
    std::unordered_set<u64> heldMeshes;
    std::unordered_set<u64> heldMaterials;
    std::unordered_set<u64> heldEnvironments;
};

RenderAssetResolver::RenderAssetResolver(RenderAssetCache& cache)
    : m_impl(std::make_unique<Impl>(cache))
{
}

RenderAssetResolver::~RenderAssetResolver() = default;

void RenderAssetResolver::update(World& world, JobSystem* jobs)
{
    ILL_PROFILE_SCOPE_N("RenderAssetResolver::update");
    Impl& impl = *m_impl;
    impl.statistics.resolved = 0;
    impl.statistics.pending = 0;
    // After a reload, handles may be stale and failed assets may load now: resolve everything again.
    const bool everything = impl.epoch != impl.cache->epoch;
    impl.epoch = impl.cache->epoch;
    impl.statistics.reresolves += everything ? 1u : 0u;
    impl.updateMeshes(world, jobs, everything);
    impl.updateSkies(world, everything);
}

u32 RenderAssetResolver::releaseUnused(World& world)
{
    return m_impl->releaseUnused(world);
}

void RenderAssetResolver::waitIdle(World& world, JobSystem* jobs)
{
    update(world, jobs);
    while (m_impl->statistics.pending > 0)
    {
        m_impl->owner->waitIdle();
        update(world, jobs);
    }
}

const RenderAssetResolverStatistics& RenderAssetResolver::statistics() const noexcept
{
    return m_impl->statistics;
}

} // namespace illusion

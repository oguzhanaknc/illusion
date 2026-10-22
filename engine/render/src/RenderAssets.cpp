#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Environment.hpp>
#include <illusion/assets/MaterialAsset.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>
#include <illusion/render/SceneComponents.hpp>
#include <illusion/render/UploadQueue.hpp>

#include <array>
#include <atomic>
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

// A glTF model on the GPU. Only what meshes and materials of the model need is kept after the upload.
struct ModelEntry
{
    Status status = Status::Loading;
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
    AssetHandle<Image> image; // until uploaded
    TextureHandle handle;
    AssetId asset;
};

struct MaterialEntry
{
    Status status = Status::Loading;
    u64 model = 0; // ModelEntry key of a `#material/N` reference
    u32 index = 0;
    AssetHandle<MaterialAsset> file; // a .material file, until created
    bool texturesRequested = false;
    std::array<u64, 5> textures{}; // TextureEntry keys; 0 for none
    MaterialHandle handle;
    u32 references = 0;
};

struct EnvironmentEntry
{
    Status status = Status::Loading;
    AssetId asset;
    AssetHandle<Image> image; // until baked
    EnvironmentHandle handle;
    u32 references = 0;
};

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
    }

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
            entry->second.model = streamer->loadModel(file);
            entry->second.upload = uploads.upload(entry->second.model);
            loadingModels.push_back(key);
        }
        return key;
    }

    bool advanceModel(u64 key)
    {
        ModelEntry& entry = models.at(key);
        if (entry.upload->failed)
        {
            entry.status = Status::Failed;
            entry.error =
                entry.model.error().empty() ? std::string("the upload failed") : entry.model.error();
        }
        else if (entry.upload->uploaded)
        {
            const ModelData& model = entry.model.value();
            entry.handles = entry.upload->handles;
            entry.submeshMaterials.resize(model.meshes.size());
            for (usize m = 0; m < model.meshes.size(); ++m)
            {
                for (const Submesh& submesh : model.meshes[m].submeshes)
                {
                    entry.submeshMaterials[m].push_back(submesh.material);
                }
            }
            for (const ModelNode& node : model.nodes)
            {
                entry.nodes.emplace_back(node.name, node.mesh);
            }
            entry.status = Status::Ready;
        }
        else
        {
            return false;
        }
        entry.model = {};
        entry.upload.reset();
        return true;
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
        if (entry.image.ready())
        {
            entry.handle = resources->createTexture(
                entry.image.value(), std::string(AssetPathTable::instance().path(entry.asset)).c_str());
            entry.status = Status::Ready;
        }
        else
        {
            fail(entry.status, entry.asset, entry.image.error());
        }
        entry.image = {};
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
        if (!entry.file.ready())
        {
            fail(entry.status, id, entry.file.error());
            entry.file = {};
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
                fail(entry.status, id,
                     std::format("texture '{}' failed to load",
                                 AssetPathTable::instance().path(texture.asset)));
                entry.file = {};
                return true;
            }
            handles[t] = texture.handle;
        }
        MaterialDesc material;
        material.parameters = asset.material;
        material.baseColor = handles[0];
        material.metallicRoughness = handles[1];
        material.normal = handles[2];
        material.occlusion = handles[3];
        material.emissive = handles[4];
        entry.handle = resources->createMaterial(material);
        entry.status = Status::Ready;
        entry.file = {};
        return true;
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
            entry.image = streamer->loadImage(parts->file, {.srgb = true, .generateMips = false});
            loadingEnvironments.push_back(key);
        }
        return {key, &entry};
    }

    bool advanceEnvironment(u64 key)
    {
        EnvironmentEntry& entry = environments.at(key);
        if (entry.image.state() == AssetState::Loading)
        {
            return false;
        }
        if (entry.image.ready())
        {
            entry.handle = resources->createEnvironment(
                entry.image.value(), desc.environment,
                std::string(AssetPathTable::instance().path(entry.asset)).c_str());
            entry.status = Status::Ready;
        }
        else
        {
            fail(entry.status, entry.asset, entry.image.error());
        }
        entry.image = {};
        return true;
    }

    [[nodiscard]] u32 loading() const noexcept
    {
        return static_cast<u32>(loadingModels.size() + loadingTextures.size() + loadingMeshes.size() +
                                loadingMaterials.size() + loadingEnvironments.size());
    }

    void update()
    {
        ILL_PROFILE_SCOPE_N("RenderAssetCache::update");
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

void RenderAssetCache::waitIdle()
{
    // Every step either publishes loads or uploads at least one model, so this ends.
    while (m_impl->loading() > 0)
    {
        m_impl->streamer->waitIdle();
        m_impl->update();
    }
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

    void updateMeshes(World& world, JobSystem* jobs)
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
        const auto compare = [out, &found](Entity entity, const MeshRenderer& renderer,
                                           const RenderMesh& mesh) {
            if (renderSourceKey(renderer) != mesh.sourceKey)
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

    void updateSkies(World& world)
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
            [this, &world](Entity entity, const SkyLight& sky, RenderSky& target) {
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
                if (key == target.sourceKey)
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

    RenderAssetCache* owner;
    RenderAssetCache::Impl* cache;
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
    m_impl->statistics.resolved = 0;
    m_impl->statistics.pending = 0;
    m_impl->updateMeshes(world, jobs);
    m_impl->updateSkies(world);
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

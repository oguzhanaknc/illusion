#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/Ragdoll.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

// Fragments that select a mesh: a built-in or whole file (None), `#mesh/N` or `#node/<name>`.
bool isMeshFragment(AssetFragment fragment) noexcept
{
    return fragment == AssetFragment::None || fragment == AssetFragment::Mesh ||
           fragment == AssetFragment::Node;
}

enum class Status : u8
{
    Loading,
    Ready,
    Failed,
};

// Geometry cooked from one mesh asset, mesh index and shape kind.
struct GeometryEntry
{
    Status status = Status::Loading;
    AssetId asset;
    u32 meshIndex = 0;
    bool triangles = false;       // ColliderShape::Mesh; otherwise a convex hull
    AssetHandle<ModelData> model; // until cooked
    PhysicsMeshHandle geometry;
    u32 triangleCount = 0;  // triangles of `geometry` (Mesh shapes only)
    bool reloading = false; // `model` is a reload of a file that changed (AssetReloadedEvent)
    bool again = false;     // the file changed again during the reload
};

// Decoder name of `.ragdoll` files in the AssetStreamer.
constexpr std::string_view kRagdollDecoder = "illusion.ragdoll";

// A `.ragdoll` profile and its running load.
struct ProfileEntry
{
    Status status = Status::Loading;
    AssetHandle<RagdollProfile> load;
    bool again = false; // the file changed during the load
    RagdollProfile profile;
};

std::optional<RagdollProfile> decodeProfile(std::vector<std::byte>&& bytes, std::string& error)
{
    Expected<RagdollProfile, std::string> profile =
        parseRagdollProfile(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    if (!profile)
    {
        error = std::move(profile.error());
        return std::nullopt;
    }
    return std::move(*profile);
}

u64 geometryKey(AssetId asset, u32 meshIndex, ColliderShape shape) noexcept
{
    u64 key = hashCombine(asset.value, meshIndex);
    key = hashCombine(key, static_cast<u64>(shape));
    return key == 0 ? 1 : key;
}

u64 geometryKey(const Collider& collider) noexcept
{
    return geometryKey(collider.mesh.id, collider.meshIndex, collider.shape);
}

u64 geometryKey(const GeometryEntry& entry) noexcept
{
    return geometryKey(entry.asset, entry.meshIndex,
                       entry.triangles ? ColliderShape::Mesh : ColliderShape::ConvexHull);
}

bool usesMeshAsset(ColliderShape shape) noexcept
{
    return shape == ColliderShape::ConvexHull || shape == ColliderShape::Mesh;
}

} // namespace

struct PhysicsAssetResolver::Impl
{
    PhysicsWorld* physics;
    AssetStreamer* streamer;
    PhysicsAssetStatistics statistics;
    std::unordered_map<u64, GeometryEntry> entries;
    std::vector<u64> loading;
    std::vector<AssetId> reloads;            // files reloaded since the last update()
    std::vector<u64> refreshed;              // entries whose geometry changed: their Colliders take it again
    std::vector<PhysicsMeshHandle> replaced; // destroyed once the Colliders use the new geometry
    std::unordered_map<u64, ProfileEntry> profiles;
    Subscription reloaded;

    void startProfileLoad(AssetId id, ProfileEntry& entry)
    {
        entry.load = streamer->loadFile<RagdollProfile>(AssetPathTable::instance().path(id), kRagdollDecoder,
                                                        decodeProfile);
    }

    // Takes the finished load of @p entry; a failed reload keeps the previous profile.
    void advanceProfile(AssetId id, ProfileEntry& entry)
    {
        if (!entry.load || entry.load.state() == AssetState::Loading)
        {
            return;
        }
        if (const RagdollProfile* loaded = entry.load.get())
        {
            entry.profile = *loaded;
            entry.status = Status::Ready;
        }
        else if (entry.status == Status::Ready)
        {
            ILL_LOG_WARN("Physics", "cannot reload the ragdoll profile '{}', keeping the previous one: {}",
                         AssetPathTable::instance().path(id), entry.load.error());
        }
        else
        {
            entry.status = Status::Failed;
            ++statistics.failures;
            ILL_LOG_WARN("Physics", "cannot use the ragdoll profile '{}': {}",
                         AssetPathTable::instance().path(id), entry.load.error());
        }
        entry.load = {};
        if (std::exchange(entry.again, false))
        {
            startProfileLoad(id, entry);
        }
    }

    void fail(GeometryEntry& entry, std::string_view message)
    {
        entry.status = Status::Failed;
        ++statistics.failures;
        ILL_LOG_WARN("Physics", "cannot build collision geometry from '{}': {}",
                     AssetPathTable::instance().path(entry.asset), message);
    }

    void cook(GeometryEntry& entry, const MeshData& mesh)
    {
        std::vector<Vec3> positions;
        positions.reserve(mesh.vertices.size());
        for (const Vertex& vertex : mesh.vertices)
        {
            positions.push_back(vertex.position);
        }
        // Levels of detail are for drawing: collide with the full-detail surface only.
        const std::span<const u32> indices = fullDetailIndices(mesh);
        const PhysicsMeshHandle geometry =
            entry.triangles ? physics->createMesh(positions, indices) : physics->createConvexHull(positions);
        if (!geometry.valid())
        {
            failReload(entry, "the mesh is empty or degenerate");
            return;
        }
        if (entry.geometry.valid())
        {
            replaced.push_back(entry.geometry);
        }
        else
        {
            ++statistics.geometries;
        }
        const u32 triangleCount = entry.triangles ? static_cast<u32>(indices.size() / 3) : 0u;
        statistics.triangles = statistics.triangles - entry.triangleCount + triangleCount;
        entry.triangleCount = triangleCount;
        entry.geometry = geometry;
        entry.status = Status::Ready;
    }

    // A load that ended without geometry: a reload keeps the previous geometry.
    void failReload(GeometryEntry& entry, std::string_view message)
    {
        if (!entry.geometry.valid())
        {
            fail(entry, message);
            return;
        }
        entry.status = Status::Ready;
        ++statistics.failures;
        ILL_LOG_WARN("Physics", "cannot rebuild collision geometry from '{}', keeping the previous one: {}",
                     AssetPathTable::instance().path(entry.asset), message);
    }

    // Cooks the entry once its model is loaded; returns false while it loads.
    bool advance(GeometryEntry& entry)
    {
        if (entry.model.state() == AssetState::Loading)
        {
            return false;
        }
        const std::optional<AssetPathParts> parts =
            splitAssetPath(AssetPathTable::instance().path(entry.asset));
        const ModelData* model = entry.model.get();
        if (model == nullptr)
        {
            failReload(entry, entry.model.error());
        }
        else
        {
            i32 mesh = parts->fragment == AssetFragment::Mesh ? static_cast<i32>(parts->index)
                                                              : static_cast<i32>(entry.meshIndex);
            if (parts->fragment == AssetFragment::Node)
            {
                mesh = -1;
                for (const ModelNode& node : model->nodes)
                {
                    if (node.name == parts->node)
                    {
                        mesh = node.mesh;
                        break;
                    }
                }
            }
            if (mesh < 0 || static_cast<usize>(mesh) >= model->meshes.size())
            {
                failReload(entry, "the model has no such mesh");
            }
            else
            {
                cook(entry, model->meshes[static_cast<usize>(mesh)]);
            }
        }
        entry.model = {};
        if (std::exchange(entry.reloading, false))
        {
            ++statistics.reloads;
            refreshed.push_back(geometryKey(entry));
            if (std::exchange(entry.again, false))
            {
                startReload(entry);
            }
        }
        return true;
    }

    // Loads the entry's model again (its file changed); the geometry stays until the new one is cooked.
    void startReload(GeometryEntry& entry)
    {
        if (entry.model)
        {
            entry.again = true; // a load is running: reload once more when it ends
            return;
        }
        const std::optional<AssetPathParts> parts =
            splitAssetPath(AssetPathTable::instance().path(entry.asset));
        entry.reloading = true;
        entry.model = streamer->loadModel(parts->file);
        loading.push_back(geometryKey(entry));
    }

    // Starts reloading the entries made from the files in `reloads` (built-in meshes never change).
    void startReloads()
    {
        std::vector<AssetId> files;
        files.swap(reloads);
        for (const AssetId file : files)
        {
            if (const auto profile = profiles.find(file.value); profile != profiles.end())
            {
                if (profile->second.load)
                {
                    profile->second.again = true;
                }
                else
                {
                    startProfileLoad(file, profile->second);
                }
            }
        }
        for (auto& [key, entry] : entries)
        {
            const std::optional<AssetPathParts> parts =
                splitAssetPath(AssetPathTable::instance().path(entry.asset));
            if (!parts || isOnBuiltinMount(parts->file) || !isMeshFragment(parts->fragment))
            {
                continue;
            }
            if (std::ranges::find(files, AssetId::fromPath(parts->file)) != files.end())
            {
                startReload(entry);
            }
        }
    }

    GeometryEntry& request(u64 key, const Collider& collider)
    {
        const auto [found, inserted] = entries.try_emplace(key);
        GeometryEntry& entry = found->second;
        if (!inserted)
        {
            return entry;
        }
        entry.asset = collider.mesh.id;
        entry.meshIndex = collider.meshIndex;
        entry.triangles = collider.shape == ColliderShape::Mesh;
        const std::optional<AssetPathParts> parts = splitAssetPath(collider.mesh.path());
        if (!parts || !isMeshFragment(parts->fragment))
        {
            fail(entry, "not a mesh asset");
        }
        else if (isOnBuiltinMount(parts->file))
        {
            const std::optional<MeshData> mesh =
                parts->fragment == AssetFragment::None ? makeBuiltinMesh(parts->file) : std::nullopt;
            if (mesh)
            {
                cook(entry, *mesh);
            }
            else
            {
                fail(entry, "not a built-in mesh (see builtinAssetPaths())");
            }
        }
        else
        {
            entry.model = streamer->loadModel(parts->file);
            loading.push_back(key);
        }
        return entry;
    }

    void update()
    {
        ILL_PROFILE_SCOPE_N("PhysicsAssetResolver::update");
        if (!reloads.empty())
        {
            startReloads();
        }
        for (auto& [key, profile] : profiles)
        {
            advanceProfile(AssetId{key}, profile);
        }
        for (usize i = 0; i < loading.size();)
        {
            if (advance(entries.at(loading[i])))
            {
                loading[i] = loading.back();
                loading.pop_back();
            }
            else
            {
                ++i;
            }
        }
        statistics.pending = 0;
        World& world = physics->world();
        world.query<Collider>().without<Inactive>().each([&](Entity entity, Collider& collider) {
            if (!collider.mesh || !usesMeshAsset(collider.shape))
            {
                if (collider.geometryKey != 0) // the asset was removed: the geometry was ours
                {
                    collider.geometry = {};
                    collider.geometryKey = 0;
                    rebuildIfBuilt(world, entity);
                }
                return;
            }
            const u64 key = geometryKey(collider);
            if (key == collider.geometryKey &&
                (refreshed.empty() || std::ranges::find(refreshed, key) == refreshed.end()))
            {
                return;
            }
            const GeometryEntry& entry = request(key, collider);
            if (entry.status == Status::Loading)
            {
                ++statistics.pending;
                return;
            }
            collider.geometry = entry.geometry;
            collider.geometryKey = key;
            rebuildIfBuilt(world, entity);
        });
        refreshed.clear();
        // Bodies keep the geometry they were built with until they are rebuilt, so it can go at once.
        for (const PhysicsMeshHandle geometry : replaced)
        {
            physics->destroyMesh(geometry);
        }
        replaced.clear();
    }

    void rebuildIfBuilt(const World& world, Entity entity)
    {
        if (world.has<PhysicsBody>(entity))
        {
            physics->rebuild(entity);
        }
    }
};

PhysicsAssetResolver::PhysicsAssetResolver(PhysicsWorld& physics, AssetStreamer& streamer)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->physics = &physics;
    m_impl->streamer = &streamer;
    m_impl->reloaded = streamer.events().subscribe<AssetReloadedEvent>(
        [impl = m_impl.get()](const AssetReloadedEvent& event) { impl->reloads.push_back(event.asset); });
}

PhysicsAssetResolver::~PhysicsAssetResolver()
{
    for (const auto& [key, entry] : m_impl->entries)
    {
        if (entry.geometry.valid())
        {
            m_impl->physics->destroyMesh(entry.geometry);
        }
    }
    for (const PhysicsMeshHandle geometry : m_impl->replaced)
    {
        m_impl->physics->destroyMesh(geometry);
    }
}

void PhysicsAssetResolver::update()
{
    m_impl->update();
}

void PhysicsAssetResolver::waitIdle()
{
    update();
    const auto profileLoading = [this] {
        return std::ranges::any_of(m_impl->profiles,
                                   [](const auto& entry) { return bool(entry.second.load); });
    };
    while (m_impl->statistics.pending > 0 || !m_impl->loading.empty() || profileLoading())
    {
        m_impl->streamer->waitIdle();
        update();
    }
}

const PhysicsAssetStatistics& PhysicsAssetResolver::statistics() const noexcept
{
    return m_impl->statistics;
}

const RagdollProfile* PhysicsAssetResolver::ragdollProfile(const AssetRef& profile, bool& failed)
{
    failed = false;
    const auto [found, inserted] = m_impl->profiles.try_emplace(profile.id.value);
    ProfileEntry& entry = found->second;
    if (inserted)
    {
        const std::string_view path = profile.path();
        const std::optional<AssetPathParts> parts = splitAssetPath(path);
        if (!profile || !parts || parts->fragment != AssetFragment::None ||
            !path.ends_with(kRagdollProfileExtension) || isOnBuiltinMount(path))
        {
            entry.status = Status::Failed;
            ++m_impl->statistics.failures;
            ILL_LOG_WARN("Physics", "cannot use '{}': not a ragdoll profile (.ragdoll)", path);
        }
        else
        {
            m_impl->startProfileLoad(profile.id, entry);
        }
    }
    m_impl->advanceProfile(profile.id, entry);
    failed = entry.status == Status::Failed;
    return entry.status == Status::Ready ? &entry.profile : nullptr;
}

} // namespace illusion

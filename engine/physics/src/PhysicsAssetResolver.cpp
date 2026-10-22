#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <optional>
#include <string>
#include <unordered_map>
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

// Geometry cooked from one mesh asset, mesh index and shape kind.
struct GeometryEntry
{
    Status status = Status::Loading;
    AssetId asset;
    u32 meshIndex = 0;
    bool triangles = false;       // ColliderShape::Mesh; otherwise a convex hull
    AssetHandle<ModelData> model; // until cooked
    PhysicsMeshHandle geometry;
};

u64 geometryKey(const Collider& collider) noexcept
{
    u64 key = hashCombine(collider.mesh.id.value, collider.meshIndex);
    key = hashCombine(key, static_cast<u64>(collider.shape));
    return key == 0 ? 1 : key;
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
        entry.geometry = entry.triangles ? physics->createMesh(positions, mesh.indices)
                                         : physics->createConvexHull(positions);
        if (entry.geometry.valid())
        {
            entry.status = Status::Ready;
            ++statistics.geometries;
        }
        else
        {
            fail(entry, "the mesh is empty or degenerate");
        }
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
            fail(entry, entry.model.error());
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
                fail(entry, "the model has no such mesh");
            }
            else
            {
                cook(entry, model->meshes[static_cast<usize>(mesh)]);
            }
        }
        entry.model = {};
        return true;
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
        if (!parts || parts->fragment == AssetFragment::Material)
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
            if (key == collider.geometryKey)
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
}

void PhysicsAssetResolver::update()
{
    m_impl->update();
}

void PhysicsAssetResolver::waitIdle()
{
    update();
    while (m_impl->statistics.pending > 0 || !m_impl->loading.empty())
    {
        m_impl->streamer->waitIdle();
        update();
    }
}

const PhysicsAssetStatistics& PhysicsAssetResolver::statistics() const noexcept
{
    return m_impl->statistics;
}

} // namespace illusion

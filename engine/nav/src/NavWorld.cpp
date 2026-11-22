#include "NavAllocator.hpp"
#include "NavBake.hpp"
#include "NavCrowd.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/Mesh.hpp>
#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/EventBus.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavBuild.hpp>
#include <illusion/nav/NavMeshData.hpp>
#include <illusion/nav/NavWorld.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

constexpr std::string_view kDecoder = "illusion.navmesh";
constexpr i32 kQueryNodes = 16384;    // A* nodes of a query (Detour's limit is 65535)
constexpr i32 kQueryPolygons = 4096;  // polygons of a query's path
constexpr i32 kQueryCorners = 1024;   // corners of a query's straight path
constexpr u32 kRandomTries = 16;      // random points drawn until one lies within the radius
constexpr f32 kNoProgress = 1e-3f;    // a partial path that gets no nearer than this is no path, in m
constexpr u32 kNoTrack = 0xFFFFFFFFu; // no index
constexpr f32 kYawEpsilon = 1e-3f;    // rotation of a carving obstacle that counts as a move, in radians
constexpr f32 kDebugLift = 0.02f;     // debug lines above the navmesh, in m
constexpr u32 kCircleSegments = 16;   // of debug cylinders
constexpr u32 kArcSegments = 8;       // of debug links

[[nodiscard]] bool isFinite(const Vec3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] u64 bits(f32 value) noexcept
{
    return std::bit_cast<u32>(value);
}

[[nodiscard]] u64 hashVec(u64 seed, const Vec3& value) noexcept
{
    return hashCombine(hashCombine(hashCombine(seed, bits(value.x)), bits(value.y)), bits(value.z));
}

[[nodiscard]] u64 hashQuat(u64 seed, const Quat& value) noexcept
{
    return hashCombine(hashVec(seed, Vec3(value.x, value.y, value.z)), bits(value.w));
}

[[nodiscard]] bool sameVolume(const NavVolumeData& a, const NavVolumeData& b) noexcept
{
    return bits(a.center.x) == bits(b.center.x) && bits(a.center.y) == bits(b.center.y) &&
           bits(a.center.z) == bits(b.center.z) && bits(a.halfExtents.x) == bits(b.halfExtents.x) &&
           bits(a.halfExtents.y) == bits(b.halfExtents.y) && bits(a.halfExtents.z) == bits(b.halfExtents.z) &&
           bits(a.yaw) == bits(b.yaw) && a.cylinder == b.cylinder && a.area == b.area;
}

// The carved volume of an obstacle, exactly as collectNavGeometry() makes it.
[[nodiscard]] NavVolumeData obstacleVolume(const NavObstacle& obstacle, const Transform* transform) noexcept
{
    const bool cylinder = obstacle.shape == NavObstacleShape::Cylinder;
    return NavVolumeData{.center = transform != nullptr ? transform->position : Vec3(0.0f),
                         .halfExtents = cylinder
                                            ? Vec3(obstacle.radius, obstacle.height * 0.5f, obstacle.radius)
                                            : obstacle.size * 0.5f,
                         .yaw = cylinder || transform == nullptr ? 0.0f : detail::navYaw(transform->rotation),
                         .cylinder = cylinder,
                         .area = kNavAreaNotWalkable};
}

std::optional<NavMeshData> decodeNavMeshFile(std::vector<std::byte>&& bytes, std::string& error)
{
    Expected<NavMeshData, NavDataError> decoded = decodeNavMeshData(bytes);
    if (!decoded)
    {
        error = "the file is not a valid .navmesh (" + std::string(navDataErrorName(decoded.error())) + ")";
        return std::nullopt;
    }
    return std::move(*decoded);
}

// Detour's random points take a plain function: the generator of the running query, on its thread.
thread_local Random* tQueryRandom = nullptr;

f32 queryRandom()
{
    return tQueryRandom->uniformF32();
}

// A colour per area, spread around the hue circle.
[[nodiscard]] Vec4 areaColor(u32 area) noexcept
{
    const f32 hue = static_cast<f32>((area * 7u) % kMaxNavAreas) / static_cast<f32>(kMaxNavAreas);
    const auto channel = [hue](f32 offset) {
        const f32 x = std::abs(std::fmod(hue * 6.0f + offset, 6.0f) - 3.0f) - 1.0f;
        return 0.15f + 0.75f * std::clamp(x, 0.0f, 1.0f);
    };
    return Vec4(channel(0.0f), channel(4.0f), channel(2.0f), 1.0f);
}

struct DetourDeleter
{
    void operator()(dtNavMesh* mesh) const noexcept { dtFreeNavMesh(mesh); }
    void operator()(dtNavMeshQuery* query) const noexcept { dtFreeNavMeshQuery(query); }
};

using MeshPtr = std::unique_ptr<dtNavMesh, DetourDeleter>;
using QueryPtr = std::unique_ptr<dtNavMeshQuery, DetourDeleter>;

[[nodiscard]] MeshPtr makeMesh(const detail::NavTileGrid& grid)
{
    dtNavMeshParams params{};
    params.orig[0] = grid.origin.x;
    params.orig[1] = grid.origin.y;
    params.orig[2] = grid.origin.z;
    params.tileWidth = grid.tileWorld;
    params.tileHeight = grid.tileWorld;
    params.maxTiles = static_cast<i32>(grid.tilesX * grid.tilesZ);
    params.maxPolys = 1 << DT_POLY_BITS;
    MeshPtr mesh(dtAllocNavMesh());
    if (!mesh || dtStatusFailed(mesh->init(&params)))
    {
        return {};
    }
    return mesh;
}

[[nodiscard]] QueryPtr makeQuery(const dtNavMesh& mesh)
{
    QueryPtr query(dtAllocNavMeshQuery());
    if (!query || dtStatusFailed(query->init(&mesh, kQueryNodes)))
    {
        return {};
    }
    return query;
}

// Adds a copy of @p bytes as a tile of @p mesh; 0 when Detour rejects it.
[[nodiscard]] dtTileRef addTile(dtNavMesh& mesh, std::span<const std::byte> bytes)
{
    auto* data = static_cast<unsigned char*>(dtAlloc(bytes.size(), DT_ALLOC_PERM));
    if (data == nullptr)
    {
        return 0;
    }
    std::memcpy(data, bytes.data(), bytes.size());
    dtTileRef ref = 0;
    if (dtStatusFailed(mesh.addTile(data, static_cast<i32>(bytes.size()), DT_TILE_FREE_DATA, 0, &ref)))
    {
        dtFree(data);
        return 0;
    }
    return ref;
}

[[nodiscard]] u32 polygonsOf(const dtNavMesh& mesh, dtTileRef ref) noexcept
{
    const dtMeshTile* tile = mesh.getTileByRef(ref);
    return tile != nullptr && tile->header != nullptr ? static_cast<u32>(tile->header->polyCount) : 0;
}

// The bake input of one collection, shared by the tile builds that read it.
struct BakeInput
{
    NavGeometry geometry;
    detail::NavTileGrid grid;
    std::vector<std::vector<u32>> candidates;
};

struct TileSlot
{
    u64 hash = 0;       // input hash of the tile in the navmesh
    u64 target = 0;     // input hash the pending or running build makes (0 when not known)
    u32 generation = 0; // grows when the wanted input changes; older builds are dropped
    dtTileRef ref = 0;
    u32 polygons = 0;
    bool pending = false;
    bool inFlight = false;
    bool stale = false;  // input changed, kept (no rebuildAtRuntime)
    bool rehash = false; // compared again at the next collection
};

enum class Phase : u8
{
    Idle,    // no surface
    Loading, // waits for its first file
    Collect, // waits for its bake input
    Ready,   // has a navmesh; tiles may be pending
    Failed,  // cannot be baked with its values
};

struct Surface
{
    Entity entity;
    NavMeshSurface settings;
    u64 layoutHash = 0; // of what the tiles depend on: a change resets the surface
    u32 generation = 0; // grows at every reset and file install; builds of older ones are dropped
    Phase phase = Phase::Idle;
    AssetHandle<NavMeshData> load;
    bool loading = false;
    bool loadAgain = false; // the file changed while it loaded
    bool fileReady = false; // a loaded file waits to be installed at the start of the next update
    bool bakeAll = false;   // the next collection starts a new grid and builds every tile
    bool rehashAll = false; // the next collection compares every tile
    bool collect = false;   // the bake input must be collected
    bool waitsForMeshes = false;
    bool staleLogged = false;
    bool failureLogged = false;
    MeshPtr mesh;
    QueryPtr query;
    detail::NavTileGrid grid;
    std::vector<TileSlot> tiles;
    u32 installed = 0; // tiles with polygons in the navmesh
    u32 pending = 0;   // tiles waiting for a build, after the last update
    std::shared_ptr<const BakeInput> input;
    std::vector<EntityGuid> links;
    std::vector<Aabb> dirty;

    [[nodiscard]] bool used() const noexcept { return !entity.isNull(); }
};

struct TileTask
{
    JobCounter counter;
    JobSystem* jobs = nullptr;
    std::shared_ptr<const BakeInput> input;
    u32 surface = 0;
    u32 surfaceGeneration = 0;
    u32 tile = 0;
    u32 tileGeneration = 0;
    detail::NavTileResult result;
};

void runTask(TileTask& task)
{
    const BakeInput& input = *task.input;
    const NavTileCoord coord{.x = static_cast<i32>(task.tile % input.grid.tilesX),
                             .z = static_cast<i32>(task.tile / input.grid.tilesX)};
    detail::buildNavTile(input.geometry, input.grid, coord, input.candidates[task.tile], task.result);
}

// What a source Entity adds to the bake input, for finding the ones that changed.
struct SourceRecord
{
    u32 generation = 0;
    u32 stamp = 0;
    u64 hash = 0;
    Aabb bounds;
    bool unbounded = false; // its extent is not known (a mesh that has not loaded)
    bool present = false;
};

struct ObstacleTrack
{
    Entity entity;
    NavVolumeData seen;   // where it was in the last update
    NavVolumeData carved; // where the navmesh has it cut out
    f32 still = 0.0f;     // seconds without moving
    u32 stamp = 0;
    bool hasCarved = false;
};

struct MeshEntry
{
    AssetHandle<ModelData> model;
    std::optional<MeshData> builtin;
    const MeshData* mesh = nullptr;
    u32 meshIndex = 0;
    AssetId asset;
    bool loading = false;
};

struct Versions
{
    u64 structural = 0;
    u64 transform = 0;
    u64 collider = 0;
    u64 body = 0;
    u64 area = 0;
    u64 link = 0;
    u64 inactive = 0;
    u64 surface = 0;

    friend bool operator==(const Versions&, const Versions&) = default;
};

} // namespace

struct NavWorld::Impl
{
    World* world = nullptr;
    AssetStreamer* streamer = nullptr;
    NavWorldDesc desc;
    NavStatistics statistics;
    std::array<Surface, kMaxNavAgentTypes> surfaces;

    std::unique_ptr<TileTask[]> tasks;
    u32 taskCapacity = 0;
    u32 taskHead = 0;
    u32 taskCount = 0;

    std::unique_ptr<dtPolyRef[]> queryPolygons;
    std::unique_ptr<f32[]> queryCorners;
    std::unique_ptr<u8[]> queryCornerFlags;

    std::unordered_map<u64, MeshEntry> meshes;
    u32 meshesLoading = 0;

    std::vector<SourceRecord> sources; // by Entity index
    u32 sourceStamp = 0;
    bool sourcesScanned = false;
    Versions sourceVersions;
    bool dirtyAll = false; // a source without known bounds changed

    std::vector<ObstacleTrack> obstacles;
    std::vector<u32> obstacleOf; // by Entity index: its track, or kNoTrack
    u32 obstacleStamp = 0;
    bool obstaclesSeen = false;

    Versions surfaceVersions;
    bool surfacesSynced = false;
    std::vector<Entity> reported; // ignored surfaces already logged

    std::vector<AssetId> reloads;
    Subscription reloaded;

    std::unique_ptr<detail::NavCrowd> crowd;

    // --- Versions -------------------------------------------------------------------------------------

    [[nodiscard]] Versions currentVersions() const noexcept
    {
        return Versions{.structural = world->structuralVersion(),
                        .transform = world->componentVersion<Transform>(),
                        .collider = world->componentVersion<Collider>(),
                        .body = world->componentVersion<RigidBody>(),
                        .area = world->componentVersion<NavArea>(),
                        .link = world->componentVersion<NavLink>(),
                        .inactive = world->componentVersion<Inactive>(),
                        .surface = world->componentVersion<NavMeshSurface>()};
    }

    [[nodiscard]] bool anyRebuild() const noexcept
    {
        return std::ranges::any_of(surfaces,
                                   [](const Surface& s) { return s.used() && s.settings.rebuildAtRuntime; });
    }

    [[nodiscard]] bool anySurface() const noexcept
    {
        return std::ranges::any_of(surfaces, [](const Surface& s) { return s.used(); });
    }

    void logFailure(Surface& surface, std::string_view message)
    {
        ++statistics.failures;
        if (!std::exchange(surface.failureLogged, true))
        {
            ILL_LOG_WARN("Navigation", "the navmesh of agent type {} cannot be used: {}",
                         surface.settings.agentType, message);
        }
    }

    // --- Surfaces -------------------------------------------------------------------------------------

    [[nodiscard]] static u64 layoutHash(const NavMeshSurface& settings, const Vec3& center) noexcept
    {
        u64 hash = hashVec(0x6e61766d657368ull, settings.size);
        hash = hashVec(hash, center);
        for (const f32 value : {settings.agentRadius, settings.agentHeight, settings.agentMaxClimb,
                                settings.agentMaxSlope, settings.build.cellSize, settings.build.cellHeight,
                                settings.build.minRegionArea, settings.build.mergeRegionArea,
                                settings.build.maxEdgeLength, settings.build.maxSimplificationError,
                                settings.build.detailSampleDistance, settings.build.detailSampleMaxError})
        {
            hash = hashCombine(hash, bits(value));
        }
        hash = hashCombine(hash, settings.build.tileSize);
        hash = hashCombine(hash, settings.layerMask);
        return hashCombine(hash, settings.data.id.value);
    }

    void report(Entity entity, std::string_view message)
    {
        if (std::ranges::find(reported, entity) == reported.end())
        {
            reported.push_back(entity);
            ILL_LOG_WARN("Navigation", "NavMeshSurface of Entity {} ignored: {}", entity.index, message);
        }
    }

    void startLoad(Surface& surface)
    {
        surface.load =
            streamer->loadFile<NavMeshData>(surface.settings.data.path(), kDecoder, decodeNavMeshFile);
        surface.loading = true;
    }

    void startBake(Surface& surface) noexcept
    {
        surface.phase = Phase::Collect;
        surface.bakeAll = true;
        surface.collect = true;
    }

    void resetSurface(Surface& surface, Entity entity, const NavMeshSurface& settings, u64 layout)
    {
        surface.entity = entity;
        surface.settings = settings;
        surface.layoutHash = layout;
        ++surface.generation;
        surface.phase = entity.isNull() ? Phase::Idle : Phase::Collect;
        surface.load = {};
        surface.loading = false;
        surface.loadAgain = false;
        surface.fileReady = false;
        surface.bakeAll = false;
        surface.rehashAll = false;
        surface.collect = false;
        surface.waitsForMeshes = false;
        surface.staleLogged = false;
        surface.failureLogged = false;
        surface.query.reset();
        surface.mesh.reset();
        surface.tiles.clear();
        surface.installed = 0;
        surface.pending = 0;
        surface.input.reset();
        surface.links.clear();
        surface.dirty.clear();
        if (entity.isNull())
        {
            return;
        }
        if (settings.data && streamer != nullptr)
        {
            const std::string_view path = settings.data.path();
            const std::optional<AssetPathParts> parts = splitAssetPath(path);
            if (parts && parts->fragment == AssetFragment::None && path.ends_with(kNavMeshExtension) &&
                !isOnBuiltinMount(path))
            {
                surface.phase = Phase::Loading;
                startLoad(surface);
                return;
            }
            ++statistics.failures;
            ILL_LOG_WARN("Navigation", "'{}' is not a .navmesh file: baking the navmesh of agent type {}",
                         path, settings.agentType);
        }
        startBake(surface);
    }

    void syncSurfaces()
    {
        std::array<Entity, kMaxNavAgentTypes> chosen{};
        world->query<const NavMeshSurface>().without<Inactive>().each(
            [&](Entity entity, const NavMeshSurface& settings) {
                if (settings.agentType >= kMaxNavAgentTypes)
                {
                    report(entity, "agentType must be below 8");
                    return;
                }
                Entity& slot = chosen[settings.agentType];
                if (slot.isNull() || entity.index < slot.index)
                {
                    if (!slot.isNull())
                    {
                        report(slot, "another surface has the same agent type");
                    }
                    slot = entity;
                }
                else
                {
                    report(entity, "another surface has the same agent type");
                }
            });
        for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
        {
            Surface& surface = surfaces[type];
            const Entity entity = chosen[type];
            if (entity.isNull())
            {
                if (surface.used())
                {
                    resetSurface(surface, kNullEntity, NavMeshSurface{}, 0);
                }
                continue;
            }
            const NavMeshSurface& settings = world->get<NavMeshSurface>(entity);
            const Transform* transform = world->tryGet<Transform>(entity);
            const u64 layout = layoutHash(settings, transform != nullptr ? transform->position : Vec3(0.0f));
            if (surface.entity != entity || surface.layoutHash != layout)
            {
                resetSurface(surface, entity, settings, layout);
                continue;
            }
            const bool rebuildOn = settings.rebuildAtRuntime && !surface.settings.rebuildAtRuntime;
            surface.settings = settings;
            if (rebuildOn && surface.phase == Phase::Ready)
            {
                surface.rehashAll = true;
                surface.collect = true;
            }
        }
    }

    // --- Files ----------------------------------------------------------------------------------------

    void processReloads()
    {
        for (const AssetId asset : reloads)
        {
            for (Surface& surface : surfaces)
            {
                if (!surface.used() || !surface.settings.data || surface.settings.data.id != asset)
                {
                    continue;
                }
                if (surface.loading)
                {
                    surface.loadAgain = true;
                    continue;
                }
                surface.fileReady = false;
                startLoad(surface);
            }
        }
        reloads.clear();
    }

    void advanceLoad(Surface& surface)
    {
        if (!surface.loading || surface.load.state() == AssetState::Loading)
        {
            return;
        }
        surface.loading = false;
        if (std::exchange(surface.loadAgain, false))
        {
            startLoad(surface);
            return;
        }
        if (surface.load.ready())
        {
            surface.fileReady = true;
            return;
        }
        ++statistics.failures;
        if (surface.phase == Phase::Loading)
        {
            ILL_LOG_WARN("Navigation", "cannot load '{}': {}; baking the navmesh of agent type {}",
                         surface.settings.data.path(), surface.load.error(), surface.settings.agentType);
            startBake(surface);
        }
        else
        {
            ILL_LOG_WARN("Navigation", "cannot reload '{}': {}; keeping its tiles",
                         surface.settings.data.path(), surface.load.error());
        }
        surface.load = {};
    }

    // Replaces the surface's navmesh by the tiles of its loaded file.
    void installFile(Surface& surface)
    {
        surface.fileReady = false;
        const NavMeshData& data = surface.load.value();
        const detail::NavTileGrid grid{.origin = data.origin,
                                       .tileWorld = data.tileWorldSize,
                                       .border = 0.0f,
                                       .borderCells = 0,
                                       .tilesX = data.tilesX,
                                       .tilesZ = data.tilesZ};
        MeshPtr mesh = makeMesh(grid);
        QueryPtr query = mesh ? makeQuery(*mesh) : QueryPtr{};
        std::vector<TileSlot> tiles(data.tiles.size());
        u32 installed = 0;
        bool valid = query != nullptr;
        for (usize i = 0; valid && i < data.tiles.size(); ++i)
        {
            tiles[i].hash = data.tiles[i].inputHash;
            if (data.tiles[i].detour.empty())
            {
                continue;
            }
            tiles[i].ref = addTile(*mesh, data.tiles[i].detour);
            tiles[i].polygons = polygonsOf(*mesh, tiles[i].ref);
            valid = tiles[i].ref != 0;
            installed += tiles[i].polygons > 0 ? 1u : 0u;
        }
        const bool first = surface.phase == Phase::Loading;
        if (!valid)
        {
            ++statistics.failures;
            ILL_LOG_WARN("Navigation", "'{}' has tiles Detour rejects: {}", surface.settings.data.path(),
                         first ? "baking it" : "keeping the previous tiles");
            surface.load = {};
            if (first)
            {
                startBake(surface);
            }
            return;
        }
        ++surface.generation;
        surface.query = std::move(query);
        surface.mesh = std::move(mesh);
        surface.grid = grid;
        surface.tiles = std::move(tiles);
        surface.installed = installed;
        surface.links = data.links;
        surface.staleLogged = false;
        surface.load = {};
        surface.phase = Phase::Collect;
        surface.bakeAll = false;
        surface.rehashAll = true;
        surface.collect = true;
    }

    // --- Meshes of Mesh and ConvexHull Colliders ------------------------------------------------------

    void resolveMesh(MeshEntry& entry)
    {
        entry.loading = false;
        --meshesLoading;
        const ModelData* model = entry.model.get();
        const std::optional<AssetPathParts> parts =
            splitAssetPath(AssetPathTable::instance().path(entry.asset));
        if (model == nullptr || !parts)
        {
            ILL_LOG_WARN("Navigation", "cannot read the mesh '{}' of a Collider: {}",
                         AssetPathTable::instance().path(entry.asset), entry.model.error());
            return;
        }
        i32 index = parts->fragment == AssetFragment::Mesh ? static_cast<i32>(parts->index)
                                                           : static_cast<i32>(entry.meshIndex);
        if (parts->fragment == AssetFragment::Node)
        {
            const auto node = std::ranges::find(model->nodes, parts->node, &ModelNode::name);
            index = node != model->nodes.end() ? node->mesh : -1;
        }
        if (index >= 0 && static_cast<usize>(index) < model->meshes.size())
        {
            entry.mesh = &model->meshes[static_cast<usize>(index)];
        }
    }

    void advanceMeshes()
    {
        if (meshesLoading == 0)
        {
            return;
        }
        for (auto& [key, entry] : meshes)
        {
            if (entry.loading && entry.model.state() != AssetState::Loading)
            {
                resolveMesh(entry);
            }
        }
    }

    [[nodiscard]] static u64 meshKey(const AssetRef& mesh, u32 meshIndex) noexcept
    {
        return hashCombine(mesh.id.value, meshIndex);
    }

    // The mesh of a Collider, requested on first use; null while it loads (setting @p waits) or if it fails.
    const MeshData* findMesh(const AssetRef& ref, u32 meshIndex, bool& waits)
    {
        const auto [found, inserted] = meshes.try_emplace(meshKey(ref, meshIndex));
        MeshEntry& entry = found->second;
        if (inserted)
        {
            entry.asset = ref.id;
            entry.meshIndex = meshIndex;
            const std::optional<AssetPathParts> parts = splitAssetPath(ref.path());
            const bool selectsMesh =
                parts && (parts->fragment == AssetFragment::None || parts->fragment == AssetFragment::Mesh ||
                          parts->fragment == AssetFragment::Node);
            if (!selectsMesh)
            {
                ILL_LOG_WARN("Navigation", "'{}' of a Collider is not a mesh", ref.path());
            }
            else if (isOnBuiltinMount(parts->file))
            {
                entry.builtin =
                    parts->fragment == AssetFragment::None ? makeBuiltinMesh(parts->file) : std::nullopt;
                entry.mesh = entry.builtin ? &*entry.builtin : nullptr;
            }
            else if (streamer != nullptr)
            {
                entry.model = streamer->loadModel(parts->file);
                entry.loading = true;
                ++meshesLoading;
            }
        }
        if (entry.loading)
        {
            if (entry.model.state() == AssetState::Loading)
            {
                waits = true;
                return nullptr;
            }
            resolveMesh(entry);
        }
        return entry.mesh;
    }

    // Local bounds of a Collider's shape; std::nullopt for a mesh that is not known yet.
    [[nodiscard]] std::optional<Aabb> shapeBounds(const Collider& collider) const
    {
        switch (collider.shape)
        {
        case ColliderShape::Box:
            return Aabb{.min = -collider.halfExtents, .max = collider.halfExtents};
        case ColliderShape::Sphere:
            return Aabb{.min = Vec3(-collider.radius), .max = Vec3(collider.radius)};
        case ColliderShape::Capsule:
        {
            const Vec3 half(collider.radius, collider.halfHeight + collider.radius, collider.radius);
            return Aabb{.min = -half, .max = half};
        }
        case ColliderShape::Cylinder:
        {
            const Vec3 half(collider.radius, collider.halfHeight, collider.radius);
            return Aabb{.min = -half, .max = half};
        }
        case ColliderShape::ConvexHull:
        case ColliderShape::Mesh:
            break;
        }
        if (!collider.mesh)
        {
            return Aabb{};
        }
        const auto found = meshes.find(meshKey(collider.mesh, collider.meshIndex));
        if (found == meshes.end() || found->second.loading || found->second.mesh == nullptr)
        {
            return std::nullopt;
        }
        return found->second.mesh->bounds;
    }

    // --- Changed sources ------------------------------------------------------------------------------

    void markDirty(const Aabb& bounds)
    {
        if (!bounds.isValid())
        {
            return;
        }
        for (Surface& surface : surfaces)
        {
            if (surface.used() && surface.settings.rebuildAtRuntime && surface.phase == Phase::Ready)
            {
                surface.dirty.push_back(bounds);
            }
        }
    }

    void visitSource(Entity entity)
    {
        if (sources.size() <= entity.index)
        {
            sources.resize(entity.index + 1);
        }
        SourceRecord& record = sources[entity.index];
        if (record.stamp == sourceStamp && record.generation == entity.generation && record.present)
        {
            return;
        }
        const Transform* transform = world->tryGet<Transform>(entity);
        const Collider* collider = world->tryGet<Collider>(entity);
        const RigidBody* body = world->tryGet<RigidBody>(entity);
        const NavArea* area = world->tryGet<NavArea>(entity);
        const NavLink* link = world->tryGet<NavLink>(entity);
        const bool dynamic = body != nullptr && body->motionType == MotionType::Dynamic;

        u64 hash = hashCombine(0x736f75726365ull, world->has<Inactive>(entity) ? 1u : 0u);
        hash = hashCombine(hash, dynamic ? 1u : 0u);
        Aabb bounds;
        bool unbounded = false;
        const Mat4 matrix = transform != nullptr ? transform->matrix() : Mat4(1.0f);
        const bool placed = collider != nullptr ? !dynamic || area != nullptr || link != nullptr : true;
        if (placed && transform != nullptr)
        {
            hash = hashVec(hash, transform->position);
            hash = hashQuat(hash, transform->rotation);
            hash = hashVec(hash, transform->scale);
        }
        if (collider != nullptr && !dynamic)
        {
            hash = hashCombine(hash, static_cast<u64>(collider->shape));
            hash = hashVec(hash, collider->halfExtents);
            hash = hashCombine(hashCombine(hash, bits(collider->radius)), bits(collider->halfHeight));
            hash = hashVec(hash, collider->offset);
            hash = hashQuat(hash, collider->rotation);
            hash = hashCombine(hashCombine(hash, collider->mesh.id.value), collider->meshIndex);
            hash = hashCombine(hash, body != nullptr ? body->layer : 0u);
            if (const std::optional<Aabb> local = shapeBounds(*collider))
            {
                if (local->isValid())
                {
                    const Mat4 shape = matrix * glm::translate(Mat4(1.0f), collider->offset) *
                                       glm::mat4_cast(glm::normalize(collider->rotation));
                    const Vec3 center = Vec3(shape * Vec4(local->center(), 1.0f));
                    const Vec3 half = (local->max - local->min) * 0.5f;
                    const Mat3 axes(shape);
                    const Vec3 extent =
                        glm::abs(axes[0]) * half.x + glm::abs(axes[1]) * half.y + glm::abs(axes[2]) * half.z;
                    bounds.expand(Aabb{.min = center - extent, .max = center + extent});
                }
            }
            else
            {
                unbounded = true;
            }
        }
        const Vec3 position = transform != nullptr ? transform->position : Vec3(0.0f);
        if (area != nullptr)
        {
            hash = hashCombine(hashCombine(hashCombine(hash, area->area), area->walkable ? 1u : 0u),
                               area->volume ? 1u : 0u);
            hash = hashVec(hash, area->size);
            if (area->volume)
            {
                bounds.expand(Aabb{.min = position - area->size * 0.5f, .max = position + area->size * 0.5f});
            }
        }
        if (link != nullptr)
        {
            hash = hashVec(hash, link->end);
            hash =
                hashCombine(hashCombine(hash, static_cast<u64>(link->kind)), link->bidirectional ? 1u : 0u);
            hash = hashCombine(hashCombine(hash, bits(link->radius)), link->area);
            hash = hashCombine(hash, link->agentTypeMask);
            const Vec3 end = Vec3(matrix * Vec4(link->end, 1.0f));
            const Vec3 reach(link->radius);
            bounds.expand(
                Aabb{.min = glm::min(position, end) - reach, .max = glm::max(position, end) + reach});
        }

        const bool same = record.present && record.generation == entity.generation;
        if (sourcesScanned && (!same || record.hash != hash))
        {
            if (record.present)
            {
                dirtyAll = dirtyAll || record.unbounded;
                markDirty(record.bounds);
            }
            dirtyAll = dirtyAll || unbounded;
            markDirty(bounds);
        }
        record = SourceRecord{.generation = entity.generation,
                              .stamp = sourceStamp,
                              .hash = hash,
                              .bounds = bounds,
                              .unbounded = unbounded,
                              .present = true};
    }

    void trackSources()
    {
        const Versions now = currentVersions();
        if (!anyRebuild())
        {
            sourcesScanned = false;
            return;
        }
        if (sourcesScanned && now == sourceVersions)
        {
            return;
        }
        ILL_PROFILE_SCOPE_N("NavWorld::trackSources");
        ++sourceStamp;
        world->query<const Collider>().each([&](Entity entity, const Collider&) { visitSource(entity); });
        world->query<const NavArea>().each([&](Entity entity, const NavArea&) { visitSource(entity); });
        world->query<const NavLink>().each([&](Entity entity, const NavLink&) { visitSource(entity); });
        for (SourceRecord& record : sources)
        {
            if (record.present && record.stamp != sourceStamp)
            {
                dirtyAll = dirtyAll || record.unbounded;
                markDirty(record.bounds);
                record.present = false;
            }
        }
        sourcesScanned = true;
        sourceVersions = now;
    }

    // --- Carving obstacles ----------------------------------------------------------------------------

    void trackObstacles(f32 seconds)
    {
        if (!anySurface())
        {
            return;
        }
        ++obstacleStamp;
        const bool initial = !obstaclesSeen;
        world->query<const NavObstacle>().each([&](Entity entity, const NavObstacle& obstacle) {
            if (!obstacle.carve || world->has<Inactive>(entity))
            {
                return;
            }
            const NavVolumeData volume = obstacleVolume(obstacle, world->tryGet<Transform>(entity));
            if (obstacleOf.size() <= entity.index)
            {
                obstacleOf.resize(entity.index + 1, kNoTrack);
            }
            u32& index = obstacleOf[entity.index];
            if (index != kNoTrack && obstacles[index].entity != entity)
            {
                index = kNoTrack;
            }
            if (index == kNoTrack)
            {
                index = static_cast<u32>(obstacles.size());
                obstacles.push_back(ObstacleTrack{.entity = entity,
                                                  .seen = volume,
                                                  .carved = volume,
                                                  .still = 0.0f,
                                                  .stamp = obstacleStamp,
                                                  .hasCarved = initial});
                return;
            }
            ObstacleTrack& track = obstacles[index];
            track.stamp = obstacleStamp;
            if (!sameVolume(track.seen, volume))
            {
                track.seen = volume;
                track.still = 0.0f;
                return;
            }
            track.still += seconds;
            const bool moved = !track.hasCarved ||
                               glm::distance(track.carved.center, volume.center) > obstacle.moveThreshold ||
                               track.carved.cylinder != volume.cylinder ||
                               glm::any(glm::notEqual(track.carved.halfExtents, volume.halfExtents)) ||
                               std::abs(track.carved.yaw - volume.yaw) > kYawEpsilon;
            if (moved && track.still >= obstacle.settleSeconds && seconds > 0.0f)
            {
                if (track.hasCarved)
                {
                    markDirty(detail::navVolumeBounds(track.carved));
                }
                markDirty(detail::navVolumeBounds(volume));
                track.carved = volume;
                track.hasCarved = true;
            }
        });
        for (usize i = 0; i < obstacles.size();)
        {
            if (obstacles[i].stamp == obstacleStamp)
            {
                ++i;
                continue;
            }
            if (obstacles[i].hasCarved)
            {
                markDirty(detail::navVolumeBounds(obstacles[i].carved));
            }
            const auto back = static_cast<u32>(obstacles.size() - 1);
            if (obstacleOf[obstacles[i].entity.index] == i)
            {
                obstacleOf[obstacles[i].entity.index] = kNoTrack;
            }
            if (i != back)
            {
                obstacles[i] = obstacles[back];
                if (obstacleOf[obstacles[i].entity.index] == back)
                {
                    obstacleOf[obstacles[i].entity.index] = static_cast<u32>(i);
                }
            }
            obstacles.pop_back();
        }
        obstaclesSeen = true;
    }

    // Puts the carving obstacles of @p geometry where the navmesh has them (an obstacle that moved carves
    // only once it settled).
    void placeObstacles(NavGeometry& geometry) const
    {
        for (const ObstacleTrack& track : obstacles)
        {
            if (track.hasCarved && sameVolume(track.carved, track.seen))
            {
                continue;
            }
            const auto found = std::ranges::find_if(geometry.volumes, [&](const NavVolumeData& volume) {
                return sameVolume(volume, track.seen);
            });
            if (found == geometry.volumes.end())
            {
                continue;
            }
            if (track.hasCarved)
            {
                *found = track.carved;
            }
            else
            {
                geometry.volumes.erase(found);
            }
        }
    }

    // --- Collection -----------------------------------------------------------------------------------

    void applyDirty(Surface& surface)
    {
        if (surface.dirty.empty() && !dirtyAll)
        {
            return;
        }
        if (dirtyAll && surface.settings.rebuildAtRuntime && surface.phase == Phase::Ready)
        {
            surface.rehashAll = true;
            surface.collect = true;
        }
        const detail::NavTileGrid& grid = surface.grid;
        const f32 border = surface.input ? surface.input->grid.border : 0.0f;
        const auto cell = [&](f32 value, f32 origin, u32 count) {
            const f32 index = std::floor((value - origin) / grid.tileWorld);
            return static_cast<i64>(std::clamp(index, -1.0f, static_cast<f32>(count)));
        };
        for (const Aabb& bounds : surface.dirty)
        {
            const i64 x0 = std::max<i64>(cell(bounds.min.x - border, grid.origin.x, grid.tilesX), 0);
            const i64 x1 =
                std::min<i64>(cell(bounds.max.x + border, grid.origin.x, grid.tilesX), i64{grid.tilesX} - 1);
            const i64 z0 = std::max<i64>(cell(bounds.min.z - border, grid.origin.z, grid.tilesZ), 0);
            const i64 z1 =
                std::min<i64>(cell(bounds.max.z + border, grid.origin.z, grid.tilesZ), i64{grid.tilesZ} - 1);
            for (i64 z = z0; z <= z1; ++z)
            {
                for (i64 x = x0; x <= x1; ++x)
                {
                    surface.tiles[static_cast<usize>(z) * grid.tilesX + static_cast<usize>(x)].rehash = true;
                    surface.collect = true;
                }
            }
        }
        surface.dirty.clear();
    }

    [[nodiscard]] static bool sameGrid(const detail::NavTileGrid& a, const detail::NavTileGrid& b) noexcept
    {
        return bits(a.origin.x) == bits(b.origin.x) && bits(a.origin.y) == bits(b.origin.y) &&
               bits(a.origin.z) == bits(b.origin.z) && bits(a.tileWorld) == bits(b.tileWorld) &&
               a.tilesX == b.tilesX && a.tilesZ == b.tilesZ;
    }

    // A new, empty navmesh of @p grid whose every tile waits for a build.
    bool startGrid(Surface& surface, const detail::NavTileGrid& grid)
    {
        MeshPtr mesh = makeMesh(grid);
        QueryPtr query = mesh ? makeQuery(*mesh) : QueryPtr{};
        if (!query)
        {
            logFailure(surface, "Detour cannot create a navmesh of that many tiles");
            surface.phase = Phase::Failed;
            return false;
        }
        ++surface.generation;
        surface.query = std::move(query);
        surface.mesh = std::move(mesh);
        surface.grid = grid;
        surface.tiles.assign(usize{grid.tilesX} * grid.tilesZ, TileSlot{});
        for (TileSlot& tile : surface.tiles)
        {
            tile.pending = true;
        }
        surface.installed = 0;
        surface.links.clear();
        return true;
    }

    void collect(Surface& surface)
    {
        if (surface.waitsForMeshes && meshesLoading > 0)
        {
            return;
        }
        ILL_PROFILE_SCOPE_N("NavWorld::collect");
        bool waits = false;
        Expected<NavGeometry, std::string> geometry =
            collectNavGeometry(*world, surface.entity, [&](const AssetRef& mesh, u32 meshIndex) {
                return findMesh(mesh, meshIndex, waits);
            });
        surface.waitsForMeshes = waits;
        if (waits)
        {
            return;
        }
        if (!geometry)
        {
            logFailure(surface, geometry.error());
            surface.phase = Phase::Failed;
            surface.collect = false;
            return;
        }
        placeObstacles(*geometry);
        Expected<detail::NavTileGrid, std::string> grid = detail::makeNavTileGrid(*geometry);
        if (!grid)
        {
            logFailure(surface, grid.error());
            surface.phase = Phase::Failed;
            surface.collect = false;
            return;
        }
        auto input = std::make_shared<BakeInput>();
        input->candidates = detail::navTileCandidates(*geometry, *grid);
        input->grid = *grid;
        input->geometry = std::move(*geometry);

        const bool rebuild = surface.settings.rebuildAtRuntime;
        bool all = surface.rehashAll ||
                   !std::ranges::equal(surface.links, input->geometry.links, {}, {}, &NavLinkData::entity);
        bool compare = true;
        if (surface.bakeAll || !surface.mesh || (!sameGrid(surface.grid, *grid) && rebuild))
        {
            if (!startGrid(surface, *grid))
            {
                surface.collect = false;
                return;
            }
            compare = false;
        }
        else if (!sameGrid(surface.grid, *grid))
        {
            for (TileSlot& tile : surface.tiles)
            {
                tile.stale = true;
                tile.rehash = false;
            }
            compare = false;
        }
        if (compare && (all || std::ranges::any_of(surface.tiles, &TileSlot::rehash)))
        {
            for (usize i = 0; i < surface.tiles.size(); ++i)
            {
                TileSlot& tile = surface.tiles[i];
                if (!all && !tile.rehash)
                {
                    continue;
                }
                tile.rehash = false;
                const NavTileCoord coord{.x = static_cast<i32>(i % grid->tilesX),
                                         .z = static_cast<i32>(i / grid->tilesX)};
                const u64 hash =
                    detail::navTileHash(input->geometry, input->grid, coord, input->candidates[i]);
                const bool building = tile.pending || tile.inFlight;
                if (building && hash == tile.target)
                {
                    continue;
                }
                if (hash == tile.hash)
                {
                    if (building)
                    {
                        ++tile.generation;
                    }
                    tile.pending = false;
                    tile.stale = false;
                }
                else if (rebuild)
                {
                    ++tile.generation;
                    tile.pending = true;
                    tile.target = hash;
                    tile.stale = false;
                }
                else
                {
                    tile.stale = true;
                }
            }
        }
        surface.input = std::move(input);
        surface.bakeAll = false;
        surface.rehashAll = false;
        surface.collect = false;
        surface.phase = Phase::Ready;
        const auto stale = static_cast<u32>(std::ranges::count_if(surface.tiles, &TileSlot::stale));
        if (stale > 0 && !std::exchange(surface.staleLogged, true))
        {
            ILL_LOG_WARN("Navigation",
                         "{} tiles of the navmesh of agent type {} are stale: bake it again or set "
                         "rebuildAtRuntime",
                         stale, surface.settings.agentType);
        }
    }

    // --- Tile builds ----------------------------------------------------------------------------------

    void installTask(TileTask& task)
    {
        Surface& surface = surfaces[task.surface];
        if (surface.generation != task.surfaceGeneration || task.tile >= surface.tiles.size())
        {
            return;
        }
        TileSlot& tile = surface.tiles[task.tile];
        tile.inFlight = false;
        if (tile.generation != task.tileGeneration)
        {
            return;
        }
        if (!task.result.error.empty())
        {
            logFailure(surface, task.result.error);
            return;
        }
        if (tile.ref != 0)
        {
            surface.installed -= tile.polygons > 0 ? 1u : 0u;
            surface.mesh->removeTile(tile.ref, nullptr, nullptr);
            tile.ref = 0;
            tile.polygons = 0;
        }
        tile.hash = task.result.inputHash;
        tile.stale = false;
        ++statistics.tileBuilds;
        if (!task.result.detour.empty())
        {
            tile.ref = addTile(*surface.mesh, task.result.detour);
            if (tile.ref == 0)
            {
                logFailure(surface, "Detour rejected a built tile");
                return;
            }
            tile.polygons = polygonsOf(*surface.mesh, tile.ref);
            surface.installed += tile.polygons > 0 ? 1u : 0u;
        }
        const std::vector<NavLinkData>& links = task.input->geometry.links;
        if (!std::ranges::equal(surface.links, links, {}, {}, &NavLinkData::entity))
        {
            surface.links.resize(links.size());
            std::ranges::transform(links, surface.links.begin(), &NavLinkData::entity);
        }
    }

    // Installs finished builds in the order they started, so that the navmesh does not depend on timing.
    void installFinished()
    {
        while (taskCount > 0)
        {
            TileTask& task = tasks[taskHead];
            if (!task.counter.isDone())
            {
                break;
            }
            installTask(task);
            task.input.reset();
            task.result.detour.clear();
            task.result.error.clear();
            task.jobs = nullptr;
            taskHead = (taskHead + 1) % taskCapacity;
            --taskCount;
        }
    }

    void schedule(JobSystem* jobs)
    {
        for (u32 type = 0; type < kMaxNavAgentTypes && taskCount < taskCapacity; ++type)
        {
            Surface& surface = surfaces[type];
            if (!surface.used() || surface.phase != Phase::Ready || !surface.input)
            {
                continue;
            }
            for (u32 i = 0; i < surface.tiles.size() && taskCount < taskCapacity; ++i)
            {
                TileSlot& tile = surface.tiles[i];
                if (!tile.pending || tile.inFlight)
                {
                    continue;
                }
                TileTask& task = tasks[(taskHead + taskCount) % taskCapacity];
                ++taskCount;
                task.jobs = jobs;
                task.input = surface.input;
                task.surface = type;
                task.surfaceGeneration = surface.generation;
                task.tile = i;
                task.tileGeneration = tile.generation;
                tile.pending = false;
                tile.inFlight = true;
                if (jobs != nullptr)
                {
                    jobs->submit([job = &task] { runTask(*job); }, &task.counter);
                }
                else
                {
                    runTask(task);
                }
            }
        }
    }

    void waitTasks()
    {
        for (u32 i = 0; i < taskCount; ++i)
        {
            TileTask& task = tasks[(taskHead + i) % taskCapacity];
            if (task.jobs != nullptr)
            {
                task.jobs->wait(task.counter);
            }
        }
    }

    // --- Update ---------------------------------------------------------------------------------------

    void updateStatistics() noexcept
    {
        NavStatistics& s = statistics;
        s.surfaces = 0;
        s.tiles = 0;
        s.polygons = 0;
        s.pendingTiles = 0;
        s.staleTiles = 0;
        for (Surface& surface : surfaces)
        {
            surface.pending = 0;
            if (!surface.used())
            {
                continue;
            }
            u32 stale = 0;
            for (const TileSlot& tile : surface.tiles)
            {
                surface.pending += tile.pending || tile.inFlight ? 1u : 0u;
                stale += tile.stale ? 1u : 0u;
                s.polygons += tile.polygons;
            }
            if (surface.phase == Phase::Collect && surface.bakeAll)
            {
                ++surface.pending;
            }
            s.surfaces += surface.installed > 0 ? 1u : 0u;
            s.tiles += surface.installed;
            s.pendingTiles += surface.pending;
            s.staleTiles += stale;
        }
    }

    void update(f32 seconds, JobSystem* jobs, bool advance)
    {
        installFinished();
        for (Surface& surface : surfaces)
        {
            if (surface.fileReady)
            {
                installFile(surface);
            }
        }
        processReloads();
        const Versions now = currentVersions();
        if (!surfacesSynced || now.structural != surfaceVersions.structural ||
            now.surface != surfaceVersions.surface || now.transform != surfaceVersions.transform ||
            now.inactive != surfaceVersions.inactive)
        {
            syncSurfaces();
            surfacesSynced = true;
            surfaceVersions = now;
        }
        advanceMeshes();
        for (Surface& surface : surfaces)
        {
            advanceLoad(surface);
        }
        trackObstacles(advance ? seconds : 0.0f);
        trackSources();
        for (Surface& surface : surfaces)
        {
            if (surface.used() && surface.phase == Phase::Ready)
            {
                applyDirty(surface);
            }
            surface.dirty.clear();
        }
        dirtyAll = false;
        for (Surface& surface : surfaces)
        {
            if (surface.used() && surface.collect &&
                (surface.phase == Phase::Collect || surface.phase == Phase::Ready))
            {
                collect(surface);
            }
        }
        schedule(jobs != nullptr ? jobs : (streamer != nullptr ? &streamer->jobs() : nullptr));
        updateStatistics();
        std::array<detail::NavCrowdSurface, kMaxNavAgentTypes> walkable{};
        for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
        {
            const Surface& surface = surfaces[type];
            if (surface.used() && surface.mesh && surface.installed > 0)
            {
                walkable[type] = detail::NavCrowdSurface{.mesh = surface.mesh.get(),
                                                         .generation = surface.generation,
                                                         .areaCosts = surface.settings.areaCosts,
                                                         .links = surface.links};
            }
        }
        crowd->update(seconds, jobs, advance, walkable, statistics);
    }

    [[nodiscard]] bool busy() const noexcept
    {
        if (taskCount > 0)
        {
            return true;
        }
        return std::ranges::any_of(surfaces, [](const Surface& surface) {
            return surface.used() &&
                   (surface.loading || surface.fileReady || surface.pending > 0 ||
                    (surface.collect && (surface.phase == Phase::Collect || surface.phase == Phase::Ready)));
        });
    }

    // --- Queries --------------------------------------------------------------------------------------

    [[nodiscard]] Expected<const Surface*, NavError> surfaceFor(const NavQueryFilter& filter) const noexcept
    {
        if (filter.agentType >= kMaxNavAgentTypes || !isFinite(filter.searchExtents) ||
            filter.searchExtents.x < 0.0f || filter.searchExtents.y < 0.0f || filter.searchExtents.z < 0.0f)
        {
            return unexpected(NavError::InvalidValue);
        }
        const Surface& surface = surfaces[filter.agentType];
        if (!surface.used() || !surface.query || (surface.installed == 0 && surface.pending > 0))
        {
            return unexpected(NavError::NoNavMesh);
        }
        return &surface;
    }

    static void makeFilter(const Surface& surface, const NavQueryFilter& filter, dtQueryFilter& out) noexcept
    {
        out.setIncludeFlags(static_cast<u16>(filter.areaMask & 0xFFFFu));
        out.setExcludeFlags(0);
        for (u32 area = 0; area < kMaxNavAreas; ++area)
        {
            const f32 cost = surface.settings.areaCosts[area];
            out.setAreaCost(static_cast<i32>(area), std::isfinite(cost) ? std::max(cost, 1.0f) : 1.0f);
        }
    }

    [[nodiscard]] static Expected<dtPolyRef, NavError> nearest(const Surface& surface, const Vec3& position,
                                                               const NavQueryFilter& filter,
                                                               const dtQueryFilter& detour,
                                                               Vec3& point) noexcept
    {
        dtPolyRef ref = 0;
        const dtStatus status =
            surface.query->findNearestPoly(glm::value_ptr(position), glm::value_ptr(filter.searchExtents),
                                           &detour, &ref, glm::value_ptr(point));
        if (dtStatusFailed(status) || ref == 0)
        {
            return unexpected(NavError::OffMesh);
        }
        return ref;
    }
};

NavWorld::NavWorld(World& world, AssetStreamer* streamer, const NavWorldDesc& desc)
    : m_impl(std::make_unique<Impl>())
{
    detail::installNavAllocators();
    Impl& impl = *m_impl;
    impl.world = &world;
    impl.streamer = streamer;
    impl.desc = desc;
    impl.taskCapacity = std::max(desc.maxTileBuildsInFlight, 1u);
    impl.tasks = std::make_unique<TileTask[]>(impl.taskCapacity);
    impl.queryPolygons = std::make_unique<dtPolyRef[]>(static_cast<usize>(kQueryPolygons));
    impl.queryCorners = std::make_unique<f32[]>(static_cast<usize>(kQueryCorners) * 3);
    impl.queryCornerFlags = std::make_unique<u8[]>(static_cast<usize>(kQueryCorners));
    impl.crowd = std::make_unique<detail::NavCrowd>(world, desc);
    if (streamer != nullptr)
    {
        impl.reloaded = streamer->events().subscribe<AssetReloadedEvent>(
            [&impl](const AssetReloadedEvent& event) { impl.reloads.push_back(event.asset); });
    }
}

NavWorld::~NavWorld()
{
    m_impl->waitTasks();
}

void NavWorld::update(f32 seconds, JobSystem* jobs, bool advance)
{
    ILL_PROFILE_SCOPE();
    m_impl->update(seconds, jobs, advance);
}

void NavWorld::waitIdle(JobSystem* jobs)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.update(0.0f, jobs, false);
    while (impl.busy())
    {
        if (impl.streamer != nullptr &&
            (impl.meshesLoading > 0 ||
             std::ranges::any_of(impl.surfaces, [](const Surface& surface) { return surface.loading; })))
        {
            impl.streamer->waitIdle();
        }
        impl.waitTasks();
        impl.update(0.0f, jobs, false);
    }
}

bool NavWorld::isReady(u32 agentType) const noexcept
{
    if (agentType >= kMaxNavAgentTypes)
    {
        return false;
    }
    const Surface& surface = m_impl->surfaces[agentType];
    return surface.used() && surface.mesh != nullptr && surface.phase == Phase::Ready && !surface.collect &&
           !surface.loading && !surface.fileReady &&
           std::ranges::none_of(surface.tiles,
                                [](const TileSlot& tile) { return tile.pending || tile.inFlight; });
}

Expected<NavPathResult, NavError> NavWorld::findPath(const Vec3& from, const Vec3& to, std::span<Vec3> out,
                                                     const NavQueryFilter& filter) const
{
    ILL_PROFILE_SCOPE();
    if (out.empty() || !isFinite(from) || !isFinite(to))
    {
        return unexpected(NavError::InvalidValue);
    }
    Impl& impl = *m_impl;
    const Expected<const Surface*, NavError> found = impl.surfaceFor(filter);
    if (!found)
    {
        return unexpected(found.error());
    }
    const Surface& surface = **found;
    dtQueryFilter detour;
    Impl::makeFilter(surface, filter, detour);
    Vec3 start(0.0f);
    Vec3 end(0.0f);
    const Expected<dtPolyRef, NavError> startRef = Impl::nearest(surface, from, filter, detour, start);
    if (!startRef)
    {
        return unexpected(startRef.error());
    }
    const Expected<dtPolyRef, NavError> endRef = Impl::nearest(surface, to, filter, detour, end);
    if (!endRef)
    {
        return unexpected(endRef.error());
    }
    dtPolyRef* polygons = impl.queryPolygons.get();
    i32 polygonCount = 0;
    const dtStatus status =
        surface.query->findPath(*startRef, *endRef, glm::value_ptr(start), glm::value_ptr(end), &detour,
                                polygons, &polygonCount, kQueryPolygons);
    if (dtStatusFailed(status) || polygonCount == 0)
    {
        return unexpected(NavError::NoPath);
    }
    const bool partial = polygons[polygonCount - 1] != *endRef;
    if (partial)
    {
        Vec3 clamped = end;
        if (dtStatusSucceed(surface.query->closestPointOnPoly(polygons[polygonCount - 1], glm::value_ptr(end),
                                                              glm::value_ptr(clamped), nullptr)))
        {
            // Nothing reachable is nearer the destination than the start.
            if (glm::distance(clamped, end) >= glm::distance(start, end) - kNoProgress)
            {
                return unexpected(NavError::NoPath);
            }
            end = clamped;
        }
    }
    f32* corners = impl.queryCorners.get();
    i32 cornerCount = 0;
    if (dtStatusFailed(surface.query->findStraightPath(glm::value_ptr(start), glm::value_ptr(end), polygons,
                                                       polygonCount, corners, impl.queryCornerFlags.get(),
                                                       nullptr, &cornerCount, kQueryCorners)) ||
        cornerCount == 0)
    {
        return unexpected(NavError::NoPath);
    }
    NavPathResult result{
        .count = 0, .total = static_cast<u32>(cornerCount), .length = 0.0f, .partial = partial};
    result.count = std::min(result.total, static_cast<u32>(out.size()));
    for (u32 i = 0; i < result.total; ++i)
    {
        const Vec3 corner(corners[i * 3], corners[i * 3 + 1], corners[i * 3 + 2]);
        if (i < result.count)
        {
            out[i] = corner;
        }
        if (i > 0)
        {
            result.length += glm::distance(
                Vec3(corners[(i - 1) * 3], corners[(i - 1) * 3 + 1], corners[(i - 1) * 3 + 2]), corner);
        }
    }
    return result;
}

Expected<Vec3, NavError> NavWorld::nearestPoint(const Vec3& position, const NavQueryFilter& filter) const
{
    ILL_PROFILE_SCOPE();
    if (!isFinite(position))
    {
        return unexpected(NavError::InvalidValue);
    }
    const Expected<const Surface*, NavError> found = m_impl->surfaceFor(filter);
    if (!found)
    {
        return unexpected(found.error());
    }
    dtQueryFilter detour;
    Impl::makeFilter(**found, filter, detour);
    Vec3 point(0.0f);
    const Expected<dtPolyRef, NavError> ref = Impl::nearest(**found, position, filter, detour, point);
    if (!ref)
    {
        return unexpected(ref.error());
    }
    return point;
}

Expected<NavRaycastHit, NavError> NavWorld::raycast(const Vec3& from, const Vec3& to,
                                                    const NavQueryFilter& filter) const
{
    ILL_PROFILE_SCOPE();
    if (!isFinite(from) || !isFinite(to))
    {
        return unexpected(NavError::InvalidValue);
    }
    Impl& impl = *m_impl;
    const Expected<const Surface*, NavError> found = impl.surfaceFor(filter);
    if (!found)
    {
        return unexpected(found.error());
    }
    const Surface& surface = **found;
    dtQueryFilter detour;
    Impl::makeFilter(surface, filter, detour);
    Vec3 start(0.0f);
    const Expected<dtPolyRef, NavError> startRef = Impl::nearest(surface, from, filter, detour, start);
    if (!startRef)
    {
        return unexpected(startRef.error());
    }
    f32 t = 0.0f;
    Vec3 normal(0.0f);
    i32 polygonCount = 0;
    if (dtStatusFailed(surface.query->raycast(*startRef, glm::value_ptr(start), glm::value_ptr(to), &detour,
                                              &t, glm::value_ptr(normal), impl.queryPolygons.get(),
                                              &polygonCount, kQueryPolygons)))
    {
        return unexpected(NavError::NoPath);
    }
    NavRaycastHit hit{.hit = t <= 1.0f, .point = to, .normal = Vec3(0.0f), .fraction = 1.0f};
    if (hit.hit)
    {
        hit.fraction = std::clamp(t, 0.0f, 1.0f);
        hit.point = start + (to - start) * hit.fraction;
        const Vec3 flat(normal.x, 0.0f, normal.z);
        const f32 length = glm::length(flat);
        hit.normal = length > 0.0f ? flat / length : Vec3(0.0f);
    }
    f32 height = 0.0f;
    if (polygonCount > 0 &&
        dtStatusSucceed(surface.query->getPolyHeight(impl.queryPolygons[static_cast<usize>(polygonCount - 1)],
                                                     glm::value_ptr(hit.point), &height)))
    {
        hit.point.y = height;
    }
    return hit;
}

Expected<Vec3, NavError> NavWorld::randomPoint(const Vec3& center, f32 radius, u64 seed,
                                               const NavQueryFilter& filter) const
{
    ILL_PROFILE_SCOPE();
    if (!isFinite(center) || !std::isfinite(radius) || radius < 0.0f)
    {
        return unexpected(NavError::InvalidValue);
    }
    const Expected<const Surface*, NavError> found = m_impl->surfaceFor(filter);
    if (!found)
    {
        return unexpected(found.error());
    }
    const Surface& surface = **found;
    dtQueryFilter detour;
    Impl::makeFilter(surface, filter, detour);
    Vec3 start(0.0f);
    const Expected<dtPolyRef, NavError> startRef = Impl::nearest(surface, center, filter, detour, start);
    if (!startRef)
    {
        return unexpected(startRef.error());
    }
    if (radius == 0.0f)
    {
        return start;
    }
    Random random(seed);
    tQueryRandom = &random;
    Vec3 result = start;
    for (u32 attempt = 0; attempt < kRandomTries; ++attempt)
    {
        dtPolyRef ref = 0;
        Vec3 point(0.0f);
        if (dtStatusFailed(surface.query->findRandomPointAroundCircle(
                *startRef, glm::value_ptr(start), radius, &detour, queryRandom, &ref, glm::value_ptr(point))))
        {
            break;
        }
        const Vec2 offset(point.x - center.x, point.z - center.z);
        if (glm::dot(offset, offset) <= radius * radius)
        {
            result = point;
            break;
        }
    }
    tQueryRandom = nullptr;
    return result;
}

Expected<void, NavError> NavWorld::setDestination(Entity entity, const Vec3& destination)
{
    World& world = *m_impl->world;
    NavAgent* agent = world.isAlive(entity) ? world.tryGet<NavAgent>(entity) : nullptr;
    if (agent == nullptr)
    {
        return unexpected(NavError::NoAgent);
    }
    if (!isFinite(destination))
    {
        return unexpected(NavError::InvalidValue);
    }
    agent->destination = destination;
    world.markChanged<NavAgent>();
    return {};
}

Expected<void, NavError> NavWorld::stop(Entity entity)
{
    World& world = *m_impl->world;
    NavAgent* agent = world.isAlive(entity) ? world.tryGet<NavAgent>(entity) : nullptr;
    if (agent == nullptr)
    {
        return unexpected(NavError::NoAgent);
    }
    agent->destination.reset();
    world.markChanged<NavAgent>();
    return {};
}

Expected<void, NavError> NavWorld::warp(Entity entity, const Vec3& position)
{
    World& world = *m_impl->world;
    const NavAgent* agent = world.isAlive(entity) ? world.tryGet<NavAgent>(entity) : nullptr;
    if (agent == nullptr)
    {
        return unexpected(NavError::NoAgent);
    }
    NavQueryFilter filter;
    filter.agentType = agent->agentType;
    filter.areaMask = agent->areaMask;
    const Expected<Vec3, NavError> point = nearestPoint(position, filter);
    if (!point)
    {
        return unexpected(point.error());
    }
    if (Transform* transform = world.tryGet<Transform>(entity))
    {
        transform->position = *point;
        world.markChanged<Transform>();
    }
    else
    {
        Transform placed;
        placed.position = *point;
        world.set(entity, std::move(placed));
    }
    m_impl->crowd->warped(entity);
    return {};
}

std::optional<u32> NavWorld::findSlot(Entity entity) const noexcept
{
    return m_impl->crowd->findSlot(entity);
}

std::span<const Vec3> NavWorld::pathCorners(u32 slot) const noexcept
{
    return m_impl->crowd->pathCorners(slot);
}

std::span<const NavEvent> NavWorld::events() const noexcept
{
    return m_impl->crowd->events();
}

void NavWorld::invalidate(const Aabb& bounds)
{
    if (!bounds.isValid() || !isFinite(bounds.min) || !isFinite(bounds.max))
    {
        return;
    }
    Impl& impl = *m_impl;
    for (Surface& surface : impl.surfaces)
    {
        if (!surface.used() || surface.phase != Phase::Ready)
        {
            continue;
        }
        surface.dirty.push_back(bounds);
        impl.applyDirty(surface);
        if (!surface.settings.rebuildAtRuntime)
        {
            for (TileSlot& tile : surface.tiles)
            {
                if (tile.rehash)
                {
                    tile.rehash = false;
                    tile.stale = true;
                }
            }
            surface.collect = false;
            continue;
        }
        for (TileSlot& tile : surface.tiles)
        {
            if (tile.rehash)
            {
                tile.rehash = false;
                ++tile.generation;
                tile.pending = true;
                tile.target = 0;
            }
        }
        surface.collect = true;
    }
}

void NavWorld::drawDebug(const NavDebugOptions& options, NavDebugSink& sink) const
{
    ILL_PROFILE_SCOPE();
    const Impl& impl = *m_impl;
    const Vec3 lift(0.0f, kDebugLift, 0.0f);
    const Vec4 tileColor(0.2f, 0.2f, 0.2f, 1.0f);
    const Vec4 linkColor(0.9f, 0.8f, 0.1f, 1.0f);
    for (u32 type = 0; type < kMaxNavAgentTypes; ++type)
    {
        const Surface& surface = impl.surfaces[type];
        if ((options.agentTypeMask & (1u << type)) == 0 || !surface.used() || !surface.mesh)
        {
            continue;
        }
        const dtNavMesh& mesh = *surface.mesh;
        for (i32 i = 0; i < mesh.getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = mesh.getTile(i);
            if (tile == nullptr || tile->header == nullptr)
            {
                continue;
            }
            const dtMeshHeader& header = *tile->header;
            const auto vertex = [&](u32 index) {
                return Vec3(tile->verts[index * 3], tile->verts[index * 3 + 1], tile->verts[index * 3 + 2]) +
                       lift;
            };
            if (options.navMesh)
            {
                for (i32 p = 0; p < header.polyCount; ++p)
                {
                    const dtPoly& poly = tile->polys[p];
                    if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                    {
                        continue;
                    }
                    const Vec4 color = areaColor(poly.getArea());
                    const u32 corners = poly.vertCount;
                    for (u32 e = 0; e < corners; ++e)
                    {
                        sink.line(vertex(poly.verts[e]), vertex(poly.verts[(e + 1) % corners]), color);
                    }
                }
            }
            if (options.tiles)
            {
                const f32 y = header.bmin[1];
                const Vec3 a(header.bmin[0], y, header.bmin[2]);
                const Vec3 b(header.bmax[0], y, header.bmin[2]);
                const Vec3 c(header.bmax[0], y, header.bmax[2]);
                const Vec3 d(header.bmin[0], y, header.bmax[2]);
                sink.line(a, b, tileColor);
                sink.line(b, c, tileColor);
                sink.line(c, d, tileColor);
                sink.line(d, a, tileColor);
            }
            if (options.links)
            {
                for (i32 c = 0; c < header.offMeshConCount; ++c)
                {
                    const dtOffMeshConnection& link = tile->offMeshCons[c];
                    const Vec3 start(link.pos[0], link.pos[1], link.pos[2]);
                    const Vec3 end(link.pos[3], link.pos[4], link.pos[5]);
                    const f32 height = 0.25f * glm::distance(Vec2(start.x, start.z), Vec2(end.x, end.z));
                    Vec3 previous = start;
                    for (u32 s = 1; s <= kArcSegments; ++s)
                    {
                        const f32 u = static_cast<f32>(s) / static_cast<f32>(kArcSegments);
                        const Vec3 point =
                            glm::mix(start, end, u) + Vec3(0.0f, 4.0f * height * u * (1.0f - u), 0.0f);
                        sink.line(previous + lift, point + lift, linkColor);
                        previous = point;
                    }
                }
            }
        }
    }
    if (options.agents || options.paths)
    {
        impl.crowd->drawDebug(options.agents, options.paths, options.agentTypeMask, sink);
    }
    if (!options.obstacles)
    {
        return;
    }
    const Vec4 avoidColor(0.9f, 0.5f, 0.1f, 1.0f);
    const Vec4 carveColor(0.9f, 0.15f, 0.1f, 1.0f);
    impl.world->query<const NavObstacle>().without<Inactive>().each([&](Entity entity,
                                                                        const NavObstacle& obstacle) {
        const NavVolumeData volume = obstacleVolume(obstacle, impl.world->tryGet<Transform>(entity));
        const Vec4 color = obstacle.carve ? carveColor : avoidColor;
        const Vec3 up(0.0f, volume.halfExtents.y, 0.0f);
        if (volume.cylinder)
        {
            const f32 r = volume.halfExtents.x;
            for (u32 s = 0; s < kCircleSegments; ++s)
            {
                const f32 a =
                    2.0f * std::numbers::pi_v<f32> * static_cast<f32>(s) / static_cast<f32>(kCircleSegments);
                const f32 b = 2.0f * std::numbers::pi_v<f32> * static_cast<f32>(s + 1) /
                              static_cast<f32>(kCircleSegments);
                const Vec3 p = volume.center + Vec3(r * std::cos(a), 0.0f, r * std::sin(a));
                const Vec3 q = volume.center + Vec3(r * std::cos(b), 0.0f, r * std::sin(b));
                sink.line(p - up, q - up, color);
                sink.line(p + up, q + up, color);
                if (s % 4 == 0)
                {
                    sink.line(p - up, p + up, color);
                }
            }
            return;
        }
        const f32 c = std::cos(volume.yaw);
        const f32 s = std::sin(volume.yaw);
        std::array<Vec3, 4> corners{};
        const f32 signs[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
        for (u32 i = 0; i < 4; ++i)
        {
            const f32 lx = signs[i][0] * volume.halfExtents.x;
            const f32 lz = signs[i][1] * volume.halfExtents.z;
            corners[i] = volume.center + Vec3(c * lx + s * lz, 0.0f, -s * lx + c * lz);
        }
        for (u32 i = 0; i < 4; ++i)
        {
            const Vec3& p = corners[i];
            const Vec3& q = corners[(i + 1) % 4];
            sink.line(p - up, q - up, color);
            sink.line(p + up, q + up, color);
            sink.line(p - up, p + up, color);
        }
    });
}

const NavStatistics& NavWorld::statistics() const noexcept
{
    return m_impl->statistics;
}

World& NavWorld::world() const noexcept
{
    return *m_impl->world;
}

} // namespace illusion

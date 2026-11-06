#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/Arrange.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/play/EditorPhysicsQueries.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace illusion::editor
{
namespace
{

constexpr f32 kMaxDrop = 100000.0f;        // how far a query looks down, in world units
constexpr f32 kGroundHalfSize = 100000.0f; // the ground plane is the top of a box this wide
constexpr f32 kGroundHalfThickness = 1.0f;
constexpr u32 kMaxLoadRounds = 8; // models load, then their geometry cooks: a few rounds at most

bool usesMeshAsset(const Collider& collider) noexcept
{
    return collider.shape == ColliderShape::ConvexHull || collider.shape == ColliderShape::Mesh;
}

bool isActive(World& world, Entity entity)
{
    return !world.has<Inactive>(entity) && !world.has<Disabled>(entity);
}

bool isSurface(World& world, Entity entity)
{
    const RigidBody* body = world.tryGet<RigidBody>(entity);
    return isActive(world, entity) && (body == nullptr || !body->sensor);
}

// A copy for the scratch World: geometry comes from the scratch resolver.
Collider scratchCopy(const Collider& collider) noexcept
{
    Collider copy = collider;
    copy.geometry = {};
    copy.geometryKey = 0;
    return copy;
}

} // namespace

struct EditorPhysicsQueries::Impl
{
    Impl(VirtualFileSystem& files, JobSystem& jobs)
        : streamer(files, jobs, AssetStreamerDesc{.cacheMount = ""})
        , physics(scratch, jobs, PhysicsWorldDesc{.tempMemoryBytes = usize{1} << 20})
        , resolver(physics, streamer)
    {
    }

    World scratch; // static bodies of one query; cooked geometry stays for the next
    AssetStreamer streamer;
    PhysicsWorld physics;
    PhysicsAssetResolver resolver;
    std::vector<Entity> created;
};

EditorPhysicsQueries::EditorPhysicsQueries(EditorContext& editor, JobSystem& jobs)
    : m_editor(editor)
    , m_impl(std::make_unique<Impl>(editor.files(), jobs))
{
    m_editor.setPhysicsQueries(this);
}

EditorPhysicsQueries::~EditorPhysicsQueries()
{
    if (m_editor.physicsQueries() == this)
    {
        m_editor.setPhysicsQueries(nullptr);
    }
}

void EditorPhysicsQueries::dropDistances(World& world, WorldPoseFunction pose,
                                         std::span<const DropQuery> queries, std::span<f32> out)
{
    ILL_ASSERT(out.size() == queries.size(), "one distance per query");
    Impl& impl = *m_impl;
    World& scratch = impl.scratch;
    for (const Entity entity : impl.created)
    {
        scratch.destroy(entity);
    }
    impl.created.clear();

    std::unordered_set<u64> falling;
    for (const DropQuery& query : queries)
    {
        for (const Entity member : query.members)
        {
            falling.insert(member.toBits());
        }
    }

    // The surfaces: every other Collider as a static body, and the ground. Falling convex hulls get a sensor
    // (never hit) so that their geometry cooks too.
    const RigidBody fixed{.motionType = MotionType::Static};
    const RigidBody sensor{.motionType = MotionType::Static, .sensor = true};
    std::unordered_map<u64, Entity> hulls; // falling entity -> its sensor in the scratch World
    std::unordered_set<std::string> files; // model files of mesh colliders: reloaded when they changed
    std::vector<std::pair<Entity, bool>> copies;
    world.query<const Collider>().each([&](Entity entity, const Collider& collider) {
        const bool fallingEntity = falling.contains(entity.toBits());
        if (fallingEntity ? collider.shape == ColliderShape::ConvexHull && isActive(world, entity)
                          : isSurface(world, entity))
        {
            copies.emplace_back(entity, fallingEntity);
        }
    });
    for (const auto& [entity, fallingEntity] : copies)
    {
        const Collider& collider = world.get<Collider>(entity);
        const Entity copy =
            scratch.create(pose(world, entity), scratchCopy(collider), fallingEntity ? sensor : fixed);
        impl.created.push_back(copy);
        if (fallingEntity)
        {
            hulls.emplace(entity.toBits(), copy);
        }
        if (usesMeshAsset(collider) && collider.mesh)
        {
            if (const std::optional<AssetPathParts> parts = splitAssetPath(collider.mesh.path()))
            {
                files.emplace(parts->file);
            }
        }
    }
    Collider ground;
    ground.halfExtents = Vec3(kGroundHalfSize, kGroundHalfThickness, kGroundHalfSize);
    Transform groundPose;
    groundPose.position.y = -kGroundHalfThickness;
    impl.created.push_back(scratch.create(groundPose, ground, fixed));

    // Load and cook the mesh assets now: the command answers in one call.
    for (const std::string& file : files)
    {
        (void)impl.streamer.reload(file);
    }
    for (u32 round = 0; round < kMaxLoadRounds; ++round)
    {
        impl.resolver.update();
        if (impl.resolver.statistics().pending == 0 && impl.streamer.pending() == 0)
        {
            break;
        }
        impl.streamer.waitIdle();
    }
    impl.physics.update(0.0); // creates the bodies without simulating

    const Vec3 down(0.0f, -1.0f, 0.0f);
    for (usize i = 0; i < queries.size(); ++i)
    {
        const DropQuery& query = queries[i];
        f32 distance = std::numeric_limits<f32>::infinity();
        bool swept = false;
        for (const Entity member : query.members)
        {
            const Collider* collider = world.tryGet<Collider>(member);
            if (collider == nullptr || collider->shape == ColliderShape::Mesh || !isActive(world, member))
            {
                continue; // triangle meshes cannot be swept
            }
            Collider shape = *collider;
            if (collider->shape == ColliderShape::ConvexHull)
            {
                const auto hull = hulls.find(member.toBits());
                if (hull == hulls.end() || !scratch.get<Collider>(hull->second).geometry.valid())
                {
                    continue; // no geometry: the hull could not be cooked
                }
                shape = scratch.get<Collider>(hull->second);
            }
            swept = true;
            const Transform placed = pose(world, member);
            const std::optional<ShapeCastHit> hit =
                impl.physics.shapeCast(shape, placed.position, placed.rotation, placed.scale, down, kMaxDrop);
            // Nothing below (it is under the ground already): it stays.
            distance = std::min(distance, hit ? hit->distance : 0.0f);
        }
        if (!swept)
        {
            // A ray from just above the bottom centre of what it draws, so that resting entities stay.
            const Aabb& box = query.box;
            const Vec3 origin((box.min.x + box.max.x) * 0.5f, box.min.y + kArrangeTolerance,
                              (box.min.z + box.max.z) * 0.5f);
            const std::optional<RaycastHit> hit =
                impl.physics.raycast(origin, down, kMaxDrop + kArrangeTolerance);
            distance = hit ? hit->distance - kArrangeTolerance : 0.0f;
        }
        out[i] = std::isfinite(distance) ? std::max(distance, 0.0f) : 0.0f;
    }
}

} // namespace illusion::editor

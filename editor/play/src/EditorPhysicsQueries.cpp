#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/Arrange.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/play/EditorPhysicsQueries.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace illusion::editor
{
namespace
{

constexpr f32 kMaxDrop = 100000.0f;        // how far a query looks down, in world units
constexpr f32 kGroundHalfSize = 100000.0f; // the ground plane is the top of a box this wide
constexpr f32 kGroundHalfThickness = 1.0f;

// Shapes that need cooked geometry, which the edit World does not have.
bool needsGeometry(const Collider& collider) noexcept
{
    return collider.shape == ColliderShape::ConvexHull || collider.shape == ColliderShape::Mesh;
}

bool isSurface(World& world, Entity entity, const Collider& collider)
{
    if (needsGeometry(collider) || world.has<Inactive>(entity) || world.has<Disabled>(entity))
    {
        return false;
    }
    const RigidBody* body = world.tryGet<RigidBody>(entity);
    return body == nullptr || !body->sensor;
}

// @p collider with @p scale applied to its size and offset, as a body would scale it (spheres and capsules
// take the largest axis). A rotated shape under a non-uniform scale is scaled along its own axes.
Collider scaled(const Collider& collider, const Vec3& scale) noexcept
{
    const Vec3 size = glm::abs(scale);
    const f32 uniform = std::max({size.x, size.y, size.z});
    Collider out = collider;
    out.halfExtents *= size;
    out.radius *= uniform;
    out.halfHeight *= size.y;
    out.offset *= scale;
    out.geometry = {};
    out.geometryKey = 0;
    return out;
}

} // namespace

EditorPhysicsQueries::EditorPhysicsQueries(EditorContext& editor, JobSystem& jobs)
    : m_editor(editor)
    , m_jobs(jobs)
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
    std::unordered_set<u64> falling;
    for (const DropQuery& query : queries)
    {
        for (const Entity member : query.members)
        {
            falling.insert(member.toBits());
        }
    }

    // The surfaces: every other Collider as a static body, and the ground.
    std::vector<std::pair<Entity, Collider>> surfaces;
    world.query<const Collider>().each([&](Entity entity, const Collider& collider) {
        if (!falling.contains(entity.toBits()) && isSurface(world, entity, collider))
        {
            surfaces.emplace_back(entity, collider);
        }
    });
    World scratch;
    PhysicsWorldDesc desc;
    desc.maxBodies = static_cast<u32>(surfaces.size()) + 1;
    desc.tempMemoryBytes = usize{1} << 20; // never steps
    PhysicsWorld physics(scratch, m_jobs, desc);
    const RigidBody fixed{.motionType = MotionType::Static};
    for (const auto& [entity, collider] : surfaces)
    {
        Collider shape = collider;
        shape.geometry = {};
        shape.geometryKey = 0;
        scratch.create(pose(world, entity), shape, fixed);
    }
    Collider ground;
    ground.halfExtents = Vec3(kGroundHalfSize, kGroundHalfThickness, kGroundHalfSize);
    Transform groundPose;
    groundPose.position.y = -kGroundHalfThickness;
    scratch.create(groundPose, ground, fixed);
    physics.update(0.0); // creates the bodies without simulating

    const Vec3 down(0.0f, -1.0f, 0.0f);
    for (usize i = 0; i < queries.size(); ++i)
    {
        const DropQuery& query = queries[i];
        f32 distance = std::numeric_limits<f32>::infinity();
        bool swept = false;
        for (const Entity member : query.members)
        {
            const Collider* collider = world.tryGet<Collider>(member);
            if (collider == nullptr || needsGeometry(*collider) || world.has<Inactive>(member) ||
                world.has<Disabled>(member))
            {
                continue;
            }
            swept = true;
            const Transform placed = pose(world, member);
            const std::optional<ShapeCastHit> hit = physics.shapeCast(
                scaled(*collider, placed.scale), placed.position, placed.rotation, down, kMaxDrop);
            // Nothing below (it is under the ground already): it stays.
            distance = std::min(distance, hit ? hit->distance : 0.0f);
        }
        if (!swept)
        {
            // A ray from just above the bottom centre of what it draws, so that resting entities stay.
            const Aabb& box = query.box;
            const Vec3 origin((box.min.x + box.max.x) * 0.5f, box.min.y + kArrangeTolerance,
                              (box.min.z + box.max.z) * 0.5f);
            const std::optional<RaycastHit> hit = physics.raycast(origin, down, kMaxDrop + kArrangeTolerance);
            distance = hit ? hit->distance - kArrangeTolerance : 0.0f;
        }
        out[i] = std::isfinite(distance) ? std::max(distance, 0.0f) : 0.0f;
    }
}

} // namespace illusion::editor

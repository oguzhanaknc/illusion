#include "JoltJobSystem.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <numbers>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace illusion
{
namespace
{

// Jolt keeps process-wide state (allocator hooks, the type factory). It is set up when the first
// PhysicsWorld is created and torn down with the last one.
std::mutex g_joltMutex;
u32 g_joltUsers = 0;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void joltTrace(const char* format, ...)
{
    std::array<char, 512> message{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message.data(), message.size(), format, args);
    va_end(args);
    ILL_LOG_INFO("Physics", "{}", message.data());
}

void acquireJolt()
{
    std::scoped_lock lock(g_joltMutex);
    if (g_joltUsers++ == 0)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &joltTrace;
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
}

void releaseJolt()
{
    std::scoped_lock lock(g_joltMutex);
    if (--g_joltUsers == 0)
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

constexpr JPH::uint kMaxPhysicsJobs = 2048;
constexpr JPH::uint kMaxPhysicsBarriers = 8;

JPH::Vec3 toJolt(const Vec3& v) noexcept
{
    return {v.x, v.y, v.z};
}

JPH::Quat toJolt(const Quat& q) noexcept
{
    return {q.x, q.y, q.z, q.w};
}

Vec3 fromJolt(JPH::Vec3Arg v) noexcept
{
    return {v.GetX(), v.GetY(), v.GetZ()};
}

Quat fromJolt(JPH::QuatArg q) noexcept
{
    return {q.GetW(), q.GetX(), q.GetY(), q.GetZ()};
}

// Object layer = collision layer * 2 + 1 for moving bodies (kinematic, dynamic), + 0 for static ones.
// Two broad-phase layers keep static geometry in its own, rarely updated tree.
constexpr JPH::BroadPhaseLayer kStaticBroadPhase(0);
constexpr JPH::BroadPhaseLayer kMovingBroadPhase(1);

JPH::ObjectLayer objectLayer(u32 layer, bool moving) noexcept
{
    return static_cast<JPH::ObjectLayer>(layer * 2 + (moving ? 1 : 0));
}

u32 collisionLayer(JPH::ObjectLayer layer) noexcept
{
    return static_cast<u32>(layer) / 2;
}

bool isMoving(JPH::ObjectLayer layer) noexcept
{
    return (layer & 1) != 0;
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return isMoving(layer) ? kMovingBroadPhase : kStaticBroadPhase;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == kMovingBroadPhase ? "Moving" : "Static";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
    {
        // Static bodies never look for other static bodies.
        return isMoving(layer) || broadPhase == kMovingBroadPhase;
    }
};

// Collision matrix between layers; read by simulation jobs, written only between updates.
class LayerPairs final : public JPH::ObjectLayerPairFilter
{
public:
    LayerPairs() { m_masks.fill(0xFFFFu); }

    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        if (!isMoving(a) && !isMoving(b))
        {
            return false;
        }
        return collides(collisionLayer(a), collisionLayer(b));
    }

    [[nodiscard]] bool collides(u32 a, u32 b) const noexcept { return (m_masks[a] >> b & 1u) != 0; }

    void set(u32 a, u32 b, bool collide) noexcept
    {
        const auto bitB = static_cast<u16>(1u << b);
        const auto bitA = static_cast<u16>(1u << a);
        m_masks[a] = static_cast<u16>(collide ? m_masks[a] | bitB : m_masks[a] & ~bitB);
        m_masks[b] = static_cast<u16>(collide ? m_masks[b] | bitA : m_masks[b] & ~bitA);
    }

private:
    std::array<u16, kMaxPhysicsLayers> m_masks{};
};

// Entity of each body, indexed by the body's index. Written only between updates, read by contact
// callbacks during an update.
struct BodySlot
{
    JPH::BodyID body;
    Entity entity;
    bool sensor = false;
};

class ContactCollector final : public JPH::ContactListener
{
public:
    ContactCollector(const std::vector<BodySlot>& slots, u32 capacity)
        : m_slots(slots)
        , m_events(capacity)
    {
    }

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override
    {
        ContactEvent event;
        event.type = ContactEventType::Begin;
        event.a = Entity::fromBits(body1.GetUserData());
        event.b = Entity::fromBits(body2.GetUserData());
        event.sensor = body1.IsSensor() || body2.IsSensor();
        const JPH::RVec3 point = manifold.GetWorldSpaceContactPointOn1(0);
        event.point = fromJolt(JPH::Vec3(point));
        event.normal = fromJolt(manifold.mWorldSpaceNormal);
        // Called before the solver runs, so these are the velocities of the impact. The normal points
        // from body 1 to body 2.
        const JPH::Vec3 approach = body1.GetPointVelocity(point) - body2.GetPointVelocity(point);
        event.speed = std::max(approach.Dot(manifold.mWorldSpaceNormal), 0.0f);
        push(event);
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        ContactEvent event;
        event.type = ContactEventType::End;
        // Bodies are locked during this callback (and may be gone), so use the slots.
        const BodySlot* first = slotOf(pair.GetBody1ID());
        const BodySlot* second = slotOf(pair.GetBody2ID());
        event.a = first != nullptr ? first->entity : kNullEntity;
        event.b = second != nullptr ? second->entity : kNullEntity;
        event.sensor = (first != nullptr && first->sensor) || (second != nullptr && second->sensor);
        push(event);
    }

    void reset() noexcept
    {
        m_count.store(0, std::memory_order_relaxed);
        m_view = {};
        m_dropped = 0;
    }

    // Orders the events of the update deterministically (callbacks arrive from several threads).
    void finish()
    {
        const u32 written = m_count.load(std::memory_order_relaxed);
        const auto capacity = static_cast<u32>(m_events.size());
        m_dropped = written > capacity ? written - capacity : 0;
        const u32 count = std::min(written, capacity);
        for (u32 i = 0; i < count; ++i)
        {
            ContactEvent& event = m_events[i];
            if (event.b.toBits() < event.a.toBits())
            {
                std::swap(event.a, event.b);
                event.normal = -event.normal;
            }
        }
        std::sort(m_events.begin(), m_events.begin() + count,
                  [](const ContactEvent& x, const ContactEvent& y) {
                      if (x.type != y.type)
                      {
                          return x.type < y.type;
                      }
                      if (x.a != y.a)
                      {
                          return x.a.toBits() < y.a.toBits();
                      }
                      return x.b.toBits() < y.b.toBits();
                  });
        m_view = std::span<const ContactEvent>(m_events.data(), count);
    }

    [[nodiscard]] std::span<const ContactEvent> events() const noexcept { return m_view; }
    [[nodiscard]] u32 dropped() const noexcept { return m_dropped; }

private:
    // Slots are cleared only when Jolt reuses the body index, so removed bodies still resolve.
    const BodySlot* slotOf(const JPH::BodyID& id) const noexcept
    {
        const BodySlot& slot = m_slots[id.GetIndex()];
        return slot.body == id ? &slot : nullptr;
    }

    void push(const ContactEvent& event) noexcept
    {
        const u32 index = m_count.fetch_add(1, std::memory_order_relaxed);
        if (index < m_events.size())
        {
            m_events[index] = event;
        }
    }

    const std::vector<BodySlot>& m_slots;
    std::vector<ContactEvent> m_events;
    std::atomic<u32> m_count{0};
    std::span<const ContactEvent> m_view;
    u32 m_dropped = 0;
};

// Ignores sensors and one optional body in queries.
class SolidBodies final : public JPH::BodyFilter
{
public:
    explicit SolidBodies(JPH::BodyID ignore = {}) noexcept
        : m_ignore(ignore)
    {
    }

    bool ShouldCollide(const JPH::BodyID& body) const override { return body != m_ignore; }
    bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }

private:
    JPH::BodyID m_ignore;
};

// Collects the Entities of the bodies a shape touches.
class EntityCollector final : public JPH::CollideShapeCollector
{
public:
    EntityCollector(const JPH::BodyInterface& bodies, std::vector<Entity>& out) noexcept
        : m_bodies(bodies)
        , m_out(out)
    {
    }

    void AddHit(const JPH::CollideShapeResult& result) override
    {
        m_out.push_back(Entity::fromBits(m_bodies.GetUserData(result.mBodyID2)));
    }

private:
    const JPH::BodyInterface& m_bodies;
    std::vector<Entity>& m_out;
};

class LayerMaskFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit LayerMaskFilter(u32 mask) noexcept
        : m_mask(mask)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return (m_mask >> collisionLayer(layer) & 1u) != 0;
    }

private:
    u32 m_mask;
};

JPH::EMotionType toJolt(MotionType type) noexcept
{
    switch (type)
    {
    case MotionType::Static:
        return JPH::EMotionType::Static;
    case MotionType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:
        return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

// Builds the collision shape of @p collider scaled by @p scale; @p geometry is the resolved
// Collider::mesh of ConvexHull and Mesh colliders (null if the handle is invalid). Returns null and sets
// @p error if the collider is invalid.
JPH::RefConst<JPH::Shape> createShape(const Collider& collider, const Vec3& scale, const JPH::Shape* geometry,
                                      std::string& error)
{
    JPH::ShapeSettings::ShapeResult result;
    JPH::RefConst<JPH::Shape> shape;
    switch (collider.shape)
    {
    case ColliderShape::ConvexHull:
    case ColliderShape::Mesh:
        if (geometry == nullptr)
        {
            error = collider.shape == ColliderShape::Mesh
                        ? "the collider has no valid mesh (createMesh())"
                        : "the collider has no valid hull (createConvexHull())";
            return nullptr;
        }
        shape = geometry;
        break;
    case ColliderShape::Box:
    {
        const f32 smallest =
            std::min({collider.halfExtents.x, collider.halfExtents.y, collider.halfExtents.z});
        result =
            JPH::BoxShapeSettings(toJolt(collider.halfExtents), std::min(JPH::cDefaultConvexRadius, smallest))
                .Create();
        break;
    }
    case ColliderShape::Sphere:
        result = JPH::SphereShapeSettings(collider.radius).Create();
        break;
    case ColliderShape::Capsule:
        result = JPH::CapsuleShapeSettings(collider.halfHeight, collider.radius).Create();
        break;
    case ColliderShape::Cylinder:
    {
        const f32 convexRadius = std::min({JPH::cDefaultConvexRadius, collider.halfHeight, collider.radius});
        result = JPH::CylinderShapeSettings(collider.halfHeight, collider.radius, convexRadius).Create();
        break;
    }
    }
    if (shape == nullptr)
    {
        if (result.HasError())
        {
            error = result.GetError().c_str();
            return nullptr;
        }
        shape = result.Get();
    }
    if (collider.offset != Vec3(0.0f) || collider.rotation != Quat(1.0f, 0.0f, 0.0f, 0.0f))
    {
        result = JPH::RotatedTranslatedShapeSettings(
                     toJolt(collider.offset), toJolt(glm::normalize(collider.rotation)), shape.GetPtr())
                     .Create();
        if (result.HasError())
        {
            error = result.GetError().c_str();
            return nullptr;
        }
        shape = result.Get();
    }
    if (scale != Vec3(1.0f))
    {
        result = shape->ScaleShape(toJolt(scale));
        if (result.HasError())
        {
            error = result.GetError().c_str();
            return nullptr;
        }
        shape = result.Get();
    }
    return shape;
}

// The shape of a query, built without allocating: primitives live in this object, hulls are shared, and
// Collider::offset and rotation are folded into the query transform.
class QueryShape
{
public:
    QueryShape(const Collider& collider, const JPH::Shape* geometry)
    {
        const f32 radius = collider.radius;
        const f32 halfHeight = collider.halfHeight;
        switch (collider.shape)
        {
        case ColliderShape::Box:
            if (glm::all(glm::greaterThan(collider.halfExtents, Vec3(0.0f))))
            {
                m_shape = &m_box.emplace(toJolt(collider.halfExtents));
            }
            break;
        case ColliderShape::Sphere:
            if (radius > 0.0f)
            {
                m_shape = &m_sphere.emplace(radius);
            }
            break;
        case ColliderShape::Capsule:
            if (radius > 0.0f && halfHeight > 0.0f)
            {
                m_shape = &m_capsule.emplace(halfHeight, radius);
            }
            break;
        case ColliderShape::Cylinder:
            if (radius > 0.0f && halfHeight > 0.0f)
            {
                m_shape = &m_cylinder.emplace(halfHeight, radius,
                                              std::min({JPH::cDefaultConvexRadius, halfHeight, radius}));
            }
            break;
        case ColliderShape::ConvexHull:
            m_shape = geometry;
            break;
        case ColliderShape::Mesh:
            ILL_ASSERT(false, "queries cannot use Mesh colliders (they are not convex); use ConvexHull");
            break;
        }
        if (m_shape != nullptr && m_shape != geometry)
        {
            m_shape->SetEmbedded(); // owned here: Jolt must never delete it
        }
    }

    QueryShape(const QueryShape&) = delete;
    QueryShape& operator=(const QueryShape&) = delete;

    // Null if the collider is invalid.
    [[nodiscard]] const JPH::Shape* get() const noexcept { return m_shape; }

    // Places the shape for a query at @p position with @p rotation.
    [[nodiscard]] static JPH::RMat44 transform(const Collider& collider, const Vec3& position,
                                               const Quat& rotation)
    {
        const Quat orientation = glm::normalize(rotation) * glm::normalize(collider.rotation);
        const Vec3 origin = position + rotation * collider.offset;
        return JPH::RMat44::sRotationTranslation(toJolt(orientation), toJolt(origin));
    }

private:
    std::optional<JPH::BoxShape> m_box;
    std::optional<JPH::SphereShape> m_sphere;
    std::optional<JPH::CapsuleShape> m_capsule;
    std::optional<JPH::CylinderShape> m_cylinder;
    const JPH::Shape* m_shape = nullptr;
};

} // namespace

struct PhysicsWorld::Impl
{
    struct JoltScope
    {
        JoltScope() { acquireJolt(); }
        ~JoltScope() { releaseJolt(); }
        JoltScope(const JoltScope&) = delete;
        JoltScope& operator=(const JoltScope&) = delete;
    };

    // A live body and its Entity, for detecting removed Entities and components.
    struct Record
    {
        Entity entity;
        JPH::BodyID body;
    };

    // Collision geometry made by createMesh() or createConvexHull().
    struct Geometry
    {
        JPH::RefConst<JPH::Shape> shape;
        bool mesh = false;
        bool live = false;
        u32 generation = 1;
    };

    struct Character
    {
        Entity entity;
        JPH::Ref<JPH::CharacterVirtual> character; // null for a free slot
        f32 gravityFactor = 1.0f;
        f32 stepHeight = 0.0f;
        JPH::ObjectLayer layer = 0;
    };

    struct JointRecord
    {
        Entity entity;
        JPH::Ref<JPH::TwoBodyConstraint> constraint; // null for a free slot
        JointType type = JointType::Point;
        JPH::BodyID bodyA;
        JPH::BodyID bodyB;  // invalid when connected to the world
        Vec3 anchorA{0.0f}; // in body A's space, scaled
        Vec3 anchorB{0.0f}; // in body B's space (world space without body B)
        f32 breakForce = 0.0f;
        bool motor = false;
        bool broken = false;
    };

    Impl(World& targetWorld, JobSystem& jobSystem, const PhysicsWorldDesc& settings)
        : world(targetWorld)
        , desc(settings)
        , timestep(settings.stepSeconds, settings.maxStepsPerUpdate)
        , joltJobs(jobSystem, kMaxPhysicsJobs, kMaxPhysicsBarriers)
        , tempAllocator(static_cast<JPH::uint>(settings.tempMemoryBytes))
        , slots(settings.maxBodies)
        , contacts(slots, settings.maxContactEvents)
    {
        physics.Init(settings.maxBodies, 0, settings.maxBodyPairs, settings.maxContactConstraints,
                     broadPhaseLayers, objectVsBroadPhase, layerPairs);
        physics.SetGravity(toJolt(settings.gravity));
        physics.SetContactListener(&contacts);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    [[nodiscard]] bool isRebuilt(Entity entity) const
    {
        return std::find(rebuilds.begin(), rebuilds.end(), entity) != rebuilds.end();
    }

    // --- Geometry -------------------------------------------------------------------------------------

    PhysicsMeshHandle addGeometry(JPH::RefConst<JPH::Shape> shape, bool mesh)
    {
        u32 index = 0;
        if (!freeGeometry.empty())
        {
            index = freeGeometry.back();
            freeGeometry.pop_back();
        }
        else
        {
            index = static_cast<u32>(geometry.size());
            geometry.emplace_back();
        }
        Geometry& slot = geometry[index];
        slot.shape = std::move(shape);
        slot.mesh = mesh;
        slot.live = true;
        return PhysicsMeshHandle{index, slot.generation};
    }

    [[nodiscard]] const Geometry* geometryOf(PhysicsMeshHandle handle) const noexcept
    {
        if (handle.index >= geometry.size())
        {
            return nullptr;
        }
        const Geometry& slot = geometry[handle.index];
        return slot.live && slot.generation == handle.generation ? &slot : nullptr;
    }

    // The geometry of a ConvexHull or Mesh collider, or null if its handle is invalid or of the other kind.
    [[nodiscard]] const JPH::Shape* resolveGeometry(const Collider& collider) const noexcept
    {
        if (collider.shape != ColliderShape::ConvexHull && collider.shape != ColliderShape::Mesh)
        {
            return nullptr;
        }
        const Geometry* found = geometryOf(collider.mesh);
        return found != nullptr && found->mesh == (collider.shape == ColliderShape::Mesh)
                   ? found->shape.GetPtr()
                   : nullptr;
    }

    // --- Characters -----------------------------------------------------------------------------------

    [[nodiscard]] JPH::CharacterVirtual* characterOf(Entity entity) const
    {
        if (!world.isAlive(entity))
        {
            return nullptr;
        }
        const CharacterBody* link = world.tryGet<CharacterBody>(entity);
        if (link == nullptr || link->id >= characters.size())
        {
            return nullptr;
        }
        const Character& slot = characters[link->id];
        return slot.entity == entity ? slot.character.GetPtr() : nullptr;
    }

    // The body that represents @p entity in queries: its rigid body or its character's inner body.
    [[nodiscard]] JPH::BodyID queryBodyOf(Entity entity) const
    {
        if (entity.isNull())
        {
            return {};
        }
        const JPH::BodyID body = bodyOf(entity);
        if (!body.IsInvalid())
        {
            return body;
        }
        const JPH::CharacterVirtual* character = characterOf(entity);
        return character != nullptr ? character->GetInnerBodyID() : JPH::BodyID();
    }

    void destroyCharacter(u32 id)
    {
        Character& slot = characters[id];
        characterCollision.Remove(slot.character.GetPtr());
        slot = Character{}; // the CharacterVirtual removes its inner body
        freeCharacters.push_back(id);
    }

    void removeStaleCharacters()
    {
        scratchEntities.clear();
        for (u32 id = 0; id < characters.size(); ++id)
        {
            const Character& slot = characters[id];
            if (slot.character == nullptr)
            {
                continue;
            }
            const bool alive = world.isAlive(slot.entity);
            const CharacterBody* link = alive ? world.tryGet<CharacterBody>(slot.entity) : nullptr;
            const bool keep = link != nullptr && link->id == id &&
                              world.has<CharacterController>(slot.entity) &&
                              world.has<Transform>(slot.entity) && !world.has<Inactive>(slot.entity) &&
                              !isRebuilt(slot.entity);
            if (!keep)
            {
                if (link != nullptr)
                {
                    scratchEntities.push_back(slot.entity);
                }
                destroyCharacter(id);
            }
        }
        for (const Entity entity : rebuilds)
        {
            // Characters that failed to build get another chance.
            if (world.isAlive(entity) && world.has<CharacterBody>(entity) &&
                std::find(scratchEntities.begin(), scratchEntities.end(), entity) == scratchEntities.end())
            {
                scratchEntities.push_back(entity);
            }
        }
        for (const Entity entity : scratchEntities)
        {
            world.remove<CharacterBody>(entity);
            world.remove<WorldSpaceAuthority>(entity);
        }
    }

    void createCharacters()
    {
        scratchEntities.clear();
        world.query<const Transform, const CharacterController>().without<CharacterBody, Inactive>().each(
            [this](Entity entity, const Transform&, const CharacterController&) {
                scratchEntities.push_back(entity);
            });
        for (const Entity entity : scratchEntities)
        {
            const Transform& transform = world.get<Transform>(entity);
            const CharacterController& controller = world.get<CharacterController>(entity);
            CharacterBody link;
            link.previousPosition = link.writtenPosition = transform.position;
            const f32 halfCylinder = 0.5f * controller.height - controller.radius;
            if (!(controller.radius > 0.0f) || !(halfCylinder > 0.0f) || !(controller.mass > 0.0f))
            {
                ILL_LOG_WARN(
                    "Physics",
                    "entity {}: cannot create a character: radius and mass must be positive and height "
                    "larger than 2 * radius (fix it and call rebuild())",
                    entity.index);
                world.emplace<CharacterBody>(entity, link);
                continue;
            }
            ILL_ASSERT(controller.layer < kMaxPhysicsLayers, "physics layer {} out of range",
                       controller.layer);
            const JPH::ObjectLayer layer =
                objectLayer(std::min(controller.layer, kMaxPhysicsLayers - 1), true);
            // The capsule stands on the origin, so the Transform position is at the feet.
            const JPH::RefConst<JPH::Shape> capsule =
                JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, 0.5f * controller.height, 0.0f),
                                                    JPH::Quat::sIdentity(),
                                                    new JPH::CapsuleShape(halfCylinder, controller.radius))
                    .Create()
                    .Get();
            JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
            settings->mShape = capsule;
            settings->mInnerBodyShape = capsule; // lets raycasts, sensors and contacts see the character
            settings->mInnerBodyLayer = layer;
            settings->mMaxSlopeAngle = controller.maxSlope;
            settings->mMass = controller.mass;
            settings->mMaxStrength = std::max(controller.maxPushForce, 0.0f);
            // Contacts with the lower half-sphere support the character.
            settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -controller.radius);

            u32 id = 0;
            if (!freeCharacters.empty())
            {
                id = freeCharacters.back();
                freeCharacters.pop_back();
            }
            else
            {
                id = static_cast<u32>(characters.size());
                characters.emplace_back();
            }
            Character& slot = characters[id];
            slot.entity = entity;
            slot.character = new JPH::CharacterVirtual(settings, toJolt(transform.position),
                                                       JPH::Quat::sIdentity(), entity.toBits(), &physics);
            slot.gravityFactor = controller.gravityFactor;
            slot.stepHeight = std::max(controller.stepHeight, 0.0f);
            slot.layer = layer;
            slot.character->SetCharacterVsCharacterCollision(&characterCollision);
            characterCollision.Add(slot.character.GetPtr());
            const JPH::BodyID inner = slot.character->GetInnerBodyID();
            if (!inner.IsInvalid())
            {
                slots[inner.GetIndex()] = BodySlot{inner, entity, false};
            }
            link.id = id;
            world.emplace<CharacterBody>(entity, link);
            world.set(entity, WorldSpaceAuthority{}); // the character moves it in world space
        }
    }

    // Moves every character by one step. Gravity accelerates characters in the air; the ground stops
    // downward motion and a ceiling stops upward motion.
    void stepCharacters(f32 seconds)
    {
        const JPH::Vec3 gravity = physics.GetGravity();
        for (Character& slot : characters)
        {
            if (slot.character == nullptr)
            {
                continue;
            }
            JPH::CharacterVirtual& character = *slot.character;
            JPH::Vec3 velocity = character.GetLinearVelocity();
            if (character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround)
            {
                const f32 groundY = character.GetGroundVelocity().GetY();
                if (velocity.GetY() < groundY)
                {
                    velocity.SetY(groundY);
                }
            }
            else
            {
                velocity += gravity * (slot.gravityFactor * seconds);
            }
            character.SetLinearVelocity(velocity);

            JPH::CharacterVirtual::ExtendedUpdateSettings update;
            update.mStickToFloorStepDown = JPH::Vec3(0.0f, -slot.stepHeight, 0.0f);
            update.mWalkStairsStepUp = JPH::Vec3(0.0f, slot.stepHeight, 0.0f);
            const JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(objectVsBroadPhase, slot.layer);
            const JPH::DefaultObjectLayerFilter layerFilter(layerPairs, slot.layer);
            const JPH::BodyFilter bodyFilter;
            const JPH::ShapeFilter shapeFilter;
            const f32 startY = static_cast<f32>(character.GetPosition().GetY());
            character.ExtendedUpdate(seconds, gravity * slot.gravityFactor, update, broadPhaseFilter,
                                     layerFilter, bodyFilter, shapeFilter, tempAllocator);
            if (velocity.GetY() > 0.0f)
            {
                const f32 rise = (static_cast<f32>(character.GetPosition().GetY()) - startY) / seconds;
                if (rise < velocity.GetY())
                {
                    velocity.SetY(std::max(rise, 0.0f));
                    character.SetLinearVelocity(velocity);
                }
            }
        }
    }

    // --- Joints ---------------------------------------------------------------------------------------

    void destroyJoint(u32 id)
    {
        JointRecord& record = joints[id];
        physics.RemoveConstraint(record.constraint);
        record = JointRecord{};
        freeJoints.push_back(id);
    }

    // Removes joints whose Entity is gone, lost its Joint or is rebuilt, and those attached to a body in
    // @p destroyedBodies (Jolt constraints must not outlive their bodies).
    void removeStaleJoints(std::span<const JPH::BodyID> destroyedBodies)
    {
        scratchJoints.clear();
        for (u32 id = 0; id < joints.size(); ++id)
        {
            const JointRecord& record = joints[id];
            if (record.constraint == nullptr)
            {
                continue;
            }
            const bool alive = world.isAlive(record.entity);
            const PhysicsJoint* link = alive ? world.tryGet<PhysicsJoint>(record.entity) : nullptr;
            const auto destroyed = [&](const JPH::BodyID& body) {
                return !body.IsInvalid() && std::find(destroyedBodies.begin(), destroyedBodies.end(), body) !=
                                                destroyedBodies.end();
            };
            const bool keep = link != nullptr && link->id == id && world.has<Joint>(record.entity) &&
                              !world.has<Inactive>(record.entity) && !isRebuilt(record.entity) &&
                              !destroyed(record.bodyA) && !destroyed(record.bodyB);
            if (!keep)
            {
                if (link != nullptr)
                {
                    scratchJoints.push_back(record.entity);
                }
                destroyJoint(id);
            }
        }
        for (const Entity entity : rebuilds)
        {
            if (world.isAlive(entity) && world.has<PhysicsJoint>(entity) &&
                std::find(scratchJoints.begin(), scratchJoints.end(), entity) == scratchJoints.end())
            {
                scratchJoints.push_back(entity); // a joint that failed to build
            }
        }
        for (const Entity entity : scratchJoints)
        {
            world.remove<PhysicsJoint>(entity);
        }
    }

    // Body and pose of the joint end on @p entity, or false (with @p error) if it has none.
    bool jointEnd(Entity entity, JPH::BodyID& body, JPH::RVec3& position, JPH::Quat& rotation, Vec3& scale,
                  std::string_view& error) const
    {
        if (!world.isAlive(entity))
        {
            error = "the connected Entity does not exist";
            return false;
        }
        body = bodyOf(entity);
        if (body.IsInvalid())
        {
            error = "an Entity of the joint has no body (RigidBody + Collider)";
            return false;
        }
        physics.GetBodyInterfaceNoLock().GetPositionAndRotation(body, position, rotation);
        const Transform* transform = world.tryGet<Transform>(entity);
        scale = transform != nullptr ? transform->scale : Vec3(1.0f);
        return true;
    }

    void createJoints()
    {
        scratchJoints.clear();
        // A joint whose connected Entity is Inactive waits until it is active again.
        world.query<const Joint>().without<PhysicsJoint, Inactive>().each(
            [this](Entity entity, const Joint& joint) {
                if (joint.connected.isNull() || !world.isAlive(joint.connected) ||
                    !world.has<Inactive>(joint.connected))
                {
                    scratchJoints.push_back(entity);
                }
            });
        for (const Entity entity : scratchJoints)
        {
            PhysicsJoint link;
            std::string_view error;
            if (const std::optional<u32> id = createJoint(entity, world.get<Joint>(entity), error))
            {
                link.id = *id;
            }
            else
            {
                ILL_LOG_WARN("Physics", "entity {}: cannot create a joint: {} (fix it and call rebuild())",
                             entity.index, error);
            }
            world.emplace<PhysicsJoint>(entity, link);
        }
    }

    std::optional<u32> createJoint(Entity entity, const Joint& joint, std::string_view& error)
    {
        JPH::BodyID bodyA;
        JPH::RVec3 positionA;
        JPH::Quat rotationA;
        Vec3 scaleA;
        if (!jointEnd(entity, bodyA, positionA, rotationA, scaleA, error))
        {
            return std::nullopt;
        }
        JPH::BodyID bodyB;
        JPH::RVec3 positionB = JPH::RVec3::sZero();
        JPH::Quat rotationB = JPH::Quat::sIdentity();
        Vec3 scaleB(1.0f);
        if (!joint.connected.isNull() &&
            !jointEnd(joint.connected, bodyB, positionB, rotationB, scaleB, error))
        {
            return std::nullopt;
        }
        if (bodyA == bodyB)
        {
            error = "a joint cannot connect a body to itself";
            return std::nullopt;
        }
        const JPH::BodyLockInterfaceNoLock& locks = physics.GetBodyLockInterfaceNoLock();
        JPH::BodyLockWrite lockA(locks, bodyA);
        std::optional<JPH::BodyLockWrite> lockB;
        if (!bodyB.IsInvalid())
        {
            lockB.emplace(locks, bodyB);
        }
        // Jolt measures a joint's angle or position as body 2 relative to body 1, so the connected body
        // (or the world) is body 1 and this Entity's body is body 2.
        JPH::Body& a = lockA.GetBody();
        JPH::Body& b = lockB ? lockB->GetBody() : JPH::Body::sFixedToWorld;
        if (!a.IsDynamic() && !b.IsDynamic())
        {
            error = "at least one of the joined bodies must be dynamic";
            return std::nullopt;
        }

        const Vec3 localAnchorA = scaleA * joint.anchor;
        const JPH::RVec3 worldAnchor = positionA + rotationA * toJolt(localAnchorA);
        const f32 axisLength = glm::length(joint.axis);
        const JPH::Vec3 worldAxis = axisLength > 0.0f
                                        ? (rotationA * toJolt(joint.axis / axisLength)).Normalized()
                                        : JPH::Vec3::sAxisX();
        const JPH::Vec3 normal = worldAxis.GetNormalizedPerpendicular();
        const f32 minLimit = std::min(joint.minLimit, 0.0f);
        const f32 maxLimit = std::max(joint.maxLimit, 0.0f);
        const bool motor = joint.motorMaxForce > 0.0f;

        JointRecord record;
        record.entity = entity;
        record.type = joint.type;
        record.bodyA = bodyA;
        record.bodyB = bodyB;
        record.anchorA = localAnchorA;
        record.anchorB = lockB ? scaleB * joint.connectedAnchor : joint.connectedAnchor;
        record.breakForce = std::max(joint.breakForce, 0.0f);
        record.motor = motor && (joint.type == JointType::Hinge || joint.type == JointType::Slider);

        switch (joint.type)
        {
        case JointType::Fixed:
        {
            JPH::FixedConstraintSettings settings;
            settings.mPoint1 = settings.mPoint2 = worldAnchor;
            record.constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(b, a));
            break;
        }
        case JointType::Point:
        {
            JPH::PointConstraintSettings settings;
            settings.mPoint1 = settings.mPoint2 = worldAnchor;
            record.constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(b, a));
            break;
        }
        case JointType::Hinge:
        {
            JPH::HingeConstraintSettings settings;
            settings.mPoint1 = settings.mPoint2 = worldAnchor;
            settings.mHingeAxis1 = settings.mHingeAxis2 = worldAxis;
            settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
            if (joint.limited)
            {
                settings.mLimitsMin = std::max(minLimit, -std::numbers::pi_v<f32>);
                settings.mLimitsMax = std::min(maxLimit, std::numbers::pi_v<f32>);
            }
            if (record.motor)
            {
                settings.mMotorSettings.SetTorqueLimit(joint.motorMaxForce);
            }
            auto* hinge = static_cast<JPH::HingeConstraint*>(settings.Create(b, a));
            if (record.motor)
            {
                hinge->SetMotorState(JPH::EMotorState::Velocity);
                hinge->SetTargetAngularVelocity(joint.motorSpeed);
            }
            record.constraint = hinge;
            break;
        }
        case JointType::Slider:
        {
            JPH::SliderConstraintSettings settings;
            settings.mPoint1 = settings.mPoint2 = worldAnchor;
            settings.mSliderAxis1 = settings.mSliderAxis2 = worldAxis;
            settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
            if (joint.limited)
            {
                settings.mLimitsMin = minLimit;
                settings.mLimitsMax = maxLimit;
            }
            if (record.motor)
            {
                settings.mMotorSettings.SetForceLimit(joint.motorMaxForce);
            }
            auto* slider = static_cast<JPH::SliderConstraint*>(settings.Create(b, a));
            if (record.motor)
            {
                slider->SetMotorState(JPH::EMotorState::Velocity);
                slider->SetTargetVelocity(joint.motorSpeed);
            }
            record.constraint = slider;
            break;
        }
        case JointType::Distance:
        {
            JPH::DistanceConstraintSettings settings;
            settings.mPoint1 =
                lockB ? positionB + rotationB * toJolt(record.anchorB) : JPH::RVec3(toJolt(record.anchorB));
            settings.mPoint2 = worldAnchor;
            if (joint.minLimit != 0.0f || joint.maxLimit != 0.0f)
            {
                settings.mMinDistance = std::max(joint.minLimit, 0.0f);
                settings.mMaxDistance = std::max(joint.maxLimit, settings.mMinDistance);
            }
            record.constraint = static_cast<JPH::TwoBodyConstraint*>(settings.Create(b, a));
            break;
        }
        }
        physics.AddConstraint(record.constraint);
        JPH::BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        lockB.reset();
        bodies.ActivateBody(bodyA);
        if (!bodyB.IsInvalid())
        {
            bodies.ActivateBody(bodyB);
        }

        u32 id = 0;
        if (!freeJoints.empty())
        {
            id = freeJoints.back();
            freeJoints.pop_back();
        }
        else
        {
            id = static_cast<u32>(joints.size());
            joints.emplace_back();
        }
        joints[id] = std::move(record);
        return id;
    }

    // Applies Joint::motorSpeed, which game code may change every frame.
    void driveJointMotors()
    {
        for (const JointRecord& record : joints)
        {
            if (record.constraint == nullptr || !record.motor || record.broken)
            {
                continue;
            }
            const Joint* joint = world.tryGet<Joint>(record.entity);
            if (joint == nullptr)
            {
                continue;
            }
            if (record.type == JointType::Hinge)
            {
                static_cast<JPH::HingeConstraint*>(record.constraint.GetPtr())
                    ->SetTargetAngularVelocity(joint->motorSpeed);
            }
            else
            {
                static_cast<JPH::SliderConstraint*>(record.constraint.GetPtr())
                    ->SetTargetVelocity(joint->motorSpeed);
            }
        }
    }

    // Linear force in N the joint applied during the last step of @p seconds.
    static f32 jointForce(const JointRecord& record, f32 seconds)
    {
        JPH::Constraint* constraint = record.constraint.GetPtr();
        f32 impulse = 0.0f;
        switch (record.type)
        {
        case JointType::Fixed:
            impulse = static_cast<JPH::FixedConstraint*>(constraint)->GetTotalLambdaPosition().Length();
            break;
        case JointType::Point:
            impulse = static_cast<JPH::PointConstraint*>(constraint)->GetTotalLambdaPosition().Length();
            break;
        case JointType::Hinge:
            impulse = static_cast<JPH::HingeConstraint*>(constraint)->GetTotalLambdaPosition().Length();
            break;
        case JointType::Slider:
        {
            const JPH::Vector<2> lambda =
                static_cast<JPH::SliderConstraint*>(constraint)->GetTotalLambdaPosition();
            impulse = std::sqrt(lambda[0] * lambda[0] + lambda[1] * lambda[1]);
            break;
        }
        case JointType::Distance:
            impulse = std::abs(static_cast<JPH::DistanceConstraint*>(constraint)->GetTotalLambdaPosition());
            break;
        }
        return impulse / seconds;
    }

    // Disables joints that carried more than their break force in the last step.
    void breakJoints(f32 seconds)
    {
        for (JointRecord& record : joints)
        {
            if (record.constraint == nullptr || record.broken || record.breakForce <= 0.0f ||
                jointForce(record, seconds) <= record.breakForce)
            {
                continue;
            }
            record.broken = true;
            record.constraint->SetEnabled(false);
            brokenJoints.push_back(record.entity);
            if (PhysicsJoint* link = world.tryGet<PhysicsJoint>(record.entity))
            {
                link->broken = true;
            }
        }
    }

    JPH::BodyID bodyOf(Entity entity) const
    {
        if (!world.isAlive(entity))
        {
            return {};
        }
        const PhysicsBody* body = world.tryGet<PhysicsBody>(entity);
        return body != nullptr && body->id != PhysicsBody::kInvalidId ? JPH::BodyID(body->id) : JPH::BodyID();
    }

    void destroyBodies(std::span<JPH::BodyID> ids)
    {
        if (ids.empty())
        {
            return;
        }
        JPH::BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        bodies.RemoveBodies(ids.data(), static_cast<int>(ids.size()));
        bodies.DestroyBodies(ids.data(), static_cast<int>(ids.size()));
    }

    // Removes bodies whose Entity is gone, lost a component, or is being rebuilt.
    void removeStaleBodies()
    {
        scratchIds.clear();
        scratchEntities.clear();
        for (usize i = 0; i < records.size();)
        {
            const Record& record = records[i];
            const bool alive = world.isAlive(record.entity);
            const PhysicsBody* link = alive ? world.tryGet<PhysicsBody>(record.entity) : nullptr;
            const bool keep = link != nullptr && link->id == record.body.GetIndexAndSequenceNumber() &&
                              world.has<RigidBody>(record.entity) && world.has<Collider>(record.entity) &&
                              world.has<Transform>(record.entity) && !world.has<Inactive>(record.entity) &&
                              !isRebuilt(record.entity);
            if (keep)
            {
                ++i;
                continue;
            }
            scratchIds.push_back(record.body);
            if (link != nullptr)
            {
                scratchEntities.push_back(record.entity);
            }
            records[i] = records.back();
            records.pop_back();
        }
        removeStaleJoints(scratchIds); // before their bodies go
        destroyBodies(scratchIds);
        // Rebuilt Entities and those that lost a component also lose their PhysicsBody link.
        for (const Entity entity : rebuilds)
        {
            if (world.isAlive(entity) && world.has<PhysicsBody>(entity) &&
                std::find(scratchEntities.begin(), scratchEntities.end(), entity) == scratchEntities.end())
            {
                scratchEntities.push_back(entity); // a body that failed to build
            }
        }
        for (const Entity entity : scratchEntities)
        {
            world.remove<PhysicsBody>(entity);
            world.remove<WorldSpaceAuthority>(entity);
        }
    }

    void createBodies()
    {
        scratchEntities.clear();
        world.query<const Transform, const RigidBody, const Collider>().without<PhysicsBody, Inactive>().each(
            [this](Entity entity, const Transform&, const RigidBody&, const Collider&) {
                scratchEntities.push_back(entity);
            });
        if (scratchEntities.empty())
        {
            return;
        }
        ILL_PROFILE_SCOPE_N("Create bodies");
        JPH::BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        scratchIds.clear();
        for (const Entity entity : scratchEntities)
        {
            const Transform& transform = world.get<Transform>(entity);
            const RigidBody& rigidBody = world.get<RigidBody>(entity);
            const Collider& collider = world.get<Collider>(entity);
            PhysicsBody link;
            link.previousPosition = link.writtenPosition = transform.position;
            link.previousRotation = link.writtenRotation = transform.rotation;

            std::string error;
            JPH::RefConst<JPH::Shape> shape;
            if (collider.shape == ColliderShape::Mesh && rigidBody.motionType == MotionType::Dynamic)
            {
                error = "mesh colliders only work on static and kinematic bodies (use ConvexHull)";
            }
            else
            {
                shape = createShape(collider, transform.scale, resolveGeometry(collider), error);
            }
            JPH::Body* body = nullptr;
            if (shape != nullptr)
            {
                ILL_ASSERT(rigidBody.layer < kMaxPhysicsLayers, "physics layer {} out of range",
                           rigidBody.layer);
                const bool moving = rigidBody.motionType != MotionType::Static;
                JPH::BodyCreationSettings settings(
                    shape, toJolt(transform.position), toJolt(glm::normalize(transform.rotation)),
                    toJolt(rigidBody.motionType),
                    objectLayer(std::min(rigidBody.layer, kMaxPhysicsLayers - 1), moving));
                settings.mFriction = rigidBody.friction;
                settings.mRestitution = rigidBody.restitution;
                settings.mLinearDamping = rigidBody.linearDamping;
                settings.mAngularDamping = rigidBody.angularDamping;
                settings.mGravityFactor = rigidBody.gravityFactor;
                settings.mIsSensor = rigidBody.sensor;
                settings.mAllowSleeping = rigidBody.allowSleeping;
                settings.mMotionQuality = rigidBody.continuousCollision ? JPH::EMotionQuality::LinearCast
                                                                        : JPH::EMotionQuality::Discrete;
                settings.mUserData = entity.toBits();
                if (rigidBody.motionType == MotionType::Dynamic && rigidBody.mass > 0.0f)
                {
                    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                    settings.mMassPropertiesOverride.mMass = rigidBody.mass;
                }
                // Sensors should also see static geometry.
                settings.mCollideKinematicVsNonDynamic = rigidBody.sensor;
                body = bodies.CreateBody(settings);
                if (body == nullptr)
                {
                    error = "the maximum number of bodies is reached";
                }
            }
            if (body == nullptr)
            {
                ILL_LOG_WARN("Physics", "entity {}: cannot create a body: {} (fix it and call rebuild())",
                             entity.index, error);
            }
            else
            {
                const JPH::BodyID id = body->GetID();
                link.id = id.GetIndexAndSequenceNumber();
                scratchIds.push_back(id);
                slots[id.GetIndex()] = BodySlot{id, entity, rigidBody.sensor};
                records.push_back(Record{entity, id});
            }
            world.emplace<PhysicsBody>(entity, link);
            if (link.id != PhysicsBody::kInvalidId && rigidBody.motionType == MotionType::Dynamic)
            {
                world.set(entity, WorldSpaceAuthority{}); // the body moves it in world space
            }
        }
        if (!scratchIds.empty())
        {
            const JPH::BodyInterface::AddState state =
                bodies.AddBodiesPrepare(scratchIds.data(), static_cast<int>(scratchIds.size()));
            bodies.AddBodiesFinalize(scratchIds.data(), static_cast<int>(scratchIds.size()), state,
                                     JPH::EActivation::Activate);
            if (scratchIds.size() >= 256)
            {
                physics.OptimizeBroadPhase();
            }
        }
    }

    // Static bodies and teleported dynamic bodies take their Transform; kinematic bodies are driven
    // towards theirs over @p seconds.
    void followTransforms(f64 seconds)
    {
        JPH::BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        world.query<const Transform, const RigidBody, PhysicsBody>().each(
            [&](const Transform& transform, const RigidBody& rigidBody, PhysicsBody& link) {
                if (link.id == PhysicsBody::kInvalidId)
                {
                    return;
                }
                const JPH::BodyID id(link.id);
                const bool changed =
                    transform.position != link.writtenPosition || transform.rotation != link.writtenRotation;
                if (rigidBody.motionType == MotionType::Kinematic)
                {
                    if (seconds > 0.0)
                    {
                        bodies.MoveKinematic(id, toJolt(transform.position),
                                             toJolt(glm::normalize(transform.rotation)),
                                             static_cast<f32>(seconds));
                        link.writtenPosition = transform.position;
                        link.writtenRotation = transform.rotation;
                    }
                    return;
                }
                if (!changed)
                {
                    return;
                }
                bodies.SetPositionAndRotation(
                    id, toJolt(transform.position), toJolt(glm::normalize(transform.rotation)),
                    rigidBody.motionType == MotionType::Dynamic ? JPH::EActivation::Activate
                                                                : JPH::EActivation::DontActivate);
                link.previousPosition = link.writtenPosition = transform.position;
                link.previousRotation = link.writtenRotation = transform.rotation;
            });
        // Characters whose Transform game code moved are teleported.
        world.query<const Transform, CharacterBody>().each(
            [&](const Transform& transform, CharacterBody& link) {
                if (link.id == CharacterBody::kInvalidId || transform.position == link.writtenPosition)
                {
                    return;
                }
                characters[link.id].character->SetPosition(toJolt(transform.position));
                link.previousPosition = link.writtenPosition = transform.position;
            });
    }

    void capturePrevious()
    {
        const JPH::BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        world.query<const RigidBody, PhysicsBody>().each([&](const RigidBody& rigidBody, PhysicsBody& link) {
            if (rigidBody.motionType != MotionType::Dynamic || link.id == PhysicsBody::kInvalidId)
            {
                return;
            }
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodies.GetPositionAndRotation(JPH::BodyID(link.id), position, rotation);
            link.previousPosition = fromJolt(JPH::Vec3(position));
            link.previousRotation = fromJolt(rotation);
        });
        world.query<CharacterBody>().each([&](CharacterBody& link) {
            if (link.id != CharacterBody::kInvalidId)
            {
                link.previousPosition = fromJolt(JPH::Vec3(characters[link.id].character->GetPosition()));
            }
        });
    }

    void writeTransforms(f32 alpha)
    {
        ILL_PROFILE_SCOPE_N("Write transforms");
        const JPH::BodyInterface& bodies = physics.GetBodyInterfaceNoLock();
        world.query<Transform, const RigidBody, PhysicsBody>().each(
            [&](Transform& transform, const RigidBody& rigidBody, PhysicsBody& link) {
                if (rigidBody.motionType != MotionType::Dynamic || link.id == PhysicsBody::kInvalidId)
                {
                    return;
                }
                JPH::RVec3 position;
                JPH::Quat rotation;
                bodies.GetPositionAndRotation(JPH::BodyID(link.id), position, rotation);
                transform.position = glm::mix(link.previousPosition, fromJolt(JPH::Vec3(position)), alpha);
                transform.rotation = glm::slerp(link.previousRotation, fromJolt(rotation), alpha);
                link.writtenPosition = transform.position;
                link.writtenRotation = transform.rotation;
            });
        world.query<Transform, CharacterBody>().each([&](Transform& transform, CharacterBody& link) {
            if (link.id == CharacterBody::kInvalidId)
            {
                return;
            }
            const JPH::CharacterVirtual& character = *characters[link.id].character;
            transform.position =
                glm::mix(link.previousPosition, fromJolt(JPH::Vec3(character.GetPosition())), alpha);
            link.writtenPosition = transform.position;
            link.velocity = fromJolt(character.GetLinearVelocity());
            link.grounded = character.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            const bool touching = character.GetGroundState() != JPH::CharacterBase::EGroundState::InAir;
            link.groundNormal = touching ? fromJolt(character.GetGroundNormal()) : Vec3(0.0f, 1.0f, 0.0f);
            const JPH::BodyID ground = character.GetGroundBodyID();
            link.ground =
                touching && !ground.IsInvalid() ? Entity::fromBits(bodies.GetUserData(ground)) : kNullEntity;
        });
    }

    World& world;
    PhysicsWorldDesc desc;
    JoltScope jolt;
    FixedTimestep timestep;
    detail::JoltJobSystem joltJobs;
    JPH::TempAllocatorImpl tempAllocator;
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    LayerPairs layerPairs;
    std::vector<BodySlot> slots;
    ContactCollector contacts;
    JPH::PhysicsSystem physics;
    std::vector<Record> records;
    std::vector<Geometry> geometry;
    std::vector<u32> freeGeometry;
    // Declared after the PhysicsSystem: characters remove their inner bodies when they are destroyed.
    JPH::CharacterVsCharacterCollisionSimple characterCollision;
    std::vector<Character> characters;
    std::vector<u32> freeCharacters;
    std::vector<JointRecord> joints;
    std::vector<u32> freeJoints;
    std::vector<Entity> brokenJoints;
    std::vector<Entity> rebuilds;
    std::vector<Entity> scratchEntities;
    std::vector<Entity> scratchJoints;
    std::vector<JPH::BodyID> scratchIds;
};

PhysicsWorld::PhysicsWorld(World& world, JobSystem& jobs, const PhysicsWorldDesc& desc)
    : m_impl(std::make_unique<Impl>(world, jobs, desc))
{
}

namespace
{

// Removes component T from every Entity that has it, and the WorldSpaceAuthority tag that bodies add.
template <class T>
void removeAll(World& world, std::vector<Entity>& scratch)
{
    scratch.clear();
    world.query<const T>().each([&scratch](Entity entity, const T&) { scratch.push_back(entity); });
    for (const Entity entity : scratch)
    {
        world.remove<T>(entity);
        if constexpr (!std::is_same_v<T, PhysicsJoint>)
        {
            world.remove<WorldSpaceAuthority>(entity);
        }
    }
}

} // namespace

PhysicsWorld::~PhysicsWorld()
{
    Impl& impl = *m_impl;
    for (u32 id = 0; id < impl.joints.size(); ++id)
    {
        if (impl.joints[id].constraint != nullptr)
        {
            impl.destroyJoint(id);
        }
    }
    for (u32 id = 0; id < impl.characters.size(); ++id)
    {
        if (impl.characters[id].character != nullptr)
        {
            impl.destroyCharacter(id);
        }
    }
    impl.scratchIds.clear();
    for (const Impl::Record& record : impl.records)
    {
        impl.scratchIds.push_back(record.body);
    }
    impl.destroyBodies(impl.scratchIds);
    removeAll<PhysicsJoint>(impl.world, impl.scratchEntities);
    removeAll<CharacterBody>(impl.world, impl.scratchEntities);
    removeAll<PhysicsBody>(impl.world, impl.scratchEntities);
}

void PhysicsWorld::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.contacts.reset();
    impl.brokenJoints.clear();
    impl.removeStaleBodies(); // also removes stale joints
    impl.removeStaleCharacters();
    impl.rebuilds.clear();
    impl.createBodies();
    impl.createCharacters();
    impl.createJoints();
    const u32 steps = impl.timestep.advance(deltaSeconds);
    const auto stepSeconds = static_cast<f32>(impl.timestep.stepSeconds());
    impl.followTransforms(static_cast<f64>(steps) * impl.timestep.stepSeconds());
    impl.driveJointMotors();
    for (u32 step = 0; step < steps; ++step)
    {
        if (step + 1 == steps)
        {
            impl.capturePrevious();
        }
        ILL_PROFILE_SCOPE_N("Physics step");
        impl.stepCharacters(stepSeconds);
        const JPH::EPhysicsUpdateError error =
            impl.physics.Update(stepSeconds, 1, &impl.tempAllocator, &impl.joltJobs);
        impl.joltJobs.waitForEngineJobs();
        if (error != JPH::EPhysicsUpdateError::None)
        {
            ILL_LOG_WARN("Physics",
                         "simulation limits exceeded (error flags {}); raise PhysicsWorldDesc limits",
                         static_cast<u32>(error));
        }
        impl.breakJoints(stepSeconds);
    }
    impl.writeTransforms(static_cast<f32>(impl.timestep.alpha()));
    impl.contacts.finish();
    std::sort(impl.brokenJoints.begin(), impl.brokenJoints.end(),
              [](Entity a, Entity b) { return a.toBits() < b.toBits(); });
}

void PhysicsWorld::rebuild(Entity entity)
{
    m_impl->rebuilds.push_back(entity);
}

u32 PhysicsWorld::bodyCount() const noexcept
{
    return m_impl->physics.GetNumBodies();
}

u32 PhysicsWorld::characterCount() const noexcept
{
    return static_cast<u32>(m_impl->characters.size() - m_impl->freeCharacters.size());
}

u32 PhysicsWorld::jointCount() const noexcept
{
    return static_cast<u32>(m_impl->joints.size() - m_impl->freeJoints.size());
}

PhysicsMeshHandle PhysicsWorld::createMesh(std::span<const Vec3> vertices, std::span<const u32> indices)
{
    if (vertices.empty() || indices.empty() || indices.size() % 3 != 0)
    {
        ILL_LOG_WARN("Physics", "createMesh: needs vertices and three indices per triangle");
        return {};
    }
    JPH::VertexList points;
    points.reserve(vertices.size());
    for (const Vec3& vertex : vertices)
    {
        points.push_back(JPH::Float3(vertex.x, vertex.y, vertex.z));
    }
    JPH::IndexedTriangleList triangles;
    triangles.reserve(indices.size() / 3);
    for (usize i = 0; i < indices.size(); i += 3)
    {
        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() ||
            indices[i + 2] >= vertices.size())
        {
            ILL_LOG_WARN("Physics", "createMesh: index out of range in triangle {}", i / 3);
            return {};
        }
        triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2], 0));
    }
    const JPH::ShapeSettings::ShapeResult result =
        JPH::MeshShapeSettings(std::move(points), std::move(triangles)).Create();
    if (result.HasError())
    {
        ILL_LOG_WARN("Physics", "createMesh: {}", result.GetError().c_str());
        return {};
    }
    return m_impl->addGeometry(result.Get(), true);
}

PhysicsMeshHandle PhysicsWorld::createConvexHull(std::span<const Vec3> points)
{
    JPH::Array<JPH::Vec3> hull;
    hull.reserve(points.size());
    for (const Vec3& point : points)
    {
        hull.push_back(toJolt(point));
    }
    const JPH::ShapeSettings::ShapeResult result = JPH::ConvexHullShapeSettings(hull).Create();
    if (result.HasError())
    {
        ILL_LOG_WARN("Physics", "createConvexHull: {}", result.GetError().c_str());
        return {};
    }
    return m_impl->addGeometry(result.Get(), false);
}

void PhysicsWorld::destroyMesh(PhysicsMeshHandle mesh)
{
    Impl& impl = *m_impl;
    if (impl.geometryOf(mesh) == nullptr)
    {
        return;
    }
    Impl::Geometry& slot = impl.geometry[mesh.index];
    slot.shape = nullptr; // bodies keep their own references
    slot.live = false;
    slot.generation = slot.generation == 0xFFFFFFFFu ? 1 : slot.generation + 1;
    impl.freeGeometry.push_back(mesh.index);
}

bool PhysicsWorld::isMeshValid(PhysicsMeshHandle mesh) const noexcept
{
    return m_impl->geometryOf(mesh) != nullptr;
}

void PhysicsWorld::setCharacterVelocity(Entity entity, const Vec3& velocity)
{
    if (JPH::CharacterVirtual* character = m_impl->characterOf(entity))
    {
        character->SetLinearVelocity(toJolt(velocity));
        m_impl->world.get<CharacterBody>(entity).velocity = velocity;
    }
}

Vec3 PhysicsWorld::characterVelocity(Entity entity) const
{
    const JPH::CharacterVirtual* character = m_impl->characterOf(entity);
    return character != nullptr ? fromJolt(character->GetLinearVelocity()) : Vec3(0.0f);
}

bool PhysicsWorld::isCharacterGrounded(Entity entity) const
{
    const JPH::CharacterVirtual* character = m_impl->characterOf(entity);
    return character != nullptr && character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
}

f32 PhysicsWorld::jointPosition(Entity entity) const
{
    const Impl& impl = *m_impl;
    if (!impl.world.isAlive(entity))
    {
        return 0.0f;
    }
    const PhysicsJoint* link = impl.world.tryGet<PhysicsJoint>(entity);
    if (link == nullptr || link->id >= impl.joints.size())
    {
        return 0.0f;
    }
    const Impl::JointRecord& record = impl.joints[link->id];
    switch (record.type)
    {
    case JointType::Hinge:
        return static_cast<const JPH::HingeConstraint*>(record.constraint.GetPtr())->GetCurrentAngle();
    case JointType::Slider:
        return static_cast<const JPH::SliderConstraint*>(record.constraint.GetPtr())->GetCurrentPosition();
    case JointType::Distance:
    {
        const JPH::BodyInterface& bodies = impl.physics.GetBodyInterfaceNoLock();
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(record.bodyA, position, rotation);
        const JPH::RVec3 a = position + rotation * toJolt(record.anchorA);
        JPH::RVec3 b(toJolt(record.anchorB));
        if (!record.bodyB.IsInvalid())
        {
            bodies.GetPositionAndRotation(record.bodyB, position, rotation);
            b = position + rotation * toJolt(record.anchorB);
        }
        return static_cast<f32>(JPH::Vec3(a - b).Length());
    }
    case JointType::Fixed:
    case JointType::Point:
        break;
    }
    return 0.0f;
}

std::span<const Entity> PhysicsWorld::brokenJoints() const noexcept
{
    return m_impl->brokenJoints;
}

u64 PhysicsWorld::stepCount() const noexcept
{
    return m_impl->timestep.totalSteps();
}

void PhysicsWorld::setGravity(const Vec3& gravity)
{
    m_impl->physics.SetGravity(toJolt(gravity));
}

Vec3 PhysicsWorld::gravity() const
{
    return fromJolt(m_impl->physics.GetGravity());
}

void PhysicsWorld::setLayersCollide(u32 a, u32 b, bool collide)
{
    ILL_ASSERT(a < kMaxPhysicsLayers && b < kMaxPhysicsLayers, "physics layer out of range");
    m_impl->layerPairs.set(a, b, collide);
}

bool PhysicsWorld::layersCollide(u32 a, u32 b) const
{
    ILL_ASSERT(a < kMaxPhysicsLayers && b < kMaxPhysicsLayers, "physics layer out of range");
    return m_impl->layerPairs.collides(a, b);
}

void PhysicsWorld::setLinearVelocity(Entity entity, const Vec3& velocity)
{
    if (const JPH::BodyID id = m_impl->bodyOf(entity); !id.IsInvalid())
    {
        m_impl->physics.GetBodyInterfaceNoLock().SetLinearVelocity(id, toJolt(velocity));
    }
}

Vec3 PhysicsWorld::linearVelocity(Entity entity) const
{
    const JPH::BodyID id = m_impl->bodyOf(entity);
    return id.IsInvalid() ? Vec3(0.0f)
                          : fromJolt(m_impl->physics.GetBodyInterfaceNoLock().GetLinearVelocity(id));
}

void PhysicsWorld::setAngularVelocity(Entity entity, const Vec3& velocity)
{
    if (const JPH::BodyID id = m_impl->bodyOf(entity); !id.IsInvalid())
    {
        m_impl->physics.GetBodyInterfaceNoLock().SetAngularVelocity(id, toJolt(velocity));
    }
}

Vec3 PhysicsWorld::angularVelocity(Entity entity) const
{
    const JPH::BodyID id = m_impl->bodyOf(entity);
    return id.IsInvalid() ? Vec3(0.0f)
                          : fromJolt(m_impl->physics.GetBodyInterfaceNoLock().GetAngularVelocity(id));
}

void PhysicsWorld::addForce(Entity entity, const Vec3& force)
{
    if (const JPH::BodyID id = m_impl->bodyOf(entity); !id.IsInvalid())
    {
        m_impl->physics.GetBodyInterfaceNoLock().AddForce(id, toJolt(force));
    }
}

void PhysicsWorld::addTorque(Entity entity, const Vec3& torque)
{
    if (const JPH::BodyID id = m_impl->bodyOf(entity); !id.IsInvalid())
    {
        m_impl->physics.GetBodyInterfaceNoLock().AddTorque(id, toJolt(torque));
    }
}

void PhysicsWorld::addImpulse(Entity entity, const Vec3& impulse)
{
    if (const JPH::BodyID id = m_impl->bodyOf(entity); !id.IsInvalid())
    {
        m_impl->physics.GetBodyInterfaceNoLock().AddImpulse(id, toJolt(impulse));
    }
}

f32 PhysicsWorld::mass(Entity entity) const
{
    const JPH::BodyID id = m_impl->bodyOf(entity);
    if (id.IsInvalid())
    {
        return 0.0f;
    }
    const JPH::BodyLockRead lock(m_impl->physics.GetBodyLockInterfaceNoLock(), id);
    if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
    {
        return 0.0f;
    }
    const f32 inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
    return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
}

bool PhysicsWorld::isAwake(Entity entity) const
{
    const JPH::BodyID id = m_impl->bodyOf(entity);
    return !id.IsInvalid() && m_impl->physics.GetBodyInterfaceNoLock().IsActive(id);
}

std::optional<RaycastHit> PhysicsWorld::raycast(const Vec3& origin, const Vec3& direction, f32 maxDistance,
                                                u32 layerMask, Entity ignore) const
{
    const f32 length = glm::length(direction);
    if (length <= 0.0f || maxDistance <= 0.0f)
    {
        return std::nullopt;
    }
    const Vec3 unit = direction / length;
    const JPH::RRayCast ray(toJolt(origin), toJolt(unit * maxDistance));
    JPH::RayCastResult result;
    const LayerMaskFilter layers(layerMask);
    const SolidBodies solid(m_impl->queryBodyOf(ignore));
    if (!m_impl->physics.GetNarrowPhaseQueryNoLock().CastRay(ray, result, {}, layers, solid))
    {
        return std::nullopt;
    }
    const JPH::BodyLockRead lock(m_impl->physics.GetBodyLockInterfaceNoLock(), result.mBodyID);
    if (!lock.Succeeded())
    {
        return std::nullopt;
    }
    const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    RaycastHit hit;
    hit.entity = Entity::fromBits(lock.GetBody().GetUserData());
    hit.point = fromJolt(JPH::Vec3(point));
    hit.normal = fromJolt(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    hit.distance = result.mFraction * maxDistance;
    return hit;
}

std::optional<ShapeCastHit> PhysicsWorld::shapeCast(const Collider& shape, const Vec3& position,
                                                    const Quat& rotation, const Vec3& direction,
                                                    f32 maxDistance, u32 layerMask, Entity ignore) const
{
    const f32 length = glm::length(direction);
    if (length <= 0.0f || maxDistance <= 0.0f)
    {
        return std::nullopt;
    }
    const QueryShape query(shape, m_impl->resolveGeometry(shape));
    if (query.get() == nullptr)
    {
        return std::nullopt;
    }
    const Vec3 displacement = direction / length * maxDistance;
    const JPH::RShapeCast cast = JPH::RShapeCast::sFromWorldTransform(
        query.get(), JPH::Vec3::sOne(), QueryShape::transform(shape, position, rotation),
        toJolt(displacement));
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    const LayerMaskFilter layers(layerMask);
    const SolidBodies solid(m_impl->queryBodyOf(ignore));
    m_impl->physics.GetNarrowPhaseQueryNoLock().CastShape(cast, settings, JPH::RVec3::sZero(), collector, {},
                                                          layers, solid);
    if (!collector.HadHit())
    {
        return std::nullopt;
    }
    const JPH::ShapeCastResult& result = collector.mHit;
    ShapeCastHit hit;
    hit.entity = Entity::fromBits(m_impl->physics.GetBodyInterfaceNoLock().GetUserData(result.mBodyID2));
    hit.point = fromJolt(JPH::Vec3(result.mContactPointOn2));
    const JPH::Vec3 axis = result.mPenetrationAxis;
    hit.normal = axis.LengthSq() > 0.0f ? fromJolt(-axis.Normalized()) : -displacement / maxDistance;
    hit.distance = result.mFraction * maxDistance;
    return hit;
}

u32 PhysicsWorld::overlap(const Collider& shape, const Vec3& position, const Quat& rotation,
                          std::vector<Entity>& out, u32 layerMask, Entity ignore) const
{
    out.clear();
    const QueryShape query(shape, m_impl->resolveGeometry(shape));
    if (query.get() == nullptr)
    {
        return 0;
    }
    const JPH::CollideShapeSettings settings;
    EntityCollector collector(m_impl->physics.GetBodyInterfaceNoLock(), out);
    const LayerMaskFilter layers(layerMask);
    const SolidBodies solid(m_impl->queryBodyOf(ignore));
    m_impl->physics.GetNarrowPhaseQueryNoLock().CollideShape(
        query.get(), JPH::Vec3::sOne(), QueryShape::transform(shape, position, rotation), settings,
        JPH::RVec3::sZero(), collector, {}, layers, solid);
    const auto byBits = [](Entity a, Entity b) {
        return a.toBits() < b.toBits();
    };
    std::sort(out.begin(), out.end(), byBits);
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return static_cast<u32>(out.size());
}

std::span<const ContactEvent> PhysicsWorld::contactEvents() const noexcept
{
    return m_impl->contacts.events();
}

u32 PhysicsWorld::droppedContactEvents() const noexcept
{
    return m_impl->contacts.dropped();
}

} // namespace illusion

#include "JoltJobSystem.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
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
        event.point = fromJolt(JPH::Vec3(manifold.GetWorldSpaceContactPointOn1(0)));
        event.normal = fromJolt(manifold.mWorldSpaceNormal);
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

// Ignores sensors in queries.
class SolidBodies final : public JPH::BodyFilter
{
public:
    bool ShouldCollideLocked(const JPH::Body& body) const override { return !body.IsSensor(); }
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

// Builds the collision shape of @p collider scaled by @p scale. Returns null and sets @p error if the
// collider is invalid.
JPH::RefConst<JPH::Shape> createShape(const Collider& collider, const Vec3& scale, std::string& error)
{
    JPH::ShapeSettings::ShapeResult result;
    switch (collider.shape)
    {
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
    if (result.HasError())
    {
        error = result.GetError().c_str();
        return nullptr;
    }
    JPH::RefConst<JPH::Shape> shape = result.Get();
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
                              world.has<Transform>(record.entity) &&
                              std::find(rebuilds.begin(), rebuilds.end(), record.entity) == rebuilds.end();
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
        }
        rebuilds.clear();
    }

    void createBodies()
    {
        scratchEntities.clear();
        world.query<const Transform, const RigidBody, const Collider>().without<PhysicsBody>().each(
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
            const JPH::RefConst<JPH::Shape> shape = createShape(collider, transform.scale, error);
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
    std::vector<Entity> rebuilds;
    std::vector<Entity> scratchEntities;
    std::vector<JPH::BodyID> scratchIds;
};

PhysicsWorld::PhysicsWorld(World& world, JobSystem& jobs, const PhysicsWorldDesc& desc)
    : m_impl(std::make_unique<Impl>(world, jobs, desc))
{
}

PhysicsWorld::~PhysicsWorld()
{
    Impl& impl = *m_impl;
    impl.scratchIds.clear();
    for (const Impl::Record& record : impl.records)
    {
        impl.scratchIds.push_back(record.body);
    }
    impl.destroyBodies(impl.scratchIds);
    impl.scratchEntities.clear();
    impl.world.query<const PhysicsBody>().each(
        [&impl](Entity entity, const PhysicsBody&) { impl.scratchEntities.push_back(entity); });
    for (const Entity entity : impl.scratchEntities)
    {
        impl.world.remove<PhysicsBody>(entity);
    }
}

void PhysicsWorld::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.contacts.reset();
    impl.removeStaleBodies();
    impl.createBodies();
    const u32 steps = impl.timestep.advance(deltaSeconds);
    impl.followTransforms(static_cast<f64>(steps) * impl.timestep.stepSeconds());
    for (u32 step = 0; step < steps; ++step)
    {
        if (step + 1 == steps)
        {
            impl.capturePrevious();
        }
        ILL_PROFILE_SCOPE_N("Physics step");
        const JPH::EPhysicsUpdateError error = impl.physics.Update(
            static_cast<f32>(impl.timestep.stepSeconds()), 1, &impl.tempAllocator, &impl.joltJobs);
        if (error != JPH::EPhysicsUpdateError::None)
        {
            ILL_LOG_WARN("Physics",
                         "simulation limits exceeded (error flags {}); raise PhysicsWorldDesc limits",
                         static_cast<u32>(error));
        }
    }
    impl.writeTransforms(static_cast<f32>(impl.timestep.alpha()));
    impl.contacts.finish();
}

void PhysicsWorld::rebuild(Entity entity)
{
    m_impl->rebuilds.push_back(entity);
}

u32 PhysicsWorld::bodyCount() const noexcept
{
    return m_impl->physics.GetNumBodies();
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
                                                u32 layerMask) const
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
    const SolidBodies solid;
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

std::span<const ContactEvent> PhysicsWorld::contactEvents() const noexcept
{
    return m_impl->contacts.events();
}

u32 PhysicsWorld::droppedContactEvents() const noexcept
{
    return m_impl->contacts.dropped();
}

} // namespace illusion

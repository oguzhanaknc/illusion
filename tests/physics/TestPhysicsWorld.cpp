#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsWorld.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

using namespace illusion;

namespace
{

constexpr f64 kStep = 1.0 / 60.0;

// Advances by whole steps.
void simulate(PhysicsWorld& physics, f64 seconds)
{
    const auto steps = static_cast<u32>(std::lround(seconds / kStep));
    for (u32 i = 0; i < steps; ++i)
    {
        physics.update(kStep);
    }
}

Transform at(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

RigidBody body(MotionType type)
{
    RigidBody rigidBody;
    rigidBody.motionType = type;
    return rigidBody;
}

Collider box(const Vec3& halfExtents)
{
    Collider collider;
    collider.shape = ColliderShape::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

Collider sphere(f32 radius)
{
    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.radius = radius;
    return collider;
}

// A 100 x 1 x 100 static floor whose top is at y = 0.
Entity createGround(World& world)
{
    return world.create(at(Vec3(0.0f, -0.5f, 0.0f)), body(MotionType::Static), box(Vec3(50.0f, 0.5f, 50.0f)));
}

struct Fixture
{
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorld physics{world, jobs};
};

} // namespace

TEST_CASE("dynamic bodies fall under gravity")
{
    Fixture f;
    RigidBody rigidBody = body(MotionType::Dynamic);
    rigidBody.linearDamping = 0.0f;
    const Entity falling = f.world.create(at(Vec3(0.0f, 100.0f, 0.0f)), rigidBody, sphere(0.5f));
    f.physics.update(0.0);
    CHECK(f.physics.bodyCount() == 1);
    CHECK(f.world.has<PhysicsBody>(falling));

    simulate(f.physics, 1.0);
    CHECK(f.physics.stepCount() == 60);
    // y = y0 - g t^2 / 2 (semi-implicit Euler is slightly ahead of the analytic curve).
    const f32 y = f.world.get<Transform>(falling).position.y;
    CHECK(y == doctest::Approx(100.0 - 0.5 * 9.81).epsilon(0.01));
    CHECK(f.physics.linearVelocity(falling).y == doctest::Approx(-9.81).epsilon(0.01));
}

TEST_CASE("bodies come to rest on static ground")
{
    Fixture f;
    createGround(f.world);
    const Entity cube =
        f.world.create(at(Vec3(0.0f, 2.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    Collider capsule;
    capsule.shape = ColliderShape::Capsule;
    capsule.halfHeight = 0.5f;
    capsule.radius = 0.25f;
    const Entity pill = f.world.create(at(Vec3(3.0f, 2.0f, 0.0f)), body(MotionType::Dynamic), capsule);
    Collider cylinder;
    cylinder.shape = ColliderShape::Cylinder;
    cylinder.halfHeight = 0.3f;
    cylinder.radius = 0.4f;
    const Entity can = f.world.create(at(Vec3(-3.0f, 2.0f, 0.0f)), body(MotionType::Dynamic), cylinder);
    Transform scaled = at(Vec3(6.0f, 3.0f, 0.0f));
    scaled.scale = Vec3(2.0f);
    const Entity big = f.world.create(scaled, body(MotionType::Dynamic), box(Vec3(0.5f)));

    simulate(f.physics, 4.0);
    CHECK(f.world.get<Transform>(cube).position.y == doctest::Approx(0.5).epsilon(0.02));
    CHECK(f.world.get<Transform>(pill).position.y == doctest::Approx(0.75).epsilon(0.02));
    CHECK(f.world.get<Transform>(can).position.y == doctest::Approx(0.3).epsilon(0.02));
    CHECK(f.world.get<Transform>(big).position.y == doctest::Approx(1.0).epsilon(0.02));
    CHECK_FALSE(f.physics.isAwake(cube)); // asleep
}

TEST_CASE("transforms are interpolated between the last two steps")
{
    Fixture f;
    RigidBody rigidBody = body(MotionType::Dynamic);
    rigidBody.gravityFactor = 0.0f;
    rigidBody.linearDamping = 0.0f;
    const Entity mover = f.world.create(at(Vec3(0.0f)), rigidBody, sphere(0.5f));
    f.physics.update(0.0);
    f.physics.setLinearVelocity(mover, Vec3(6.0f, 0.0f, 0.0f)); // 0.1 m per step

    f.physics.update(kStep * 1.5); // one step, half a step left over
    CHECK(f.physics.stepCount() == 1);
    CHECK(f.world.get<Transform>(mover).position.x == doctest::Approx(0.05).epsilon(0.001));
    f.physics.update(kStep * 0.5); // completes the second step
    CHECK(f.physics.stepCount() == 2);
    // Rendering lags one step: with no time left over, the Transform shows the pose before the step.
    CHECK(f.world.get<Transform>(mover).position.x == doctest::Approx(0.1).epsilon(0.001));
    f.physics.update(kStep * 0.25);
    CHECK(f.world.get<Transform>(mover).position.x == doctest::Approx(0.125).epsilon(0.001));
}

TEST_CASE("game code can teleport bodies and drive kinematic ones")
{
    Fixture f;
    const Entity cube =
        f.world.create(at(Vec3(0.0f, 10.0f, 0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    const Entity platform =
        f.world.create(at(Vec3(0.0f)), body(MotionType::Kinematic), box(Vec3(2.0f, 0.1f, 2.0f)));
    const Entity wall =
        f.world.create(at(Vec3(20.0f, 0.0f, 0.0f)), body(MotionType::Static), box(Vec3(1.0f)));
    simulate(f.physics, 0.5);

    f.world.get<Transform>(cube).position = Vec3(50.0f, 50.0f, 0.0f);
    f.world.get<Transform>(platform).position = Vec3(0.0f, 0.6f, 0.0f);
    f.world.get<Transform>(wall).position = Vec3(30.0f, 0.0f, 0.0f);
    f.physics.update(kStep);
    f.physics.update(kStep);
    CHECK(f.world.get<Transform>(cube).position.x == doctest::Approx(50.0f));
    CHECK(f.world.get<Transform>(cube).position.y < 50.0f); // falling from there

    // The kinematic platform reached its target and the static wall moved.
    const auto hitPlatform = f.physics.raycast(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f);
    REQUIRE(hitPlatform.has_value());
    CHECK(hitPlatform->entity == platform);
    CHECK(hitPlatform->point.y == doctest::Approx(0.7f).epsilon(0.001));
    const auto hitWall = f.physics.raycast(Vec3(25.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), 10.0f);
    REQUIRE(hitWall.has_value());
    CHECK(hitWall->entity == wall);
    CHECK(hitWall->distance == doctest::Approx(4.0f).epsilon(0.001));
}

TEST_CASE("velocities, forces and impulses use the body's mass")
{
    Fixture f;
    RigidBody rigidBody = body(MotionType::Dynamic);
    rigidBody.gravityFactor = 0.0f;
    rigidBody.linearDamping = 0.0f;
    rigidBody.angularDamping = 0.0f;
    rigidBody.mass = 2.0f;
    const Entity heavy = f.world.create(at(Vec3(0.0f)), rigidBody, sphere(0.5f));
    rigidBody.mass = 0.0f; // from density: 1000 kg/m^3 * (4/3) pi 0.5^3
    const Entity dense = f.world.create(at(Vec3(10.0f, 0.0f, 0.0f)), rigidBody, sphere(0.5f));
    f.physics.update(0.0);

    CHECK(f.physics.mass(heavy) == doctest::Approx(2.0f));
    CHECK(f.physics.mass(dense) == doctest::Approx(1000.0 * 4.0 / 3.0 * 3.14159265 * 0.125).epsilon(0.01));

    f.physics.addImpulse(heavy, Vec3(4.0f, 0.0f, 0.0f));
    CHECK(f.physics.linearVelocity(heavy).x == doctest::Approx(2.0f));
    f.physics.setLinearVelocity(heavy, Vec3(0.0f));
    f.physics.addForce(heavy, Vec3(0.0f, 120.0f, 0.0f)); // a = 60 m/s^2 for one step
    f.physics.update(kStep);
    CHECK(f.physics.linearVelocity(heavy).y == doctest::Approx(1.0f).epsilon(0.001));

    f.physics.setAngularVelocity(heavy, Vec3(0.0f, 2.0f, 0.0f));
    CHECK(f.physics.angularVelocity(heavy).y == doctest::Approx(2.0f));
    f.physics.addTorque(dense, Vec3(0.0f, 1.0f, 0.0f));
    f.physics.update(kStep);
    CHECK(f.physics.angularVelocity(dense).y > 0.0f);

    // Bodies that do not exist are ignored.
    const Entity none = f.world.create();
    f.physics.addImpulse(none, Vec3(1.0f));
    CHECK(f.physics.linearVelocity(none) == Vec3(0.0f));
    CHECK(f.physics.mass(none) == 0.0f);
    CHECK_FALSE(f.physics.isAwake(none));
}

TEST_CASE("bodies are removed with their entity or components and rebuilt on request")
{
    Fixture f;
    const Entity a = f.world.create(at(Vec3(0.0f)), body(MotionType::Static), box(Vec3(1.0f)));
    const Entity b = f.world.create(at(Vec3(5.0f, 0.0f, 0.0f)), body(MotionType::Static), box(Vec3(1.0f)));
    const Entity c = f.world.create(at(Vec3(10.0f, 0.0f, 0.0f)), body(MotionType::Static), box(Vec3(1.0f)));
    f.physics.update(0.0);
    CHECK(f.physics.bodyCount() == 3);

    f.world.destroy(a);
    f.world.remove<RigidBody>(b);
    f.physics.update(0.0);
    CHECK(f.physics.bodyCount() == 1);
    CHECK_FALSE(f.world.has<PhysicsBody>(b));
    CHECK_FALSE(f.physics.raycast(Vec3(5.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f).has_value());

    // Collider changes apply after rebuild().
    f.world.get<Collider>(c).halfExtents = Vec3(3.0f);
    f.physics.rebuild(c);
    f.physics.update(0.0);
    CHECK(f.physics.bodyCount() == 1);
    const auto hit = f.physics.raycast(Vec3(10.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 20.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->point.y == doctest::Approx(3.0f).epsilon(0.001));
    CHECK(hit->normal.y == doctest::Approx(1.0f));
}

TEST_CASE("invalid colliders are reported and can be fixed")
{
    Fixture f;
    const Entity broken = f.world.create(at(Vec3(0.0f)), body(MotionType::Static), sphere(-1.0f));
    f.physics.update(0.0);
    CHECK(f.physics.bodyCount() == 0);
    REQUIRE(f.world.has<PhysicsBody>(broken));
    CHECK(f.world.get<PhysicsBody>(broken).id == PhysicsBody::kInvalidId);
    f.physics.update(0.0); // not retried every frame
    CHECK(f.physics.bodyCount() == 0);

    f.world.get<Collider>(broken).radius = 1.0f;
    f.physics.rebuild(broken);
    f.physics.update(0.0);
    CHECK(f.physics.bodyCount() == 1);
}

TEST_CASE("raycasts report the closest solid body on the requested layers")
{
    Fixture f;
    const Entity near =
        f.world.create(at(Vec3(0.0f, 0.0f, -5.0f)), body(MotionType::Static), box(Vec3(1.0f)));
    RigidBody farBody = body(MotionType::Static);
    farBody.layer = 3;
    const Entity far = f.world.create(at(Vec3(0.0f, 0.0f, -10.0f)), farBody, box(Vec3(1.0f)));
    RigidBody trigger = body(MotionType::Static);
    trigger.sensor = true;
    f.world.create(at(Vec3(0.0f, 0.0f, -2.0f)), trigger, box(Vec3(0.5f)));
    f.physics.update(0.0);

    const auto hit = f.physics.raycast(Vec3(0.0f), Vec3(0.0f, 0.0f, -2.0f), 100.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->entity == near); // the sensor in front is ignored
    CHECK(hit->distance == doctest::Approx(4.0f).epsilon(0.001));
    CHECK(hit->normal.z == doctest::Approx(1.0f));

    const auto layer3 = f.physics.raycast(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f), 100.0f, 1u << 3);
    REQUIRE(layer3.has_value());
    CHECK(layer3->entity == far);
    CHECK_FALSE(f.physics.raycast(Vec3(0.0f), Vec3(0.0f, 0.0f, -1.0f), 3.0f).has_value());
    CHECK_FALSE(f.physics.raycast(Vec3(0.0f), Vec3(0.0f, 1.0f, 0.0f), 100.0f).has_value());
    CHECK_FALSE(f.physics.raycast(Vec3(0.0f), Vec3(0.0f), 100.0f).has_value());
}

TEST_CASE("contact events report touching bodies and sensors")
{
    Fixture f;
    const Entity ground = createGround(f.world);
    RigidBody trigger = body(MotionType::Static);
    trigger.sensor = true;
    const Entity zone = f.world.create(at(Vec3(0.0f, 1.0f, 0.0f)), trigger, box(Vec3(2.0f, 0.5f, 2.0f)));
    const Entity ball = f.world.create(at(Vec3(0.0f, 3.0f, 0.0f)), body(MotionType::Dynamic), sphere(0.25f));

    bool enteredZone = false;
    bool hitGround = false;
    for (int i = 0; i < 120 && !hitGround; ++i)
    {
        f.physics.update(kStep);
        for (const ContactEvent& event : f.physics.contactEvents())
        {
            CHECK(event.a.toBits() < event.b.toBits());
            if (event.type == ContactEventType::Begin && event.sensor)
            {
                CHECK(((event.a == zone && event.b == ball) || (event.a == ball && event.b == zone)));
                enteredZone = true;
            }
            if (event.type == ContactEventType::Begin && !event.sensor)
            {
                CHECK(((event.a == ground && event.b == ball) || (event.a == ball && event.b == ground)));
                CHECK(std::abs(event.normal.y) == doctest::Approx(1.0f).epsilon(0.01));
                CHECK(event.point.y == doctest::Approx(0.0f).scale(1.0).epsilon(0.05));
                hitGround = true;
            }
        }
    }
    CHECK(enteredZone);
    CHECK(hitGround);
    CHECK(f.physics.droppedContactEvents() == 0);

    // Lifting the ball ends both contacts.
    f.world.get<Transform>(ball).position = Vec3(0.0f, 10.0f, 0.0f);
    bool leftGround = false;
    for (int i = 0; i < 3 && !leftGround; ++i)
    {
        f.physics.update(kStep);
        for (const ContactEvent& event : f.physics.contactEvents())
        {
            leftGround = leftGround || (event.type == ContactEventType::End && !event.sensor);
        }
    }
    CHECK(leftGround);
}

TEST_CASE("collision layers can be disabled pairwise")
{
    Fixture f;
    createGround(f.world);
    RigidBody ghost = body(MotionType::Dynamic);
    ghost.layer = 1;
    const Entity falling = f.world.create(at(Vec3(0.0f, 1.0f, 0.0f)), ghost, sphere(0.25f));
    CHECK(f.physics.layersCollide(0, 1));
    f.physics.setLayersCollide(1, 0, false);
    CHECK_FALSE(f.physics.layersCollide(0, 1));
    CHECK(f.physics.layersCollide(1, 1));
    simulate(f.physics, 1.0);
    CHECK(f.world.get<Transform>(falling).position.y < -1.0f); // fell through the ground
}

TEST_CASE("restitution makes bodies bounce")
{
    Fixture f;
    createGround(f.world);
    RigidBody bouncy = body(MotionType::Dynamic);
    bouncy.restitution = 0.9f;
    const Entity ball = f.world.create(at(Vec3(0.0f, 2.0f, 0.0f)), bouncy, sphere(0.25f));
    f32 lowest = 10.0f;
    f32 highestAfterBounce = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        f.physics.update(kStep);
        const f32 y = f.world.get<Transform>(ball).position.y;
        lowest = std::min(lowest, y);
        if (lowest < 0.3f)
        {
            highestAfterBounce = std::max(highestAfterBounce, y);
        }
    }
    CHECK(highestAfterBounce > 1.0f);
}

TEST_CASE("gravity can be changed")
{
    Fixture f;
    CHECK(f.physics.gravity().y == doctest::Approx(-9.81f));
    f.physics.setGravity(Vec3(0.0f, 5.0f, 0.0f));
    CHECK(f.physics.gravity().y == doctest::Approx(5.0f));
    const Entity rising = f.world.create(at(Vec3(0.0f)), body(MotionType::Dynamic), sphere(0.5f));
    simulate(f.physics, 0.5);
    CHECK(f.world.get<Transform>(rising).position.y > 0.5f);
}

TEST_CASE("simulation results do not depend on the number of worker threads")
{
    const auto run = [](u32 workers) {
        JobSystem jobs(JobSystemDesc{workers, 1024});
        World world;
        std::vector<Entity> cubes;
        {
            PhysicsWorld physics(world, jobs);
            createGround(world);
            for (int i = 0; i < 64; ++i)
            {
                Transform transform =
                    at(Vec3(static_cast<f32>(i % 4) * 0.3f, 1.0f + static_cast<f32>(i) * 1.1f,
                            static_cast<f32>(i % 3) * 0.2f));
                cubes.push_back(world.create(transform, body(MotionType::Dynamic), box(Vec3(0.5f))));
            }
            simulate(physics, 2.0);
        }
        std::vector<Vec3> positions;
        for (const Entity cube : cubes)
        {
            positions.push_back(world.get<Transform>(cube).position);
        }
        return positions;
    };
    CHECK(run(1) == run(4));
}

TEST_CASE("simulation runs on the waiting thread when the job queue is full")
{
    const auto run = [](bool queueFull) {
        JobSystem jobs(JobSystemDesc{1, 2});
        // Occupying the only worker keeps the two queue slots taken, so most physics jobs do not fit.
        std::atomic<bool> started{false};
        std::atomic<bool> release{false};
        JobCounter blocker;
        if (queueFull)
        {
            jobs.submit(
                [&started, &release] {
                    started.store(true, std::memory_order_release);
                    while (!release.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                },
                &blocker);
            while (!started.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
        World world;
        std::vector<Entity> cubes;
        {
            PhysicsWorld physics(world, jobs);
            createGround(world);
            for (int i = 0; i < 16; ++i)
            {
                Transform transform =
                    at(Vec3(static_cast<f32>(i % 4) * 0.3f, 1.0f + static_cast<f32>(i) * 1.1f,
                            static_cast<f32>(i % 3) * 0.2f));
                cubes.push_back(world.create(transform, body(MotionType::Dynamic), box(Vec3(0.5f))));
            }
            simulate(physics, 1.0);
            release.store(true, std::memory_order_release);
            jobs.wait(blocker);
        }
        std::vector<Vec3> positions;
        for (const Entity cube : cubes)
        {
            positions.push_back(world.get<Transform>(cube).position);
        }
        return positions;
    };
    CHECK(run(true) == run(false));
}

TEST_CASE("physics components are saved in scenes and the runtime link is not")
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    Fixture f;
    RigidBody rigidBody = body(MotionType::Kinematic);
    rigidBody.friction = 0.25f;
    rigidBody.layer = 2;
    rigidBody.sensor = true;
    Collider collider;
    collider.shape = ColliderShape::Capsule;
    collider.offset = Vec3(0.0f, 1.0f, 0.0f);
    f.world.create(at(Vec3(1.0f)), rigidBody, collider);
    f.physics.update(0.0);

    const SceneSaveResult saved = saveScene(f.world);
    REQUIRE(saved.ok());
    CHECK(saved.skippedComponents == 1); // PhysicsBody
    CHECK(saved.json.find("\"motionType\": \"Kinematic\"") != std::string::npos);
    CHECK(saved.json.find("\"shape\": \"Capsule\"") != std::string::npos);

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    const RigidBody& copy = loaded.get<RigidBody>(result.entities[0]);
    CHECK(copy.motionType == MotionType::Kinematic);
    CHECK(copy.friction == 0.25f);
    CHECK(copy.layer == 2);
    CHECK(copy.sensor);
    CHECK(loaded.get<Collider>(result.entities[0]).shape == ColliderShape::Capsule);
    CHECK(loaded.get<Collider>(result.entities[0]).offset == collider.offset);
    CHECK_FALSE(loaded.has<PhysicsBody>(result.entities[0]));
}

TEST_CASE("destroying the physics world removes the runtime links")
{
    JobSystem jobs(JobSystemDesc{2, 256});
    World world;
    const Entity cube = world.create(at(Vec3(0.0f)), body(MotionType::Dynamic), box(Vec3(0.5f)));
    {
        PhysicsWorld physics(world, jobs);
        physics.update(kStep);
        CHECK(world.has<PhysicsBody>(cube));
    }
    CHECK_FALSE(world.has<PhysicsBody>(cube));
    // A new physics world picks the Entity up again.
    PhysicsWorld again(world, jobs);
    again.update(0.0);
    CHECK(again.bodyCount() == 1);
}

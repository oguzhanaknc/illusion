// The Cameras step of GameSession (ADR 0064): CameraWorld after the hierarchy, with the spring arm casting
// spheres through PhysicsWorld (PhysicsCameraCollision).

#include "TestSupport.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraWorld.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/runtime/GameSession.hpp>
#include <illusion/runtime/PhysicsCameraCollision.hpp>
#include <illusion/runtime/ProjectSettings.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

using namespace illusion;
using test::at;
using test::body;
using test::box;

namespace
{

void reflectAll()
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    reflectAudioComponents();
    reflectParticleComponents();
    reflectRenderComponents();
    registerAnimationComponents(ComponentRegistry::instance());
    registerCameraComponents(ComponentRegistry::instance());
    reflectRuntimeStructs();
}

struct Fixture
{
    Fixture()
    {
        reflectAll();
        REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
        desc.jobs = &jobs;
        desc.files = &files;
        desc.audioOutput = AudioOutput::Offline;
        desc.fixedStep = true;
        auto created = GameSession::create(desc);
        REQUIRE(created.hasValue());
        session = std::move(*created);
    }

    [[nodiscard]] World& world() { return session->world(); }

    test::TempDirectory directory;
    VirtualFileSystem files;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    GameSessionDesc desc;
    std::unique_ptr<GameSession> session;
};

// A character box at the origin with a third-person spring arm straight behind it (+z), a brain, and with
// @p wall a static wall whose front face is at z = 2.4.
struct ArmScene
{
    explicit ArmScene(Fixture& fixture, bool wall)
    {
        World& world = fixture.world();
        character = world.create(at(Vec3(0.0f, 1.0f, 0.0f)), body(MotionType::Kinematic),
                                 box(Vec3(0.4f, 1.0f, 0.4f)));
        if (wall)
        {
            world.create(at(Vec3(0.0f, 1.0f, 2.5f)), body(MotionType::Static), box(Vec3(5.0f, 5.0f, 0.1f)));
        }
        VirtualCamera camera;
        camera.follow = character;
        CameraSpringArm arm;
        arm.shoulderOffset = Vec3(0.0f, 0.6f, 0.0f);
        arm.armLength = 4.0f;
        arm.pitch = 0.0f;
        arm.collisionRadius = 0.2f;
        world.create(at(Vec3(0.0f)), camera, arm);
        brain = world.create(at(Vec3(0.0f)), Camera{}, CameraBrain{});
    }

    Entity character;
    Entity brain;
};

} // namespace

TEST_CASE("a spring arm keeps the camera out of a wall behind its target")
{
    Fixture fixture;
    const ArmScene scene(fixture, true);
    fixture.session->start();
    for (u32 i = 0; i < 30; ++i)
    {
        fixture.session->update(test::kStep);
        const Vec3 position = fixture.world().get<Transform>(scene.brain).position;
        CHECK(position.z <= 2.4f - 0.2f + 1e-3f);
        CHECK(position.z > 1.8f);
    }
    CHECK(fixture.session->cameras().statistics().evaluated == 1);
}

TEST_CASE("a spring arm without a wall reaches its full length through its own target's body")
{
    Fixture fixture;
    const ArmScene scene(fixture, false);
    fixture.session->start();
    fixture.session->update(test::kStep);
    CHECK(fixture.world().get<Transform>(scene.brain).position.z == doctest::Approx(4.0f));
    CHECK(fixture.world().get<Transform>(scene.brain).position.y == doctest::Approx(1.6f));
}

TEST_CASE("the cameras do not run before the session starts")
{
    Fixture fixture;
    const ArmScene scene(fixture, false);
    fixture.world().get<Transform>(scene.brain).position = Vec3(9.0f);
    fixture.session->update(test::kStep);
    CHECK(fixture.world().get<Transform>(scene.brain).position == Vec3(9.0f));
}

TEST_CASE("the physics collision skips the bodies of the ignored target and its carrier's children")
{
    test::PhysicsFixture physics;
    const Entity character = physics.world.create(at(Vec3(0.0f, 1.0f, 0.0f)), body(MotionType::Kinematic),
                                                  box(Vec3(0.4f, 1.0f, 0.4f)));
    const Entity backpack = physics.world.create(at(Vec3(0.0f, 1.0f, 0.6f)), body(MotionType::Kinematic),
                                                 box(Vec3(0.3f, 0.3f, 0.2f)));
    physics.world.emplace<Hierarchy>(backpack, Hierarchy{character, 0});
    physics.world.markChanged<Hierarchy>();
    const Entity wall = physics.world.create(at(Vec3(0.0f, 1.0f, 3.0f)), body(MotionType::Static),
                                             box(Vec3(5.0f, 5.0f, 0.1f)));
    physics.physics.update(test::kStep);

    PhysicsCameraCollision collision(physics.world, physics.physics);
    const Vec3 origin(0.0f, 1.0f, 0.0f);
    const Vec3 back(0.0f, 0.0f, 1.0f);
    const std::optional<f32> blocked = collision.sphereCast(origin, back, 5.0f, 0.1f, 0xFFFFFFFFu);
    REQUIRE(blocked.has_value());
    CHECK(*blocked < 0.5f);

    collision.setIgnored(character);
    const std::optional<f32> throughOwn = collision.sphereCast(origin, back, 5.0f, 0.1f, 0xFFFFFFFFu);
    REQUIRE(throughOwn.has_value());
    CHECK(*throughOwn == doctest::Approx(2.8f).epsilon(0.01));

    CHECK_FALSE(collision.sphereCast(origin, back, 2.0f, 0.1f, 0xFFFFFFFFu).has_value());
    CHECK_FALSE(collision.sphereCast(origin, back, 0.0f, 0.1f, 0xFFFFFFFFu).has_value());
    collision.setIgnored(kNullEntity);
    CHECK(collision.sphereCast(origin, back, 5.0f, 0.1f, 0xFFFFFFFFu).has_value());
    (void)wall;
}

TEST_CASE("fixed-step sessions with cameras repeat bit for bit")
{
    const auto run = [] {
        Fixture fixture;
        const ArmScene scene(fixture, true);
        World& world = fixture.world();
        VirtualCamera orbitCamera;
        orbitCamera.priority = 0;
        orbitCamera.follow = scene.character;
        const Entity orbit = world.create(at(Vec3(0.0f)), orbitCamera, CameraOrbit{}, CameraComposer{});
        world.create(at(Vec3(0.0f, 1.0f, 1.0f)), CameraImpulse{});
        fixture.session->start();
        std::vector<Transform> poses;
        for (u32 i = 0; i < 120; ++i)
        {
            fixture.world().get<Transform>(scene.character).position.x = 0.01f * static_cast<f32>(i);
            if (i == 60)
            {
                fixture.world().get<VirtualCamera>(orbit).priority = 20;
            }
            fixture.session->update(test::kStep);
            poses.push_back(fixture.world().get<Transform>(scene.brain));
        }
        return poses;
    };
    const std::vector<Transform> first = run();
    const std::vector<Transform> second = run();
    REQUIRE(first.size() == second.size());
    for (usize i = 0; i < first.size(); ++i)
    {
        CHECK(std::memcmp(&first[i].position, &second[i].position, sizeof(Vec3)) == 0);
        CHECK(std::memcmp(&first[i].rotation, &second[i].rotation, sizeof(Quat)) == 0);
    }
}

// CameraWorldService (ADR 0064): the scripting API forwards to its CameraWorld, writes the Components of the
// World, and returns every CameraError instead of asserting.

#include "TestSupport.hpp"

#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraWorld.hpp>
#include <illusion/camera/CameraWorldService.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

using namespace illusion;

namespace
{

constexpr f32 kDt = 1.0f / 60.0f;

struct ServiceFixture
{
    ServiceFixture()
        : cameras(world, CameraWorldDesc{.maxShakes = 2})
        , service(cameras)
    {
        reflectCoreComponents();
        reflectRenderComponents();
        registerCameraComponents(ComponentRegistry::instance());
        brain = world.create(Transform{}, Camera{}, CameraBrain{});
        target = world.create(Transform{});
        low = shot(5);
        high = shot(8);
    }

    Entity shot(i32 priority)
    {
        VirtualCamera camera;
        camera.priority = priority;
        return world.create(Transform{}, camera);
    }

    void run(u32 steps)
    {
        for (u32 i = 0; i < steps; ++i)
        {
            cameras.update(kDt);
        }
    }

    World world;
    CameraWorld cameras;
    CameraWorldService service;
    Entity brain;
    Entity target;
    Entity low;
    Entity high;
};

} // namespace

TEST_CASE("activating a virtual camera raises it above the others until it is deactivated")
{
    ServiceFixture f;
    f.run(1);
    CHECK(f.service.liveCamera(f.brain).valueOr(Entity{}) == f.high);

    REQUIRE(f.service.activate(f.low).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).priority == 9);
    f.run(1);
    CHECK(f.service.liveCamera(f.brain).valueOr(Entity{}) == f.low);

    REQUIRE(f.service.activate(f.low).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).priority == 9);

    REQUIRE(f.service.activate(f.high).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.high).priority == 10);
    REQUIRE(f.service.deactivate(f.high).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.high).priority == 8);

    REQUIRE(f.service.deactivate(f.low).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).priority == 5);
    f.run(1);
    CHECK(f.service.liveCamera(f.brain).valueOr(Entity{}) == f.high);
}

TEST_CASE("activating the highest camera again keeps its priority and deactivating gives the first one back")
{
    ServiceFixture f;
    REQUIRE(f.service.activate(f.high).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.high).priority == 8);
    REQUIRE(f.service.deactivate(f.high).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.high).priority == 8);
    CHECK(f.service.deactivate(f.high).error() == CameraError::NotActivated);
}

TEST_CASE("setting a priority ends an activation and a camera that is not activated cannot be deactivated")
{
    ServiceFixture f;
    CHECK(f.service.deactivate(f.low).error() == CameraError::NotActivated);
    REQUIRE(f.service.activate(f.low).hasValue());
    REQUIRE(f.service.setPriority(f.low, 3).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).priority == 3);
    CHECK(f.service.deactivate(f.low).error() == CameraError::NotActivated);
    CHECK(f.world.get<VirtualCamera>(f.low).priority == 3);

    REQUIRE(f.service.setPriority(f.low, 20).hasValue());
    f.run(1);
    CHECK(f.service.liveCamera(f.brain).valueOr(Entity{}) == f.low);
}

TEST_CASE("activating next to the largest possible priority does not overflow")
{
    ServiceFixture f;
    const Entity top = f.shot(std::numeric_limits<i32>::max());
    REQUIRE(f.service.activate(f.low).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).priority == std::numeric_limits<i32>::max());
    f.world.destroy(f.low);
    REQUIRE(f.service.activate(top).hasValue());
    CHECK(f.world.get<VirtualCamera>(top).priority == std::numeric_limits<i32>::max());
}

TEST_CASE("the camera calls return NoCamera for an Entity that is not a live virtual camera")
{
    ServiceFixture f;
    const Entity dead = f.shot(1);
    f.world.destroy(dead);
    for (const Entity entity : {Entity{}, dead, f.target})
    {
        CHECK(f.service.activate(entity).error() == CameraError::NoCamera);
        CHECK(f.service.deactivate(entity).error() == CameraError::NoCamera);
        CHECK(f.service.setPriority(entity, 1).error() == CameraError::NoCamera);
        CHECK(f.service.setFollow(entity, f.target).error() == CameraError::NoCamera);
        CHECK(f.service.setLookAt(entity, f.target).error() == CameraError::NoCamera);
        CHECK(f.service.setAngles(entity, 0.0f, 0.0f).error() == CameraError::NoCamera);
        CHECK(f.service.swapShoulder(entity).error() == CameraError::NoCamera);
        CHECK(f.service.setFieldOfView(entity, 1.0f).error() == CameraError::NoCamera);
    }
}

TEST_CASE("follow and look-at targets are set, cleared and checked")
{
    ServiceFixture f;
    REQUIRE(f.service.setFollow(f.low, f.target).hasValue());
    REQUIRE(f.service.setLookAt(f.low, f.target).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).follow == f.target);
    CHECK(f.world.get<VirtualCamera>(f.low).lookAt == f.target);

    REQUIRE(f.service.setFollow(f.low, Entity{}).hasValue());
    REQUIRE(f.service.setLookAt(f.low, Entity{}).hasValue());
    CHECK(f.world.get<VirtualCamera>(f.low).follow.isNull());
    CHECK(f.world.get<VirtualCamera>(f.low).lookAt.isNull());

    const Entity gone = f.world.create(Transform{});
    f.world.destroy(gone);
    CHECK(f.service.setFollow(f.low, gone).error() == CameraError::InvalidArgument);
    CHECK(f.service.setLookAt(f.low, gone).error() == CameraError::InvalidArgument);
    CHECK(f.world.get<VirtualCamera>(f.low).follow.isNull());
}

TEST_CASE("a follow target moves the camera the brain shows")
{
    ServiceFixture f;
    f.world.set(f.high, CameraFollow{.offset = Vec3(0.0f, 0.0f, 4.0f), .damping = Vec3(0.0f)});
    f.world.get<Transform>(f.target).position = Vec3(10.0f, 0.0f, 0.0f);
    REQUIRE(f.service.setFollow(f.high, f.target).hasValue());
    f.run(2);
    CHECK(f.world.get<Transform>(f.brain).position.x == doctest::Approx(10.0f));
    CHECK(f.world.get<Transform>(f.brain).position.z == doctest::Approx(4.0f));
}

TEST_CASE("angles turn an orbit before and after its first update and respect its limits")
{
    ServiceFixture f;
    f.world.set(f.high, CameraOrbit{});
    REQUIRE(f.service.setAngles(f.high, 0.5f, 0.2f).hasValue());
    CHECK(f.world.get<CameraOrbit>(f.high).yaw == doctest::Approx(0.5f));
    f.run(2);
    const VirtualCameraState& state = f.world.get<VirtualCameraState>(f.high);
    CHECK(state.orbit.yaw == doctest::Approx(0.5f));
    CHECK(state.orbit.pitch == doctest::Approx(0.2f));

    REQUIRE(f.service.setAngles(f.high, 7.0f, 5.0f).hasValue());
    CHECK(state.orbit.yaw == doctest::Approx(7.0f - 6.2831853f));
    CHECK(state.orbit.pitch == doctest::Approx(f.world.get<CameraOrbit>(f.high).maxPitch));
}

TEST_CASE("angles of an orbit with limited yaw are clamped")
{
    ServiceFixture f;
    CameraOrbit orbit;
    orbit.limitYaw = true;
    orbit.minYaw = -1.0f;
    orbit.maxYaw = 1.0f;
    f.world.set(f.high, orbit);
    REQUIRE(f.service.setAngles(f.high, 3.0f, 0.0f).hasValue());
    CHECK(f.world.get<CameraOrbit>(f.high).yaw == doctest::Approx(1.0f));
}

TEST_CASE("angles turn a spring arm before and after its first update")
{
    ServiceFixture f;
    f.world.set(f.high, CameraSpringArm{});
    REQUIRE(f.service.setAngles(f.high, 0.4f, 0.1f).hasValue());
    CHECK(f.world.get<CameraSpringArm>(f.high).yaw == doctest::Approx(0.4f));
    f.run(2);
    REQUIRE(f.service.setAngles(f.high, -0.6f, -9.0f).hasValue());
    const VirtualCameraState& state = f.world.get<VirtualCameraState>(f.high);
    CHECK(state.armAngles.x == doctest::Approx(-0.6f));
    CHECK(state.armAngles.y == doctest::Approx(f.world.get<CameraSpringArm>(f.high).minPitch));
    CHECK(state.armAnglesSet);
}

TEST_CASE("angles need a finite value and an orbit or a spring arm")
{
    ServiceFixture f;
    CHECK(f.service.setAngles(f.high, 0.0f, 0.0f).error() == CameraError::NoStage);
    f.world.set(f.high, CameraOrbit{});
    CHECK(f.service.setAngles(f.high, std::numeric_limits<f32>::quiet_NaN(), 0.0f).error() ==
          CameraError::InvalidArgument);
    CHECK(f.service.setAngles(f.high, 0.0f, std::numeric_limits<f32>::infinity()).error() ==
          CameraError::InvalidArgument);
}

TEST_CASE("swapping the shoulder flips the side of the spring arm")
{
    ServiceFixture f;
    CHECK(f.service.swapShoulder(f.high).error() == CameraError::NoStage);
    f.world.set(f.high, CameraSpringArm{});
    CHECK(f.service.swapShoulder(f.high).valueOr(0.0f) == doctest::Approx(-1.0f));
    CHECK(f.world.get<CameraSpringArm>(f.high).side == doctest::Approx(-1.0f));
    CHECK(f.service.swapShoulder(f.high).valueOr(0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("the field of view reaches the Camera and must be inside zero and pi")
{
    ServiceFixture f;
    REQUIRE(f.service.setFieldOfView(f.high, 0.7f).hasValue());
    f.run(1);
    CHECK(f.world.get<Camera>(f.brain).fovY == doctest::Approx(0.7f));
    for (const f32 bad : {0.0f, -1.0f, 3.2f, std::numeric_limits<f32>::quiet_NaN()})
    {
        CHECK(f.service.setFieldOfView(f.high, bad).error() == CameraError::InvalidArgument);
    }
    CHECK(f.world.get<VirtualCamera>(f.high).fovY == doctest::Approx(0.7f));
}

TEST_CASE("shakes and impulses start shakes and report why they cannot")
{
    ServiceFixture f;
    REQUIRE(f.service.shake({.origin = Vec3(0.0f), .strength = 1.0f, .radius = 5.0f, .duration = 1.0f})
                .hasValue());
    CHECK(f.service.shake({.radius = 0.0f}).error() == CameraError::InvalidArgument);

    CameraImpulse impulse;
    impulse.playOnStart = false;
    const Entity source = f.world.create(Transform{}, impulse);
    REQUIRE(f.service.playImpulse(source).hasValue());
    CHECK(f.service.shake({}).error() == CameraError::TooManyShakes);
    f.run(1);
    CHECK(f.cameras.statistics().shakes == 2);

    const Entity gone = f.world.create(Transform{});
    f.world.destroy(gone);
    CHECK(f.service.playImpulse(f.target).error() == CameraError::NoImpulse);
    CHECK(f.service.playImpulse(gone).error() == CameraError::NoImpulse);
    CHECK(f.service.playImpulse(Entity{}).error() == CameraError::NoImpulse);
}

TEST_CASE("the live camera and the blend progress follow the brain")
{
    ServiceFixture f;
    CHECK(f.service.liveCamera(f.brain).valueOr(f.low).isNull());
    CHECK(f.service.blendProgress(f.brain).valueOr(0.0f) == doctest::Approx(1.0f));
    f.run(1);
    CHECK(f.service.liveCamera(f.brain).valueOr(Entity{}) == f.high);
    CHECK(f.service.blendProgress(f.brain).valueOr(0.0f) == doctest::Approx(1.0f));

    f.world.get<CameraBrain>(f.brain).defaultBlend = CameraBlend{CameraBlendStyle::Linear, 1.0f, {}, {}};
    REQUIRE(f.service.activate(f.low).hasValue());
    f.run(1);
    CHECK(f.service.liveCamera(f.brain).valueOr(Entity{}) == f.low);
    f.run(29);
    const f32 half = f.service.blendProgress(f.brain).valueOr(0.0f);
    CHECK(half > 0.4f);
    CHECK(half < 0.6f);
    f.run(40);
    CHECK(f.service.blendProgress(f.brain).valueOr(0.0f) == doctest::Approx(1.0f));
}

TEST_CASE("reading a brain needs a CameraBrain")
{
    ServiceFixture f;
    const Entity gone = f.world.create(Transform{}, CameraBrain{});
    f.world.destroy(gone);
    for (const Entity entity : {Entity{}, gone, f.low})
    {
        CHECK(f.service.liveCamera(entity).error() == CameraError::NoBrain);
        CHECK(f.service.blendProgress(entity).error() == CameraError::NoBrain);
    }
}

TEST_CASE("every camera error has a name and a message")
{
    for (const CameraError error :
         {CameraError::InvalidArgument, CameraError::TooManyShakes, CameraError::NoCamera,
          CameraError::NoBrain, CameraError::NoStage, CameraError::NoImpulse, CameraError::NotActivated})
    {
        CHECK_FALSE(cameraErrorName(error).empty());
        CHECK(cameraErrorMessage(error) != "unknown camera error");
    }
}

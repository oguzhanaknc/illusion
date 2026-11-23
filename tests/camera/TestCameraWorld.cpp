// CameraWorld (ADR 0064): the brain's choice of the live virtual camera, blends, shakes and impulses, and the
// Camera it writes; without physics (the spring arm against PhysicsWorld is in
// runtime/TestCameraRuntime.cpp).

#include "TestSupport.hpp"

#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraWorld.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kDt = 1.0f / 60.0f;

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    registerCameraComponents(ComponentRegistry::instance());
}

[[nodiscard]] Transform at(Vec3 position, Quat rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f))
{
    return Transform{position, rotation, Vec3(1.0f)};
}

[[nodiscard]] VirtualCamera virtualCamera(i32 priority)
{
    VirtualCamera camera;
    camera.priority = priority;
    return camera;
}

[[nodiscard]] CameraBlend blendOf(CameraBlendStyle style, f32 seconds)
{
    CameraBlend blend;
    blend.style = style;
    blend.duration = seconds;
    return blend;
}

[[nodiscard]] CameraBlend linear(f32 seconds)
{
    return blendOf(CameraBlendStyle::Linear, seconds);
}

// A floor plane at y = 0 for every downward cast; records the ignored target.
class FloorCollision final : public CameraWorldCollision
{
public:
    void setIgnored(Entity target) noexcept override
    {
        if (!target.isNull())
        {
            ignored = target;
        }
    }

    std::optional<f32> sphereCast(Vec3 origin, Vec3 direction, f32 maxDistance, f32 radius,
                                  u32 /*layerMask*/) const noexcept override
    {
        ++casts;
        if (!(direction.y < -1e-4f))
        {
            return std::nullopt;
        }
        const f32 distance = (radius - origin.y) / direction.y;
        if (distance < 0.0f || distance > maxDistance)
        {
            return std::nullopt;
        }
        return distance;
    }

    Entity ignored;
    mutable u32 casts = 0;
};

struct Fixture
{
    Fixture()
    {
        reflectAll();
        brain = world.create(at(Vec3(0.0f)), Camera{}, CameraBrain{});
    }

    [[nodiscard]] const Transform& camera() const { return world.get<Transform>(brain); }
    [[nodiscard]] const CameraBrainState& state() const { return world.get<CameraBrainState>(brain); }

    void run(u32 steps, const CameraWorldUpdate& input = {})
    {
        for (u32 i = 0; i < steps; ++i)
        {
            cameras.update(kDt, input);
        }
    }

    World world;
    CameraWorld cameras{world};
    Entity brain;
};

bool same(const Transform& a, const Transform& b)
{
    return std::memcmp(&a.position, &b.position, sizeof(Vec3)) == 0 &&
           std::memcmp(&a.rotation, &b.rotation, sizeof(Quat)) == 0;
}

} // namespace

TEST_CASE("the brain moves its Camera to the virtual camera with the highest priority and takes its lens")
{
    Fixture fixture;
    VirtualCamera low = virtualCamera(1);
    VirtualCamera high = virtualCamera(5);
    high.fovY = 0.8f;
    high.nearPlane = 0.3f;
    high.dutch = 0.25f;
    fixture.world.create(at(Vec3(1.0f, 2.0f, 3.0f)), low);
    const Quat turn = glm::angleAxis(0.5f, Vec3(0.0f, 1.0f, 0.0f));
    const Entity shown = fixture.world.create(at(Vec3(-4.0f, 1.0f, 2.0f), turn), high);
    fixture.run(1);

    CHECK(fixture.state().live == shown);
    CHECK(fixture.camera().position.x == doctest::Approx(-4.0f));
    CHECK(fixture.camera().position.z == doctest::Approx(2.0f));
    const CameraPose expected{Vec3(0.0f), turn, 0.8f, 0.3f, 0.25f};
    const Quat view = cameraViewRotation(expected);
    CHECK(std::abs(glm::dot(fixture.camera().rotation, view)) == doctest::Approx(1.0f));
    CHECK(fixture.world.get<Camera>(fixture.brain).fovY == doctest::Approx(0.8f));
    CHECK(fixture.world.get<Camera>(fixture.brain).nearPlane == doctest::Approx(0.3f));
    CHECK(fixture.cameras.statistics().cameras == 2);
    CHECK(fixture.cameras.statistics().evaluated == 1);
    CHECK(fixture.cameras.statistics().brains == 1);
}

TEST_CASE("a brain without a live virtual camera leaves its Camera where it is")
{
    Fixture fixture;
    fixture.world.get<Transform>(fixture.brain).position = Vec3(7.0f, 8.0f, 9.0f);
    VirtualCamera disabled = virtualCamera(3);
    disabled.enabled = false;
    fixture.world.create(at(Vec3(1.0f)), disabled);
    const Entity inactive = fixture.world.create(at(Vec3(2.0f)), virtualCamera(4));
    fixture.world.emplace<Inactive>(inactive);
    fixture.run(3);

    CHECK(fixture.state().live.isNull());
    CHECK(fixture.camera().position == Vec3(7.0f, 8.0f, 9.0f));
    CHECK(fixture.cameras.statistics().evaluated == 0);
}

TEST_CASE("priority ties go to the virtual camera that became capable last")
{
    Fixture fixture;
    const Entity first = fixture.world.create(at(Vec3(1.0f, 0.0f, 0.0f)), virtualCamera(2));
    VirtualCamera later = virtualCamera(2);
    later.enabled = false;
    const Entity second = fixture.world.create(at(Vec3(2.0f, 0.0f, 0.0f)), later);
    fixture.run(1);
    CHECK(fixture.state().live == first);

    fixture.world.get<VirtualCamera>(second).enabled = true;
    fixture.run(1);
    CHECK(fixture.state().live == second);

    fixture.world.get<VirtualCamera>(first).enabled = false;
    fixture.run(1);
    fixture.world.get<VirtualCamera>(first).enabled = true;
    fixture.run(1);
    CHECK(fixture.state().live == first);
}

TEST_CASE("a blend between two cameras is continuous and ends on the new one")
{
    Fixture fixture;
    fixture.world.get<CameraBrain>(fixture.brain).defaultBlend = blendOf(CameraBlendStyle::EaseInOut, 1.0f);
    const Entity from = fixture.world.create(at(Vec3(0.0f, 1.0f, 0.0f)), virtualCamera(1));
    const Entity to = fixture.world.create(
        at(Vec3(6.0f, 1.0f, 0.0f), glm::angleAxis(1.0f, Vec3(0.0f, 1.0f, 0.0f))), virtualCamera(0));
    fixture.run(2);
    REQUIRE(fixture.state().live == from);

    fixture.world.get<VirtualCamera>(to).priority = 9;
    Vec3 previous = fixture.camera().position;
    Quat previousRotation = fixture.camera().rotation;
    // Ease in/out moves at most 1.5 times the average speed of 6 m/s.
    const f32 largestStep = 1.5f * 6.0f * kDt * 1.01f;
    f32 lastWeight = 0.0f;
    for (u32 i = 0; i < 70; ++i)
    {
        fixture.run(1);
        const Vec3 position = fixture.camera().position;
        CHECK(glm::length(position - previous) <= largestStep);
        CHECK(std::abs(glm::dot(fixture.camera().rotation, previousRotation)) > 0.999f);
        CHECK(fixture.state().weight >= lastWeight);
        lastWeight = fixture.state().weight;
        previous = position;
        previousRotation = fixture.camera().rotation;
        if (i < 50)
        {
            CHECK(fixture.state().blending);
            CHECK(fixture.cameras.statistics().evaluated == 2);
        }
    }
    CHECK_FALSE(fixture.state().blending);
    CHECK(fixture.state().live == to);
    CHECK(fixture.camera().position.x == doctest::Approx(6.0f));
    CHECK(fixture.cameras.statistics().evaluated == 1);
}

TEST_CASE("a blend that breaks off blends on from where the camera was")
{
    Fixture fixture;
    fixture.world.get<CameraBrain>(fixture.brain).defaultBlend = linear(1.0f);
    fixture.world.create(at(Vec3(0.0f)), virtualCamera(1));
    const Entity second = fixture.world.create(at(Vec3(10.0f, 0.0f, 0.0f)), virtualCamera(0));
    const Entity third = fixture.world.create(at(Vec3(0.0f, 0.0f, 10.0f)), virtualCamera(0));
    fixture.run(1);
    fixture.world.get<VirtualCamera>(second).priority = 5;
    fixture.run(30);
    const Vec3 halfway = fixture.camera().position;
    CHECK(halfway.x == doctest::Approx(5.0f).epsilon(0.02));

    fixture.world.get<VirtualCamera>(third).priority = 9;
    fixture.run(1);
    CHECK(fixture.state().from.isNull());
    CHECK(glm::length(fixture.camera().position - halfway) < 0.3f);
    fixture.run(60);
    CHECK(fixture.camera().position.z == doctest::Approx(10.0f));
    CHECK(fixture.camera().position.x == doctest::Approx(0.0f));
}

TEST_CASE("the blend of a change is a pair override, else the new camera's blend, else the brain's default")
{
    Fixture fixture;
    const Entity a = fixture.world.create(at(Vec3(0.0f)), virtualCamera(1));
    VirtualCamera custom = virtualCamera(0);
    custom.customBlend = true;
    custom.blendIn = linear(0.25f);
    const Entity b = fixture.world.create(at(Vec3(4.0f, 0.0f, 0.0f)), custom);
    const Entity c = fixture.world.create(at(Vec3(8.0f, 0.0f, 0.0f)), virtualCamera(0));
    CameraBrain& brain = fixture.world.get<CameraBrain>(fixture.brain);
    brain.defaultBlend = linear(2.0f);
    brain.blends.push_back(CameraBlendOverride{b, c, blendOf(CameraBlendStyle::Cut, 1.0f)});
    fixture.run(1);

    fixture.world.get<VirtualCamera>(b).priority = 5;
    fixture.run(1);
    CHECK(fixture.state().blend.duration == doctest::Approx(0.25f));
    fixture.run(20);
    CHECK_FALSE(fixture.state().blending);

    fixture.world.get<VirtualCamera>(c).priority = 9;
    fixture.run(1);
    CHECK_FALSE(fixture.state().blending);
    CHECK(fixture.camera().position.x == doctest::Approx(8.0f));

    fixture.world.get<VirtualCamera>(c).priority = 0;
    fixture.world.get<VirtualCamera>(a).priority = 9;
    fixture.run(1);
    CHECK(fixture.state().blending);
    CHECK(fixture.state().blend.duration == doctest::Approx(2.0f));
}

TEST_CASE("a follow body with a look-at aim tracks its targets")
{
    Fixture fixture;
    const Entity player = fixture.world.create(at(Vec3(0.0f)));
    const Entity enemy = fixture.world.create(at(Vec3(0.0f, 0.0f, -20.0f)));
    VirtualCamera camera = virtualCamera(1);
    camera.follow = player;
    camera.lookAt = enemy;
    CameraFollow follow;
    follow.space = CameraFollowSpace::World;
    follow.offset = Vec3(0.0f, 2.0f, 5.0f);
    fixture.world.create(at(Vec3(0.0f)), camera, follow, CameraLookAt{});
    fixture.run(1);
    CHECK(fixture.camera().position == Vec3(0.0f, 2.0f, 5.0f));
    const Vec3 forward = fixture.camera().forward();
    CHECK(glm::dot(forward, glm::normalize(Vec3(0.0f, -2.0f, -25.0f))) == doctest::Approx(1.0f));

    fixture.world.get<Transform>(player).position = Vec3(3.0f, 0.0f, 0.0f);
    fixture.run(240);
    CHECK(fixture.camera().position.x == doctest::Approx(3.0f).epsilon(0.001));
}

TEST_CASE("a virtual camera with two bodies uses the first and still runs")
{
    Fixture fixture;
    const Entity target = fixture.world.create(at(Vec3(0.0f)));
    VirtualCamera camera = virtualCamera(1);
    camera.follow = target;
    CameraFollow follow;
    follow.space = CameraFollowSpace::World;
    follow.offset = Vec3(1.0f, 2.0f, 3.0f);
    const Entity rig = fixture.world.create(at(Vec3(0.0f)), camera, follow, CameraOrbit{});
    fixture.run(2);
    CHECK(fixture.camera().position == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(fixture.world.get<VirtualCameraState>(rig).warned);
}

TEST_CASE("a spring arm casts against the collision query, skipping its follow target")
{
    Fixture fixture;
    const Entity target = fixture.world.create(at(Vec3(0.0f, 0.0f, 0.0f)));
    VirtualCamera camera = virtualCamera(1);
    camera.follow = target;
    CameraSpringArm arm;
    arm.shoulderOffset = Vec3(0.0f, 1.0f, 0.0f);
    arm.armLength = 4.0f;
    arm.pitch = 0.6f;
    arm.collisionRadius = 0.25f;
    fixture.world.create(at(Vec3(0.0f)), camera, arm);
    FloorCollision floor;
    CameraWorldUpdate input;
    input.collision = &floor;
    fixture.run(1, input);
    CHECK(floor.ignored == target);
    CHECK(floor.casts == 1);
    CHECK(fixture.camera().position.y == doctest::Approx(0.25f).epsilon(0.001));

    Fixture free;
    const Entity freeTarget = free.world.create(at(Vec3(0.0f)));
    camera.follow = freeTarget;
    free.world.create(at(Vec3(0.0f)), camera, arm);
    free.run(1);
    CHECK(free.camera().position.y < 0.0f);
}

TEST_CASE("a dolly track moves with its Entity")
{
    Fixture fixture;
    VirtualCamera camera = virtualCamera(1);
    CameraDolly dolly;
    dolly.path = {CameraPathPoint{Vec3(0.0f), Vec3(1.0f, 0.0f, 0.0f)},
                  CameraPathPoint{Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}};
    dolly.position = 1.0f;
    const Entity rail = fixture.world.create(at(Vec3(10.0f, 2.0f, 0.0f)), camera, dolly);
    fixture.run(1);
    CHECK(fixture.camera().position.x == doctest::Approx(11.0f));
    CHECK(fixture.camera().position.y == doctest::Approx(2.0f));

    fixture.world.get<Transform>(rail).position = Vec3(0.0f, 5.0f, 0.0f);
    fixture.run(1);
    CHECK(fixture.camera().position.x == doctest::Approx(1.0f));
    CHECK(fixture.camera().position.y == doctest::Approx(5.0f));
}

TEST_CASE("a brain with a parent writes its LocalTransform too")
{
    Fixture fixture;
    const Entity rig = fixture.world.create(at(Vec3(5.0f, 0.0f, 0.0f)));
    fixture.world.emplace<Hierarchy>(fixture.brain, Hierarchy{rig, 0});
    fixture.world.emplace<LocalTransform>(fixture.brain);
    fixture.world.create(at(Vec3(1.0f, 2.0f, 3.0f)), virtualCamera(1));
    fixture.run(1);
    CHECK(fixture.camera().position == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(fixture.world.get<LocalTransform>(fixture.brain).position.x == doctest::Approx(-4.0f));
}

TEST_CASE("shakes move live cameras within their radius and fade out")
{
    Fixture fixture;
    fixture.world.create(at(Vec3(0.0f)), virtualCamera(1));
    fixture.run(1);
    REQUIRE(fixture.cameras.shake({Vec3(100.0f, 0.0f, 0.0f), 1.0f, 10.0f, 0.5f}).hasValue());
    fixture.run(1);
    CHECK(fixture.camera().position == Vec3(0.0f));

    REQUIRE(fixture.cameras.shake({Vec3(1.0f, 0.0f, 0.0f), 1.0f, 10.0f, 0.5f}).hasValue());
    f32 largest = 0.0f;
    for (u32 i = 0; i < 20; ++i)
    {
        fixture.run(1);
        largest = std::max(largest, glm::length(fixture.camera().position));
    }
    CHECK(largest > 0.005f);
    fixture.run(20);
    CHECK(fixture.camera().position == Vec3(0.0f));
    CHECK(fixture.cameras.statistics().shakes == 0);
}

TEST_CASE("shake rejects bad values and more shakes than the world holds")
{
    World world;
    reflectAll();
    CameraWorld cameras(world, {.maxShakes = 2, .reservedCameras = 4});
    CHECK(cameras.shake({Vec3(0.0f), 1.0f, 0.0f, 1.0f}).error() == CameraError::InvalidArgument);
    CHECK(cameras.shake({Vec3(0.0f), 1.0f, 1.0f, -1.0f}).error() == CameraError::InvalidArgument);
    CHECK(cameras.shake({Vec3(std::numeric_limits<f32>::quiet_NaN()), 1.0f, 1.0f, 1.0f}).error() ==
          CameraError::InvalidArgument);
    CHECK(cameras.shake({Vec3(0.0f), -1.0f, 1.0f, 1.0f}).error() == CameraError::InvalidArgument);
    CHECK(cameras.shake({}).hasValue());
    CHECK(cameras.shake({}).hasValue());
    CHECK(cameras.shake({}).error() == CameraError::TooManyShakes);
    CHECK_FALSE(cameraErrorMessage(CameraError::TooManyShakes).empty());
}

TEST_CASE("a camera impulse fires on start and then every interval")
{
    Fixture fixture;
    CameraImpulse impulse;
    impulse.duration = 0.1f;
    impulse.interval = 0.5f;
    fixture.world.create(at(Vec3(0.0f)), impulse);
    CameraImpulse later;
    later.playOnStart = false;
    fixture.world.create(at(Vec3(0.0f)), later);
    fixture.run(1);
    CHECK(fixture.cameras.statistics().shakes == 1);
    fixture.run(10);
    CHECK(fixture.cameras.statistics().shakes == 0);
    fixture.run(22);
    CHECK(fixture.cameras.statistics().shakes == 1);
}

TEST_CASE("runtime states follow their saved Components")
{
    Fixture fixture;
    const Entity camera = fixture.world.create(at(Vec3(0.0f)), virtualCamera(1));
    fixture.run(1);
    CHECK(fixture.world.has<VirtualCameraState>(camera));
    fixture.world.remove<VirtualCamera>(camera);
    fixture.run(1);
    CHECK_FALSE(fixture.world.has<VirtualCameraState>(camera));
    CHECK(fixture.state().live.isNull());
}

TEST_CASE("camera runs with fixed steps repeat bit for bit")
{
    const auto run = [] {
        Fixture fixture;
        const Entity target = fixture.world.create(at(Vec3(0.0f)));
        VirtualCamera camera = virtualCamera(1);
        camera.follow = target;
        CameraNoise noise;
        noise.positionAmplitude = Vec3(0.1f);
        noise.seed = 7;
        fixture.world.create(at(Vec3(0.0f)), camera, CameraFollow{}, CameraComposer{}, noise);
        VirtualCamera other = virtualCamera(0);
        other.follow = target;
        const Entity orbit = fixture.world.create(at(Vec3(0.0f)), other, CameraOrbit{}, CameraLookAt{});
        fixture.world.create(at(Vec3(1.0f, 0.0f, 0.0f)), CameraImpulse{});
        std::vector<Transform> poses;
        for (u32 i = 0; i < 300; ++i)
        {
            fixture.world.get<Transform>(target).position =
                Vec3(std::sin(0.05f * static_cast<f32>(i)), 0.0f, 0.02f * static_cast<f32>(i));
            if (i == 100)
            {
                fixture.world.get<VirtualCamera>(orbit).priority = 5;
            }
            fixture.run(1);
            poses.push_back(fixture.camera());
        }
        return poses;
    };
    const std::vector<Transform> first = run();
    const std::vector<Transform> second = run();
    REQUIRE(first.size() == second.size());
    for (usize i = 0; i < first.size(); ++i)
    {
        CHECK(same(first[i], second[i]));
    }
}

TEST_CASE("a steady camera update allocates nothing")
{
    Fixture fixture;
    const Entity target = fixture.world.create(at(Vec3(0.0f)));
    VirtualCamera camera = virtualCamera(1);
    camera.follow = target;
    fixture.world.create(at(Vec3(0.0f)), camera, CameraSpringArm{}, CameraComposer{}, CameraNoise{});
    VirtualCamera other = virtualCamera(0);
    other.follow = target;
    const Entity orbit = fixture.world.create(at(Vec3(0.0f)), other, CameraOrbit{}, CameraLookAt{});
    fixture.world.get<CameraBrain>(fixture.brain).blends.push_back(CameraBlendOverride{});
    FloorCollision floor;
    CameraWorldUpdate input;
    input.collision = &floor;
    fixture.run(3, input);
    fixture.world.get<VirtualCamera>(orbit).priority = 5;
    {
        test::ScopedAllocationCounter counter;
        fixture.run(60, input);
        REQUIRE(fixture.cameras.shake({}).hasValue());
        fixture.run(60, input);
        CHECK(counter.count() == 0);
    }
}

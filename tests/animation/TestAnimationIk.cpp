// Look-at, two-bone IK and foot placement through AnimationWorld and GameSession (ADR 0061): reflection,
// targets as points and Entities, world-to-model conversion with a turned and scaled Animator, limbs on
// children, smoothing, feet on a slope and on a step, the pelvis drop and its clamp, easing off without
// ground, the character's own bodies, the scripting API with its errors, fixed-step determinism and
// allocation.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/runtime/GameSession.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kSkeleton = "game:/legs.gltf#skin/0";
constexpr f32 kAnkle = 0.08f; // the feet's height in the rest pose

// "hips" 0.9 m up; legs "thigh_l"/"shin_l"/"foot_l" at x = 0.15 and "thigh_r"/"shin_r"/"foot_r" at x = -0.15
// with two bones of 0.41 m; "spine" and "head" above the hips (the head at 1.5 m). No clips: the rest pose
// shows.
void writeLegs(VirtualFileSystem& files)
{
    const std::string_view gltf = R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
"nodes": [{"name": "hips", "translation": [0, 0.9, 0], "children": [1, 4, 7]},
{"name": "thigh_l", "translation": [0.15, 0, 0], "children": [2]}, {"name": "shin_l", "translation": [0, -0.41, 0], "children": [3]},
{"name": "foot_l", "translation": [0, -0.41, 0]},
{"name": "thigh_r", "translation": [-0.15, 0, 0], "children": [5]}, {"name": "shin_r", "translation": [0, -0.41, 0], "children": [6]},
{"name": "foot_r", "translation": [0, -0.41, 0]},
{"name": "spine", "translation": [0, 0.15, 0], "children": [8]}, {"name": "head", "translation": [0, 0.45, 0]}],
"skins": [{"joints": [0, 1, 2, 3, 4, 5, 6, 7, 8]}]})";
    REQUIRE(files.writeText("game:/legs.gltf", gltf) == FileError::None);
}

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    reflectPhysicsComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

Animator legsAnimator()
{
    Animator animator;
    animator.skeleton = AssetRef(kSkeleton);
    return animator;
}

Transform placed(f32 yaw, Vec3 position = Vec3(0.0f), Vec3 scale = Vec3(1.0f))
{
    Transform transform;
    transform.position = position;
    transform.rotation = glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
    transform.scale = scale;
    return transform;
}

LookAt lookAt(Vec3 point, f32 speed = 0.0f)
{
    LookAt look;
    look.joint = "head";
    look.point = point;
    look.speed = speed;
    return look;
}

TwoBoneIk limb(std::string_view joint, Vec3 point = Vec3(0.0f))
{
    TwoBoneIk ik;
    ik.joint = joint;
    ik.point = point;
    return ik;
}

bool near(Vec3 a, Vec3 b, f32 epsilon = 1e-3f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec3(epsilon)));
}

f32 angleBetween(Vec3 a, Vec3 b)
{
    return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f));
}

struct Fixture
{
    Fixture()
    {
        reflectAll();
        writeLegs(project.files);
    }

    [[nodiscard]] Mat4 joint(Entity entity, std::string_view name) const
    {
        const Expected<Mat4, AnimationError> matrix = service.jointTransform(entity, name);
        REQUIRE(matrix.hasValue());
        return *matrix;
    }

    [[nodiscard]] Vec3 position(Entity entity, std::string_view name) const
    {
        return Vec3(joint(entity, name)[3]);
    }

    // The world-space direction of the head's forward axis (+z).
    [[nodiscard]] Vec3 gaze(Entity entity) const
    {
        return glm::normalize(Mat3(joint(entity, "head")) * Vec3(0.0f, 0.0f, 1.0f));
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

// A fixed-step, started GameSession over the legs.
struct Session
{
    Session()
    {
        reflectAll();
        writeLegs(project.files);
        REQUIRE(project.files.mount("assets", project.directory.path / "game") == FileError::None);
        GameSessionDesc desc;
        desc.jobs = &jobs;
        desc.files = &project.files;
        desc.streamer = &streamer;
        desc.audioOutput = AudioOutput::Offline;
        desc.fixedStep = true;
        auto created = GameSession::create(desc);
        REQUIRE(created.hasValue());
        session = std::move(*created);
    }

    [[nodiscard]] World& world() const { return session->world(); }

    // A character standing at @p position with both legs as feet of a FootPlacement.
    Entity character(Vec3 position, const FootPlacement& placement = footPlacement())
    {
        const Entity entity =
            world().create(placed(0.0f, position), legsAnimator(), placement, limb("foot_l"));
        const Entity right = world().create(Transform{}, limb("foot_r"));
        REQUIRE(setParent(world(), right, entity, ParentMode::KeepLocal).hasValue());
        return entity;
    }

    static FootPlacement footPlacement()
    {
        FootPlacement placement;
        placement.pelvis = "hips";
        placement.rayLength = 2.0f;
        return placement;
    }

    void start() const
    {
        session->animation()->waitIdle();
        session->start();
    }

    void run(int updates) const
    {
        for (int i = 0; i < updates; ++i)
        {
            session->update(0.0);
        }
    }

    [[nodiscard]] Vec3 position(Entity entity, std::string_view name) const
    {
        AnimationWorldService service(*session->animation());
        const Expected<Mat4, AnimationError> matrix = service.jointTransform(entity, name);
        REQUIRE(matrix.hasValue());
        return Vec3((*matrix)[3]);
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    AssetStreamer streamer{project.files, jobs};
    std::unique_ptr<GameSession> session;
};

} // namespace

TEST_CASE(
    "LookAt, TwoBoneIk and FootPlacement are reflected as saved Components with units and manual sections")
{
    reflectAll();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    struct Expectation
    {
        std::string_view name;
        std::string_view anchor;
    };
    for (const Expectation& expected :
         {Expectation{"illusion.LookAt", "manual/animation.md#look-at"},
          Expectation{"illusion.TwoBoneIk", "manual/animation.md#two-bone-ik"},
          Expectation{"illusion.FootPlacement", "manual/animation.md#foot-placement"}})
    {
        const ComponentId id = registry.findByStableName(expected.name);
        REQUIRE(id != kInvalidComponentId);
        const TypeReflection* type = registry.reflection(id);
        REQUIRE(type != nullptr);
        CHECK_FALSE(type->meta.transient);
        CHECK(type->meta.docsAnchor == expected.anchor);
        CHECK_FALSE(type->meta.icon.empty());
    }
    const TypeReflection* look = registry.reflection(registry.findByStableName("illusion.LookAt"));
    CHECK(look->field("maxAngle")->meta.unit == Unit::Radians);
    CHECK(look->field("maxAngle")->meta.displayUnit == Unit::Degrees);
    CHECK(look->field("target")->type == FieldType::Entity);
    const TypeReflection* ik = registry.reflection(registry.findByStableName("illusion.TwoBoneIk"));
    CHECK(ik->field("pole")->type == FieldType::Entity);
    const TypeReflection* feet = registry.reflection(registry.findByStableName("illusion.FootPlacement"));
    CHECK(feet->field("maxStep")->meta.unit == Unit::Meters);
    const ComponentId pose = registry.findByStableName("illusion.FootPlacementPose");
    REQUIRE(pose != kInvalidComponentId);
    CHECK(registry.reflection(pose)->meta.transient);
}

TEST_CASE("a LookAt turns the head towards a world point through a turned and scaled Animator")
{
    Fixture f;
    const Transform transform = placed(0.7f, Vec3(2.0f, 0.5f, -1.0f), Vec3(1.5f));
    const Vec3 target = Vec3(transform.matrix() * Vec4(0.6f, 1.8f, 2.0f, 1.0f));
    const Entity entity = f.world.create(transform, legsAnimator(), lookAt(target));
    f.animation.waitIdle(); // not advancing: the head reaches its target at once
    CHECK(angleBetween(f.gaze(entity), target - f.position(entity, "head")) < 2e-3f);
    CHECK(near(f.position(entity, "hips"), Vec3(transform.matrix() * Vec4(0.0f, 0.9f, 0.0f, 1.0f))));
}

TEST_CASE("a LookAt follows its target Entity and shares the turn with its chain")
{
    Fixture f;
    const Entity target = f.world.create(placed(0.0f, Vec3(1.0f, 1.6f, 2.0f)));
    LookAt look = lookAt(Vec3(0.0f));
    look.target = target;
    look.chainLength = 2;
    const Entity entity = f.world.create(Transform{}, legsAnimator(), look);
    f.animation.waitIdle();
    CHECK(angleBetween(f.gaze(entity), Vec3(1.0f, 1.6f, 2.0f) - f.position(entity, "head")) < 2e-3f);
    // The spine turned too: the head moved off the axis.
    CHECK(std::abs(f.position(entity, "head").x) > 1e-3f);

    f.world.get<Transform>(target).position = Vec3(-1.0f, 1.4f, 2.0f);
    f.animation.update(0.0f, &f.jobs, false);
    CHECK(angleBetween(f.gaze(entity), Vec3(-1.0f, 1.4f, 2.0f) - f.position(entity, "head")) < 2e-3f);

    // Weight 0 shows the animated pose.
    f.world.get<LookAt>(entity).weight = 0.0f;
    f.animation.update(0.0f, &f.jobs, false);
    CHECK(angleBetween(f.gaze(entity), Vec3(0.0f, 0.0f, 1.0f)) < 1e-5f);
}

TEST_CASE("LookAt::speed turns the head at most speed times the step, deterministically")
{
    const auto run = [] {
        Fixture f;
        const Entity entity =
            f.world.create(Transform{}, legsAnimator(), lookAt(Vec3(3.0f, 1.5f, 1.7f), 1.0f));
        f.animation.waitIdle(); // the first pose: not advancing reaches the target
        f.world.get<LookAt>(entity).point = Vec3(-3.0f, 1.5f, 1.7f); // to the other side
        std::vector<f32> angles;
        Vec3 previous = f.gaze(entity);
        for (int i = 0; i < 30; ++i)
        {
            f.animation.update(0.1f, &f.jobs, true);
            const Vec3 gaze = f.gaze(entity);
            angles.push_back(angleBetween(previous, gaze));
            previous = gaze;
        }
        angles.push_back(angleBetween(previous, Vec3(-3.0f, 1.5f, 1.7f) - f.position(entity, "head")));
        return angles;
    };
    const std::vector<f32> first = run();
    for (usize i = 0; i + 1 < first.size(); ++i)
    {
        CHECK(first[i] <= 0.1f + 1e-4f);
    }
    CHECK(first.front() == doctest::Approx(0.1f).epsilon(1e-3));
    CHECK(first.back() < 2e-3f); // it arrived
    const std::vector<f32> second = run();
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(f32)) == 0);
}

TEST_CASE("TwoBoneIk limbs on the Animator and on a child reach world-space targets")
{
    Fixture f;
    const Transform transform = placed(-0.4f, Vec3(1.0f, 0.0f, 2.0f), Vec3(2.0f));
    const auto world = [&](Vec3 model) {
        return Vec3(transform.matrix() * Vec4(model, 1.0f));
    };
    const Entity entity =
        f.world.create(transform, legsAnimator(), limb("foot_l", world(Vec3(0.2f, 0.3f, 0.25f))));
    const Entity goal = f.world.create(placed(0.3f, world(Vec3(-0.2f, 0.25f, 0.2f))));
    TwoBoneIk right = limb("foot_r");
    right.target = goal;
    right.alignToTarget = true;
    const Entity child = f.world.create(Transform{}, right);
    REQUIRE(setParent(f.world, child, entity, ParentMode::KeepLocal).hasValue());
    f.animation.waitIdle();
    CHECK(near(f.position(entity, "foot_l"), world(Vec3(0.2f, 0.3f, 0.25f)), 2e-3f));
    CHECK(near(f.position(entity, "foot_r"), world(Vec3(-0.2f, 0.25f, 0.2f)), 2e-3f));
    // The knees bend forward, towards the default pole.
    CHECK(f.position(entity, "shin_l").y > world(Vec3(0.0f, 0.3f, 0.0f)).y);
    // The right foot took the goal's rotation.
    const Mat3 foot(f.joint(entity, "foot_r"));
    const Quat rotation = glm::normalize(
        glm::quat_cast(Mat3(glm::normalize(foot[0]), glm::normalize(foot[1]), glm::normalize(foot[2]))));
    CHECK(std::abs(glm::dot(rotation, f.world.get<Transform>(goal).rotation)) ==
          doctest::Approx(1.0f).epsilon(1e-4));

    // Weight 0 leaves the leg as animated.
    f.world.get<TwoBoneIk>(entity).weight = 0.0f;
    f.animation.update(0.0f, &f.jobs, false);
    CHECK(near(f.position(entity, "foot_l"), world(Vec3(0.15f, kAnkle, 0.0f))));
}

TEST_CASE("a pole Entity decides the way the knee bends")
{
    Fixture f;
    const Entity back = f.world.create(placed(0.0f, Vec3(0.0f, 0.5f, -3.0f)));
    TwoBoneIk ik = limb("foot_l", Vec3(0.15f, 0.4f, 0.0f));
    ik.pole = back;
    const Entity entity = f.world.create(Transform{}, legsAnimator(), ik);
    f.animation.waitIdle();
    CHECK(near(f.position(entity, "foot_l"), Vec3(0.15f, 0.4f, 0.0f)));
    CHECK(f.position(entity, "shin_l").z < -0.1f);
}

TEST_CASE("the IK scripting API sets targets and weights and returns every error")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, legsAnimator(), lookAt(Vec3(0.0f, 1.5f, 3.0f)),
                                         limb("foot_l", Vec3(0.15f, 0.3f, 0.2f)));
    const Entity child = f.world.create(Transform{}, limb("foot_r"));
    REQUIRE(setParent(f.world, child, entity, ParentMode::KeepLocal).hasValue());
    const Entity bare = f.world.create(Transform{}, legsAnimator());
    const Entity plain = f.world.create(Transform{});
    const Entity goal = f.world.create(Transform{});
    f.world.get<LookAt>(entity).target = goal;
    f.world.get<TwoBoneIk>(child).target = goal;
    f.animation.waitIdle();

    REQUIRE(f.service.setLookAtTarget(entity, Vec3(2.0f, 1.5f, 2.0f)).hasValue());
    CHECK(f.world.get<LookAt>(entity).target.isNull());
    REQUIRE(f.service.setIkTarget(entity, "foot_r", Vec3(-0.15f, 0.35f, 0.1f)).hasValue());
    CHECK(f.world.get<TwoBoneIk>(child).target.isNull());
    REQUIRE(f.service.setIkTarget(entity, "foot_l", Vec3(0.2f, 0.25f, 0.15f)).hasValue());
    REQUIRE(f.service.setIkWeight(entity, "foot_r", 2.0f).hasValue());
    CHECK(f.world.get<TwoBoneIk>(child).weight == 1.0f); // clamped
    REQUIRE(f.service.setIkWeight(entity, "foot_l", -1.0f).hasValue());
    CHECK(f.world.get<TwoBoneIk>(entity).weight == 0.0f);
    f.animation.update(0.0f, &f.jobs, false);
    CHECK(angleBetween(f.gaze(entity), Vec3(2.0f, 1.5f, 2.0f) - f.position(entity, "head")) < 2e-3f);
    CHECK(near(f.position(entity, "foot_r"), Vec3(-0.15f, 0.35f, 0.1f)));
    CHECK(near(f.position(entity, "foot_l"), Vec3(0.15f, kAnkle, 0.0f))); // weight 0

    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    CHECK(f.service.setLookAtTarget(plain, Vec3(0.0f)).error() == AnimationError::NoAnimator);
    CHECK(f.service.setLookAtTarget(Entity{}, Vec3(0.0f)).error() == AnimationError::NoAnimator);
    CHECK(f.service.setLookAtTarget(bare, Vec3(0.0f)).error() == AnimationError::UnknownName);
    CHECK(f.service.setLookAtTarget(entity, Vec3(nan)).error() == AnimationError::InvalidValue);
    CHECK(f.service.setIkTarget(plain, "foot_l", Vec3(0.0f)).error() == AnimationError::NoAnimator);
    CHECK(f.service.setIkTarget(entity, "hand_l", Vec3(0.0f)).error() == AnimationError::UnknownName);
    CHECK(f.service.setIkTarget(bare, "foot_l", Vec3(0.0f)).error() == AnimationError::UnknownName);
    CHECK(f.service.setIkTarget(entity, "foot_l", Vec3(0.0f, nan, 0.0f)).error() ==
          AnimationError::InvalidValue);
    CHECK(f.service.setIkWeight(plain, "foot_l", 1.0f).error() == AnimationError::NoAnimator);
    CHECK(f.service.setIkWeight(entity, "", 1.0f).error() == AnimationError::UnknownName);
    CHECK(f.service.setIkWeight(entity, "foot_l", nan).error() == AnimationError::InvalidValue);
    f.world.destroy(entity);
    CHECK(f.service.setIkWeight(entity, "foot_l", 1.0f).error() == AnimationError::NoAnimator);
}

TEST_CASE("a steady-state update with look-at and IK allocates nothing")
{
    Fixture f;
    std::vector<Entity> targets;
    for (u32 i = 0; i < 16; ++i)
    {
        const f32 x = static_cast<f32>(i);
        const Entity entity = f.world.create(placed(0.1f * x, Vec3(x, 0.0f, 0.0f)), legsAnimator(),
                                             lookAt(Vec3(x + 1.0f, 1.5f, 2.0f), 2.0f),
                                             limb("foot_l", Vec3(x + 0.15f, 0.2f, 0.1f)));
        const Entity child = f.world.create(Transform{}, limb(i % 4 == 0 ? "missing" : "foot_r"));
        REQUIRE(setParent(f.world, child, entity, ParentMode::KeepLocal).hasValue());
        if (i % 2 == 0)
        {
            f.world.emplace<FootPlacement>(entity, Session::footPlacement());
        }
    }
    f.animation.waitIdle();
    for (int i = 0; i < 3; ++i)
    {
        f.animation.update(1.0f / 60.0f, &f.jobs, true);
    }
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 60; ++i)
        {
            f.animation.update(1.0f / 60.0f, &f.jobs, true);
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
    CHECK(f.animation.statistics().posed == 16);
}

TEST_CASE("without a running session the feet keep their animated pose")
{
    Fixture f;
    const Entity entity =
        f.world.create(Transform{}, legsAnimator(), Session::footPlacement(), limb("foot_l"));
    f.animation.waitIdle();
    const FootPlacementPose* pose = f.world.tryGet<FootPlacementPose>(entity);
    REQUIRE(pose != nullptr);
    CHECK(pose->footCount == 1);
    CHECK(pose->feet[0].limb == entity);
    CHECK(near(pose->feet[0].animated, Vec3(0.15f, kAnkle, 0.0f)));
    CHECK(near(pose->pelvis, Vec3(0.0f, 0.9f, 0.0f)));
    CHECK(near(f.position(entity, "foot_l"), Vec3(0.15f, kAnkle, 0.0f)));
    f.world.remove<FootPlacement>(entity);
    f.animation.update(0.0f, &f.jobs, false);
    CHECK_FALSE(f.world.has<FootPlacementPose>(entity));
}

TEST_CASE("foot placement puts both feet on a slope and lowers the pelvis to the lower one")
{
    Session s;
    World& world = s.world();
    constexpr f32 kSlope = 0.2f;
    // A plane through the origin that rises along +x: y = x tan(kSlope).
    Transform ground = test::at(Vec3(0.5f * std::sin(kSlope), -0.5f * std::cos(kSlope), 0.0f));
    ground.rotation = glm::angleAxis(kSlope, Vec3(0.0f, 0.0f, 1.0f));
    world.create(ground, test::body(MotionType::Static), test::box(Vec3(5.0f, 0.5f, 5.0f)));
    FootPlacement placement = Session::footPlacement();
    const Entity entity = s.character(Vec3(0.0f), placement);
    world.get<TwoBoneIk>(entity).alignToTarget = true;
    s.start();
    s.run(120);
    const f32 rise = 0.15f * std::tan(kSlope);
    CHECK(near(s.position(entity, "foot_l"), Vec3(0.15f, rise + kAnkle, 0.0f), 2e-3f));
    CHECK(near(s.position(entity, "foot_r"), Vec3(-0.15f, -rise + kAnkle, 0.0f), 2e-3f));
    const FootPlacementPose& pose = world.get<FootPlacementPose>(entity);
    CHECK(pose.pelvisOffset == doctest::Approx(-rise).epsilon(1e-2));
    CHECK(s.position(entity, "hips").y == doctest::Approx(0.9f - rise).epsilon(1e-3));
    CHECK(pose.feet[0].weight == doctest::Approx(1.0f).epsilon(1e-4));
    // The left foot (alignToTarget) lies on the slope; its up axis is the ground normal.
    AnimationWorldService service(*s.session->animation());
    const Vec3 up = glm::normalize(Mat3(*service.jointTransform(entity, "foot_l")) * Vec3(0.0f, 1.0f, 0.0f));
    CHECK(angleBetween(up, Vec3(-std::sin(kSlope), std::cos(kSlope), 0.0f)) < 2e-3f);
}

TEST_CASE("foot placement lifts a foot onto a step and clamps by maxStep")
{
    for (const f32 maxStep : {0.5f, 0.1f})
    {
        Session s;
        World& world = s.world();
        test::createGround(world);
        // A 0.2 m step under the left foot only.
        world.create(test::at(Vec3(0.5f, 0.1f, 0.0f)), test::body(MotionType::Static),
                     test::box(Vec3(0.4f, 0.1f, 1.0f)));
        FootPlacement placement = Session::footPlacement();
        placement.maxStep = maxStep;
        const Entity entity = s.character(Vec3(0.0f), placement);
        s.start();
        s.run(120);
        CHECK(s.position(entity, "foot_l").y ==
              doctest::Approx(std::min(0.2f, maxStep) + kAnkle).epsilon(2e-3));
        CHECK(s.position(entity, "foot_r").y == doctest::Approx(kAnkle).epsilon(2e-3));
        CHECK(world.get<FootPlacementPose>(entity).pelvisOffset == doctest::Approx(0.0f).epsilon(1e-4));
    }
}

TEST_CASE("foot placement drops the pelvis for a character above the ground, at most by maxStep")
{
    for (const f32 maxStep : {0.5f, 0.2f})
    {
        Session s;
        World& world = s.world();
        test::createGround(world);
        FootPlacement placement = Session::footPlacement();
        placement.maxStep = maxStep;
        const Entity entity = s.character(Vec3(0.0f, 0.3f, 0.0f), placement);
        s.start();
        s.run(120);
        const f32 drop = std::min(0.3f, maxStep);
        CHECK(world.get<FootPlacementPose>(entity).pelvisOffset == doctest::Approx(-drop).epsilon(1e-3));
        CHECK(s.position(entity, "hips").y == doctest::Approx(1.2f - drop).epsilon(1e-3));
        CHECK(s.position(entity, "foot_l").y == doctest::Approx(0.3f + kAnkle - drop).epsilon(2e-3));
    }
}

TEST_CASE("foot placement eases a foot's IK off when its ray finds no ground")
{
    Session s;
    World& world = s.world();
    test::createGround(world); // 100 m wide
    const Entity entity = s.character(Vec3(0.0f, 0.2f, 0.0f));
    s.start();
    s.run(60);
    CHECK(world.get<FootPlacementPose>(entity).feet[0].weight > 0.99f);
    CHECK(s.position(entity, "foot_l").y == doctest::Approx(kAnkle).epsilon(2e-3));

    world.get<Transform>(entity).position = Vec3(80.0f, 0.2f, 0.0f); // beyond the ground's edge
    f32 previous = 1.0f;
    bool falling = true;
    for (int i = 0; i < 60; ++i)
    {
        s.run(1);
        const f32 weight = world.get<FootPlacementPose>(entity).feet[0].weight;
        falling = falling && weight <= previous;
        previous = weight;
    }
    CHECK(falling);
    CHECK(previous < 0.01f);
    CHECK(s.position(entity, "foot_l").y == doctest::Approx(0.2f + kAnkle).epsilon(2e-3));
}

TEST_CASE("foot placement rays pass through the character's own bodies")
{
    Session s;
    World& world = s.world();
    test::createGround(world);
    const Entity entity = s.character(Vec3(0.0f, 0.1f, 0.0f));
    world.emplace<RigidBody>(entity, test::body(MotionType::Kinematic));
    world.emplace<Collider>(entity, test::box(Vec3(0.4f, 0.8f, 0.3f)));
    // A shield on a child, right below the hips.
    const Entity shield = world.create(test::at(Vec3(0.0f, 0.6f, 0.0f)), test::body(MotionType::Kinematic),
                                       test::box(Vec3(0.25f, 0.03f, 0.1f)));
    REQUIRE(setParent(world, shield, entity, ParentMode::KeepWorld).hasValue());
    s.start();
    s.run(120);
    CHECK(s.position(entity, "foot_l").y == doctest::Approx(kAnkle).epsilon(2e-3));
    CHECK(world.get<FootPlacementPose>(entity).pelvisOffset == doctest::Approx(-0.1f).epsilon(1e-3));
}

TEST_CASE("fixed-step sessions with look-at, IK and foot placement repeat exactly")
{
    const auto run = [] {
        Session s;
        World& world = s.world();
        test::createGround(world);
        world.create(test::at(Vec3(0.5f, 0.1f, 0.0f)), test::body(MotionType::Static),
                     test::box(Vec3(0.4f, 0.1f, 1.0f)));
        const Entity entity = s.character(Vec3(0.0f, 0.05f, 0.0f));
        world.emplace<LookAt>(entity, lookAt(Vec3(2.0f, 1.0f, 2.0f), 3.0f));
        s.start();
        std::vector<f32> trace;
        for (int i = 0; i < 90; ++i)
        {
            world.get<Transform>(entity).position.x = 0.01f * static_cast<f32>(i);
            s.run(1);
            for (const std::string_view joint : {"foot_l", "foot_r", "hips", "head"})
            {
                const Vec3 position = s.position(entity, joint);
                trace.insert(trace.end(), {position.x, position.y, position.z});
            }
        }
        return trace;
    };
    const std::vector<f32> first = run();
    const std::vector<f32> second = run();
    REQUIRE(first.size() == second.size());
    CHECK(std::memcmp(first.data(), second.data(), first.size() * sizeof(f32)) == 0);
}

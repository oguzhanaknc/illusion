// The ragdoll side of animation (ADR 0061): AnimationWorld's blend between the animated and a handed-over
// ragdoll pose in both directions without jumps, AnimationService::setRagdoll() and its errors, and
// GameSession starting, suspending and handing over a Jolt ragdoll with a `.ragdoll` profile.

#include "TestSupport.hpp"
#include "assets/AssetTestProject.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/Ragdoll.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/runtime/GameSession.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr f32 kStep = 1.0f / 60.0f;
constexpr std::array<std::string_view, 5> kJoints{"hips", "thigh_l", "shin_l", "spine", "head"};

// "hips" 0.9 m up, a left leg of two 0.41 m bones and "spine" and "head" above the hips. No clips.
void writeBody(VirtualFileSystem& files)
{
    REQUIRE(files.writeText("game:/body.gltf",
                            R"({"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
"nodes": [{"name": "hips", "translation": [0, 0.9, 0], "children": [1, 3]},
{"name": "thigh_l", "translation": [0.15, 0, 0], "children": [2]}, {"name": "shin_l", "translation": [0, -0.41, 0]},
{"name": "spine", "translation": [0, 0.15, 0], "children": [4]}, {"name": "head", "translation": [0, 0.45, 0]}],
"skins": [{"joints": [0, 1, 2, 3, 4]}]})") == FileError::None);
    REQUIRE(files.writeText("game:/body.ragdoll", R"({"format": "illusion.ragdoll", "version": 1, "parts": [
  {"joint": "hips", "radius": 0.12, "halfHeight": 0.05, "rotation": [0, 0, 0.7071068, 0.7071068]},
  {"joint": "thigh_l", "radius": 0.06, "halfHeight": 0.13, "offset": [0, -0.205, 0]},
  {"joint": "shin_l", "radius": 0.05, "halfHeight": 0.14, "offset": [0, -0.205, 0]},
  {"joint": "spine", "radius": 0.1, "halfHeight": 0.12, "offset": [0, 0.225, 0]},
  {"joint": "head", "radius": 0.1, "halfHeight": 0, "offset": [0, 0.12, 0]}]})") == FileError::None);
}

void reflectAll()
{
    reflectCoreComponents();
    reflectRenderComponents();
    reflectPhysicsComponents();
    registerAnimationComponents(ComponentRegistry::instance());
}

Animator bodyAnimator()
{
    Animator animator;
    animator.skeleton = AssetRef("game:/body.gltf#skin/0");
    return animator;
}

// The world positions of kJoints.
std::array<Vec3, kJoints.size()> jointPositions(const AnimationWorldService& service, Entity entity)
{
    std::array<Vec3, kJoints.size()> positions{};
    for (usize j = 0; j < kJoints.size(); ++j)
    {
        const Expected<Mat4, AnimationError> matrix = service.jointTransform(entity, kJoints[j]);
        REQUIRE(matrix.hasValue());
        positions[j] = Vec3((*matrix)[3]);
    }
    return positions;
}

f32 largestMove(const std::array<Vec3, kJoints.size()>& a, const std::array<Vec3, kJoints.size()>& b)
{
    f32 largest = 0.0f;
    for (usize j = 0; j < a.size(); ++j)
    {
        largest = std::max(largest, glm::length(a[j] - b[j]));
    }
    return largest;
}

struct Fixture
{
    Fixture()
    {
        reflectAll();
        writeBody(project.files);
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 256}};
    AssetStreamer streamer{project.files, jobs};
    World world;
    AnimationWorld animation{world, streamer};
    AnimationWorldService service{animation};
};

// A fixed-step, started GameSession over the body.
struct Session
{
    Session()
    {
        reflectAll();
        writeBody(project.files);
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

    void run(int updates)
    {
        for (int i = 0; i < updates; ++i)
        {
            streamer.update();
            session->update(0.0);
        }
    }

    test::AssetProject project;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    AssetStreamer streamer{project.files, jobs};
    std::unique_ptr<GameSession> session;
};

} // namespace

TEST_CASE("Ragdoll is reflected as a saved Component with a profile asset, units and a manual section")
{
    reflectAll();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId id = registry.findByStableName("illusion.Ragdoll");
    REQUIRE(id != kInvalidComponentId);
    const TypeReflection* type = registry.reflection(id);
    CHECK_FALSE(type->meta.transient);
    CHECK(type->meta.docsAnchor == "manual/animation.md#ragdolls");
    CHECK(type->field("profile")->meta.assetKind == AssetKind::RagdollProfile);
    CHECK(type->field("blendSeconds")->meta.unit == Unit::Seconds);
    CHECK(type->field("mass")->meta.unit == Unit::Kilograms);
    CHECK(type->field("layer")->meta.hint == FieldHint::Layer);
}

TEST_CASE("the ragdoll blend moves in and out of a handed-over pose without jumps")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, bodyAnimator());
    f.animation.waitIdle();
    const std::optional<u32> slot = f.animation.findSlot(entity);
    REQUIRE(slot.has_value());
    const std::array<Vec3, kJoints.size()> animated = jointPositions(f.service, entity);

    // The body lying on its back along +x, a metre further on.
    const Mat4 lying = glm::translate(Mat4(1.0f), Vec3(1.0f, 0.1f, 0.0f)) *
                       glm::rotate(Mat4(1.0f), -1.5707964f, Vec3(0.0f, 0.0f, 1.0f)) *
                       glm::translate(Mat4(1.0f), Vec3(0.0f, -0.9f, 0.0f));
    const std::span<const Mat4> model = f.animation.jointMatrices(*slot);
    std::vector<Mat4> pose;
    for (const Mat4& joint : model)
    {
        pose.push_back(lying * joint);
    }
    std::array<Vec3, kJoints.size()> lyingPositions{};
    {
        const Skeleton& skeleton = *f.animation.skeleton(*slot);
        for (usize j = 0; j < kJoints.size(); ++j)
        {
            const auto found = std::ranges::find(skeleton.joints, kJoints[j], &SkeletonJoint::name);
            lyingPositions[j] = Vec3(pose[static_cast<usize>(found - skeleton.joints.begin())][3]);
        }
    }
    const f32 distance = largestMove(animated, lyingPositions);
    constexpr f32 kBlend = 0.25f;
    constexpr int kFrames = 15; // kBlend at 60 updates per second
    // A frame of the blend moves a joint by at most its share of the whole way, with room for the arcs.
    const f32 tolerance = 1.6f * distance * kStep / kBlend;

    REQUIRE(f.animation.setRagdollPose(*slot, pose, true, kBlend));
    std::array<Vec3, kJoints.size()> last = animated;
    for (int frame = 1; frame <= kFrames + 2; ++frame)
    {
        f.animation.update(kStep, &f.jobs, true);
        const std::array<Vec3, kJoints.size()> now = jointPositions(f.service, entity);
        CAPTURE(frame);
        CHECK(largestMove(last, now) <= tolerance);
        if (frame == 1)
        {
            CHECK(f.animation.ragdollWeight(*slot) == doctest::Approx(kStep / kBlend));
            CHECK(largestMove(animated, now) > 0.0f);
        }
        last = now;
    }
    CHECK(f.animation.ragdollWeight(*slot) == 1.0f);
    CHECK(largestMove(last, lyingPositions) < 1e-4f);

    // Back to the animation from the last pose given.
    REQUIRE(f.animation.setRagdollPose(*slot, {}, false, kBlend));
    for (int frame = 1; frame <= kFrames + 2; ++frame)
    {
        f.animation.update(kStep, &f.jobs, true);
        const std::array<Vec3, kJoints.size()> now = jointPositions(f.service, entity);
        CAPTURE(frame);
        CHECK(largestMove(last, now) <= tolerance);
        last = now;
    }
    CHECK(f.animation.ragdollWeight(*slot) == 0.0f);
    CHECK(largestMove(last, animated) < 1e-5f);

    // A zero blend switches at once.
    REQUIRE(f.animation.setRagdollPose(*slot, pose, true, 0.0f));
    f.animation.update(kStep, &f.jobs, true);
    CHECK(largestMove(jointPositions(f.service, entity), lyingPositions) < 1e-4f);
}

TEST_CASE("setRagdollPose refuses poses that do not fit the slot")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, bodyAnimator());
    f.animation.waitIdle();
    const u32 slot = f.animation.findSlot(entity).value();
    const std::vector<Mat4> shortPose(2, Mat4(1.0f));
    CHECK_FALSE(f.animation.setRagdollPose(slot, shortPose, true, 0.2f));
    CHECK_FALSE(f.animation.setRagdollPose(slot, {}, true, 0.2f)); // nothing to keep yet
    CHECK_FALSE(f.animation.setRagdollPose(slot + 7, {}, false, 0.2f));
    CHECK(f.animation.setRagdollPose(slot, {}, false, 0.2f));
    f.animation.update(kStep, &f.jobs, true);
    CHECK(f.animation.ragdollWeight(slot) == 0.0f);
    CHECK(f.animation.jointBounds(slot).empty()); // no skinned mesh
}

TEST_CASE("setRagdoll turns the Ragdoll on and off and returns every error")
{
    Fixture f;
    const Entity entity = f.world.create(Transform{}, bodyAnimator(), Ragdoll{});
    const Entity bare = f.world.create(Transform{}, bodyAnimator());
    const Entity plain = f.world.create(Transform{});
    REQUIRE(f.service.setRagdoll(entity, true).hasValue());
    CHECK(f.world.get<Ragdoll>(entity).active);
    REQUIRE(f.service.setRagdoll(entity, false).hasValue());
    CHECK_FALSE(f.world.get<Ragdoll>(entity).active);
    CHECK(f.service.setRagdoll(bare, true).error() == AnimationError::UnknownName);
    CHECK(f.service.setRagdoll(plain, true).error() == AnimationError::NoAnimator);
    CHECK(f.service.setRagdoll(Entity{}, true).error() == AnimationError::NoAnimator);
    f.world.destroy(entity);
    CHECK(f.service.setRagdoll(entity, true).error() == AnimationError::NoAnimator);
}

TEST_CASE("a session starts a ragdoll from its profile, blends it in, suspends the character and blends back")
{
    Session s;
    World& world = s.world();
    test::createGround(world);
    Ragdoll ragdoll;
    ragdoll.profile = AssetRef("game:/body.ragdoll");
    ragdoll.blendSeconds = 0.2f;
    Transform transform;
    transform.position = Vec3(0.0f, 0.0f, 0.0f);
    CharacterController character;
    character.height = 1.6f;
    const Entity entity = world.create(transform, bodyAnimator(), ragdoll, character);
    s.session->animation()->waitIdle();
    s.session->start();
    s.run(10);
    PhysicsWorld& physics = s.session->physics();
    AnimationWorld& animation = *s.session->animation();
    AnimationWorldService service(animation);
    CHECK_FALSE(physics.hasRagdoll(entity));
    const Vec3 standing = jointPositions(service, entity)[4];
    const f32 feet = world.get<Transform>(entity).position.y;

    REQUIRE(service.setRagdoll(entity, true).hasValue());
    s.run(1); // requests the profile, which loads in the background
    CHECK_FALSE(physics.hasRagdoll(entity));
    s.streamer.waitIdle();
    s.run(1);
    REQUIRE(physics.hasRagdoll(entity));
    const u32 slot = animation.findSlot(entity).value();
    s.run(1);
    CHECK(animation.ragdollWeight(slot) > 0.0f);
    CHECK(animation.ragdollWeight(slot) < 1.0f);
    s.run(180);
    CHECK(animation.ragdollWeight(slot) == 1.0f);
    const Vec3 fallen = jointPositions(service, entity)[4];
    CHECK(fallen.y < standing.y - 0.8f); // the head is down on the ground
    CHECK(fallen.y > 0.0f);
    CHECK(world.get<Transform>(entity).position.y == feet); // the character waits

    REQUIRE(service.setRagdoll(entity, false).hasValue());
    s.run(1);
    CHECK_FALSE(physics.hasRagdoll(entity));
    CHECK(animation.ragdollWeight(slot) < 1.0f);
    s.run(30);
    CHECK(animation.ragdollWeight(slot) == 0.0f);
    CHECK(glm::length(jointPositions(service, entity)[4] - standing) < 0.05f);
}

TEST_CASE("a session without a readable profile makes one from the skeleton, or leaves the Ragdoll off")
{
    Session s;
    World& world = s.world();
    Ragdoll ragdoll;
    ragdoll.profile = AssetRef("game:/missing.ragdoll");
    ragdoll.active = true;
    const Entity entity = world.create(Transform{}, bodyAnimator(), ragdoll);
    s.session->animation()->waitIdle();
    s.session->start();
    s.run(20);
    // The body has no skinned mesh, so the made profile is empty: no ragdoll, and no retry every update.
    CHECK_FALSE(s.session->physics().hasRagdoll(entity));
    CHECK(s.session->physics().ragdollCount() == 0);
}

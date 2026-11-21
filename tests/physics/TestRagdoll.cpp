// Ragdolls (ADR 0061): `.ragdoll` profiles and their errors, profiles made from a skeleton, and Jolt ragdolls
// in PhysicsWorld: falling down stairs until they settle, deterministically, starting with the pose's motion,
// and taking the character out of the simulation while they run.

#include "physics/PhysicsTestSupport.hpp"

#include <illusion/assets/Animation.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsWorld.hpp>
#include <illusion/physics/Ragdoll.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

// A figure standing on its feet at the origin: "hips" 0.92 m up with "spine" and "head" above it, and two
// legs of "thigh", "shin" and "foot" (0.42 m bones).
Skeleton figure()
{
    struct Joint
    {
        std::string_view name;
        i32 parent;
        Vec3 translation;
    };
    const Joint joints[] = {
        {"hips", -1, Vec3(0.0f, 0.92f, 0.0f)},      {"spine", 0, Vec3(0.0f, 0.2f, 0.0f)},
        {"head", 1, Vec3(0.0f, 0.35f, 0.0f)},       {"thigh_l", 0, Vec3(0.12f, -0.04f, 0.0f)},
        {"shin_l", 3, Vec3(0.0f, -0.42f, 0.0f)},    {"foot_l", 4, Vec3(0.0f, -0.42f, 0.0f)},
        {"thigh_r", 0, Vec3(-0.12f, -0.04f, 0.0f)}, {"shin_r", 6, Vec3(0.0f, -0.42f, 0.0f)},
        {"foot_r", 7, Vec3(0.0f, -0.42f, 0.0f)},
    };
    Skeleton skeleton;
    std::vector<Vec3> model;
    for (const Joint& joint : joints)
    {
        SkeletonJoint entry;
        entry.name = joint.name;
        entry.parent = joint.parent;
        entry.translation = joint.translation;
        const Vec3 position =
            (joint.parent >= 0 ? model[static_cast<usize>(joint.parent)] : Vec3(0.0f)) + joint.translation;
        model.push_back(position);
        entry.inverseBind = glm::translate(Mat4(1.0f), -position);
        skeleton.joints.push_back(std::move(entry));
    }
    return skeleton;
}

// World-space rest pose of figure() placed by @p placement.
std::vector<Mat4> restPose(const Skeleton& skeleton, const Mat4& placement)
{
    std::vector<Mat4> joints;
    for (const SkeletonJoint& joint : skeleton.joints)
    {
        joints.push_back(placement * glm::inverse(joint.inverseBind));
    }
    return joints;
}

constexpr std::string_view kProfile = R"({"format": "illusion.ragdoll", "version": 1, "parts": [
  {"joint": "hips", "radius": 0.12, "halfHeight": 0.04, "offset": [0, 0.08, 0], "rotation": [0, 0, 0.7071068, 0.7071068], "massShare": 0.3},
  {"joint": "spine", "radius": 0.11, "halfHeight": 0.08, "offset": [0, 0.18, 0], "swingLimit": 0.4},
  {"joint": "head", "radius": 0.1, "halfHeight": 0, "offset": [0, 0.1, 0], "twistMin": -0.6, "twistMax": 0.6},
  {"joint": "thigh_l", "radius": 0.07, "halfHeight": 0.14, "offset": [0, -0.21, 0]},
  {"joint": "shin_l", "radius": 0.055, "halfHeight": 0.15, "offset": [0, -0.21, 0]},
  {"joint": "thigh_r", "radius": 0.07, "halfHeight": 0.14, "offset": [0, -0.21, 0]},
  {"joint": "shin_r", "radius": 0.055, "halfHeight": 0.15, "offset": [0, -0.21, 0]},
  {"joint": "tail", "radius": 0.05}]})";

RagdollProfile profile()
{
    Expected<RagdollProfile, std::string> parsed = parseRagdollProfile(kProfile);
    REQUIRE_MESSAGE(parsed.hasValue(), parsed.error());
    return *parsed;
}

Ragdoll active()
{
    Ragdoll ragdoll;
    ragdoll.active = true;
    return ragdoll;
}

// Six steps of 0.2 m going down along +x from a 1.2 m landing to the ground.
void createStairs(World& world)
{
    test::createGround(world);
    for (int i = 0; i < 6; ++i)
    {
        const f32 top = 1.2f - 0.2f * static_cast<f32>(i);
        world.create(test::at(Vec3(0.4f * static_cast<f32>(i), 0.5f * top, 0.0f)),
                     test::body(MotionType::Static), test::box(Vec3(0.2f, 0.5f * top, 1.0f)));
    }
}

struct StairsRun
{
    Aabb bounds;
    f32 startHips = 0.0f;
    f32 lowest = 0.0f;  // lowest joint over the whole fall
    f32 settled = 1e9f; // largest joint motion over the last second
    bool awake = true;
};

// The figure stands on the top landing, gets a push down the stairs and falls for ten seconds.
StairsRun fallDownStairs()
{
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorld physics(world, jobs);
    createStairs(world);
    const Entity body = world.create(test::at(Vec3(0.0f, 1.2f, 0.0f)), active());
    physics.update(test::kStep); // the stairs get their bodies

    const Skeleton skeleton = figure();
    const Mat4 placement = glm::translate(Mat4(1.0f), Vec3(0.0f, 1.2f, 0.0f));
    const std::vector<Mat4> joints = restPose(skeleton, placement);
    // One step earlier the figure stood 2.5 cm further back: it moves at 1.5 m/s along +x.
    const std::vector<Mat4> before = restPose(
        skeleton, glm::translate(placement, Vec3(-1.5f * static_cast<f32>(test::kStep), 0.0f, 0.0f)));
    REQUIRE(physics.startRagdoll(body, world.get<Ragdoll>(body), profile(), skeleton, joints, before,
                                 static_cast<f32>(test::kStep)));
    CHECK(physics.hasRagdoll(body));
    CHECK(physics.ragdollCount() == 1);
    CHECK(physics.bodyCount() == 7 + 7); // the ground, six steps and seven parts ("tail" is skipped)

    StairsRun run;
    run.startHips = joints[0][3].y;
    run.lowest = run.startHips;
    std::vector<Mat4> last(joints.size());
    constexpr int kSteps = 600;
    for (int step = 0; step < kSteps; ++step)
    {
        physics.update(test::kStep);
        const std::span<const Mat4> pose = physics.ragdollJoints(body);
        REQUIRE(pose.size() == joints.size());
        for (usize j = 0; j < pose.size(); ++j)
        {
            run.lowest = std::min(run.lowest, pose[j][3].y);
            if (step >= kSteps - 60)
            {
                const f32 motion = glm::length(Vec3(pose[j][3]) - Vec3(last[j][3]));
                run.settled = step == kSteps - 60 ? 0.0f : std::max(run.settled, motion);
            }
            last[j] = pose[j];
        }
    }
    for (const Mat4& joint : physics.ragdollJoints(body))
    {
        run.bounds.expand(Vec3(joint[3]));
    }
    run.awake = physics.isRagdollAwake(body);
    return run;
}

} // namespace

TEST_CASE("a .ragdoll profile is parsed with its defaults and normalized rotations")
{
    const RagdollProfile parsed = profile();
    REQUIRE(parsed.parts.size() == 8);
    CHECK(parsed.parts[0].joint == "hips");
    CHECK(parsed.parts[0].massShare == doctest::Approx(0.3f));
    CHECK(parsed.parts[0].rotation.w == doctest::Approx(0.7071068f));
    CHECK(parsed.parts[0].rotation.z == doctest::Approx(0.7071068f));
    CHECK(parsed.parts[1].swingLimit == doctest::Approx(0.4f));
    CHECK(parsed.parts[2].twistMin == doctest::Approx(-0.6f));
    CHECK(parsed.parts[2].halfHeight == 0.0f);
    const RagdollPart defaults;
    CHECK(parsed.parts[7].halfHeight == defaults.halfHeight);
    CHECK(parsed.parts[7].swingLimit == defaults.swingLimit);
    CHECK(parsed.parts[7].offset == Vec3(0.0f));

    const Expected<RagdollProfile, std::string> unnormalized = parseRagdollProfile(
        R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "rotation": [0, 2, 0, 0]}]})");
    REQUIRE(unnormalized.hasValue());
    CHECK(unnormalized->parts[0].rotation.y == doctest::Approx(1.0f));
}

TEST_CASE("a .ragdoll profile that cannot be used names the JSON path of the problem")
{
    struct Case
    {
        std::string_view json;
        std::string_view error;
    };
    const Case cases[] = {
        {"{", "invalid JSON"},
        {R"({"version": 1, "parts": []})", "not a ragdoll profile"},
        {R"({"format": "illusion.ragdoll", "version": 2, "parts": []})", "version: unsupported"},
        {R"({"format": "illusion.ragdoll", "version": 1, "bodies": []})", "unknown key 'bodies'"},
        {R"({"format": "illusion.ragdoll", "version": 1})", "parts: is required"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": []})", "parts: expected an array"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [3]})", "parts[0]: expected an object"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"radius": 1}]})",
         "parts[0].joint: is required"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a"}, {"joint": "b", "radius": -1}]})",
         "parts[1].radius: must be at least 0"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "radius": 0}]})",
         "parts[0].radius: must be positive"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "offset": [1, 2]}]})",
         "parts[0].offset: expected an array of 3 numbers"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "offset": [1, "x", 2]}]})",
         "parts[0].offset[1]: expected a number"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "rotation": [0, 0, 0, 0]}]})",
         "parts[0].rotation: must not be zero"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "swingLimit": 4}]})",
         "parts[0].swingLimit: must be in [0, 3.14"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "twistMin": 0.5}]})",
         "parts[0].twistMin: must be in [-3.14"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "mass": 3}]})",
         "parts[0]: unknown key 'mass'"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a"}, {"joint": "a"}]})",
         "parts[1].joint: duplicate joint 'a'"},
        {R"({"format": "illusion.ragdoll", "version": 1, "parts": [{"joint": "a", "massShare": 0.7}, {"joint": "b", "massShare": 0.6}]})",
         "parts[1].massShare: the shares add up to more than 1"},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.json);
        const Expected<RagdollProfile, std::string> parsed = parseRagdollProfile(c.json);
        REQUIRE_FALSE(parsed.hasValue());
        CHECK_MESSAGE(parsed.error().find(c.error) != std::string::npos, parsed.error());
    }
}

TEST_CASE("a profile made from the skeleton fits a capsule along each bone and skips small joints")
{
    const Skeleton skeleton = figure();
    std::vector<Aabb> bounds(skeleton.joints.size());
    const auto box = [&](usize joint, Vec3 low, Vec3 high) {
        bounds[joint].expand(low);
        bounds[joint].expand(high);
    };
    box(0, Vec3(-0.15f, -0.1f, -0.1f), Vec3(0.15f, 0.15f, 0.1f));
    box(1, Vec3(-0.12f, 0.0f, -0.08f), Vec3(0.12f, 0.35f, 0.08f));
    box(2, Vec3(-0.09f, 0.0f, -0.1f), Vec3(0.09f, 0.24f, 0.1f));
    for (const usize leg : {usize{3}, usize{6}})
    {
        box(leg, Vec3(-0.07f, -0.42f, -0.07f), Vec3(0.07f, 0.0f, 0.07f));
        box(leg + 1, Vec3(-0.05f, -0.42f, -0.05f), Vec3(0.05f, 0.0f, 0.05f));
        box(leg + 2, Vec3(-0.01f, -0.015f, 0.0f), Vec3(0.01f, 0.0f, 0.015f)); // 3 cm: below minSize
    }
    const RagdollProfile made = makeRagdollProfile(skeleton, bounds);
    REQUIRE(made.parts.size() == 7);
    const std::vector<std::string_view> names{"hips",   "spine",   "head",  "thigh_l",
                                              "shin_l", "thigh_r", "shin_r"};
    for (usize k = 0; k < names.size(); ++k)
    {
        CHECK(made.parts[k].joint == names[k]);
    }
    const RagdollPart& thigh = made.parts[3];
    CHECK(thigh.radius == doctest::Approx(0.07f));
    CHECK(thigh.halfHeight == doctest::Approx(0.21f - 0.07f));
    CHECK(glm::length(thigh.offset - Vec3(0.0f, -0.21f, 0.0f)) < 1e-5f);
    CHECK(glm::length(thigh.rotation * Vec3(0.0f, 1.0f, 0.0f) - Vec3(0.0f, -1.0f, 0.0f)) < 1e-5f);
    const RagdollPart& head = made.parts[2]; // a leaf: along the longest side of its bounds
    CHECK(glm::length(head.rotation * Vec3(0.0f, 1.0f, 0.0f) - Vec3(0.0f, 1.0f, 0.0f)) < 1e-5f);
    CHECK(glm::length(head.offset - Vec3(0.0f, 0.12f, 0.0f)) < 1e-5f);
    CHECK(head.radius == doctest::Approx(0.095f));
    CHECK(head.halfHeight == doctest::Approx(0.025f));
    for (const RagdollPart& part : made.parts)
    {
        CHECK(part.massShare == 0.0f);
        CHECK(part.swingLimit > 0.0f);
    }
    CHECK(makeRagdollProfile(skeleton, {}).parts.empty());

    // A model in centimetres: the capsules come out in metres.
    Skeleton scaled = skeleton;
    for (SkeletonJoint& joint : scaled.joints)
    {
        joint.inverseBind = joint.inverseBind * glm::scale(Mat4(1.0f), Vec3(100.0f));
    }
    std::vector<Aabb> centimetres = bounds;
    for (Aabb& b : centimetres)
    {
        b = Aabb{b.min * 100.0f, b.max * 100.0f};
    }
    const RagdollProfile big = makeRagdollProfile(scaled, centimetres);
    REQUIRE(big.parts.size() == 7);
    CHECK(big.parts[3].radius == doctest::Approx(0.07f));
}

TEST_CASE("a ragdoll falls down stairs and settles, the same way every time")
{
    const StairsRun first = fallDownStairs();
    CHECK(first.lowest < first.startHips - 0.5f); // it went down
    CHECK(first.lowest > -0.05f);                 // and not through the ground
    CHECK(first.bounds.max.x > 0.4f);             // down the stairs, not just over
    CHECK(first.settled < 2e-3f);
    CHECK_FALSE(first.awake);
    const StairsRun second = fallDownStairs();
    CHECK(second.bounds.min == first.bounds.min);
    CHECK(second.bounds.max == first.bounds.max);
}

TEST_CASE("a ragdoll starts with the motion of the pose and keeps unparted joints with their part")
{
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorldDesc desc;
    desc.gravity = Vec3(0.0f);
    PhysicsWorld physics(world, jobs, desc);
    const Entity body = world.create(Transform{}, active());
    const Skeleton skeleton = figure();
    const Mat4 placement = glm::translate(Mat4(1.0f), Vec3(0.0f, 3.0f, 0.0f));
    const std::vector<Mat4> joints = restPose(skeleton, placement);
    const std::vector<Mat4> before = restPose(skeleton, glm::translate(placement, Vec3(0.0f, 0.0f, -0.05f)));
    REQUIRE(physics.startRagdoll(body, world.get<Ragdoll>(body), profile(), skeleton, joints, before, 0.1f));
    const std::span<const Mat4> start = physics.ragdollJoints(body);
    REQUIRE(start.size() == joints.size());
    for (usize j = 0; j < joints.size(); ++j)
    {
        CHECK(glm::length(Vec3(start[j][3]) - Vec3(joints[j][3])) < 1e-5f);
    }
    for (int step = 0; step < 30; ++step)
    {
        physics.update(test::kStep);
    }
    // Half a second at 0.5 m/s without gravity: everything moved 0.25 m along +z, feet with their shins.
    const std::span<const Mat4> moved = physics.ragdollJoints(body);
    for (usize j = 0; j < joints.size(); ++j)
    {
        CAPTURE(j);
        CHECK(glm::length(Vec3(moved[j][3]) - (Vec3(joints[j][3]) + Vec3(0.0f, 0.0f, 0.25f))) < 0.02f);
    }

    world.get<Ragdoll>(body).active = false;
    physics.update(test::kStep);
    CHECK_FALSE(physics.hasRagdoll(body));
    CHECK(physics.ragdollJoints(body).empty());
    CHECK(physics.bodyCount() == 0);
}

TEST_CASE("startRagdoll refuses poses and profiles that do not fit the skeleton")
{
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorld physics(world, jobs);
    const Entity body = world.create(Transform{}, active());
    const Skeleton skeleton = figure();
    const std::vector<Mat4> joints = restPose(skeleton, Mat4(1.0f));
    RagdollProfile strangers;
    strangers.parts.emplace_back().joint = "wing";
    CHECK_FALSE(physics.startRagdoll(body, world.get<Ragdoll>(body), strangers, skeleton, joints));
    CHECK_FALSE(physics.startRagdoll(body, world.get<Ragdoll>(body), profile(), skeleton,
                                     std::span<const Mat4>(joints).first(3)));
    CHECK_FALSE(physics.startRagdoll(Entity{}, Ragdoll{}, profile(), skeleton, joints));
    CHECK(physics.ragdollCount() == 0);

    // A destroyed Entity loses its ragdoll at the next update.
    REQUIRE(physics.startRagdoll(body, world.get<Ragdoll>(body), profile(), skeleton, joints));
    world.destroy(body);
    physics.update(test::kStep);
    CHECK(physics.ragdollCount() == 0);
    CHECK(physics.bodyCount() == 0);
}

TEST_CASE("a character is out of the simulation while its ragdoll runs and comes back after it")
{
    JobSystem jobs{JobSystemDesc{2, 1024}};
    World world;
    PhysicsWorld physics(world, jobs);
    test::createGround(world);
    const Entity character = world.create(test::at(Vec3(0.0f, 5.0f, 0.0f)), CharacterController{}, active());
    physics.update(test::kStep);
    REQUIRE(physics.characterCount() == 1);
    const Skeleton skeleton = figure();
    // The ragdoll lies far from the character, so that only the character's own motion is measured.
    REQUIRE(physics.startRagdoll(character, world.get<Ragdoll>(character), profile(), skeleton,
                                 restPose(skeleton, glm::translate(Mat4(1.0f), Vec3(10.0f, 0.0f, 0.0f)))));
    const std::optional<RaycastHit> before =
        physics.raycast(Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 20.0f);
    REQUIRE(before.has_value());
    CHECK(before->point.y < 0.01f); // through the character to the ground
    for (int step = 0; step < 30; ++step)
    {
        physics.update(test::kStep);
    }
    CHECK(world.get<Transform>(character).position.y == doctest::Approx(5.0f));

    world.get<Ragdoll>(character).active = false;
    physics.update(test::kStep);
    CHECK_FALSE(physics.hasRagdoll(character));
    const std::optional<RaycastHit> after =
        physics.raycast(Vec3(0.0f, 10.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f), 20.0f);
    REQUIRE(after.has_value());
    CHECK(after->entity == character);
    for (int step = 0; step < 30; ++step)
    {
        physics.update(test::kStep);
    }
    CHECK(world.get<Transform>(character).position.y < 4.5f); // it falls again
}

// The look-at and two-bone IK solvers (ADR 0061) on hand-made skeletons, without a World: reach, out of
// reach, full length, the pole side, weights, the look-at clamp, targets behind the head, chain sharing,
// alignment, degenerate input and allocation.

#include "TestSupport.hpp"

#include <illusion/animation/InverseKinematics.hpp>
#include <illusion/animation/Pose.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace illusion;

namespace
{

SkeletonJoint joint(i32 parent, Vec3 translation)
{
    SkeletonJoint result;
    result.parent = parent;
    result.translation = translation;
    return result;
}

// "hip" two metres up, "knee" and "ankle" one metre below each other: a straight leg of two 1 m bones.
Skeleton leg()
{
    Skeleton skeleton;
    skeleton.joints = {joint(-1, Vec3(0.0f, 2.0f, 0.0f)), joint(0, Vec3(0.0f, -1.0f, 0.0f)),
                       joint(1, Vec3(0.0f, -1.0f, 0.0f))};
    return skeleton;
}

// "chest" one metre up, "neck" 0.3 m and "head" 0.2 m above it; forward is +z.
Skeleton neck()
{
    Skeleton skeleton;
    skeleton.joints = {joint(-1, Vec3(0.0f, 1.0f, 0.0f)), joint(0, Vec3(0.0f, 0.3f, 0.0f)),
                       joint(1, Vec3(0.0f, 0.2f, 0.0f))};
    return skeleton;
}

std::vector<JointPose> restOf(const Skeleton& skeleton)
{
    std::vector<JointPose> pose(skeleton.joints.size());
    restPose(skeleton, pose);
    return pose;
}

std::vector<Mat4> modelOf(const Skeleton& skeleton, const std::vector<JointPose>& pose)
{
    std::vector<Mat4> model(pose.size());
    localToModel(skeleton, pose, model);
    return model;
}

Vec3 positionOf(const Skeleton& skeleton, const std::vector<JointPose>& pose, usize joint)
{
    return Vec3(modelOf(skeleton, pose)[joint][3]);
}

// The model-space direction of @p joint's +z axis.
Vec3 forwardOf(const Skeleton& skeleton, const std::vector<JointPose>& pose, usize joint)
{
    return glm::normalize(Mat3(modelOf(skeleton, pose)[joint]) * Vec3(0.0f, 0.0f, 1.0f));
}

f32 angleBetween(Vec3 a, Vec3 b)
{
    return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f));
}

bool near(Vec3 a, Vec3 b, f32 epsilon = 1e-4f)
{
    return glm::all(glm::lessThanEqual(glm::abs(a - b), Vec3(epsilon)));
}

bool finitePose(const std::vector<JointPose>& pose)
{
    for (const JointPose& joint : pose)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(joint.translation[i]) || !std::isfinite(joint.scale[i]))
            {
                return false;
            }
        }
        for (int i = 0; i < 4; ++i)
        {
            if (!std::isfinite(joint.rotation[i]))
            {
                return false;
            }
        }
    }
    return true;
}

TwoBoneSolve reach(Vec3 target, Vec3 pole = Vec3(0.0f, 1.0f, 5.0f), f32 weight = 1.0f)
{
    TwoBoneSolve solve;
    solve.target = target;
    solve.pole = pole;
    solve.weight = weight;
    return solve;
}

LookAtSolve look(Vec3 target, f32 maxAngle = 1.2217305f, f32 weight = 1.0f)
{
    LookAtSolve solve;
    solve.target = target;
    solve.maxAngle = maxAngle;
    solve.weight = weight;
    return solve;
}

} // namespace

TEST_CASE("two-bone IK reaches a reachable target and keeps the bone lengths")
{
    const Skeleton skeleton = leg();
    std::vector<JointPose> pose = restOf(skeleton);
    const Vec3 target(0.5f, 0.6f, 0.3f);
    const f32 remaining = solveTwoBoneIk(skeleton, 2, reach(target), pose);
    CHECK(remaining < 1e-4f);
    CHECK(near(positionOf(skeleton, pose, 2), target));
    CHECK(glm::length(positionOf(skeleton, pose, 1) - positionOf(skeleton, pose, 0)) ==
          doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(glm::length(positionOf(skeleton, pose, 2) - positionOf(skeleton, pose, 1)) ==
          doctest::Approx(1.0f).epsilon(1e-5));
    CHECK(near(positionOf(skeleton, pose, 0), Vec3(0.0f, 2.0f, 0.0f))); // the root stays
}

TEST_CASE("two-bone IK stretches straight towards a target out of reach")
{
    const Skeleton skeleton = leg();
    std::vector<JointPose> pose = restOf(skeleton);
    const Vec3 target(3.0f, 2.0f, 0.0f); // three metres from the hip
    const f32 remaining = solveTwoBoneIk(skeleton, 2, reach(target), pose);
    CHECK(remaining == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(near(positionOf(skeleton, pose, 1), Vec3(1.0f, 2.0f, 0.0f)));
    CHECK(near(positionOf(skeleton, pose, 2), Vec3(2.0f, 2.0f, 0.0f)));
    CHECK(finitePose(pose));
}

TEST_CASE("two-bone IK reaches a target exactly at full length without NaN")
{
    const Skeleton skeleton = leg();
    std::vector<JointPose> pose = restOf(skeleton);
    const Vec3 target(0.0f, 2.0f, 2.0f);
    const f32 remaining = solveTwoBoneIk(skeleton, 2, reach(target), pose);
    CHECK(remaining < 1e-4f);
    CHECK(finitePose(pose));
    CHECK(near(positionOf(skeleton, pose, 1), Vec3(0.0f, 2.0f, 1.0f)));
}

TEST_CASE("two-bone IK bends the middle joint towards the pole")
{
    const Skeleton skeleton = leg();
    const Vec3 target(0.0f, 0.8f, 0.0f); // closer than the full length: the knee must bend
    for (const f32 side : {1.0f, -1.0f})
    {
        std::vector<JointPose> pose = restOf(skeleton);
        CHECK(solveTwoBoneIk(skeleton, 2, reach(target, Vec3(0.0f, 1.0f, 5.0f * side)), pose) < 1e-4f);
        CHECK(positionOf(skeleton, pose, 1).z * side > 0.5f);
        CHECK(std::abs(positionOf(skeleton, pose, 1).x) < 1e-4f);
    }
    // A pole on the line from the root to the target leaves the bend as the animation has it.
    std::vector<JointPose> pose = restOf(skeleton);
    pose[0].rotation = glm::angleAxis(0.3f, Vec3(1.0f, 0.0f, 0.0f));
    const f32 before = positionOf(skeleton, pose, 1).z;
    CHECK(solveTwoBoneIk(skeleton, 2, reach(target, Vec3(0.0f, -4.0f, 0.0f)), pose) < 1e-4f);
    CHECK(positionOf(skeleton, pose, 1).z * before > 0.0f);
}

TEST_CASE("two-bone IK blends by weight")
{
    const Skeleton skeleton = leg();
    const Vec3 target(0.6f, 0.9f, 0.2f);
    const std::vector<JointPose> rest = restOf(skeleton);

    std::vector<JointPose> none = rest;
    const f32 untouched = solveTwoBoneIk(skeleton, 2, reach(target, Vec3(0.0f, 1.0f, 5.0f), 0.0f), none);
    CHECK(untouched == doctest::Approx(glm::length(Vec3(0.0f) - target)));
    for (usize j = 0; j < rest.size(); ++j)
    {
        CHECK(none[j].rotation == rest[j].rotation);
    }

    std::vector<JointPose> half = rest;
    const f32 halfway = solveTwoBoneIk(skeleton, 2, reach(target, Vec3(0.0f, 1.0f, 5.0f), 0.5f), half);
    std::vector<JointPose> full = rest;
    const f32 reached = solveTwoBoneIk(skeleton, 2, reach(target, Vec3(0.0f, 1.0f, 5.0f), 1.0f), full);
    CHECK(reached < 1e-4f);
    CHECK(halfway > reached);
    CHECK(halfway < untouched);
}

TEST_CASE("two-bone IK aligns the end joint to a model-space rotation")
{
    const Skeleton skeleton = leg();
    std::vector<JointPose> pose = restOf(skeleton);
    TwoBoneSolve solve = reach(Vec3(0.3f, 0.5f, 0.4f));
    solve.alignEnd = true;
    solve.endRotation = glm::angleAxis(0.4f, glm::normalize(Vec3(1.0f, 0.0f, 1.0f)));
    CHECK(solveTwoBoneIk(skeleton, 2, solve, pose) < 1e-4f);
    const Mat3 end(modelOf(skeleton, pose)[2]);
    const Quat rotation = glm::normalize(glm::quat_cast(end));
    CHECK(std::abs(glm::dot(rotation, solve.endRotation)) == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("two-bone IK leaves degenerate chains and input without NaN")
{
    // Zero-length bones.
    Skeleton flat = leg();
    flat.joints[1].translation = Vec3(0.0f);
    flat.joints[2].translation = Vec3(0.0f);
    std::vector<JointPose> pose = restOf(flat);
    CHECK(std::isfinite(solveTwoBoneIk(flat, 2, reach(Vec3(1.0f)), pose)));
    CHECK(finitePose(pose));

    const Skeleton skeleton = leg();
    // A target on the root, a pole on the target, a pole that is not finite.
    for (const TwoBoneSolve& solve :
         {reach(Vec3(0.0f, 2.0f, 0.0f)), reach(Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)),
          reach(Vec3(0.2f, 1.0f, 0.0f), Vec3(std::numeric_limits<f32>::quiet_NaN()))})
    {
        pose = restOf(skeleton);
        CHECK(std::isfinite(solveTwoBoneIk(skeleton, 2, solve, pose)));
        CHECK(finitePose(pose));
    }
    // A target that is not finite, an end without a grandparent and an end out of range change nothing.
    const std::vector<JointPose> rest = restOf(skeleton);
    pose = rest;
    CHECK_FALSE(
        std::isfinite(solveTwoBoneIk(skeleton, 2, reach(Vec3(std::numeric_limits<f32>::infinity())), pose)));
    CHECK_FALSE(std::isfinite(solveTwoBoneIk(skeleton, 1, reach(Vec3(0.0f)), pose)));
    CHECK_FALSE(std::isfinite(solveTwoBoneIk(skeleton, 7, reach(Vec3(0.0f)), pose)));
    for (usize j = 0; j < rest.size(); ++j)
    {
        CHECK(pose[j].rotation == rest[j].rotation);
    }
}

TEST_CASE("look-at points the head at a target within its limit")
{
    const Skeleton skeleton = neck();
    std::vector<JointPose> pose = restOf(skeleton);
    const Vec3 target(1.0f, 2.0f, 2.0f);
    const std::array<u32, 1> chain{2};
    const f32 turned = solveLookAt(skeleton, chain, look(target), pose);
    const Vec3 head = positionOf(skeleton, pose, 2);
    CHECK(angleBetween(forwardOf(skeleton, pose, 2), target - head) < 1e-3f);
    CHECK(turned == doctest::Approx(angleBetween(Vec3(0.0f, 0.0f, 1.0f), target - head)).epsilon(1e-3));
}

TEST_CASE("look-at clamps the turn to maxAngle and scales it by weight")
{
    const Skeleton skeleton = neck();
    const std::array<u32, 1> chain{2};
    const Vec3 side(5.0f, 1.5f, 0.0f); // 90 degrees to the right
    std::vector<JointPose> pose = restOf(skeleton);
    CHECK(solveLookAt(skeleton, chain, look(side, 1.0f), pose) == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(angleBetween(forwardOf(skeleton, pose, 2), Vec3(0.0f, 0.0f, 1.0f)) ==
          doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(forwardOf(skeleton, pose, 2).x > 0.0f); // towards the target

    pose = restOf(skeleton);
    CHECK(solveLookAt(skeleton, chain, look(side, 1.0f, 0.5f), pose) == doctest::Approx(0.5f).epsilon(1e-4));
    pose = restOf(skeleton);
    CHECK(solveLookAt(skeleton, chain, look(side, 1.0f, 0.0f), pose) == 0.0f);
    CHECK(pose[2].rotation == Quat(1.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("look-at fades out for a target behind the head instead of flipping")
{
    const Skeleton skeleton = neck();
    const std::array<u32, 1> chain{2};
    std::vector<JointPose> pose = restOf(skeleton);
    CHECK(solveLookAt(skeleton, chain, look(Vec3(0.0f, 1.5f, -5.0f)), pose) == 0.0f);
    CHECK(finitePose(pose));
    // Just left and just right of straight behind: small turns of opposite sides, no jump to maxAngle.
    std::vector<JointPose> left = restOf(skeleton);
    std::vector<JointPose> right = restOf(skeleton);
    const f32 leftTurn = solveLookAt(skeleton, chain, look(Vec3(0.05f, 1.5f, -5.0f)), left);
    const f32 rightTurn = solveLookAt(skeleton, chain, look(Vec3(-0.05f, 1.5f, -5.0f)), right);
    CHECK(leftTurn < 0.05f);
    CHECK(rightTurn == doctest::Approx(leftTurn).epsilon(1e-4));
    CHECK(forwardOf(skeleton, left, 2).x > 0.0f);
    CHECK(forwardOf(skeleton, right, 2).x < 0.0f);
    // A target on the forward axis needs no turn.
    pose = restOf(skeleton);
    CHECK(solveLookAt(skeleton, chain, look(Vec3(0.0f, 1.5f, 3.0f)), pose) ==
          doctest::Approx(0.0f).epsilon(1e-6));
}

TEST_CASE("look-at shares the turn evenly along the chain")
{
    const Skeleton skeleton = neck();
    const std::array<u32, 3> chain{0, 1, 2};
    std::vector<JointPose> pose = restOf(skeleton);
    const f32 turned = solveLookAt(skeleton, chain, look(Vec3(5.0f, 1.5f, 5.0f)), pose);
    CHECK(turned > 0.5f);
    for (const u32 joint : chain)
    {
        CHECK(glm::angle(pose[joint].rotation) == doctest::Approx(turned / 3.0f).epsilon(1e-3));
    }
    // The whole turn reaches the head's forward axis.
    CHECK(angleBetween(forwardOf(skeleton, pose, 2), Vec3(0.0f, 0.0f, 1.0f)) ==
          doctest::Approx(turned).epsilon(1e-3));
}

TEST_CASE("look-at ignores degenerate input")
{
    const Skeleton skeleton = neck();
    const std::vector<JointPose> rest = restOf(skeleton);
    std::vector<JointPose> pose = rest;
    const std::array<u32, 1> head{2};
    CHECK(solveLookAt(skeleton, head, look(Vec3(0.0f, 1.5f, 0.0f)), pose) == 0.0f); // the target is the head
    LookAtSolve noAxis = look(Vec3(1.0f, 1.5f, 1.0f));
    noAxis.forward = Vec3(0.0f);
    CHECK(solveLookAt(skeleton, head, noAxis, pose) == 0.0f);
    CHECK(solveLookAt(skeleton, head, look(Vec3(std::numeric_limits<f32>::quiet_NaN())), pose) == 0.0f);
    CHECK(solveLookAt(skeleton, {}, look(Vec3(1.0f, 1.5f, 1.0f)), pose) == 0.0f);
    const std::array<u32, 1> outside{9};
    CHECK(solveLookAt(skeleton, outside, look(Vec3(1.0f, 1.5f, 1.0f)), pose) == 0.0f);
    const std::array<u32, 5> tooLong{0, 1, 2, 2, 2};
    CHECK(solveLookAt(skeleton, tooLong, look(Vec3(1.0f, 1.5f, 1.0f)), pose) == 0.0f);
    for (usize j = 0; j < rest.size(); ++j)
    {
        CHECK(pose[j].rotation == rest[j].rotation);
    }
}

TEST_CASE("the IK solvers do not allocate")
{
    const Skeleton legSkeleton = leg();
    const Skeleton neckSkeleton = neck();
    std::vector<JointPose> legPose = restOf(legSkeleton);
    std::vector<JointPose> neckPose = restOf(neckSkeleton);
    const std::array<u32, 2> chain{1, 2};
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        for (int i = 0; i < 100; ++i)
        {
            const f32 t = 0.01f * static_cast<f32>(i);
            static_cast<void>(solveTwoBoneIk(legSkeleton, 2, reach(Vec3(t, 0.5f + t, 0.2f)), legPose));
            static_cast<void>(
                solveLookAt(neckSkeleton, chain, look(Vec3(std::sin(t), 1.5f, 1.0f)), neckPose));
        }
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}

// Every camera stage chained as CameraWorld will run them (ADR 0064): two runs of the same inputs give the
// same poses bit for bit, and a run allocates nothing.

#include "TestSupport.hpp"

#include <illusion/camera/CameraAim.hpp>
#include <illusion/camera/CameraBlend.hpp>
#include <illusion/camera/CameraBody.hpp>
#include <illusion/camera/CameraNoise.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstring>
#include <optional>

using namespace illusion;

namespace
{

constexpr usize kSteps = 240;
constexpr f32 kDt = 1.0f / 60.0f;

// A floor plane at y = 0.5 below every cast that points down, otherwise nothing.
class FloorQuery final : public CameraCollisionQuery
{
public:
    std::optional<f32> sphereCast(Vec3 origin, Vec3 direction, f32 maxDistance, f32 radius,
                                  u32 /*layerMask*/) const noexcept override
    {
        if (!(direction.y < -1e-4f))
        {
            return std::nullopt;
        }
        const f32 distance = (0.5f + radius - origin.y) / direction.y;
        if (distance < 0.0f || distance > maxDistance)
        {
            return std::nullopt;
        }
        return distance;
    }
};

const std::array<CameraPathPoint, 3> kTrack{CameraPathPoint{Vec3(-5.0f, 2.0f, 4.0f), Vec3(4.0f, 0.0f, 0.0f)},
                                            CameraPathPoint{Vec3(0.0f, 3.0f, 6.0f), Vec3(5.0f, 0.5f, 0.0f)},
                                            CameraPathPoint{Vec3(5.0f, 2.0f, 4.0f), Vec3(4.0f, -0.5f, 0.0f)}};

struct Rig
{
    CameraFollowState follow;
    CameraOrbitState orbit;
    CameraSpringArmState arm;
    CameraDollyState dolly;
    CameraLookAtState lookAt;
    CameraComposerState composer;
    CameraNoiseState noise{.time = 0.0, .seed = 17};
};

void run(Rig& rig, std::array<CameraPose, kSteps>& poses)
{
    const FloorQuery floor;
    CameraFollowSettings follow;
    CameraOrbitSettings orbit;
    CameraSpringArmSettings arm;
    arm.pullInSpeed = 8.0f;
    CameraDollySettings dolly;
    dolly.mode = CameraDollyMode::NearestToTarget;
    const CameraLookAtSettings lookAt;
    const CameraComposerSettings composer;
    CameraNoiseSettings noise;
    noise.positionAmplitude = Vec3(0.02f);
    const CameraBlend blend{.style = CameraBlendStyle::Curve,
                            .duration = 2.0f,
                            .control1 = Vec2(0.3f, 0.0f),
                            .control2 = Vec2(0.2f, 1.0f)};
    for (usize i = 0; i < kSteps; ++i)
    {
        const f32 time = static_cast<f32>(i) * kDt;
        CameraTarget target;
        target.position = Vec3(4.0f * std::sin(time), 0.0f, 3.0f * std::cos(0.7f * time));
        target.rotation = glm::angleAxis(0.5f * time, Vec3(0.0f, 1.0f, 0.0f));
        arm.side = (i / 60) % 2 == 0 ? 1.0f : -1.0f;

        const CameraPlacement followed = stepCameraFollow(follow, rig.follow, target, kDt);
        const CameraPlacement orbited = stepCameraOrbit(
            orbit, rig.orbit, CameraOrbitInput{.target = target, .yaw = 0.02f, .pitch = -0.01f, .zoom = 0.0f},
            kDt);
        const Quat armRotation = target.rotation * glm::angleAxis(0.6f, Vec3(1.0f, 0.0f, 0.0f));
        const CameraPlacement armed = stepCameraSpringArm(
            arm, rig.arm,
            CameraSpringArmInput{.target = target, .rotation = armRotation, .collision = &floor}, kDt);
        const CameraPlacement railed =
            stepCameraDolly(dolly, rig.dolly, CameraDollyInput{.path = kTrack, .target = target}, kDt);

        CameraPose from;
        from.position = 0.5f * (followed.position + orbited.position);
        from.rotation = stepCameraLookAt(lookAt, rig.lookAt,
                                         CameraAimInput{.cameraPosition = from.position,
                                                        .bodyRotation = followed.rotation,
                                                        .targetPosition = target.position},
                                         kDt);
        CameraPose to;
        to.position = 0.5f * (armed.position + railed.position);
        to.rotation = stepCameraComposer(composer, rig.composer,
                                         CameraComposerInput{.aim = {.cameraPosition = to.position,
                                                                     .bodyRotation = armed.rotation,
                                                                     .targetPosition = target.position},
                                                             .fovY = 1.0f,
                                                             .aspect = 1.7777778f},
                                         kDt);
        to.fovY = 0.8f;
        const CameraPose blended = blendCameraPoses(from, to, cameraBlendWeight(blend, time));
        poses[i] = applyCameraNoise(blended, stepCameraNoise(noise, rig.noise, kDt));
    }
}

} // namespace

TEST_CASE("Chained camera stages repeat bit for bit from the same states and inputs")
{
    Rig first;
    Rig second;
    std::array<CameraPose, kSteps> a{};
    std::array<CameraPose, kSteps> b{};
    run(first, a);
    run(second, b);
    CHECK(std::memcmp(a.data(), b.data(), sizeof(a)) == 0);
    for (const CameraPose& pose : a)
    {
        REQUIRE(std::isfinite(pose.position.x));
        REQUIRE(std::isfinite(pose.rotation.w));
    }
    // The spring arm met the floor at some point, and the blend moved the pose.
    CHECK(first.arm.length < std::sqrt(9.25f));
    CHECK(a.front().fovY != a.back().fovY);
}

TEST_CASE("Chained camera stages allocate nothing")
{
    Rig rig;
    std::array<CameraPose, kSteps> poses{};
    usize allocations = 0;
    {
        const test::ScopedAllocationCounter counter;
        run(rig, poses);
        allocations = counter.count();
    }
    CHECK(allocations == 0);
}

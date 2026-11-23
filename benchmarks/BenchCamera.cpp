#include "Budget.hpp"

#include <illusion/camera/CameraAim.hpp>
#include <illusion/camera/CameraBlend.hpp>
#include <illusion/camera/CameraBody.hpp>
#include <illusion/camera/CameraNoise.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kCameras = 100;

// A floor at y = 0.5: one plane test per cast, standing in for a physics sphere cast.
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

struct CameraStates
{
    CameraFollowState follow;
    CameraOrbitState orbit;
    CameraSpringArmState arm;
    CameraDollyState dolly;
    CameraLookAtState lookAt;
    CameraComposerState composer;
    CameraNoiseState noise;
};

const std::array<CameraPathPoint, 4> kTrack{CameraPathPoint{Vec3(-6.0f, 2.0f, 4.0f), Vec3(4.0f, 0.0f, 0.0f)},
                                            CameraPathPoint{Vec3(-2.0f, 3.0f, 6.0f), Vec3(4.0f, 0.5f, 0.0f)},
                                            CameraPathPoint{Vec3(2.0f, 3.0f, 6.0f), Vec3(4.0f, -0.5f, 0.0f)},
                                            CameraPathPoint{Vec3(6.0f, 2.0f, 4.0f), Vec3(4.0f, 0.0f, 0.0f)}};

} // namespace

// Budget: ns per update of 100 virtual cameras, each running one body stage (follow, orbit, spring arm with a
// cast, or dolly nearest to its target, in turn), one aim stage (look-at or composer), noise and a blend;
// about 8x a desktop result (51 us); see Budget.hpp.
ILL_BENCHMARK("camera/stages-100", 400'000.0)
{
    const FloorQuery floor;
    const CameraFollowSettings follow;
    const CameraOrbitSettings orbit;
    const CameraSpringArmSettings arm;
    CameraDollySettings dolly;
    dolly.mode = CameraDollyMode::NearestToTarget;
    const CameraLookAtSettings lookAt;
    const CameraComposerSettings composer;
    CameraNoiseSettings noise;
    noise.positionAmplitude = Vec3(0.02f);
    const CameraBlend blend;
    std::vector<CameraStates> states(kCameras);
    for (u32 i = 0; i < kCameras; ++i)
    {
        states[i].noise.seed = i;
    }
    std::vector<CameraPose> poses(kCameras);
    constexpr f32 kDt = 1.0f / 60.0f;
    f32 time = 0.0f;
    bench.run("update 100 virtual cameras", [&] {
        time += kDt;
        for (u32 i = 0; i < kCameras; ++i)
        {
            CameraStates& state = states[i];
            const f32 phase = time + 0.1f * static_cast<f32>(i);
            CameraTarget target;
            target.position = Vec3(4.0f * std::sin(phase), 0.0f, 3.0f * std::cos(0.7f * phase));
            target.rotation = glm::angleAxis(0.5f * phase, Vec3(0.0f, 1.0f, 0.0f));
            CameraPlacement body;
            switch (i % 4)
            {
            case 0:
                body = stepCameraFollow(follow, state.follow, target, kDt);
                break;
            case 1:
                body = stepCameraOrbit(
                    orbit, state.orbit,
                    CameraOrbitInput{.target = target, .yaw = 0.02f, .pitch = 0.0f, .zoom = 0.0f}, kDt);
                break;
            case 2:
                body = stepCameraSpringArm(
                    arm, state.arm,
                    CameraSpringArmInput{.target = target,
                                         .rotation =
                                             target.rotation * glm::angleAxis(0.6f, Vec3(1.0f, 0.0f, 0.0f)),
                                         .collision = &floor},
                    kDt);
                break;
            default:
                body = stepCameraDolly(dolly, state.dolly, CameraDollyInput{.path = kTrack, .target = target},
                                       kDt);
                break;
            }
            CameraPose pose;
            pose.position = body.position;
            const CameraAimInput aim{.cameraPosition = body.position,
                                     .bodyRotation = body.rotation,
                                     .targetPosition = target.position};
            pose.rotation =
                i % 2 == 0
                    ? stepCameraLookAt(lookAt, state.lookAt, aim, kDt)
                    : stepCameraComposer(composer, state.composer,
                                         CameraComposerInput{.aim = aim, .fovY = 1.0f, .aspect = 1.7777778f},
                                         kDt);
            pose = applyCameraNoise(pose, stepCameraNoise(noise, state.noise, kDt));
            poses[i] = blendCameraPoses(poses[i], pose, cameraBlendWeight(blend, 0.5f));
        }
        ankerl::nanobench::doNotOptimizeAway(poses.back());
    });
}

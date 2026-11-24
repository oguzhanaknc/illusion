// The body stages of virtual cameras (ADR 0064) without a World: follow (target and world space, per-axis
// damping), orbit (angles, limits, wrapping, zoom), spring arm (collision through a fake query, pull-in,
// recovery, shoulder swap) and dolly (Hermite paths, wrapping, nearest point, damping), against hand-computed
// values.

#include <illusion/camera/CameraBody.hpp>
#include <illusion/camera/CameraDamping.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <optional>

using namespace illusion;

namespace
{

constexpr f32 kPi = 3.14159265f;
const f32 kE = std::exp(1.0f);

Vec3 forwardOf(const Quat& rotation)
{
    return rotation * Vec3(0.0f, 0.0f, -1.0f);
}

void checkNear(Vec3 actual, Vec3 expected, f64 epsilon = 1e-5)
{
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(epsilon));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(epsilon));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(epsilon));
}

// A wall at a fixed distance along any cast, or none; remembers the last cast.
class WallQuery final : public CameraCollisionQuery
{
public:
    std::optional<f32> wall;
    mutable Vec3 origin{0.0f};
    mutable Vec3 direction{0.0f};
    mutable f32 maxDistance = 0.0f;
    mutable f32 radius = 0.0f;
    mutable u32 layerMask = 0;
    mutable u32 casts = 0;

    std::optional<f32> sphereCast(Vec3 castOrigin, Vec3 castDirection, f32 castMaxDistance, f32 castRadius,
                                  u32 castLayerMask) const noexcept override
    {
        origin = castOrigin;
        direction = castDirection;
        maxDistance = castMaxDistance;
        radius = castRadius;
        layerMask = castLayerMask;
        ++casts;
        if (wall && *wall <= castMaxDistance)
        {
            return wall;
        }
        return std::nullopt;
    }
};

const std::array<CameraPathPoint, 4> kSquare{
    CameraPathPoint{Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f)}, CameraPathPoint{Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f)},
    CameraPathPoint{Vec3(1.0f, 0.0f, 1.0f), Vec3(0.0f)}, CameraPathPoint{Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f)}};

// Three points on the x axis whose tangents equal the chords, so that the path position is the x coordinate.
const std::array<CameraPathPoint, 3> kLine{CameraPathPoint{Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)},
                                           CameraPathPoint{Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)},
                                           CameraPathPoint{Vec3(2.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)}};

} // namespace

TEST_CASE("A follow camera in target space snaps to the offset turned by the target on its first step")
{
    CameraFollowSettings settings;
    settings.offset = Vec3(0.0f, 2.0f, 5.0f);
    CameraFollowState state;
    CameraTarget target;
    target.position = Vec3(1.0f, 0.0f, 0.0f);
    target.rotation = glm::angleAxis(0.5f * kPi, Vec3(0.0f, 1.0f, 0.0f));
    const CameraPlacement placement = stepCameraFollow(settings, state, target, 0.1f);
    checkNear(placement.position, Vec3(6.0f, 2.0f, 0.0f));
    CHECK(std::abs(glm::dot(placement.rotation, target.rotation)) == doctest::Approx(1.0f));
    CHECK(state.initialised);
}

TEST_CASE("A follow camera in world space damps each axis as a critically damped spring")
{
    CameraFollowSettings settings;
    settings.offset = Vec3(0.0f, 1.0f, 0.0f);
    settings.space = CameraFollowSpace::World;
    settings.damping = Vec3(1.0f, 0.0f, 0.5f);
    CameraFollowState state;
    static_cast<void>(stepCameraFollow(settings, state, CameraTarget{}, 0.1f));
    CameraTarget moved;
    moved.position = Vec3(1.0f, 0.0f, 1.0f);
    moved.rotation = glm::angleAxis(1.0f, Vec3(0.0f, 1.0f, 0.0f));
    const CameraPlacement placement = stepCameraFollow(settings, state, moved, 0.5f);
    // x: offset -1 with w = 2 over 0.5 s leaves -2 / e; z: w = 4 leaves -3 / e^2; y has no damping.
    CHECK(placement.position.x == doctest::Approx(1.0f - 2.0f / kE));
    CHECK(placement.position.y == doctest::Approx(1.0f));
    CHECK(placement.position.z == doctest::Approx(1.0f - 3.0f / (kE * kE)));
    CHECK(std::abs(glm::dot(placement.rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f))) == doctest::Approx(1.0f));
}

TEST_CASE("An orbit camera sits behind the pivot at its yaw and distance and looks at it")
{
    CameraOrbitSettings settings;
    settings.yaw = 0.5f * kPi;
    settings.pitch = 0.0f;
    CameraOrbitState state;
    CameraOrbitInput input;
    input.target.position = Vec3(1.0f, 0.0f, 0.0f);
    const CameraPlacement placement = stepCameraOrbit(settings, state, input, 0.1f);
    checkNear(placement.position, Vec3(6.0f, 1.5f, 0.0f));
    checkNear(forwardOf(placement.rotation), Vec3(-1.0f, 0.0f, 0.0f));
}

TEST_CASE("An orbit camera clamps pitch and distance and wraps yaw")
{
    CameraOrbitSettings settings;
    settings.pitch = 0.0f;
    settings.damping = 0.0f;
    CameraOrbitState state;
    CameraOrbitInput input;
    static_cast<void>(stepCameraOrbit(settings, state, input, 0.1f));
    input.pitch = 10.0f;
    input.yaw = 2.0f * kPi + 0.5f;
    input.zoom = -100.0f;
    const CameraPlacement placement = stepCameraOrbit(settings, state, input, 0.1f);
    CHECK(state.pitch == doctest::Approx(settings.maxPitch));
    CHECK(state.smoothPitch == doctest::Approx(settings.maxPitch));
    CHECK(state.yaw == doctest::Approx(0.5f));
    CHECK(state.distance == doctest::Approx(settings.minDistance));
    // Looking up puts the camera below the pivot.
    CHECK(placement.position.y == doctest::Approx(1.5f - std::sin(settings.maxPitch)));

    settings.limitYaw = true;
    settings.minYaw = -0.2f;
    settings.maxYaw = 0.2f;
    input = {};
    input.yaw = 1.0f;
    static_cast<void>(stepCameraOrbit(settings, state, input, 0.1f));
    CHECK(state.yaw == doctest::Approx(0.2f));
}

TEST_CASE("An orbit camera damps its yaw the short way across the wrap")
{
    CameraOrbitSettings settings;
    settings.yaw = 3.0f;
    settings.damping = 0.1f;
    CameraOrbitState state;
    CameraOrbitInput input;
    static_cast<void>(stepCameraOrbit(settings, state, input, 0.1f));
    input.yaw = 0.283f;
    static_cast<void>(stepCameraOrbit(settings, state, input, 0.05f));
    const f32 moved = wrapCameraAngle(state.smoothYaw - 3.0f);
    CHECK(moved > 0.0f);
    CHECK(moved < 0.283f);
}

TEST_CASE("A spring arm without collision ends at the shoulder offset plus the arm length")
{
    CameraSpringArmSettings settings;
    CameraSpringArmState state;
    CameraSpringArmInput input;
    input.target.position = Vec3(2.0f, 0.0f, 0.0f);
    const CameraPlacement placement = stepCameraSpringArm(settings, state, input, 0.1f);
    checkNear(placement.position, Vec3(2.5f, 1.6f, 3.0f));
    CHECK(state.length == doctest::Approx(std::sqrt(9.25f)));

    input.rotation = glm::angleAxis(0.5f * kPi, Vec3(0.0f, 1.0f, 0.0f));
    checkNear(stepCameraSpringArm(settings, state, input, 0.1f).position, Vec3(5.0f, 1.6f, -0.5f));
}

TEST_CASE("A spring arm pulls in at once to a wall and recovers at its speed")
{
    CameraSpringArmSettings settings;
    settings.collisionRadius = 0.3f;
    settings.collisionLayers = 0b101u;
    CameraSpringArmState state;
    WallQuery wall;
    wall.wall = 1.0f;
    CameraSpringArmInput input;
    input.collision = &wall;
    const CameraPlacement first = stepCameraSpringArm(settings, state, input, 0.1f);
    const f32 full = std::sqrt(9.25f);
    CHECK(wall.casts == 1);
    checkNear(wall.origin, Vec3(0.0f, 1.6f, 0.0f));
    checkNear(wall.direction, Vec3(0.5f, 0.0f, 3.0f) / full);
    CHECK(wall.maxDistance == doctest::Approx(full));
    CHECK(wall.radius == doctest::Approx(0.3f));
    CHECK(wall.layerMask == 0b101u);
    CHECK(state.length == doctest::Approx(1.0f));
    checkNear(first.position, Vec3(0.0f, 1.6f, 0.0f) + Vec3(0.5f, 0.0f, 3.0f) / full);

    wall.wall = std::nullopt;
    static_cast<void>(stepCameraSpringArm(settings, state, input, 0.1f));
    CHECK(state.length == doctest::Approx(1.4f));
    for (int i = 0; i < 20; ++i)
    {
        static_cast<void>(stepCameraSpringArm(settings, state, input, 0.1f));
    }
    CHECK(state.length == doctest::Approx(full));

    settings.pullInSpeed = 2.0f;
    wall.wall = 1.0f;
    static_cast<void>(stepCameraSpringArm(settings, state, input, 0.25f));
    CHECK(state.length == doctest::Approx(full - 0.5f));
}

TEST_CASE("A spring arm swaps shoulders as a critically damped spring")
{
    CameraSpringArmSettings settings;
    settings.sideDamping = 1.0f;
    CameraSpringArmState state;
    const CameraSpringArmInput input;
    static_cast<void>(stepCameraSpringArm(settings, state, input, 0.1f));
    CHECK(state.side == 1.0f);
    settings.side = -1.0f;
    static_cast<void>(stepCameraSpringArm(settings, state, input, 0.5f));
    CHECK(state.side == doctest::Approx(-1.0f + 4.0f / kE));
    settings.sideDamping = 0.0f;
    const CameraPlacement placement = stepCameraSpringArm(settings, state, input, 0.1f);
    CHECK(state.side == -1.0f);
    CHECK(placement.position.x == doctest::Approx(-0.5f));
}

TEST_CASE("Dolly paths are cubic Hermite curves, clamped when open and wrapped when closed")
{
    const std::span<const CameraPathPoint> square(kSquare);
    CHECK(cameraPathEnd(square, false) == 3.0f);
    CHECK(cameraPathEnd(square, true) == 4.0f);
    CHECK(cameraPathEnd({}, true) == 0.0f);
    // With zero tangents a segment eases: h01(0.25) = 3 / 16 - 2 / 64.
    CHECK(cameraPathPosition(square, false, 0.25f).x == doctest::Approx(0.15625f));
    CHECK(cameraPathTangent(square, false, 0.5f).x == doctest::Approx(1.5f));
    checkNear(cameraPathPosition(square, false, 7.0f), Vec3(0.0f, 0.0f, 1.0f));
    checkNear(cameraPathPosition(square, false, -1.0f), Vec3(0.0f));
    checkNear(cameraPathPosition(square, true, 3.5f), Vec3(0.0f, 0.0f, 0.5f));
    checkNear(cameraPathPosition(square, true, 4.25f), cameraPathPosition(square, true, 0.25f));
    checkNear(cameraPathPosition(square, true, -0.75f), cameraPathPosition(square, true, 3.25f));
    checkNear(cameraPathPosition(kLine, false, 1.3f), Vec3(1.3f, 0.0f, 0.0f));
}

TEST_CASE("nearestCameraPathPosition finds the closest point of the path")
{
    CHECK(nearestCameraPathPosition(kLine, false, Vec3(1.3f, 2.0f, 0.0f)) ==
          doctest::Approx(1.3f).epsilon(1e-3));
    CHECK(nearestCameraPathPosition(kLine, false, Vec3(-5.0f, 0.0f, 0.0f)) == 0.0f);
    CHECK(nearestCameraPathPosition(kLine, false, Vec3(9.0f, 0.0f, 0.0f)) == doctest::Approx(2.0f));
    CHECK(nearestCameraPathPosition(kSquare, true, Vec3(-1.0f, 0.0f, 0.5f)) ==
          doctest::Approx(3.5f).epsilon(1e-3));
    CHECK(nearestCameraPathPosition({}, false, Vec3(1.0f)) == 0.0f);
}

TEST_CASE("A dolly camera damps its path position and faces along the path")
{
    CameraDollySettings settings;
    CameraDollyState state;
    CameraDollyInput input;
    input.path = kLine;
    static_cast<void>(stepCameraDolly(settings, state, input, 0.1f));
    CHECK(state.position == 0.0f);
    settings.position = 1.0f;
    settings.damping = 1.0f;
    const CameraPlacement placement = stepCameraDolly(settings, state, input, 0.5f);
    CHECK(state.position == doctest::Approx(1.0f - 2.0f / kE));
    CHECK(placement.position.x == doctest::Approx(1.0f - 2.0f / kE));
    checkNear(forwardOf(placement.rotation), Vec3(1.0f, 0.0f, 0.0f));
}

TEST_CASE("A dolly camera on a closed path moves the short way and follows the target in nearest mode")
{
    CameraDollySettings settings;
    settings.closed = true;
    settings.position = 3.5f;
    settings.damping = 0.1f;
    CameraDollyState state;
    CameraDollyInput input;
    input.path = kSquare;
    static_cast<void>(stepCameraDolly(settings, state, input, 0.1f));
    settings.position = 0.5f;
    static_cast<void>(stepCameraDolly(settings, state, input, 0.05f));
    const f32 travelled = std::fmod(state.position - 3.5f + 4.0f, 4.0f);
    CHECK(travelled > 0.0f);
    CHECK(travelled < 1.0f);

    CameraDollySettings nearest;
    nearest.mode = CameraDollyMode::NearestToTarget;
    nearest.damping = 0.0f;
    CameraDollyState nearestState;
    CameraDollyInput line;
    line.path = kLine;
    line.target.position = Vec3(1.3f, 2.0f, 0.0f);
    static_cast<void>(stepCameraDolly(nearest, nearestState, line, 0.1f));
    line.target.position = Vec3(0.4f, 2.0f, 0.0f);
    const CameraPlacement placement = stepCameraDolly(nearest, nearestState, line, 0.1f);
    CHECK(placement.position.x == doctest::Approx(0.4f).epsilon(1e-3));
}

TEST_CASE("A dolly camera without a path stays at the target")
{
    CameraDollyState state;
    CameraDollyInput input;
    input.target.position = Vec3(3.0f, 4.0f, 5.0f);
    checkNear(stepCameraDolly(CameraDollySettings{}, state, input, 0.1f).position, Vec3(3.0f, 4.0f, 5.0f));
}

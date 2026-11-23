// The aim stages of virtual cameras (ADR 0064) without a World: look-at with damping, the screen projection,
// and the composer's placement, dead zone, soft zone and hard limit against hand-computed screen positions.

#include <illusion/camera/CameraAim.hpp>
#include <illusion/camera/CameraDamping.hpp>

#include <doctest/doctest.h>

#include <cmath>

using namespace illusion;

namespace
{

constexpr f32 kPi = 3.14159265f;
constexpr f32 kRightAngle = 0.5f * kPi;

Vec3 forwardOf(const Quat& rotation)
{
    return rotation * Vec3(0.0f, 0.0f, -1.0f);
}

void checkNear(Vec3 actual, Vec3 expected)
{
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-5));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(1e-5));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-5));
}

CameraAimInput aimAt(Vec3 target)
{
    CameraAimInput input;
    input.targetPosition = target;
    return input;
}

// A 90 degree square lens, so that a point (x, y, -1) appears at screen position (x, y).
CameraComposerInput composeAt(Vec3 target)
{
    CameraComposerInput input;
    input.aim = aimAt(target);
    input.fovY = kRightAngle;
    input.aspect = 1.0f;
    return input;
}

Vec2 screenOf(const Quat& rotation, Vec3 target)
{
    const std::optional<Vec2> screen = cameraScreenPosition(Vec3(0.0f), rotation, kRightAngle, 1.0f, target);
    REQUIRE(screen);
    return *screen;
}

CameraComposerSettings zones()
{
    CameraComposerSettings settings;
    settings.deadZone = Vec2(0.1f);
    settings.softZone = Vec2(0.5f);
    settings.damping = Vec2(0.5f);
    return settings;
}

} // namespace

TEST_CASE("A look-at camera faces the target on its first step and then turns damped")
{
    const CameraLookAtSettings settings{.damping = 1.0f};
    CameraLookAtState state;
    checkNear(forwardOf(stepCameraLookAt(settings, state, aimAt(Vec3(10.0f, 0.0f, 0.0f)), 0.1f)),
              Vec3(1.0f, 0.0f, 0.0f));

    CameraLookAtState damped;
    static_cast<void>(stepCameraLookAt(settings, damped, aimAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f));
    // dt = T ln 2 covers half of the 90 degree turn.
    const Quat half = stepCameraLookAt(settings, damped, aimAt(Vec3(10.0f, 0.0f, 0.0f)), std::log(2.0f));
    checkNear(forwardOf(half), Vec3(std::sqrt(0.5f), 0.0f, -std::sqrt(0.5f)));
}

TEST_CASE("A look-at camera keeps its rotation when the target gives no direction")
{
    CameraLookAtState state;
    CameraAimInput input = aimAt(Vec3(0.0f));
    input.bodyRotation = glm::angleAxis(0.3f, Vec3(0.0f, 1.0f, 0.0f));
    const Quat first = stepCameraLookAt(CameraLookAtSettings{}, state, input, 0.1f);
    CHECK(std::abs(glm::dot(first, input.bodyRotation)) == doctest::Approx(1.0f));
    input.targetPosition = Vec3(0.0f, 5.0f, 0.0f);
    const Quat above = stepCameraLookAt(CameraLookAtSettings{}, state, input, 0.1f);
    CHECK(std::abs(glm::dot(above, first)) == doctest::Approx(1.0f));
}

TEST_CASE("cameraScreenPosition projects points in front of the camera through the lens")
{
    CHECK(screenOf(Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(5.0f, -2.0f, -10.0f)).x == doctest::Approx(0.5f));
    CHECK(screenOf(Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(5.0f, -2.0f, -10.0f)).y == doctest::Approx(-0.2f));
    const std::optional<Vec2> wide = cameraScreenPosition(Vec3(0.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                          kRightAngle, 2.0f, Vec3(5.0f, 0.0f, -10.0f));
    REQUIRE(wide);
    CHECK(wide->x == doctest::Approx(0.25f));
    CHECK_FALSE(cameraScreenPosition(Vec3(0.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), kRightAngle, 1.0f,
                                     Vec3(0.0f, 0.0f, 10.0f)));
}

TEST_CASE("A composer places the target at its screen position on the first step, without roll")
{
    CameraComposerSettings settings = zones();
    settings.screenPosition = Vec2(0.5f, 0.0f);
    CameraComposerState state;
    const Quat yawed = stepCameraComposer(settings, state, composeAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f);
    CHECK(std::abs(glm::dot(yawed, glm::angleAxis(std::atan(0.5f), Vec3(0.0f, 1.0f, 0.0f)))) ==
          doctest::Approx(1.0f));

    settings.screenPosition = Vec2(0.25f, -0.4f);
    CameraComposerState other;
    const Vec3 target(3.0f, 5.0f, -10.0f);
    const Quat placed = stepCameraComposer(settings, other, composeAt(target), 0.1f);
    const Vec2 screen = screenOf(placed, target);
    CHECK(screen.x == doctest::Approx(0.25f).epsilon(1e-4));
    CHECK(screen.y == doctest::Approx(-0.4f).epsilon(1e-4));
    CHECK((placed * Vec3(1.0f, 0.0f, 0.0f)).y == doctest::Approx(0.0f).epsilon(1e-6));
}

TEST_CASE("A composer holds still while the target stays in the dead zone")
{
    const CameraComposerSettings settings = zones();
    CameraComposerState state;
    const Quat start = stepCameraComposer(settings, state, composeAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f);
    const Quat held = stepCameraComposer(settings, state, composeAt(Vec3(0.5f, -0.8f, -10.0f)), 0.1f);
    CHECK(held.w == start.w);
    CHECK(held.x == start.x);
    CHECK(held.y == start.y);
    CHECK(held.z == start.z);
}

TEST_CASE("A composer turns damped towards the dead zone while the target is in the soft zone")
{
    const CameraComposerSettings settings = zones();
    const f32 factor = dampFactor(0.5f, 0.1f);
    CameraComposerState horizontal;
    static_cast<void>(stepCameraComposer(settings, horizontal, composeAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f));
    const Vec3 right(3.0f, 0.0f, -10.0f);
    const Vec2 screen = screenOf(stepCameraComposer(settings, horizontal, composeAt(right), 0.1f), right);
    CHECK(screen.x == doctest::Approx(0.3f - 0.2f * factor).epsilon(1e-4));
    CHECK(screen.y == doctest::Approx(0.0f).epsilon(1e-5));

    CameraComposerState vertical;
    static_cast<void>(stepCameraComposer(settings, vertical, composeAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f));
    const Vec3 up(0.0f, 3.0f, -10.0f);
    const Vec2 raised = screenOf(stepCameraComposer(settings, vertical, composeAt(up), 0.1f), up);
    CHECK(raised.x == doctest::Approx(0.0f).epsilon(1e-5));
    CHECK(raised.y == doctest::Approx(0.3f - 0.2f * factor).epsilon(1e-4));
}

TEST_CASE("A composer brings a target beyond the soft zone to its edge at once")
{
    const CameraComposerSettings settings = zones();
    const f32 factor = dampFactor(0.5f, 0.1f);
    CameraComposerState state;
    static_cast<void>(stepCameraComposer(settings, state, composeAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f));
    const Vec3 left(-9.0f, 0.0f, -10.0f);
    const Vec2 screen = screenOf(stepCameraComposer(settings, state, composeAt(left), 0.1f), left);
    CHECK(screen.x == doctest::Approx(-0.5f + 0.4f * factor).epsilon(1e-4));
    CHECK(screen.x <= -0.1f);
}

TEST_CASE("A composer snaps a target behind the camera to the screen position")
{
    const CameraComposerSettings settings = zones();
    CameraComposerState state;
    static_cast<void>(stepCameraComposer(settings, state, composeAt(Vec3(0.0f, 0.0f, -10.0f)), 0.1f));
    checkNear(forwardOf(stepCameraComposer(settings, state, composeAt(Vec3(0.0f, 0.0f, 10.0f)), 0.1f)),
              Vec3(0.0f, 0.0f, 1.0f));
}

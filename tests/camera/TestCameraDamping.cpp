// The smoothing helpers of the camera stages (ADR 0064) against hand-computed values: exponential damping,
// the critically damped spring, rotations along the shortest arc and angle wrapping.

#include <illusion/camera/CameraDamping.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace illusion;

namespace
{

constexpr f32 kPi = 3.14159265f;

f32 angleBetween(const Quat& a, const Quat& b)
{
    return 2.0f * std::acos(std::min(std::abs(glm::dot(a, b)), 1.0f));
}

} // namespace

TEST_CASE("dampFactor covers 1 - exp(-dt / smoothTime) of the distance")
{
    CHECK(dampFactor(1.0f, 0.5f) == doctest::Approx(1.0 - std::exp(-0.5)));
    CHECK(dampFactor(0.25f, 0.25f) == doctest::Approx(1.0 - std::exp(-1.0)));
    CHECK(dampFactor(0.0f, 0.1f) == 1.0f);
    CHECK(dampFactor(-1.0f, 0.1f) == 1.0f);
    CHECK(dampFactor(1.0f, 0.0f) == 0.0f);
}

TEST_CASE("smoothDamp follows the exact critically damped solution")
{
    // x(t) = (x0 + (v0 + w x0) t) e^(-w t) with x0 = 1, v0 = 0, w = 2 / 1, t = 0.5: x = 2 / e, v = -2 / e.
    f32 value = 3.0f;
    f32 velocity = 0.0f;
    smoothDamp(value, velocity, 2.0f, 1.0f, 0.5f);
    CHECK(value == doctest::Approx(2.0 + 2.0 / std::exp(1.0)));
    CHECK(velocity == doctest::Approx(-2.0 / std::exp(1.0)));

    // x0 = 0, v0 = 1, w = 4, t = 0.25: x = 0.25 e^-1, v = (1 - 4 0.25) e^-1 = 0.
    value = 0.0f;
    velocity = 1.0f;
    smoothDamp(value, velocity, 0.0f, 0.5f, 0.25f);
    CHECK(value == doctest::Approx(0.25 / std::exp(1.0)));
    CHECK(velocity == doctest::Approx(0.0).epsilon(1e-6));
}

TEST_CASE("smoothDamp gives the same result for one step or two half steps")
{
    f32 whole = 5.0f;
    f32 wholeVelocity = -2.0f;
    smoothDamp(whole, wholeVelocity, 1.0f, 0.3f, 0.2f);
    f32 halves = 5.0f;
    f32 halvesVelocity = -2.0f;
    smoothDamp(halves, halvesVelocity, 1.0f, 0.3f, 0.1f);
    smoothDamp(halves, halvesVelocity, 1.0f, 0.3f, 0.1f);
    CHECK(halves == doctest::Approx(whole));
    CHECK(halvesVelocity == doctest::Approx(wholeVelocity));
}

TEST_CASE("smoothDamp from rest approaches the target without overshooting")
{
    f32 value = 10.0f;
    f32 velocity = 0.0f;
    f32 previous = value;
    for (int i = 0; i < 200; ++i)
    {
        smoothDamp(value, velocity, 0.0f, 0.2f, 1.0f / 60.0f);
        CHECK(value <= previous);
        CHECK(value >= 0.0f);
        previous = value;
    }
    CHECK(value < 0.01f);
}

TEST_CASE("smoothDamp snaps with a zero smooth time and holds still with a zero step")
{
    f32 value = 4.0f;
    f32 velocity = 3.0f;
    smoothDamp(value, velocity, 4.5f, 1.0f, 0.0f);
    CHECK(value == 4.0f);
    CHECK(velocity == 3.0f);
    smoothDamp(value, velocity, 1.0f, 0.0f, 0.1f);
    CHECK(value == 1.0f);
    CHECK(velocity == 0.0f);
}

TEST_CASE("smoothDamp on vectors damps each axis with its own smooth time")
{
    Vec3 value(1.0f, 1.0f, 1.0f);
    Vec3 velocity(0.0f);
    smoothDamp(value, velocity, Vec3(0.0f), Vec3(1.0f, 0.0f, 0.5f), 0.5f);
    CHECK(value.x == doctest::Approx(2.0 / std::exp(1.0)));
    CHECK(value.y == 0.0f);
    CHECK(value.z == doctest::Approx(3.0 / std::exp(2.0)));
}

TEST_CASE("dampRotation turns along the shortest arc by the damping factor")
{
    const Quat start(1.0f, 0.0f, 0.0f, 0.0f);
    const Quat quarter = glm::angleAxis(0.5f * kPi, Vec3(0.0f, 1.0f, 0.0f));
    // dt = T ln 2 gives a factor of one half: 45 of the 90 degrees.
    const Quat half = dampRotation(start, quarter, 1.0f, std::log(2.0f));
    CHECK(angleBetween(half, glm::angleAxis(0.25f * kPi, Vec3(0.0f, 1.0f, 0.0f))) < 1e-3f);
    const Quat negated = dampRotation(start, -quarter, 1.0f, std::log(2.0f));
    CHECK(angleBetween(negated, half) < 1e-3f);
    CHECK(std::abs(glm::dot(dampRotation(start, quarter, 0.0f, 0.1f), quarter)) == doctest::Approx(1.0f));
}

TEST_CASE("wrapCameraAngle wraps angles from -pi up to but not including pi")
{
    CHECK(wrapCameraAngle(1.5f * kPi) == doctest::Approx(-0.5f * kPi));
    CHECK(wrapCameraAngle(-1.5f * kPi) == doctest::Approx(0.5f * kPi));
    CHECK(wrapCameraAngle(kPi) == doctest::Approx(-kPi));
    CHECK(wrapCameraAngle(-kPi) == doctest::Approx(-kPi));
    CHECK(wrapCameraAngle(0.25f) == doctest::Approx(0.25f));
    CHECK(wrapCameraAngle(4.0f * kPi + 0.25f) == doctest::Approx(0.25f));
}

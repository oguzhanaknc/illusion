// Camera blends (ADR 0064): the weight curves (cut, linear, ease in/out, cubic Bezier) against hand-computed
// values, and the blended pose (positions and lens linearly, rotations along the shortest arc).

#include <illusion/camera/CameraBlend.hpp>

#include <doctest/doctest.h>

#include <cmath>

using namespace illusion;

namespace
{

constexpr f32 kPi = 3.14159265f;

CameraBlend blendOf(CameraBlendStyle style, f32 duration)
{
    CameraBlend blend;
    blend.style = style;
    blend.duration = duration;
    return blend;
}

} // namespace

TEST_CASE("A cut or a blend without duration shows the new camera at once")
{
    CHECK(cameraBlendWeight(blendOf(CameraBlendStyle::Cut, 2.0f), 0.0f) == 1.0f);
    CHECK(cameraBlendWeight(blendOf(CameraBlendStyle::EaseInOut, 0.0f), 0.0f) == 1.0f);
    CHECK(cameraBlendWeight(blendOf(CameraBlendStyle::Linear, -1.0f), 0.5f) == 1.0f);
}

TEST_CASE("Linear and ease in/out weights follow their formulas and clamp between 0 and 1")
{
    const CameraBlend linear = blendOf(CameraBlendStyle::Linear, 2.0f);
    CHECK(cameraBlendWeight(linear, 0.5f) == doctest::Approx(0.25f));
    CHECK(cameraBlendWeight(linear, -1.0f) == 0.0f);
    CHECK(cameraBlendWeight(linear, 3.0f) == 1.0f);
    const CameraBlend ease = blendOf(CameraBlendStyle::EaseInOut, 1.0f);
    CHECK(cameraBlendWeight(ease, 0.25f) == doctest::Approx(0.15625f));
    CHECK(cameraBlendWeight(ease, 0.5f) == doctest::Approx(0.5f));
    CHECK(cameraBlendWeight(ease, 0.75f) == doctest::Approx(0.84375f));
    CHECK(cameraBlendWeight(ease, 1.0f) == 1.0f);
}

TEST_CASE("A curve blend evaluates the cubic Bezier as CSS cubic-bezier does")
{
    // CSS "ease" is cubic-bezier(0.25, 0.1, 0.25, 1); its value at x = 0.5 is 0.8024034 (solved numerically).
    CHECK(cubicBezierEase(Vec2(0.25f, 0.1f), Vec2(0.25f, 1.0f), 0.5f) ==
          doctest::Approx(0.8024034f).epsilon(1e-5));
    CHECK(cubicBezierEase(Vec2(1.0f / 3.0f), Vec2(2.0f / 3.0f), 0.3f) == doctest::Approx(0.3f).epsilon(1e-5));
    CameraBlend curve = blendOf(CameraBlendStyle::Curve, 1.0f);
    CHECK(cameraBlendWeight(curve, 0.25f) == doctest::Approx(0.1291619f).epsilon(1e-5));
    CHECK(cameraBlendWeight(curve, 0.0f) == doctest::Approx(0.0f));
    CHECK(cameraBlendWeight(curve, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("A curve blend with control points whose x lies outside 0 to 1 still rises monotonically")
{
    CameraBlend curve = blendOf(CameraBlendStyle::Curve, 1.0f);
    curve.control1 = Vec2(-2.0f, 0.0f);
    curve.control2 = Vec2(3.0f, 1.0f);
    f32 previous = 0.0f;
    for (int i = 0; i <= 20; ++i)
    {
        const f32 weight = cameraBlendWeight(curve, 0.05f * static_cast<f32>(i));
        CHECK(weight >= previous - 1e-6f);
        previous = weight;
    }
    CHECK(previous == doctest::Approx(1.0f));
}

TEST_CASE("blendCameraPoses mixes positions and lens linearly and rotations along the shortest arc")
{
    CameraPose from;
    from.position = Vec3(0.0f, 0.0f, 0.0f);
    from.fovY = 1.0f;
    from.nearPlane = 0.1f;
    from.dutch = 0.0f;
    CameraPose to;
    to.position = Vec3(4.0f, 2.0f, -8.0f);
    to.rotation = -glm::angleAxis(0.5f * kPi, Vec3(0.0f, 1.0f, 0.0f));
    to.fovY = 2.0f;
    to.nearPlane = 0.3f;
    to.dutch = 0.4f;

    const CameraPose quarter = blendCameraPoses(from, to, 0.25f);
    CHECK(quarter.position.x == doctest::Approx(1.0f));
    CHECK(quarter.position.y == doctest::Approx(0.5f));
    CHECK(quarter.position.z == doctest::Approx(-2.0f));
    CHECK(quarter.fovY == doctest::Approx(1.25f));
    CHECK(quarter.nearPlane == doctest::Approx(0.15f));
    CHECK(quarter.dutch == doctest::Approx(0.1f));
    const Quat expected = glm::angleAxis(0.125f * kPi, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(std::abs(glm::dot(quarter.rotation, expected)) == doctest::Approx(1.0f));

    const CameraPose start = blendCameraPoses(from, to, -1.0f);
    CHECK(start.position == from.position);
    CHECK(start.fovY == from.fovY);
    const CameraPose end = blendCameraPoses(from, to, 2.0f);
    CHECK(end.fovY == to.fovY);
    CHECK(std::abs(glm::dot(end.rotation, to.rotation)) == doctest::Approx(1.0f));
}

TEST_CASE("cameraViewRotation rolls the pose by its Dutch angle about the view axis")
{
    CameraPose pose;
    pose.dutch = 0.5f * kPi;
    const Vec3 right = cameraViewRotation(pose) * Vec3(1.0f, 0.0f, 0.0f);
    CHECK(right.x == doctest::Approx(0.0f).epsilon(1e-6));
    CHECK(right.y == doctest::Approx(1.0f));
    const Vec3 forward = cameraViewRotation(pose) * Vec3(0.0f, 0.0f, -1.0f);
    CHECK(forward.z == doctest::Approx(-1.0f));
}

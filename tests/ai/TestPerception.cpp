#include <illusion/ai/Perception.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

using namespace illusion;

namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;
const Vec3 kForward(0.0f, 0.0f, -1.0f);

AiNoise noise(Vec3 position, f32 radius)
{
    return AiNoise{.position = position, .radius = radius, .instigator = Entity{}, .team = 0};
}

} // namespace

TEST_CASE("a point is in the sight cone within its radius and half angle")
{
    const Vec3 eye(0.0f, 1.0f, 0.0f);
    const f32 angle = kPi * 0.5f;
    CHECK(inSightCone(eye, kForward, 10.0f, angle, eye + Vec3(0.0f, 0.0f, -5.0f)));
    CHECK(inSightCone(eye, kForward, 10.0f, angle, eye + Vec3(0.0f, 0.0f, -10.0f)));
    CHECK_FALSE(inSightCone(eye, kForward, 10.0f, angle, eye + Vec3(0.0f, 0.0f, -10.01f)));
    CHECK_FALSE(inSightCone(eye, kForward, 10.0f, angle, eye + Vec3(0.0f, 0.0f, 5.0f)));

    const f32 inside = kPi * 0.25f - 0.01f;
    const f32 outside = kPi * 0.25f + 0.01f;
    CHECK(inSightCone(eye, kForward, 10.0f, angle,
                      eye + 5.0f * Vec3(std::sin(inside), 0.0f, -std::cos(inside))));
    CHECK_FALSE(inSightCone(eye, kForward, 10.0f, angle,
                            eye + 5.0f * Vec3(std::sin(outside), 0.0f, -std::cos(outside))));
    CHECK(inSightCone(eye, kForward, 10.0f, angle,
                      eye + 5.0f * Vec3(0.0f, std::sin(inside), -std::cos(inside))));
}

TEST_CASE("the sight cone handles its edge cases")
{
    const Vec3 eye(0.0f);
    CHECK(inSightCone(eye, kForward, 10.0f, kPi * 0.5f, eye));
    CHECK(inSightCone(eye, kForward, 10.0f, 2.0f * kPi, Vec3(0.0f, 0.0f, 5.0f)));
    CHECK(inSightCone(eye, kForward, 10.0f, 7.0f, Vec3(0.0f, 0.0f, 5.0f)));
    CHECK_FALSE(inSightCone(eye, kForward, 10.0f, 0.0f, Vec3(0.0f, 0.0f, -5.0f) + Vec3(0.1f, 0.0f, 0.0f)));
    CHECK(inSightCone(eye, kForward, 10.0f, 0.0001f, Vec3(0.0f, 0.0f, -5.0f)));
    CHECK_FALSE(inSightCone(eye, kForward, 0.0f, kPi, Vec3(0.0f, 0.0f, -1.0f)));
    CHECK_FALSE(inSightCone(eye, kForward, 0.0f, kPi, eye));
    CHECK(inSightCone(eye, kForward, 10.0f, kPi, Vec3(5.0f, 0.0f, 0.0f)));
    CHECK_FALSE(inSightCone(eye, kForward, 10.0f, kPi, Vec3(5.0f, 0.0f, 0.1f)));
}

TEST_CASE("a noise is heard within its radius times the hearing")
{
    const Vec3 listener(0.0f);
    CHECK(hearsNoise(listener, 1.0f, noise(Vec3(10.0f, 0.0f, 0.0f), 10.0f)));
    CHECK_FALSE(hearsNoise(listener, 1.0f, noise(Vec3(10.5f, 0.0f, 0.0f), 10.0f)));
    CHECK(hearsNoise(listener, 2.0f, noise(Vec3(0.0f, 0.0f, 19.0f), 10.0f)));
    CHECK_FALSE(hearsNoise(listener, 0.5f, noise(Vec3(6.0f, 0.0f, 0.0f), 10.0f)));
    CHECK_FALSE(hearsNoise(listener, 0.0f, noise(listener, 10.0f)));
    CHECK_FALSE(hearsNoise(listener, -1.0f, noise(listener, 10.0f)));
    CHECK_FALSE(hearsNoise(listener, 1.0f, noise(listener, 0.0f)));
    CHECK(hearsNoise(listener, 1.0f, noise(listener, 0.001f)));
}

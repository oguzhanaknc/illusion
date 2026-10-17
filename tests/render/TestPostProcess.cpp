#include <illusion/render/PostProcess.hpp>

#include <doctest/doctest.h>

#include <cmath>

using namespace illusion;

TEST_CASE("the temporal jitter sequence covers the pixel and repeats every 8 frames")
{
    Vec2 sum(0.0f);
    for (u32 frame = 0; frame < 8; ++frame)
    {
        const Vec2 offset = temporalJitter(frame);
        CHECK(offset.x >= -0.5f);
        CHECK(offset.x < 0.5f);
        CHECK(offset.y >= -0.5f);
        CHECK(offset.y < 0.5f);
        CHECK(temporalJitter(frame + 8) == offset);
        sum += offset;
    }
    CHECK(std::abs(sum.x / 8.0f) < 0.07f);
    CHECK(std::abs(sum.y / 8.0f) < 0.07f);
    CHECK(temporalJitter(0) != temporalJitter(1));
}

TEST_CASE("jittering a projection moves the image by whole screen pixels")
{
    const Mat4 projection = perspectiveReversedZ(1.0f, 2.0f, 0.1f);
    const Vec2 size(200.0f, 100.0f);
    const Mat4 jittered = jitterProjection(projection, Vec2(0.5f, 0.25f), size);
    for (const Vec4 point : {Vec4(0.0f, 0.0f, -5.0f, 1.0f), Vec4(1.0f, -2.0f, -30.0f, 1.0f)})
    {
        const Vec4 a = projection * point;
        const Vec4 b = jittered * point;
        const Vec2 shiftPixels = (Vec2(b) / b.w - Vec2(a) / a.w) * size * 0.5f;
        CHECK(shiftPixels.x == doctest::Approx(0.5f));
        CHECK(shiftPixels.y == doctest::Approx(-0.25f)); // clip y points up, screen y down
        CHECK(b.z / b.w == doctest::Approx(a.z / a.w));  // depth is unchanged
    }
}

#include <illusion/render/Shadows.hpp>

#include <doctest/doctest.h>

#include <cmath>

using namespace illusion;

namespace
{

constexpr f32 kFovY = 1.0471976f; // 60 degrees
constexpr f32 kAspect = 16.0f / 9.0f;
constexpr f32 kNear = 0.1f;

Mat4 viewMatrix(Vec3 position, f32 yaw)
{
    const Mat4 cameraToWorld =
        glm::translate(Mat4(1.0f), position) * glm::rotate(Mat4(1.0f), yaw, Vec3(0.0f, 1.0f, 0.0f));
    return glm::inverse(cameraToWorld);
}

const Vec3 kLight = glm::normalize(Vec3(0.3f, -1.0f, 0.4f));

} // namespace

TEST_CASE("shadow cascades split the shadow distance from near to far")
{
    ShadowSettings settings;
    const ShadowCascades cascades = computeShadowCascades(
        viewMatrix(Vec3(0.0f), 0.0f), perspectiveReversedZ(kFovY, kAspect, kNear), kLight, settings);
    REQUIRE(cascades.count == 4);
    CHECK(cascades.cascades[0].splitNear == doctest::Approx(kNear));
    CHECK(cascades.cascades[3].splitFar == doctest::Approx(settings.maxDistance));
    for (u32 i = 1; i < cascades.count; ++i)
    {
        CHECK(cascades.cascades[i].splitNear == doctest::Approx(cascades.cascades[i - 1].splitFar));
        CHECK(cascades.cascades[i].splitFar > cascades.cascades[i].splitNear);
        CHECK(cascades.cascades[i].worldSize > cascades.cascades[i - 1].worldSize);
    }

    settings.splitLambda = 0.0f; // uniform
    settings.cascadeCount = 2;
    const ShadowCascades uniform = computeShadowCascades(
        viewMatrix(Vec3(0.0f), 0.0f), perspectiveReversedZ(kFovY, kAspect, kNear), kLight, settings);
    REQUIRE(uniform.count == 2);
    CHECK(uniform.cascades[0].splitFar == doctest::Approx((kNear + settings.maxDistance) * 0.5f));

    settings.cascadeCount = 9;
    CHECK(computeShadowCascades(Mat4(1.0f), perspectiveReversedZ(kFovY, kAspect, kNear), kLight, settings)
              .count == kMaxShadowCascades);
}

TEST_CASE("orthographic cascades contain off-centre frustum corners and respect the camera far plane")
{
    ShadowSettings settings;
    settings.resolution = 64;
    const Mat4 view = viewMatrix(Vec3(3, 2, -5), 0.7f);
    const Mat4 projection = orthographicReversedZ(-3, 9, -2, 4, 0.5f, 30);
    const ShadowCascades cascades = computeShadowCascades(view, projection, kLight, settings);
    CHECK(cascades.cascades[0].splitNear == doctest::Approx(0.5));
    CHECK(cascades.cascades[cascades.count - 1].splitFar == doctest::Approx(30));
    for (u32 c = 0; c < cascades.count; ++c)
    {
        const auto& cascade = cascades.cascades[c];
        for (const f32 depth : {cascade.splitNear, cascade.splitFar})
        {
            for (const f32 x : {-3.0f, 9.0f})
            {
                for (const f32 y : {-2.0f, 4.0f})
                {
                    const Vec4 clip = cascade.viewProjection * glm::inverse(view) * Vec4(x, y, -depth, 1);
                    CHECK(std::abs(clip.x) <= 1.0001f);
                    CHECK(std::abs(clip.y) <= 1.0001f);
                    CHECK(clip.z >= 0);
                    CHECK(clip.z <= 1);
                }
            }
        }
    }
}

TEST_CASE("each shadow cascade contains its slice of the view frustum")
{
    const ShadowSettings settings;
    const Vec3 position(3.0f, 2.0f, -5.0f);
    const f32 yaw = 0.7f;
    const Mat4 view = viewMatrix(position, yaw);
    const ShadowCascades cascades =
        computeShadowCascades(view, perspectiveReversedZ(kFovY, kAspect, kNear), kLight, settings);
    const Mat4 cameraToWorld = glm::inverse(view);
    const f32 tanY = std::tan(kFovY * 0.5f);
    const f32 tanX = tanY * kAspect;
    for (u32 c = 0; c < cascades.count; ++c)
    {
        const ShadowCascade& cascade = cascades.cascades[c];
        for (const f32 depth : {cascade.splitNear, cascade.splitFar})
        {
            for (const f32 sx : {-1.0f, 1.0f})
            {
                for (const f32 sy : {-1.0f, 1.0f})
                {
                    const Vec4 corner =
                        cameraToWorld * Vec4(sx * tanX * depth, sy * tanY * depth, -depth, 1.0f);
                    const Vec4 clip = cascade.viewProjection * corner;
                    CHECK(std::abs(clip.x) <= 1.0001f);
                    CHECK(std::abs(clip.y) <= 1.0001f);
                    CHECK(clip.z >= 0.0f);
                    CHECK(clip.z <= 1.0f);
                }
            }
        }
        // Casters up to casterDistance towards the light from the slice are inside the depth range.
        const Vec3 centre =
            Vec3(cameraToWorld * Vec4(0.0f, 0.0f, -(cascade.splitNear + cascade.splitFar) * 0.5f, 1.0f));
        const Vec4 caster =
            cascade.viewProjection * Vec4(centre - kLight * (settings.casterDistance * 0.9f), 1.0f);
        CHECK(caster.z >= 0.0f);
        CHECK(caster.z < 0.5f); // nearer the light than the slice
        CHECK(cascade.depthRange == doctest::Approx(cascade.worldSize + settings.casterDistance));
    }
}

TEST_CASE("shadow cascades do not change size when the camera turns and move in whole texels")
{
    const ShadowSettings settings;
    const Mat4 projection = perspectiveReversedZ(kFovY, kAspect, kNear);
    const ShadowCascades a =
        computeShadowCascades(viewMatrix(Vec3(0.0f), 0.0f), projection, kLight, settings);
    const ShadowCascades b =
        computeShadowCascades(viewMatrix(Vec3(0.0f), 2.1f), projection, kLight, settings);
    for (u32 c = 0; c < a.count; ++c)
    {
        CHECK(a.cascades[c].worldSize == doctest::Approx(b.cascades[c].worldSize));
    }

    // A fixed world point lands at the same sub-texel position after the camera moves by a fraction of a
    // texel, so shadow edges do not crawl.
    const ShadowCascades moved =
        computeShadowCascades(viewMatrix(Vec3(0.013f, 0.0f, 0.007f), 0.0f), projection, kLight, settings);
    const auto texelCoordinate = [&](const ShadowCascade& cascade, Vec3 point) {
        const Vec4 clip = cascade.viewProjection * Vec4(point, 1.0f);
        return (Vec2(clip) * 0.5f + 0.5f) * static_cast<f32>(settings.resolution);
    };
    for (u32 c = 0; c < a.count; ++c)
    {
        const Vec2 before = texelCoordinate(a.cascades[c], Vec3(1.234f, 0.5f, -3.21f));
        const Vec2 after = texelCoordinate(moved.cascades[c], Vec3(1.234f, 0.5f, -3.21f));
        const Vec2 shift = after - before;
        CHECK(std::abs(shift.x - std::round(shift.x)) < 0.01f);
        CHECK(std::abs(shift.y - std::round(shift.y)) < 0.01f);
    }
}

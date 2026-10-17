#include <illusion/render/Shadows.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{

ShadowCascades computeShadowCascades(const Mat4& view, const Mat4& projection, Vec3 lightDirection,
                                     const ShadowSettings& settings) noexcept
{
    ShadowCascades result;
    // perspectiveReversedZ(): m[0][0] = f / aspect, m[1][1] = f, m[3][2] = near.
    const f32 tanHalfY = 1.0f / projection[1][1];
    const f32 tanHalfX = 1.0f / projection[0][0];
    const f32 nearPlane = projection[3][2];
    const f32 farPlane = std::max(settings.maxDistance, nearPlane * 2.0f);
    const u32 count = std::clamp(settings.cascadeCount, 1u, kMaxShadowCascades);
    const f32 resolution = static_cast<f32>(std::max(settings.resolution, 1u));
    const Mat4 cameraToWorld = glm::inverse(view);

    // Light-space rotation: looks along the light direction.
    const Vec3 direction = glm::normalize(lightDirection);
    const Vec3 up = std::abs(direction.y) > 0.99f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
    const Mat4 lightRotation = glm::lookAt(Vec3(0.0f), direction, up);

    f32 sliceNear = nearPlane;
    for (u32 i = 0; i < count; ++i)
    {
        const f32 t = static_cast<f32>(i + 1) / static_cast<f32>(count);
        const f32 uniform = nearPlane + (farPlane - nearPlane) * t;
        const f32 logarithmic = nearPlane * std::pow(farPlane / nearPlane, t);
        const f32 sliceFar = i + 1 == count ? farPlane : glm::mix(uniform, logarithmic, settings.splitLambda);

        // Bounding sphere of the slice, computed in view space so it does not depend on the camera's
        // orientation. The centre lies on the view axis; the radius reaches the farthest corner.
        const f32 nearExtent2 = (tanHalfX * tanHalfX + tanHalfY * tanHalfY) * sliceNear * sliceNear;
        const f32 farExtent2 = (tanHalfX * tanHalfX + tanHalfY * tanHalfY) * sliceFar * sliceFar;
        // Point on the axis equidistant from a near corner and a far corner, clamped to the slice.
        const f32 centreDepth =
            std::clamp((sliceFar * sliceFar - sliceNear * sliceNear + farExtent2 - nearExtent2) /
                           (2.0f * (sliceFar - sliceNear)),
                       sliceNear, sliceFar);
        f32 radius = std::sqrt(std::max((sliceFar - centreDepth) * (sliceFar - centreDepth) + farExtent2,
                                        (centreDepth - sliceNear) * (centreDepth - sliceNear) + nearExtent2));
        radius = std::ceil(radius * 16.0f) / 16.0f; // stable size across tiny numeric changes
        const Vec3 centre = Vec3(cameraToWorld * Vec4(0.0f, 0.0f, -centreDepth, 1.0f));

        // Snap the centre to whole texels in light space.
        const f32 texel = 2.0f * radius / resolution;
        Vec3 lightCentre = Vec3(lightRotation * Vec4(centre, 1.0f));
        lightCentre.x = std::floor(lightCentre.x / texel) * texel;
        lightCentre.y = std::floor(lightCentre.y / texel) * texel;

        // Orthographic box in light view space (looking down -z): depth 0 at the light side.
        const f32 nearZ = lightCentre.z + radius + settings.casterDistance;
        const f32 farZ = lightCentre.z - radius;
        Mat4 ortho(0.0f);
        ortho[0][0] = 1.0f / radius;
        ortho[3][0] = -lightCentre.x / radius;
        ortho[1][1] = 1.0f / radius;
        ortho[3][1] = -lightCentre.y / radius;
        ortho[2][2] = -1.0f / (nearZ - farZ);
        ortho[3][2] = nearZ / (nearZ - farZ);
        ortho[3][3] = 1.0f;

        ShadowCascade& cascade = result.cascades[i];
        cascade.viewProjection = ortho * lightRotation;
        cascade.splitNear = sliceNear;
        cascade.splitFar = sliceFar;
        cascade.worldSize = 2.0f * radius;
        cascade.depthRange = nearZ - farZ;
        sliceNear = sliceFar;
    }
    result.count = count;
    return result;
}

} // namespace illusion

#include <illusion/render/Shadows.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{

ShadowCascades computeShadowCascades(const Mat4& view, const Mat4& projection, Vec3 lightDirection,
                                     const ShadowSettings& settings) noexcept
{
    ShadowCascades result;
    const Mat4 inverseProjection = glm::inverse(projection);
    const Vec4 nearPoint = inverseProjection * Vec4(0.0f, 0.0f, 1.0f, 1.0f);
    const Vec4 farPoint = inverseProjection * Vec4(0.0f, 0.0f, 0.0f, 1.0f);
    const f32 nearPlane = std::max(-nearPoint.z / nearPoint.w, 0.0001f);
    const f32 cameraFar = std::abs(farPoint.w) > 1e-7f ? -farPoint.z / farPoint.w : settings.maxDistance;
    const f32 farPlane = std::max(std::min(settings.maxDistance, cameraFar), nearPlane + 0.001f);
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

        // Reconstruct each corner ray from the inverse projection. Intersect it with the two
        // view-space split planes; this also handles orthographic and off-centre projections.
        std::array<Vec3, 8> corners{};
        usize cornerIndex = 0;
        Vec3 centreView(0.0f);
        for (const f32 z : {sliceNear, sliceFar})
        {
            for (const f32 x : {-1.0f, 1.0f})
            {
                for (const f32 y : {-1.0f, 1.0f})
                {
                    const Vec4 a = inverseProjection * Vec4(x, y, 1.0f, 1.0f);
                    const Vec4 b = inverseProjection * Vec4(x, y, 0.5f, 1.0f);
                    const Vec3 origin = Vec3(a) / a.w;
                    const Vec3 delta = Vec3(b) / b.w - origin;
                    const Vec3 corner = origin + delta * ((-z - origin.z) / delta.z);
                    corners[cornerIndex++] = corner;
                    centreView += corner / 8.0f;
                }
            }
        }
        f32 radius = 0.0f;
        for (const Vec3 corner : corners)
        {
            radius = std::max(radius, glm::length(corner - centreView));
        }
        // Leave a texel around the sphere for the centre snap, including very small test maps.
        radius = std::ceil(radius * 16.0f) / 16.0f;
        radius *= resolution > 2.0f ? resolution / (resolution - 2.0f) : 3.0f;
        const Vec3 centre = Vec3(cameraToWorld * Vec4(centreView, 1.0f));

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

Mat4 pointShadowFaceMatrix(Vec3 position, u32 face, f32 nearPlane) noexcept
{
    // Right, up and forward axes of each face, chosen so that the image matches the Vulkan cube map
    // layout (the bases are mirrored, which does not matter: casters are drawn with both faces).
    struct Basis
    {
        Vec3 right;
        Vec3 up;
        Vec3 forward;
    };
    static constexpr std::array<Basis, kPointShadowFaces> kBases = {{
        {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
        {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    }};
    const Basis& basis = kBases[std::min(face, kPointShadowFaces - 1)];
    const f32 scale = 1.0f / kPointShadowFaceScale;
    // Rows: x = right / scale, y = up / scale, z = near, w = forward (all relative to the light).
    Mat4 m(0.0f);
    for (int axis = 0; axis < 3; ++axis)
    {
        m[axis][0] = basis.right[axis] * scale;
        m[axis][1] = basis.up[axis] * scale;
        m[axis][3] = basis.forward[axis];
    }
    m[3][0] = -glm::dot(basis.right, position) * scale;
    m[3][1] = -glm::dot(basis.up, position) * scale;
    m[3][2] = nearPlane;
    m[3][3] = -glm::dot(basis.forward, position);
    return m;
}

u32 pointShadowFace(Vec3 direction) noexcept
{
    const Vec3 a = glm::abs(direction);
    if (a.x >= a.y && a.x >= a.z)
    {
        return direction.x >= 0.0f ? 0u : 1u;
    }
    if (a.y >= a.z)
    {
        return direction.y >= 0.0f ? 2u : 3u;
    }
    return direction.z >= 0.0f ? 4u : 5u;
}

} // namespace illusion

#pragma once

/// @file
/// Math types, based on glm. Conventions: right-handed world coordinates with +y up, cameras looking
/// down -z, angles in radians. Clip space has y pointing up (the renderer flips the Vulkan viewport, as
/// Direct3D does) and the renderer uses **reversed-Z** depth: 1 at the near plane, 0 at infinity (see
/// perspectiveReversedZ()). Include this header instead of glm directly so the configuration is
/// consistent everywhere.

#include <illusion/core/Base.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace illusion
{

using Vec2 = glm::vec2;   ///< 2D float vector.
using Vec3 = glm::vec3;   ///< 3D float vector.
using Vec4 = glm::vec4;   ///< 4D float vector.
using IVec2 = glm::ivec2; ///< 2D signed integer vector.
using UVec2 = glm::uvec2; ///< 2D unsigned integer vector.
using Mat3 = glm::mat3;   ///< 3x3 float matrix (column-major).
using Mat4 = glm::mat4;   ///< 4x4 float matrix (column-major).
using Quat = glm::quat;   ///< Rotation quaternion (w, x, y, z).

/// Right-handed perspective projection with reversed-Z and an infinite far plane: depth is 1 at
/// @p nearPlane and approaches 0 at infinity, which gives far better depth precision than a standard
/// projection. Clip-space y is up. @p fovY is the vertical field of view in radians.
[[nodiscard]] inline Mat4 perspectiveReversedZ(f32 fovY, f32 aspect, f32 nearPlane) noexcept
{
    const f32 f = 1.0f / std::tan(fovY * 0.5f);
    Mat4 m(0.0f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][3] = -1.0f;     // w = -z_view
    m[3][2] = nearPlane; // z = near, so depth = near / -z_view
    return m;
}

/// Axis-aligned bounding box. A default-constructed box is empty (min > max).
struct Aabb
{
    Vec3 min{std::numeric_limits<f32>::max()};    ///< Minimum corner.
    Vec3 max{std::numeric_limits<f32>::lowest()}; ///< Maximum corner.

    /// Returns true if the box contains at least one point.
    [[nodiscard]] bool isValid() const noexcept { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

    /// Grows the box to contain @p point.
    void expand(Vec3 point) noexcept
    {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    /// Grows the box to contain @p other.
    void expand(const Aabb& other) noexcept
    {
        if (other.isValid())
        {
            expand(other.min);
            expand(other.max);
        }
    }

    /// Center of the box.
    [[nodiscard]] Vec3 center() const noexcept { return (min + max) * 0.5f; }

    /// Half size of the box.
    [[nodiscard]] Vec3 extents() const noexcept { return (max - min) * 0.5f; }

    /// Bounding box of this box after transforming it by @p matrix.
    [[nodiscard]] Aabb transformed(const Mat4& matrix) const noexcept
    {
        if (!isValid())
        {
            return Aabb{};
        }
        // Arvo's method: transform the center and project the extents onto the new axes.
        const Vec3 c = Vec3(matrix * Vec4(center(), 1.0f));
        const Vec3 e = extents();
        const Vec3 x = glm::abs(Vec3(matrix[0])) * e.x;
        const Vec3 y = glm::abs(Vec3(matrix[1])) * e.y;
        const Vec3 z = glm::abs(Vec3(matrix[2])) * e.z;
        const Vec3 half = x + y + z;
        return Aabb{c - half, c + half};
    }
};

/// Bounding sphere.
struct Sphere
{
    Vec3 center{0.0f}; ///< Center.
    f32 radius = 0.0f; ///< Radius (0 for an empty sphere).

    /// Sphere enclosing @p box.
    [[nodiscard]] static Sphere fromAabb(const Aabb& box) noexcept
    {
        return box.isValid() ? Sphere{box.center(), glm::length(box.extents())} : Sphere{};
    }

    /// This sphere after transforming it by @p matrix (the radius uses the largest axis scale).
    [[nodiscard]] Sphere transformed(const Mat4& matrix) const noexcept
    {
        const Vec3 x(matrix[0]);
        const Vec3 y(matrix[1]);
        const Vec3 z(matrix[2]);
        const f32 scaleSquared = std::max(std::max(glm::dot(x, x), glm::dot(y, y)), glm::dot(z, z));
        return Sphere{Vec3(matrix[3]) + x * center.x + y * center.y + z * center.z,
                      radius * std::sqrt(scaleSquared)};
    }
};

/// View frustum as six inward-facing planes (xyz = normal, w = distance), for culling.
struct Frustum
{
    /// Left, right, bottom, top, near, far. A plane with a zero normal never rejects anything (the far
    /// plane of an infinite projection).
    std::array<Vec4, 6> planes{};

    /// Extracts the planes of a view-projection matrix with depth in [0, 1] (standard or reversed).
    [[nodiscard]] static Frustum fromViewProjection(const Mat4& viewProjection) noexcept
    {
        const auto row = [&](int i) {
            return Vec4(viewProjection[0][i], viewProjection[1][i], viewProjection[2][i],
                        viewProjection[3][i]);
        };
        Frustum frustum;
        frustum.planes = {row(3) + row(0), row(3) - row(0), row(3) + row(1),
                          row(3) - row(1), row(2),          row(3) - row(2)};
        for (Vec4& plane : frustum.planes)
        {
            const f32 length = glm::length(Vec3(plane));
            plane = length > 1e-6f ? plane / length : Vec4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return frustum;
    }

    /// Returns false only if @p sphere is entirely outside the frustum.
    [[nodiscard]] bool intersects(const Sphere& sphere) const noexcept
    {
        // Branch-free: visibility is unpredictable per object, and six plane tests are cheaper than a
        // mispredicted early exit.
        bool inside = true;
        for (const Vec4& plane : planes)
        {
            inside &=
                plane.x * sphere.center.x + plane.y * sphere.center.y + plane.z * sphere.center.z + plane.w >=
                -sphere.radius;
        }
        return inside;
    }

    /// Returns false only if @p box is entirely outside the frustum.
    [[nodiscard]] bool intersects(const Aabb& box) const noexcept
    {
        for (const Vec4& plane : planes)
        {
            // The box corner furthest along the plane normal.
            const Vec3 corner(plane.x >= 0.0f ? box.max.x : box.min.x,
                              plane.y >= 0.0f ? box.max.y : box.min.y,
                              plane.z >= 0.0f ? box.max.z : box.min.z);
            if (glm::dot(Vec3(plane), corner) + plane.w < 0.0f)
            {
                return false;
            }
        }
        return true;
    }
};

} // namespace illusion

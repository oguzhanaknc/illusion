#pragma once

/// @file
/// Transform component: position, rotation and scale of an Entity in world space.

#include <illusion/core/Math.hpp>

namespace illusion
{

/// World-space placement of an Entity. Scale is applied first, then rotation, then translation.
struct Transform
{
    Vec3 position{0.0f};                   ///< World position.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World rotation (identity by default).
    Vec3 scale{1.0f};                      ///< Per-axis scale.

    /// The transform as a matrix (translation * rotation * scale).
    [[nodiscard]] Mat4 matrix() const noexcept
    {
        Mat4 m = glm::mat4_cast(rotation);
        m[0] *= scale.x;
        m[1] *= scale.y;
        m[2] *= scale.z;
        m[3] = Vec4(position, 1.0f);
        return m;
    }

    /// Decomposes an affine matrix without shear into a Transform. A negative determinant (mirroring) is
    /// expressed as a negative x scale.
    [[nodiscard]] static Transform fromMatrix(const Mat4& m) noexcept
    {
        Transform t;
        t.position = Vec3(m[3]);
        Vec3 x(m[0]);
        const Vec3 y(m[1]);
        const Vec3 z(m[2]);
        t.scale = Vec3(glm::length(x), glm::length(y), glm::length(z));
        if (glm::dot(glm::cross(x, y), z) < 0.0f)
        {
            t.scale.x = -t.scale.x;
            x = -x;
        }
        const Mat3 rotation(t.scale.x != 0.0f ? x / std::abs(t.scale.x) : Vec3(1, 0, 0),
                            t.scale.y != 0.0f ? y / t.scale.y : Vec3(0, 1, 0),
                            t.scale.z != 0.0f ? z / t.scale.z : Vec3(0, 0, 1));
        t.rotation = glm::normalize(glm::quat_cast(rotation));
        return t;
    }

    /// Unit vector the transform's -z axis points along ("forward" for cameras and lights).
    [[nodiscard]] Vec3 forward() const noexcept { return rotation * Vec3(0.0f, 0.0f, -1.0f); }
};

} // namespace illusion

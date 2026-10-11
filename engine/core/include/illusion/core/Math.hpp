#pragma once

/// @file
/// Math types. illusion uses glm with Vulkan conventions: right-handed coordinates, depth range [0, 1],
/// angles in radians. Include this header instead of including glm directly so the configuration is
/// consistent everywhere.

#include <glm/glm.hpp>

namespace illusion
{

using Vec2 = glm::vec2;   ///< 2D float vector.
using Vec3 = glm::vec3;   ///< 3D float vector.
using Vec4 = glm::vec4;   ///< 4D float vector.
using IVec2 = glm::ivec2; ///< 2D signed integer vector.
using UVec2 = glm::uvec2; ///< 2D unsigned integer vector.
using Mat3 = glm::mat3;   ///< 3x3 float matrix (column-major).
using Mat4 = glm::mat4;   ///< 4x4 float matrix (column-major).

} // namespace illusion

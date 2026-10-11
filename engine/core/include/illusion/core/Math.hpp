#pragma once

/// @file
/// Math types, based on glm. Conventions: right-handed world coordinates, angles in radians, clip-space
/// depth in [0, 1] and clip-space y pointing up (the renderer flips the Vulkan viewport, as Direct3D does),
/// so glm::perspective() and glm::ortho() results can be used without a y flip. Include this header
/// instead of including glm directly so the configuration is consistent everywhere.

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

#pragma once

/// @file
/// GPU-free geometry of the view cube and the axis navigator of the Scene views: what the camera's rotation
/// makes of a unit cube and three axes in a corner of the image, which part of the cube is under the cursor,
/// and the camera that looks from there.
///
/// Cube space is the world's axes around the cube's centre, with the cube from -1 to 1 on each axis. Points
/// in the image are relative to the cube's centre in cube units, x right and y down (the panel scales them
/// to pixels).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <array>
#include <optional>
#include <span>

namespace illusion::editor
{

/// A face of the view cube as seen by the camera.
struct ViewCubeFace
{
    Vec3 normal{0.0f};                     ///< Outward normal in world space (one axis, +-1).
    ViewportView view = ViewportView::Top; ///< The standard view that looks at this face.
    std::array<Vec2, 4> corners{};         ///< Corners in image units (x right, y down), in order.
    Vec2 center{0.0f};                     ///< Centre in image units.
    f32 facing = 0.0f;                     ///< How directly it faces the camera, 0 to 1.
};

/// An end of the axis navigator's axes as seen by the camera.
struct ViewAxisEnd
{
    Vec3 direction{0.0f}; ///< The world axis (one axis, +-1).
    Vec2 point{0.0f};     ///< Position in image units (the axes have length 1).
    f32 depth = 0.0f;     ///< Towards the camera (larger is nearer).
};

/// Outer part of a face that counts as its edge or corner, in cube units.
inline constexpr f32 kViewCubeEdge = 0.35f;

/// Where the world-space point @p point (cube space) appears in image units for a camera turned by
/// @p rotation (viewportRotation()), seen without perspective.
[[nodiscard]] Vec2 viewCubeProject(const Quat& rotation, Vec3 point) noexcept;

/// The faces of the cube that face a camera turned by @p rotation, farthest first, in @p out (room for six).
/// Returns how many were written (one to three).
u32 viewCubeFaces(const Quat& rotation, std::span<ViewCubeFace, 6> out) noexcept;

/// The six ends of the axis navigator for a camera turned by @p rotation, farthest first.
[[nodiscard]] std::array<ViewAxisEnd, 6> viewAxisEnds(const Quat& rotation) noexcept;

/// The part of the cube under @p point (image units) for a camera turned by @p rotation: the direction from
/// the cube's centre to the face, edge or corner hit, with each component -1, 0 or 1 (a face has one
/// non-zero component, an edge two and a corner three). std::nullopt when @p point misses the cube.
[[nodiscard]] std::optional<Vec3> viewCubePick(const Quat& rotation, Vec2 point) noexcept;

/// The standard view that looks from @p direction (a face: one non-zero component), std::nullopt for edges
/// and corners.
[[nodiscard]] std::optional<ViewportView> viewCubeView(Vec3 direction) noexcept;

/// @p camera turned to look at its pivot from @p direction (not zero; it need not be normalized), with the
/// same projection, distance and pivot.
[[nodiscard]] ViewportCamera viewCubeCamera(const ViewportCamera& camera, Vec3 direction) noexcept;

} // namespace illusion::editor

#pragma once

/// @file
/// GPU-free math of the arranging commands (`transform.align`, `transform.distribute` and
/// `transform.drop_to_ground`): how far each entity moves along an axis, given the world-space boxes around
/// what the entities draw.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <span>

namespace illusion::editor
{

/// The edge of the boxes that `transform.align` lines up.
enum class AlignEdge : u8
{
    Min,    ///< The lowest coordinates.
    Center, ///< The centres.
    Max,    ///< The highest coordinates.
};

/// What `transform.align` lines the boxes up with.
enum class AlignTarget : u8
{
    Selection, ///< The box around all of them.
    Active,    ///< The last listed entity's box (it does not move).
};

/// How `transform.distribute` spaces the boxes between the first and the last.
enum class DistributeMode : u8
{
    Centers, ///< Equal distances between the centres.
    Gaps,    ///< Equal gaps between neighbouring boxes.
};

/// What `transform.drop_to_ground` lets the entities fall onto.
enum class DropTarget : u8
{
    Surfaces, ///< The highest box of another entity below, or the ground plane (y = 0) when there is none.
    Plane,    ///< The ground plane (y = 0) only.
};

/// Distance in world units within which a surface counts as below a box that rests slightly inside it.
inline constexpr f32 kArrangeTolerance = 0.001f;

/// Writes to @p out (the size of @p boxes) how far each box moves along @p axis (0 to 2) so that its
/// @p edge lines up with the same edge of @p target. Invalid boxes do not move.
void alignOffsets(std::span<const Aabb> boxes, u32 axis, AlignEdge edge, const Aabb& target,
                  std::span<f32> out) noexcept;

/// Writes to @p out (the size of @p boxes) how far each box moves along @p axis (0 to 2) so that the boxes
/// are spaced evenly by @p mode. The boxes keep their order by centre; the first and the last do not move.
/// Fewer than three boxes, or invalid ones, do not move.
void distributeOffsets(std::span<const Aabb> boxes, u32 axis, DistributeMode mode, std::span<f32> out);

/// The height that the bottom of @p box comes to rest at when it falls: the highest top among @p surfaces
/// that overlap it in x and z and lie below its bottom (within kArrangeTolerance), or the plane at @p ground
/// when there is none (a box below the plane then rises to it).
[[nodiscard]] f32 restingHeight(const Aabb& box, std::span<const Aabb> surfaces, f32 ground) noexcept;

} // namespace illusion::editor

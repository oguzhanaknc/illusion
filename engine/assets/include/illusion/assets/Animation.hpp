#pragma once

/// @file
/// Skeletons, animation clips and the skinning stream of a mesh, as imported from glTF and cooked into a
/// ModelData (docs/adr/0059-skeletal-animation.md). Plain data; sampling and posing live in engine/animation.

#include <illusion/assets/Rig.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <array>
#include <string>
#include <vector>

namespace illusion
{

/// Most joints a Skeleton has (joint indices are u16).
inline constexpr u32 kMaxJoints = 1024;

/// Most joints that move one vertex.
inline constexpr u32 kMaxJointInfluences = 4;

/// The joints and weights of one vertex, parallel to MeshData::vertices.
struct SkinVertex
{
    std::array<u16, kMaxJointInfluences> joints{}; ///< Joint indices into the mesh's Skeleton.
    /// Weights as unorm16 (65535 = 1.0), largest first, summing to 65535; unused influences have weight 0.
    std::array<u16, kMaxJointInfluences> weights{};
};
static_assert(sizeof(SkinVertex) == 16, "SkinVertex layout must match the skinning shader");

/// One joint of a Skeleton.
struct SkeletonJoint
{
    std::string name;       ///< The glTF node name; clips find joints by it.
    i32 parent = -1;        ///< Parent joint index (smaller than this joint's), or -1 for a root.
    Vec3 translation{0.0f}; ///< Rest pose relative to the parent.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Rest pose relative to the parent (unit).
    Vec3 scale{1.0f};                      ///< Rest pose relative to the parent.
    Mat4 inverseBind{1.0f};                ///< Model space to this joint's bind space.
};

/// The joints of a glTF skin, parents before children.
struct Skeleton
{
    std::string name;                  ///< The glTF skin name, or empty.
    std::vector<SkeletonJoint> joints; ///< At most kMaxJoints joints.
    /// Model-space matrix of the skeleton's root parent (the glTF nodes above the joints).
    Mat4 rootTransform{1.0f};
    /// The bone of every joint (empty, or parallel to joints); generated at import (generateRigMap()).
    RigMap rig{};
};

/// What an AnimationChannel moves.
enum class AnimationPath : u8
{
    Translation, ///< Vec3 values.
    Rotation,    ///< Quat values (x, y, z, w stored as Vec4).
    Scale,       ///< Vec3 values.
};

/// How values between two keyframes are found (glTF interpolation).
enum class AnimationInterpolation : u8
{
    Step,        ///< The earlier keyframe's value.
    Linear,      ///< Linear; spherical linear for rotations.
    CubicSpline, ///< Cubic Hermite; every keyframe stores in-tangent, value, out-tangent.
};

/// The keyframes of one property of one joint.
struct AnimationChannel
{
    std::string joint;                                                     ///< Joint name.
    AnimationPath path = AnimationPath::Translation;                       ///< What it moves.
    AnimationInterpolation interpolation = AnimationInterpolation::Linear; ///< How it interpolates.
    std::vector<f32> times; ///< Ascending keyframe times, seconds.
    /// Values as Vec4 (w unused for Translation and Scale): one per keyframe, three per keyframe for
    /// CubicSpline (in-tangent, value, out-tangent).
    std::vector<Vec4> values;
};

/// A glTF animation.
struct AnimationClip
{
    std::string name;                       ///< The glTF animation name (unique in its model).
    f32 duration = 0.0f;                    ///< Last keyframe time of all channels, seconds.
    std::vector<AnimationChannel> channels; ///< Channels, sorted by joint name, then path.
    /// The skin of its model whose joint names its channels hit most (lowest index on ties), or -1.
    i32 skeleton = -1;
};

} // namespace illusion

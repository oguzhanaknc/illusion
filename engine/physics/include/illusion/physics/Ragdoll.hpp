#pragma once

/// @file
/// Ragdolls (docs/adr/0061-sockets-ik-and-ragdolls.md): the saved Ragdoll Component, `.ragdoll` profiles and
/// the profile generated from a skeleton. PhysicsWorld builds a Jolt ragdoll from a profile when the
/// Component becomes active; GameSession hands it the animated pose and hands the simulated pose back to the
/// AnimationWorld.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

struct Skeleton;

/// File extension of ragdoll profiles.
inline constexpr std::string_view kRagdollProfileExtension = ".ragdoll";

/// Turns the Entity's skinned character into a physics ragdoll while `active` (ADR 0061). Saved.
struct Ragdoll
{
    AssetRef profile; ///< A `.ragdoll` profile; none generates one from the skeleton (makeRagdollProfile()).
    bool active = false;      ///< Simulated by physics instead of animated.
    f32 blendSeconds = 0.25f; ///< Time to blend between the animated and the simulated pose, both ways.
    f32 mass = 70.0f;         ///< Total mass in kg, shared by the parts by volume.
    u32 layer = 0;            ///< Collision layer of the parts in [0, kMaxPhysicsLayers).
};

/// One body of a ragdoll profile: a capsule along a joint's bone.
struct RagdollPart
{
    std::string joint;     ///< The joint the part moves; its children without a part move with it.
    f32 radius = 0.05f;    ///< Capsule radius in metres.
    f32 halfHeight = 0.1f; ///< Half the capsule's cylinder length in metres.
    Vec3 offset{0.0f};     ///< Capsule centre in the joint's space.
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Capsule orientation (axis +y) in the joint's space.
    f32 massShare = 0.0f;                  ///< Share of Ragdoll::mass; 0 shares by volume.
    f32 swingLimit = 0.7853982f;           ///< Cone limit to the parent part in radians (45°).
    f32 twistMin = -0.5235988f;            ///< Twist limit in radians (-30°).
    f32 twistMax = 0.5235988f;             ///< Twist limit in radians (30°).
};

/// The bodies of a ragdoll, parents before children (a part's parent is the part of its nearest ancestor
/// joint).
struct RagdollProfile
{
    std::vector<RagdollPart> parts; ///< At least one.
};

/// Parses and validates a `.ragdoll` file (`"format": "illusion.ragdoll"`, a version, `parts`); errors name
/// the JSON path, as parseAnimationGraph() does.
[[nodiscard]] Expected<RagdollProfile, std::string> parseRagdollProfile(std::string_view json);

/// A profile for @p skeleton from @p jointBounds (MeshData::jointBounds): a capsule for each joint whose
/// bounds are larger than @p minSize metres, fitted along the bone to its child, with conservative limits.
[[nodiscard]] RagdollProfile makeRagdollProfile(const Skeleton& skeleton, std::span<const Aabb> jointBounds,
                                                f32 minSize = 0.04f);

} // namespace illusion

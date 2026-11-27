#pragma once

/// @file
/// Rig maps (docs/adr/0065-retargeting-and-aim-layers.md): the bone name of every joint of a Skeleton, shared
/// by rigs with different joint names, so that a clip authored on one skeleton plays on another. Generated at
/// import by name and topology, corrected by the model's import settings sidecar, cooked with the Skeleton.
///
/// @code
/// illusion::RigMapReport report;
/// illusion::RigMap rig = illusion::generateRigMap(skeleton, &report);
/// const illusion::SkinImportSettings settings{.bones = {{.joint = "Bip01_Spine2", .bone = "upperChest"}}};
/// if (auto applied = illusion::applyRigOverrides(rig, skeleton, settings); !applied)
/// {
///     log(applied.error());
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

struct Skeleton;

/// The humanoid bone names in camelCase, parents before children; `root` is the optional joint on the ground
/// above `hips`, left is the character's left (+x in model space for a character facing +z).
inline constexpr std::array<std::string_view, 56> kHumanoidBones{
    "root",
    "hips",
    "spine",
    "chest",
    "upperChest",
    "neck",
    "head",
    "jaw",
    "leftEye",
    "rightEye",
    "leftShoulder",
    "leftUpperArm",
    "leftLowerArm",
    "leftHand",
    "rightShoulder",
    "rightUpperArm",
    "rightLowerArm",
    "rightHand",
    "leftUpperLeg",
    "leftLowerLeg",
    "leftFoot",
    "leftToes",
    "rightUpperLeg",
    "rightLowerLeg",
    "rightFoot",
    "rightToes",
    "leftThumbProximal",
    "leftThumbIntermediate",
    "leftThumbDistal",
    "leftIndexProximal",
    "leftIndexIntermediate",
    "leftIndexDistal",
    "leftMiddleProximal",
    "leftMiddleIntermediate",
    "leftMiddleDistal",
    "leftRingProximal",
    "leftRingIntermediate",
    "leftRingDistal",
    "leftLittleProximal",
    "leftLittleIntermediate",
    "leftLittleDistal",
    "rightThumbProximal",
    "rightThumbIntermediate",
    "rightThumbDistal",
    "rightIndexProximal",
    "rightIndexIntermediate",
    "rightIndexDistal",
    "rightMiddleProximal",
    "rightMiddleIntermediate",
    "rightMiddleDistal",
    "rightRingProximal",
    "rightRingIntermediate",
    "rightRingDistal",
    "rightLittleProximal",
    "rightLittleIntermediate",
    "rightLittleDistal",
};

/// The humanoid bones a rig needs to count as humanoid (RigMapReport::humanoid).
inline constexpr std::array<std::string_view, 15> kRequiredHumanoidBones{
    "hips",         "spine",         "head",          "leftUpperArm",  "leftLowerArm",
    "leftHand",     "rightUpperArm", "rightLowerArm", "rightHand",     "leftUpperLeg",
    "leftLowerLeg", "leftFoot",      "rightUpperLeg", "rightLowerLeg", "rightFoot",
};

/// How a retargeted clip moves the translation of a joint of the target skeleton.
enum class RetargetTranslation : u8
{
    Skeleton,  ///< The target's own rest translation (keeps its proportions); the default of most joints.
    Scaled,    ///< Target rest plus the source's motion from its rest, scaled by the hips height ratio.
    Animation, ///< The source's translation as it is (same proportions, sliding props).
};

/// The bone name and translation mode of every joint of one Skeleton, parallel to Skeleton::joints.
struct RigMap
{
    /// Per joint: a humanoid bone (kHumanoidBones), a custom bone name shared with other rigs, or empty.
    std::vector<std::string> bones;
    /// Per joint, the same size as bones: how retargeted clips move it.
    std::vector<RetargetTranslation> translation;

    /// Index of the joint mapped to @p bone, or -1.
    [[nodiscard]] i32 find(std::string_view bone) const noexcept;
    /// True when no joint has a bone.
    [[nodiscard]] bool empty() const noexcept;

    /// Compares both lists.
    friend bool operator==(const RigMap&, const RigMap&) = default;
};

/// Gives the joint @p joint the bone @p bone in an import settings sidecar (empty unmaps it).
struct RigBoneOverride
{
    std::string joint; ///< Joint name in the skin.
    std::string bone;  ///< Humanoid or custom bone name, or empty.

    /// Compares both fields.
    friend bool operator==(const RigBoneOverride&, const RigBoneOverride&) = default;
};

/// Sets the translation mode of the joint @p joint in an import settings sidecar.
struct RigTranslationOverride
{
    std::string joint;                                      ///< Joint name in the skin.
    RetargetTranslation mode = RetargetTranslation::Scaled; ///< Its mode.

    /// Compares both fields.
    friend bool operator==(const RigTranslationOverride&, const RigTranslationOverride&) = default;
};

/// The rig settings of one skin of a model sidecar (`"model": {"skins": [...]}`, one entry per `#skin/N`).
struct SkinImportSettings
{
    bool generate = true;               ///< Start from generateRigMap(); false starts from an empty map.
    std::vector<RigBoneOverride> bones; ///< Applied after generation, sorted by joint.
    std::vector<RigTranslationOverride> translation; ///< Applied after the bones, sorted by joint.

    /// Compares every setting.
    friend bool operator==(const SkinImportSettings&, const SkinImportSettings&) = default;
};

/// What generateRigMap() found.
struct RigMapReport
{
    bool humanoid = false;             ///< Every bone of kRequiredHumanoidBones was found.
    std::vector<std::string> missing;  ///< Required bones not found, in kRequiredHumanoidBones order.
    std::vector<std::string> warnings; ///< Ambiguities resolved (a name that says left on the right side).
};

/// True for a name of kHumanoidBones.
[[nodiscard]] bool isHumanoidBone(std::string_view bone) noexcept;

/// True for a valid bone name: 1 to 64 characters of letters, digits, `_`, `-` and `.`.
[[nodiscard]] bool isValidBoneName(std::string_view bone) noexcept;

/// Maps the joints of @p skeleton to humanoid bones: names first (case, separators and known prefixes such
/// as `mixamorig:` ignored; left/right from `left`, `l`, `.L`, `_l` and the like), then topology for the
/// required bones names did not find (hips: the joint where two downward chains and one upward chain meet;
/// sides from x in model space). Spine joints between hips and neck become spine, chest and upperChest in
/// order. Translation modes: Scaled for root, hips and joints without a parent, Skeleton otherwise.
/// Deterministic; allocates.
[[nodiscard]] RigMap generateRigMap(const Skeleton& skeleton, RigMapReport* report = nullptr);

/// Applies the overrides of @p settings to @p rig: a bone an override gives moves away from the joint
/// generation gave it to; fails, changing nothing, for a joint the skeleton lacks, an invalid bone name, a
/// joint listed twice, or a bone given to two overrides. Allocates.
[[nodiscard]] Expected<void, std::string> applyRigOverrides(RigMap& rig, const Skeleton& skeleton,
                                                            const SkinImportSettings& settings);

/// Stable name of @p mode ("Skeleton", "Scaled", "Animation").
[[nodiscard]] std::string_view retargetTranslationName(RetargetTranslation mode) noexcept;

} // namespace illusion

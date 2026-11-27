#pragma once

/// @file
/// Decals (docs/adr/0066-decals.md): the saved Decal Component that projects a decal material onto the opaque
/// surfaces inside a box, and the plain data of decals spawned at run time through a DecalService. This
/// header needs no GPU headers, so game modules use it through `USES render`.
///
/// @code
/// world.create(illusion::Transform{.position = {0.0f, 0.0f, 0.0f},
///                                  .rotation = illusion::decalRotation(illusion::Vec3(0.0f, 1.0f, 0.0f))},
///              illusion::Decal{.material = illusion::AssetRef("game:/decals/puddle.material"),
///                              .size = {2.0f, 2.0f, 0.4f}});
/// @endcode

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <cmath>
#include <string_view>

namespace illusion
{

/// Number of decal layers: bits 0 to 7 of MeshRenderer::decalLayers and Decal::receiverLayers.
inline constexpr u32 kDecalLayerCount = 8;
/// Every decal layer.
inline constexpr u32 kAllDecalLayers = 0xFFu;
/// The layers a MeshRenderer belongs to unless it says otherwise: layer 0.
inline constexpr u32 kDefaultDecalLayers = 1u;

/// Projects a decal material onto the opaque surfaces inside a box (reflected as "illusion.Decal"). The box
/// is the Entity's world Transform times `size`, centred on it; the decal projects along the box's local -z
/// axis, local x runs along the image's u and local y is up in the image.
struct Decal
{
    /// A `.material` file whose shader is a decal shader (`[Decal(...)]`); other materials draw nothing.
    AssetRef material{};
    Vec3 size{1.0f, 1.0f, 0.5f}; ///< Box extents in metres along local x, y and z (the projection depth).
    Vec4 color{1.0f};            ///< Linear colour the decal shader reads as SurfaceInput::color.
    f32 opacity = 1.0f;          ///< Multiplies the coverage of every channel, 0 to 1.
    /// Decals with a lower order are drawn first and covered by higher ones; equal orders compose in
    /// material order.
    i32 sortOrder = 0;
    u32 receiverLayers = kAllDecalLayers; ///< Decal layers of the surfaces it lands on.
    /// Angle in radians between the surface normal and the box's +z axis up to which the decal is fully
    /// applied.
    f32 angleFadeStart = 1.0471976f;
    /// Angle in radians from which the decal is not applied at all; at least angleFadeStart, at most pi.
    f32 angleFadeEnd = 1.3962634f;
    f32 depthFade = 0.1f; ///< Fraction of the half depth near the box's z faces over which it fades out.
    /// Metres from the camera beyond which it is not drawn, fading over the last 10 %; 0: always drawn.
    f32 drawDistance = 0.0f;
    u32 variant = 0;     ///< Atlas frame or other variation the decal shader reads (decalVariant()).
    bool visible = true; ///< Drawn at all.
};

/// A decal material interned by a DecalService (DecalService::material()); 0 is none.
struct DecalMaterial
{
    u32 index = 0; ///< Slot in the pool's material table (1-based; 0 = none).
    /// Materials are equal when they name the same slot.
    friend bool operator==(const DecalMaterial&, const DecalMaterial&) noexcept = default;
};

/// A decal spawned by a DecalService; stale once it retires or its slot is reused.
struct DecalHandle
{
    u32 slot = 0;       ///< Slot in the pool (1-based; 0 = none).
    u32 generation = 0; ///< Use of the slot the handle was made for.
    /// Handles are equal when they name the same use of the same slot.
    friend bool operator==(const DecalHandle&, const DecalHandle&) noexcept = default;
};

/// A decal to spawn at run time (DecalService::spawn()). The box and fades mean what they mean on Decal.
struct DecalSpawn
{
    DecalMaterial material{};              ///< From DecalService::material().
    Vec3 position{0.0f};                   ///< World position of the box centre (usually the hit point).
    Quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< World rotation of the box; see decalRotation().
    Vec3 size{0.2f, 0.2f, 0.1f};           ///< Box extents in metres.
    Vec4 color{1.0f};                      ///< Linear colour (SurfaceInput::color).
    f32 opacity = 1.0f;                    ///< Coverage multiplier, 0 to 1.
    f32 lifetime = 0.0f;       ///< Seconds until it starts fading out; 0 keeps it until evicted or removed.
    f32 fadeInSeconds = 0.0f;  ///< Seconds it takes to appear.
    f32 fadeOutSeconds = 1.0f; ///< Seconds it takes to disappear after its lifetime.
    i32 sortOrder = 0;         ///< Draw order (see Decal::sortOrder).
    u32 receiverLayers = kAllDecalLayers; ///< Decal layers of the surfaces it lands on.
    f32 angleFadeStart = 1.0471976f;      ///< Radians; see Decal::angleFadeStart.
    f32 angleFadeEnd = 1.3962634f;        ///< Radians; see Decal::angleFadeEnd.
    f32 depthFade = 0.1f;                 ///< See Decal::depthFade.
    u32 variant = 0;                      ///< Atlas frame or other variation (decalVariant()).
    /// Entity whose Transform it follows from now on (null: it stays in world space); it retires with the
    /// Entity.
    Entity attachTo{};
};

/// Why a DecalService call failed.
enum class DecalError : u8
{
    InvalidMaterial,   ///< The material reference is empty.
    TooManyMaterials,  ///< The pool's material table is full.
    UnknownMaterial,   ///< The DecalMaterial was not made by this pool.
    InvalidSpawn,      ///< A value of the DecalSpawn is not finite or out of range (size at most 0, angles).
    InvalidAttachment, ///< DecalSpawn::attachTo is not a live Entity with a Transform.
    NotFound,          ///< The handle's decal has retired or its slot was reused.
};

/// Stable name of @p error, e.g. "NotFound".
[[nodiscard]] constexpr std::string_view decalErrorName(DecalError error) noexcept
{
    switch (error)
    {
    case DecalError::InvalidMaterial:
        return "InvalidMaterial";
    case DecalError::TooManyMaterials:
        return "TooManyMaterials";
    case DecalError::UnknownMaterial:
        return "UnknownMaterial";
    case DecalError::InvalidSpawn:
        return "InvalidSpawn";
    case DecalError::InvalidAttachment:
        return "InvalidAttachment";
    case DecalError::NotFound:
        return "NotFound";
    }
    return "Unknown";
}

/// Rotation of a decal box that projects onto a surface with the outward @p normal (its +z axis along it),
/// turned by @p twist radians about the normal. A zero normal gives the identity.
[[nodiscard]] inline Quat decalRotation(Vec3 normal, f32 twist = 0.0f) noexcept
{
    const f32 length = glm::length(normal);
    if (!(length > 1e-6f))
    {
        return Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    const Vec3 z = normal / length;
    const Vec3 reference = std::abs(z.y) < 0.999f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 x = glm::normalize(glm::cross(reference, z));
    const Vec3 y = glm::cross(z, x);
    return glm::quat_cast(glm::mat3(x, y, z)) * glm::angleAxis(twist, Vec3(0.0f, 0.0f, 1.0f));
}

} // namespace illusion

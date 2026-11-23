#pragma once

/// @file
/// The virtual cameras of a World (docs/adr/0064-virtual-cameras.md): CameraWorld picks the live
/// VirtualCamera of every CameraBrain, evaluates its stages, blends and shakes, and writes the brain's
/// Camera.
///
/// @code
/// illusion::CameraWorld cameras(world);
/// while (running)
/// {
///     updateTransformHierarchy(world, index);
///     cameras.update(step, {.actions = &actions, .collision = &collision, .aspect = width / height});
/// }
/// @endcode

#include <illusion/camera/CameraBody.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <memory>
#include <string_view>

namespace illusion
{

class InputActions;
class World;

/// The spring arm's sphere casts for CameraWorld: before the casts of a camera it names the follow target,
/// whose own bodies the casts pass through.
class CameraWorldCollision : public CameraCollisionQuery
{
public:
    /// Makes the following casts skip the bodies of @p target (null: none).
    virtual void setIgnored(Entity target) noexcept = 0;

protected:
    CameraWorldCollision() = default;
};

/// Why a CameraWorld call failed.
enum class CameraError : u8
{
    InvalidArgument, ///< A value is not finite or out of range.
    TooManyShakes,   ///< CameraWorldDesc::maxShakes shakes run already.
};

/// English description of @p error, for logs and tools.
[[nodiscard]] std::string_view cameraErrorMessage(CameraError error) noexcept;

/// Creation parameters of a CameraWorld.
struct CameraWorldDesc
{
    u32 maxShakes = 64;        ///< Shakes running at once, reserved at construction.
    u32 reservedCameras = 256; ///< Virtual cameras and brains reserved at construction.
};

/// A decaying shake that every live camera within radius of origin gets.
struct CameraShake
{
    Vec3 origin{0.0f};   ///< Where it starts, in world space.
    f32 strength = 1.0f; ///< Shake at origin; 1 is a strong hit.
    f32 radius = 10.0f;  ///< Distance where it fades to nothing, in metres; positive.
    f32 duration = 0.5f; ///< Seconds until it fades out; positive.
};

/// Counters of the last CameraWorld::update().
struct CameraStatistics
{
    u32 cameras = 0;   ///< VirtualCameras.
    u32 brains = 0;    ///< CameraBrains.
    u32 evaluated = 0; ///< VirtualCameras whose stages ran.
    u32 shakes = 0;    ///< Shakes running after the update.
};

/// What one CameraWorld::update() reads besides the World.
struct CameraWorldUpdate
{
    const InputActions* actions = nullptr;     ///< The look and zoom actions; null: no input.
    CameraWorldCollision* collision = nullptr; ///< Spring-arm casts; null: arms never collide.
    f32 aspect = 1.7777778f;                   ///< Width over height of the view, for the composer.
};

/// Runs the virtual cameras of a World. Each update() adds the runtime states (VirtualCameraState,
/// CameraBrainState, CameraImpulseState) and removes stale ones, fires CameraImpulse sources, picks every
/// CameraBrain's live camera (the highest priority among enabled VirtualCameras without Inactive; ties: the
/// one that became capable last), evaluates each live camera and each camera blended from once, in Entity
/// order (a camera not evaluated in the previous update starts its damping afresh), blends, adds the
/// shakes and writes the brain's world Transform (its LocalTransform when it has a parent) and the fovY and
/// nearPlane of its "illusion.Camera" Component. A brain without a live camera changes nothing.
/// Deterministic: the same World, input and steps give the same poses. Allocates only when more cameras or
/// brains exist than ever before. Use on the World's thread, outside queries.
class CameraWorld : NonCopyable
{
public:
    /// @p world must outlive this object.
    explicit CameraWorld(World& world, const CameraWorldDesc& desc = {});
    ~CameraWorld();

    /// Advances the cameras by @p seconds.
    void update(f32 seconds, const CameraWorldUpdate& input = {});

    /// Starts @p shake in the next update().
    Expected<void, CameraError> shake(const CameraShake& shake);

    /// Counters.
    [[nodiscard]] const CameraStatistics& statistics() const noexcept;

    /// The World it runs.
    [[nodiscard]] World& world() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

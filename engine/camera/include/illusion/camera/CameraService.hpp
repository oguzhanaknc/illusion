#pragma once

/// @file
/// The camera part of the scripting API (docs/adr/0064-virtual-cameras.md): game modules raise and lower
/// virtual cameras, point them at targets, turn their orbit or arm, zoom, shake and read what the brain
/// shows.
///
/// @code
/// if (illusion::CameraService* cameras = context.service<illusion::CameraService>())
/// {
///     if (aiming)
///     {
///         static_cast<void>(cameras->setFieldOfView(shoulderCamera, 0.7f));
///     }
///     if (swapPressed)
///     {
///         static_cast<void>(cameras->swapShoulder(shoulderCamera));
///     }
///     if (hit)
///     {
///         static_cast<void>(cameras->shake({.origin = hitPosition, .strength = 0.8f, .radius = 12.0f}));
///     }
/// }
/// @endcode

#include <illusion/camera/CameraWorld.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Entity.hpp>

namespace illusion
{

/// Virtual cameras of the running game, implemented by the host (CameraWorldService over a CameraWorld) and
/// found by modules with `context.service<CameraService>()`. Changes are seen by the next Cameras step; reads
/// return what the last one decided. Adding a function changes the module API version.
/// @ingroup scripting_api
class CameraService : NonCopyable
{
public:
    /// Raises @p camera above every other virtual camera until deactivate(); a disabled camera stays
    /// disabled.
    virtual Expected<void, CameraError> activate(Entity camera) = 0;
    /// Gives @p camera back the priority it had before activate().
    virtual Expected<void, CameraError> deactivate(Entity camera) = 0;
    /// Sets the priority of @p camera; this ends an activation.
    virtual Expected<void, CameraError> setPriority(Entity camera, i32 priority) = 0;
    /// Sets the Entity the body of @p camera follows; null: the camera's own Entity.
    virtual Expected<void, CameraError> setFollow(Entity camera, Entity target) = 0;
    /// Sets the Entity the aim of @p camera looks at; null: the follow target.
    virtual Expected<void, CameraError> setLookAt(Entity camera, Entity target) = 0;
    /// Turns the CameraOrbit or CameraSpringArm of @p camera to @p yaw and @p pitch (radians; pitch limits
    /// apply).
    virtual Expected<void, CameraError> setAngles(Entity camera, f32 yaw, f32 pitch) = 0;
    /// Swaps the shoulder of the CameraSpringArm of @p camera and returns the new side (-1 left, 1 right).
    virtual Expected<f32, CameraError> swapShoulder(Entity camera) = 0;
    /// Sets the vertical field of view of @p camera in radians, in (0, pi).
    virtual Expected<void, CameraError> setFieldOfView(Entity camera, f32 fovY) = 0;
    /// Shakes every live camera within the radius of @p shake.
    virtual Expected<void, CameraError> shake(const CameraShake& shake) = 0;
    /// Plays the CameraImpulse of @p source at its Entity's position once.
    virtual Expected<void, CameraError> playImpulse(Entity source) = 0;

    /// The virtual camera the CameraBrain @p brain shows; null before it shows one.
    [[nodiscard]] virtual Expected<Entity, CameraError> liveCamera(Entity brain) const = 0;
    /// How far the blend of @p brain has come, from 0 to 1; 1 when none runs.
    [[nodiscard]] virtual Expected<f32, CameraError> blendProgress(Entity brain) const = 0;

protected:
    CameraService() = default;
    virtual ~CameraService() = default;
};

} // namespace illusion

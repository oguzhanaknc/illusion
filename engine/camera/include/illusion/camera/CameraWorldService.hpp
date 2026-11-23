#pragma once

/// @file
/// The host's implementation of CameraService over CameraWorld.
///
/// @code
/// illusion::CameraWorld cameras(world);
/// illusion::CameraWorldService cameraService(cameras);
/// services.provide<illusion::CameraService>(cameraService);
/// @endcode

#include <illusion/camera/CameraService.hpp>
#include <illusion/camera/CameraWorld.hpp>

#include <vector>

namespace illusion
{

/// CameraService over a CameraWorld, which must outlive it. Created by the host; a shared module that
/// constructs one fails to link, since only the host has the library's code (ADR 0041).
class CameraWorldService final : public CameraService
{
public:
    /// Serves @p cameras.
    explicit CameraWorldService(CameraWorld& cameras) noexcept
        : m_cameras(cameras)
    {
    }
    ~CameraWorldService() override;

    /// The CameraWorld it serves.
    [[nodiscard]] CameraWorld& cameras() const noexcept { return m_cameras; }

    /// @name CameraService
    /// @{
    Expected<void, CameraError> activate(Entity camera) override;
    Expected<void, CameraError> deactivate(Entity camera) override;
    Expected<void, CameraError> setPriority(Entity camera, i32 priority) override;
    Expected<void, CameraError> setFollow(Entity camera, Entity target) override;
    Expected<void, CameraError> setLookAt(Entity camera, Entity target) override;
    Expected<void, CameraError> setAngles(Entity camera, f32 yaw, f32 pitch) override;
    Expected<f32, CameraError> swapShoulder(Entity camera) override;
    Expected<void, CameraError> setFieldOfView(Entity camera, f32 fovY) override;
    Expected<void, CameraError> shake(const CameraShake& shake) override;
    Expected<void, CameraError> playImpulse(Entity source) override;
    [[nodiscard]] Expected<Entity, CameraError> liveCamera(Entity brain) const override;
    [[nodiscard]] Expected<f32, CameraError> blendProgress(Entity brain) const override;
    /// @}

private:
    struct Activation
    {
        Entity camera;
        i32 priority = 0; ///< The priority to give back.
    };

    CameraWorld& m_cameras;
    std::vector<Activation> m_activations;
};

} // namespace illusion

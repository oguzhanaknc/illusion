#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraDamping.hpp>
#include <illusion/camera/CameraWorldService.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace illusion
{
namespace
{

constexpr f32 kPi = 3.14159265f;

[[nodiscard]] bool exists(const World& world, Entity entity) noexcept
{
    return !entity.isNull() && world.isAlive(entity);
}

} // namespace

CameraWorldService::~CameraWorldService() = default;

Expected<void, CameraError> CameraWorldService::activate(Entity camera)
{
    World& world = m_cameras.world();
    VirtualCamera* virtualCamera = exists(world, camera) ? world.tryGet<VirtualCamera>(camera) : nullptr;
    if (virtualCamera == nullptr)
    {
        return unexpected(CameraError::NoCamera);
    }
    std::erase_if(m_activations, [&world](const Activation& entry) { return !world.isAlive(entry.camera); });
    i32 highest = std::numeric_limits<i32>::min();
    world.query<const VirtualCamera>().each([&](Entity entity, const VirtualCamera& other) {
        if (entity != camera)
        {
            highest = std::max(highest, other.priority);
        }
    });
    const bool recorded = std::ranges::any_of(
        m_activations, [camera](const Activation& entry) { return entry.camera == camera; });
    if (!recorded)
    {
        m_activations.push_back({camera, virtualCamera->priority});
    }
    const i32 above = highest < std::numeric_limits<i32>::max() ? highest + 1 : highest;
    virtualCamera->priority = std::max(virtualCamera->priority, above);
    return {};
}

Expected<void, CameraError> CameraWorldService::deactivate(Entity camera)
{
    World& world = m_cameras.world();
    VirtualCamera* virtualCamera = exists(world, camera) ? world.tryGet<VirtualCamera>(camera) : nullptr;
    if (virtualCamera == nullptr)
    {
        return unexpected(CameraError::NoCamera);
    }
    const auto entry = std::ranges::find_if(
        m_activations, [camera](const Activation& activation) { return activation.camera == camera; });
    if (entry == m_activations.end())
    {
        return unexpected(CameraError::NotActivated);
    }
    virtualCamera->priority = entry->priority;
    m_activations.erase(entry);
    return {};
}

Expected<void, CameraError> CameraWorldService::setPriority(Entity camera, i32 priority)
{
    World& world = m_cameras.world();
    VirtualCamera* virtualCamera = exists(world, camera) ? world.tryGet<VirtualCamera>(camera) : nullptr;
    if (virtualCamera == nullptr)
    {
        return unexpected(CameraError::NoCamera);
    }
    std::erase_if(m_activations, [camera](const Activation& entry) { return entry.camera == camera; });
    virtualCamera->priority = priority;
    return {};
}

Expected<void, CameraError> CameraWorldService::setFollow(Entity camera, Entity target)
{
    World& world = m_cameras.world();
    VirtualCamera* virtualCamera = exists(world, camera) ? world.tryGet<VirtualCamera>(camera) : nullptr;
    if (virtualCamera == nullptr)
    {
        return unexpected(CameraError::NoCamera);
    }
    if (!target.isNull() && !world.isAlive(target))
    {
        return unexpected(CameraError::InvalidArgument);
    }
    virtualCamera->follow = target;
    return {};
}

Expected<void, CameraError> CameraWorldService::setLookAt(Entity camera, Entity target)
{
    World& world = m_cameras.world();
    VirtualCamera* virtualCamera = exists(world, camera) ? world.tryGet<VirtualCamera>(camera) : nullptr;
    if (virtualCamera == nullptr)
    {
        return unexpected(CameraError::NoCamera);
    }
    if (!target.isNull() && !world.isAlive(target))
    {
        return unexpected(CameraError::InvalidArgument);
    }
    virtualCamera->lookAt = target;
    return {};
}

Expected<void, CameraError> CameraWorldService::setAngles(Entity camera, f32 yaw, f32 pitch)
{
    World& world = m_cameras.world();
    if (!exists(world, camera) || !world.has<VirtualCamera>(camera))
    {
        return unexpected(CameraError::NoCamera);
    }
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
    {
        return unexpected(CameraError::InvalidArgument);
    }
    VirtualCameraState* state = world.tryGet<VirtualCameraState>(camera);
    if (CameraOrbit* orbit = world.tryGet<CameraOrbit>(camera))
    {
        const f32 goalYaw = orbit->limitYaw
                                ? std::clamp(yaw, std::min(orbit->minYaw, orbit->maxYaw), orbit->maxYaw)
                                : wrapCameraAngle(yaw);
        const f32 goalPitch = std::clamp(pitch, std::min(orbit->minPitch, orbit->maxPitch), orbit->maxPitch);
        if (state != nullptr && state->orbit.initialised)
        {
            state->orbit.yaw = goalYaw;
            state->orbit.pitch = goalPitch;
        }
        else
        {
            orbit->yaw = goalYaw;
            orbit->pitch = goalPitch;
        }
        return {};
    }
    if (CameraSpringArm* arm = world.tryGet<CameraSpringArm>(camera))
    {
        const f32 goalYaw = wrapCameraAngle(yaw);
        const f32 goalPitch = std::clamp(pitch, std::min(arm->minPitch, arm->maxPitch), arm->maxPitch);
        if (state != nullptr)
        {
            state->armAngles = Vec2(goalYaw, goalPitch);
            state->armAnglesSet = true;
        }
        else
        {
            arm->yaw = goalYaw;
            arm->pitch = goalPitch;
        }
        return {};
    }
    return unexpected(CameraError::NoStage);
}

Expected<f32, CameraError> CameraWorldService::swapShoulder(Entity camera)
{
    World& world = m_cameras.world();
    if (!exists(world, camera) || !world.has<VirtualCamera>(camera))
    {
        return unexpected(CameraError::NoCamera);
    }
    CameraSpringArm* arm = world.tryGet<CameraSpringArm>(camera);
    if (arm == nullptr)
    {
        return unexpected(CameraError::NoStage);
    }
    arm->side = -arm->side;
    return arm->side;
}

Expected<void, CameraError> CameraWorldService::setFieldOfView(Entity camera, f32 fovY)
{
    World& world = m_cameras.world();
    VirtualCamera* virtualCamera = exists(world, camera) ? world.tryGet<VirtualCamera>(camera) : nullptr;
    if (virtualCamera == nullptr)
    {
        return unexpected(CameraError::NoCamera);
    }
    if (!(fovY > 0.0f) || !(fovY < kPi))
    {
        return unexpected(CameraError::InvalidArgument);
    }
    virtualCamera->fovY = fovY;
    return {};
}

Expected<void, CameraError> CameraWorldService::shake(const CameraShake& shake)
{
    return m_cameras.shake(shake);
}

Expected<void, CameraError> CameraWorldService::playImpulse(Entity source)
{
    const World& world = m_cameras.world();
    const CameraImpulse* impulse = exists(world, source) ? world.tryGet<CameraImpulse>(source) : nullptr;
    if (impulse == nullptr)
    {
        return unexpected(CameraError::NoImpulse);
    }
    const Transform* transform = world.tryGet<Transform>(source);
    return m_cameras.shake({transform != nullptr ? transform->position : Vec3(0.0f), impulse->strength,
                            impulse->radius, impulse->duration});
}

Expected<Entity, CameraError> CameraWorldService::liveCamera(Entity brain) const
{
    const World& world = m_cameras.world();
    if (!exists(world, brain) || !world.has<CameraBrain>(brain))
    {
        return unexpected(CameraError::NoBrain);
    }
    const CameraBrainState* state = world.tryGet<CameraBrainState>(brain);
    if (state == nullptr || !exists(world, state->live))
    {
        return kNullEntity;
    }
    return state->live;
}

Expected<f32, CameraError> CameraWorldService::blendProgress(Entity brain) const
{
    const World& world = m_cameras.world();
    if (!exists(world, brain) || !world.has<CameraBrain>(brain))
    {
        return unexpected(CameraError::NoBrain);
    }
    const CameraBrainState* state = world.tryGet<CameraBrainState>(brain);
    if (state == nullptr || !state->blending || !(state->blend.duration > 0.0f))
    {
        return 1.0f;
    }
    return std::clamp(state->elapsed / state->blend.duration, 0.0f, 1.0f);
}

} // namespace illusion

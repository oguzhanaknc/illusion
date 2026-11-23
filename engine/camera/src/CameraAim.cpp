#include <illusion/camera/CameraAim.hpp>
#include <illusion/camera/CameraDamping.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{

namespace
{

constexpr f32 kMinFov = 1e-4f;
constexpr f32 kMaxFov = 3.1414f;

struct LensSlopes
{
    f32 x;
    f32 y;
};

LensSlopes slopes(f32 fovY, f32 aspect) noexcept
{
    const f32 y = std::tan(0.5f * std::clamp(fovY, kMinFov, kMaxFov));
    return {y * std::max(aspect, 1e-4f), y};
}

// The screen correction of one axis for an offset of @p error from the composition point.
f32 correction(f32 error, f32 dead, f32 soft, f32 factor) noexcept
{
    const f32 magnitude = std::abs(error);
    if (magnitude <= dead)
    {
        return 0.0f;
    }
    const f32 hard = std::max(magnitude - soft, 0.0f);
    const f32 remaining = magnitude - hard - dead;
    return std::copysign(hard + remaining * factor, error);
}

// The roll-free rotation (yaw about world +y, then pitch about the camera's x) under which the unit world
// direction @p direction appears at @p screen.
Quat rotationPlacing(Vec3 direction, Vec2 screen, LensSlopes lens, const Quat& fallback) noexcept
{
    if (!(direction.x * direction.x + direction.z * direction.z > 1e-12f))
    {
        return fallback;
    }
    // In camera space the direction is (a, b, -1) up to length; the pitch must give it the world elevation.
    const f32 a = screen.x * lens.x;
    const f32 b = screen.y * lens.y;
    const f32 viewLength = std::sqrt(a * a + b * b + 1.0f);
    const f32 radius = std::sqrt(1.0f + b * b);
    const f32 sine = std::clamp(direction.y * viewLength / radius, -1.0f, 1.0f);
    const f32 pitch = std::asin(sine) - std::atan2(b, 1.0f);
    const f32 pitchedZ = b * std::sin(pitch) - std::cos(pitch);
    const f32 yaw = std::atan2(direction.x, direction.z) - std::atan2(a, pitchedZ);
    return glm::angleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(pitch, Vec3(1.0f, 0.0f, 0.0f));
}

} // namespace

Quat stepCameraLookAt(const CameraLookAtSettings& settings, CameraLookAtState& state,
                      const CameraAimInput& input, f32 dt) noexcept
{
    const Quat fallback = state.initialised ? state.rotation : input.bodyRotation;
    const Quat goal = cameraLookRotation(input.targetPosition - input.cameraPosition, fallback);
    state.rotation = state.initialised ? dampRotation(state.rotation, goal, settings.damping, dt) : goal;
    state.initialised = true;
    return state.rotation;
}

std::optional<Vec2> cameraScreenPosition(Vec3 position, const Quat& rotation, f32 fovY, f32 aspect,
                                         Vec3 point) noexcept
{
    const Vec3 local = glm::conjugate(rotation) * (point - position);
    if (!(local.z < -1e-6f))
    {
        return std::nullopt;
    }
    const LensSlopes lens = slopes(fovY, aspect);
    const f32 depth = -local.z;
    return Vec2(local.x / (depth * lens.x), local.y / (depth * lens.y));
}

Quat stepCameraComposer(const CameraComposerSettings& settings, CameraComposerState& state,
                        const CameraComposerInput& input, f32 dt) noexcept
{
    const Quat current = state.initialised ? state.rotation : input.aim.bodyRotation;
    const Vec3 toTarget = input.aim.targetPosition - input.aim.cameraPosition;
    const f32 distance = glm::length(toTarget);
    if (!(distance > 1e-6f))
    {
        state.rotation = current;
        state.initialised = true;
        return state.rotation;
    }
    const LensSlopes lens = slopes(input.fovY, input.aspect);
    const std::optional<Vec2> screen =
        state.initialised ? cameraScreenPosition(input.aim.cameraPosition, current, input.fovY, input.aspect,
                                                 input.aim.targetPosition)
                          : std::nullopt;
    Vec2 goal = settings.screenPosition;
    if (screen)
    {
        const Vec2 dead = glm::max(settings.deadZone, Vec2(0.0f));
        const Vec2 soft = glm::max(settings.softZone, dead);
        const Vec2 error = *screen - settings.screenPosition;
        const Vec2 shift(correction(error.x, dead.x, soft.x, dampFactor(settings.damping.x, dt)),
                         correction(error.y, dead.y, soft.y, dampFactor(settings.damping.y, dt)));
        if (shift.x == 0.0f && shift.y == 0.0f)
        {
            return state.rotation;
        }
        goal = *screen - shift;
    }
    state.rotation = rotationPlacing(toTarget / distance, goal, lens, current);
    state.initialised = true;
    return state.rotation;
}

} // namespace illusion

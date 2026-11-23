#include <illusion/camera/CameraDamping.hpp>

#include <cmath>

namespace illusion
{

f32 dampFactor(f32 smoothTime, f32 dt) noexcept
{
    if (!(smoothTime > 0.0f))
    {
        return 1.0f;
    }
    if (!(dt > 0.0f))
    {
        return 0.0f;
    }
    return 1.0f - std::exp(-dt / smoothTime);
}

void smoothDamp(f32& value, f32& velocity, f32 target, f32 smoothTime, f32 dt) noexcept
{
    if (!(smoothTime > 0.0f))
    {
        value = target;
        velocity = 0.0f;
        return;
    }
    if (!(dt > 0.0f))
    {
        return;
    }
    // x(t) = (x0 + (v0 + w x0) t) e^(-w t), the critically damped solution for the offset x from the target.
    const f32 omega = 2.0f / smoothTime;
    const f32 offset = value - target;
    const f32 decay = std::exp(-omega * dt);
    const f32 drive = (velocity + omega * offset) * dt;
    value = target + (offset + drive) * decay;
    velocity = (velocity - omega * drive) * decay;
}

void smoothDamp(Vec3& value, Vec3& velocity, Vec3 target, Vec3 smoothTime, f32 dt) noexcept
{
    for (glm::length_t axis = 0; axis < 3; ++axis)
    {
        smoothDamp(value[axis], velocity[axis], target[axis], smoothTime[axis], dt);
    }
}

Quat dampRotation(const Quat& current, const Quat& target, f32 smoothTime, f32 dt) noexcept
{
    const f32 factor = dampFactor(smoothTime, dt);
    if (factor >= 1.0f)
    {
        return target;
    }
    const Quat goal = glm::dot(current, target) < 0.0f ? -target : target;
    return glm::normalize(glm::slerp(current, goal, factor));
}

f32 wrapCameraAngle(f32 radians) noexcept
{
    constexpr f32 kPi = 3.14159265f;
    constexpr f32 kTwoPi = 6.28318531f;
    const f32 wrapped = radians - kTwoPi * std::floor((radians + kPi) / kTwoPi);
    return wrapped >= kPi ? wrapped - kTwoPi : wrapped;
}

} // namespace illusion

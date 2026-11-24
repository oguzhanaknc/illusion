#include <illusion/camera/CameraPose.hpp>

#include <cmath>

namespace illusion
{

Quat cameraViewRotation(const CameraPose& pose) noexcept
{
    return pose.rotation * glm::angleAxis(pose.dutch, Vec3(0.0f, 0.0f, 1.0f));
}

Quat cameraLookRotation(Vec3 direction, const Quat& fallback) noexcept
{
    const f32 length = glm::length(direction);
    if (!(length > 1e-6f))
    {
        return fallback;
    }
    const Vec3 forward = direction / length;
    if (std::abs(forward.y) > 0.99999f)
    {
        return fallback;
    }
    const Vec3 right = glm::normalize(glm::cross(forward, Vec3(0.0f, 1.0f, 0.0f)));
    const Vec3 up = glm::cross(right, forward);
    return glm::normalize(glm::quat_cast(Mat3(right, up, -forward)));
}

} // namespace illusion

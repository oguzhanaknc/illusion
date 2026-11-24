#include <illusion/camera/CameraBlend.hpp>

#include <algorithm>

namespace illusion
{

namespace
{

// One coordinate of the cubic Bezier from 0 through c1 and c2 to 1 at parameter s.
f32 bezier(f32 c1, f32 c2, f32 s) noexcept
{
    const f32 r = 1.0f - s;
    return 3.0f * r * r * s * c1 + 3.0f * r * s * s * c2 + s * s * s;
}

} // namespace

f32 cubicBezierEase(Vec2 control1, Vec2 control2, f32 t) noexcept
{
    const f32 x = std::clamp(t, 0.0f, 1.0f);
    const f32 c1x = std::clamp(control1.x, 0.0f, 1.0f);
    const f32 c2x = std::clamp(control2.x, 0.0f, 1.0f);
    // With both control x in [0, 1] the curve's x grows monotonically, so bisection finds the parameter.
    f32 low = 0.0f;
    f32 high = 1.0f;
    for (u32 i = 0; i < 32; ++i)
    {
        const f32 middle = 0.5f * (low + high);
        if (bezier(c1x, c2x, middle) < x)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return bezier(control1.y, control2.y, 0.5f * (low + high));
}

f32 cameraBlendWeight(const CameraBlend& blend, f32 elapsed) noexcept
{
    if (blend.style == CameraBlendStyle::Cut || !(blend.duration > 0.0f))
    {
        return 1.0f;
    }
    const f32 t = std::clamp(elapsed / blend.duration, 0.0f, 1.0f);
    switch (blend.style)
    {
    case CameraBlendStyle::Linear:
        return t;
    case CameraBlendStyle::EaseInOut:
        return t * t * (3.0f - 2.0f * t);
    case CameraBlendStyle::Curve:
        return cubicBezierEase(blend.control1, blend.control2, t);
    case CameraBlendStyle::Cut:
        break;
    }
    return 1.0f;
}

CameraPose blendCameraPoses(const CameraPose& from, const CameraPose& to, f32 t) noexcept
{
    const f32 weight = std::clamp(t, 0.0f, 1.0f);
    const Quat goal = glm::dot(from.rotation, to.rotation) < 0.0f ? -to.rotation : to.rotation;
    CameraPose pose;
    pose.position = glm::mix(from.position, to.position, weight);
    pose.rotation = glm::normalize(glm::slerp(from.rotation, goal, weight));
    pose.fovY = glm::mix(from.fovY, to.fovY, weight);
    pose.nearPlane = glm::mix(from.nearPlane, to.nearPlane, weight);
    pose.dutch = glm::mix(from.dutch, to.dutch, weight);
    return pose;
}

} // namespace illusion

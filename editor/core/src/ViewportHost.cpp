#include <illusion/editor/core/ViewportHost.hpp>

#include <algorithm>
#include <cmath>

namespace illusion::editor
{
namespace
{

// Orthographic views show this far in front of and behind the eye.
constexpr f32 kOrthographicDepth = 10000.0f;

f32 halfFieldTangent(const ViewportCamera& camera) noexcept
{
    return std::tan(glm::radians(std::clamp(camera.fieldOfView, 1.0f, 170.0f)) * 0.5f);
}

} // namespace

Quat viewportRotation(const ViewportCamera& camera) noexcept
{
    return glm::angleAxis(glm::radians(camera.yaw), Vec3(0.0f, 1.0f, 0.0f)) *
           glm::angleAxis(glm::radians(camera.pitch), Vec3(1.0f, 0.0f, 0.0f));
}

Vec3 viewportForward(const ViewportCamera& camera) noexcept
{
    return glm::normalize(viewportRotation(camera) * Vec3(0.0f, 0.0f, -1.0f));
}

Vec3 viewportEye(const ViewportCamera& camera) noexcept
{
    return camera.pivot - viewportForward(camera) * camera.distance;
}

Mat4 viewportViewMatrix(const ViewportCamera& camera) noexcept
{
    const Mat4 world =
        glm::translate(Mat4(1.0f), viewportEye(camera)) * glm::mat4_cast(viewportRotation(camera));
    return glm::inverse(world);
}

Mat4 viewportProjectionMatrix(const ViewportCamera& camera, f32 aspect) noexcept
{
    const f32 safeAspect = aspect > 0.0f && std::isfinite(aspect) ? aspect : 1.0f;
    if (camera.projection == ViewportProjection::Orthographic)
    {
        const f32 halfHeight =
            std::clamp(camera.orthographicHeight, kViewportMinExtent, kViewportMaxExtent) * 0.5f;
        const f32 halfWidth = halfHeight * safeAspect;
        return orthographicReversedZ(-halfWidth, halfWidth, -halfHeight, halfHeight, -kOrthographicDepth,
                                     std::max(camera.distance, 0.0f) + kOrthographicDepth);
    }
    const f32 nearPlane = std::clamp(camera.distance * 0.01f, 0.01f, 1.0f);
    return perspectiveReversedZ(glm::radians(std::clamp(camera.fieldOfView, 1.0f, 170.0f)), safeAspect,
                                nearPlane);
}

ViewportCamera viewportPreset(const ViewportCamera& camera, ViewportView view) noexcept
{
    ViewportCamera out = camera;
    if (view == ViewportView::Perspective)
    {
        if (camera.projection == ViewportProjection::Orthographic)
        {
            out.distance = std::clamp(camera.orthographicHeight * 0.5f / halfFieldTangent(camera),
                                      kViewportMinExtent, kViewportMaxExtent);
        }
        out.projection = ViewportProjection::Perspective;
        return out;
    }
    if (camera.projection == ViewportProjection::Perspective)
    {
        out.orthographicHeight = std::clamp(2.0f * camera.distance * halfFieldTangent(camera),
                                            kViewportMinExtent, kViewportMaxExtent);
    }
    out.projection = ViewportProjection::Orthographic;
    switch (view)
    {
    case ViewportView::Top:
        out.yaw = 0.0f;
        out.pitch = -90.0f;
        break;
    case ViewportView::Bottom:
        out.yaw = 0.0f;
        out.pitch = 90.0f;
        break;
    case ViewportView::Front:
        out.yaw = 0.0f;
        out.pitch = 0.0f;
        break;
    case ViewportView::Back:
        out.yaw = 180.0f;
        out.pitch = 0.0f;
        break;
    case ViewportView::Left:
        out.yaw = -90.0f;
        out.pitch = 0.0f;
        break;
    case ViewportView::Right:
        out.yaw = 90.0f;
        out.pitch = 0.0f;
        break;
    case ViewportView::Perspective:
        break;
    }
    return out;
}

std::optional<ViewportCamera> aimViewportCamera(const ViewportCamera& camera, Vec3 eye) noexcept
{
    const Vec3 toPivot = camera.pivot - eye;
    const f32 length = glm::length(toPivot);
    if (!std::isfinite(length) || length < kViewportMinExtent)
    {
        return std::nullopt;
    }
    const Vec3 forward = toPivot / length;
    ViewportCamera out = camera;
    out.distance = std::min(length, kViewportMaxExtent);
    out.pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    // Straight up or down the yaw is free: keep the current one.
    if (std::abs(forward.x) > 1e-6f || std::abs(forward.z) > 1e-6f)
    {
        out.yaw = glm::degrees(std::atan2(-forward.x, -forward.z));
    }
    return out;
}

ViewportPoint projectToViewport(const Mat4& viewProjection, UVec2 size, Vec3 point) noexcept
{
    ViewportPoint out;
    if (size.x == 0 || size.y == 0)
    {
        return out;
    }
    const f32 width = static_cast<f32>(size.x);
    const f32 height = static_cast<f32>(size.y);
    const Vec4 clip = viewProjection * Vec4(point, 1.0f);
    if (!(clip.w > 0.0f))
    {
        return out;
    }
    const Vec3 ndc = Vec3(clip) / clip.w;
    out.pixel = Vec2((ndc.x * 0.5f + 0.5f) * width, (0.5f - ndc.y * 0.5f) * height);
    out.depth = ndc.z;
    out.inFront = ndc.z >= 0.0f && ndc.z <= 1.0f;
    out.inside = out.inFront && out.pixel.x >= 0.0f && out.pixel.x < width && out.pixel.y >= 0.0f &&
                 out.pixel.y < height;
    return out;
}

ViewportPoint projectToViewport(const ViewportCamera& camera, UVec2 size, Vec3 point) noexcept
{
    if (size.x == 0 || size.y == 0)
    {
        return {};
    }
    const f32 aspect = static_cast<f32>(size.x) / static_cast<f32>(size.y);
    return projectToViewport(viewportProjectionMatrix(camera, aspect) * viewportViewMatrix(camera), size,
                             point);
}

UVec2 viewportImageSize(UVec2 panel, const ViewportSettings& settings) noexcept
{
    if (panel.x == 0 || panel.y == 0)
    {
        return UVec2(0);
    }
    f32 ratio = 0.0f;
    switch (settings.aspect)
    {
    case ViewportAspect::Free:
        return panel;
    case ViewportAspect::Wide16x9:
        ratio = 16.0f / 9.0f;
        break;
    case ViewportAspect::Wide16x10:
        ratio = 16.0f / 10.0f;
        break;
    case ViewportAspect::Standard4x3:
        ratio = 4.0f / 3.0f;
        break;
    case ViewportAspect::Ultrawide21x9:
        ratio = 21.0f / 9.0f;
        break;
    case ViewportAspect::Square:
        ratio = 1.0f;
        break;
    case ViewportAspect::Fixed:
        if (settings.width != 0 && settings.height != 0)
        {
            return UVec2(settings.width, settings.height);
        }
        return panel;
    }
    if (ratio == 0.0f)
    {
        return panel;
    }
    const f32 width = static_cast<f32>(panel.x);
    const f32 height = static_cast<f32>(panel.y);
    if (width / height > ratio)
    {
        return UVec2(std::max(static_cast<u32>(height * ratio + 0.5f), 1u), panel.y);
    }
    return UVec2(panel.x, std::max(static_cast<u32>(width / ratio + 0.5f), 1u));
}

} // namespace illusion::editor

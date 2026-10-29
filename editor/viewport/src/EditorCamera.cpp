#include <illusion/editor/viewport/EditorCamera.hpp>

#include <algorithm>
#include <cmath>

namespace illusion::editor::viewport
{
namespace
{

// Bounds smaller than this are framed as if they were this big.
constexpr f32 kMinFrameRadius = 0.25f;
constexpr f32 kFrameMargin = 1.15f;

bool finite(Vec3 value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

f32 finiteOr(f32 value, f32 fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

f32 easeInOut(f32 t) noexcept
{
    const f32 x = std::clamp(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

f32 geometric(f32 from, f32 to, f32 t) noexcept
{
    return std::exp(std::lerp(std::log(from), std::log(to), t));
}

// Moves the eye in camera space by @p offset (x right, y up, z forward) together with the pivot.
void translateCamera(ViewportCamera& camera, Vec3 offset) noexcept
{
    const Quat rotation = viewportRotation(camera);
    camera.pivot += rotation * Vec3(offset.x, offset.y, -offset.z);
}

} // namespace

NavigationPreset navigationPresetFromName(std::string_view name) noexcept
{
    if (name == "unity")
    {
        return NavigationPreset::Unity;
    }
    if (name == "unreal")
    {
        return NavigationPreset::Unreal;
    }
    if (name == "blender")
    {
        return NavigationPreset::Blender;
    }
    return NavigationPreset::Illusion;
}

NavigationMode navigationMode(NavigationPreset preset, u32 button, const ViewportInput& input) noexcept
{
    if (button == 0 && (input.orbit || input.alt))
    {
        return NavigationMode::Orbit;
    }
    switch (preset)
    {
    case NavigationPreset::Illusion:
    case NavigationPreset::Unity:
    case NavigationPreset::Unreal:
        if (button == 0)
        {
            return preset == NavigationPreset::Unreal ? NavigationMode::Walk : NavigationMode::None;
        }
        if (button == 1)
        {
            return input.alt ? NavigationMode::Dolly : NavigationMode::Fly;
        }
        return button == 2 ? NavigationMode::Pan : NavigationMode::None;
    case NavigationPreset::Blender:
        if (button != 2)
        {
            return NavigationMode::None;
        }
        return input.shift ? NavigationMode::Pan : input.ctrl ? NavigationMode::Dolly : NavigationMode::Orbit;
    }
    return NavigationMode::None;
}

ViewportCamera clampCamera(const ViewportCamera& camera) noexcept
{
    const ViewportCamera defaults;
    ViewportCamera out = camera;
    out.pivot = finite(camera.pivot)
                    ? glm::clamp(camera.pivot, Vec3(-kViewportMaxExtent), Vec3(kViewportMaxExtent))
                    : defaults.pivot;
    out.distance =
        std::clamp(finiteOr(camera.distance, defaults.distance), kViewportMinExtent, kViewportMaxExtent);
    out.yaw = std::remainder(finiteOr(camera.yaw, defaults.yaw), 360.0f);
    out.pitch = std::clamp(finiteOr(camera.pitch, defaults.pitch), -90.0f, 90.0f);
    out.fieldOfView = std::clamp(finiteOr(camera.fieldOfView, defaults.fieldOfView), 1.0f, 170.0f);
    out.orthographicHeight = std::clamp(finiteOr(camera.orthographicHeight, defaults.orthographicHeight),
                                        kViewportMinExtent, kViewportMaxExtent);
    return out;
}

ViewportCamera interpolateCamera(const ViewportCamera& from, const ViewportCamera& to, f32 t) noexcept
{
    const f32 s = easeInOut(t);
    ViewportCamera out = to;
    out.pivot = glm::mix(from.pivot, to.pivot, s);
    out.distance = geometric(from.distance, to.distance, s);
    out.orthographicHeight = geometric(from.orthographicHeight, to.orthographicHeight, s);
    out.yaw = from.yaw + std::remainder(to.yaw - from.yaw, 360.0f) * s;
    out.pitch = std::lerp(from.pitch, to.pitch, s);
    out.fieldOfView = std::lerp(from.fieldOfView, to.fieldOfView, s);
    return out;
}

ViewportCamera frameBounds(const ViewportCamera& camera, const Aabb& bounds, f32 aspect) noexcept
{
    ViewportCamera out = clampCamera(camera);
    const Aabb box = bounds.isValid() ? bounds : Aabb{Vec3(-1.0f), Vec3(1.0f)};
    const f32 radius = std::max(glm::length(box.extents()), kMinFrameRadius) * kFrameMargin;
    const f32 safeAspect = aspect > 0.0f && std::isfinite(aspect) ? aspect : 1.0f;
    const f32 halfVertical = glm::radians(out.fieldOfView) * 0.5f;
    const f32 halfHorizontal = std::atan(std::tan(halfVertical) * safeAspect);
    out.pivot = box.center();
    out.distance = std::clamp(radius / std::sin(std::min(halfVertical, halfHorizontal)), kViewportMinExtent,
                              kViewportMaxExtent);
    out.orthographicHeight =
        std::clamp(2.0f * radius * std::max(1.0f, 1.0f / safeAspect), kViewportMinExtent, kViewportMaxExtent);
    return clampCamera(out);
}

EditorCamera::EditorCamera(const ViewportCamera& camera, NavigationSettings settings) noexcept
    : m_settings(settings)
    , m_camera(clampCamera(camera))
    , m_target(m_camera)
    , m_from(m_camera)
{
}

void EditorCamera::set(const ViewportCamera& camera, bool animate) noexcept
{
    m_target = clampCamera(camera);
    if (!animate || m_settings.transitionSeconds <= 0.0f)
    {
        m_camera = m_target;
        m_elapsed = 0.0f;
        m_duration = 0.0f;
        return;
    }
    m_from = m_camera;
    m_elapsed = 0.0f;
    m_duration = m_settings.transitionSeconds;
}

f32 EditorCamera::unitsPerPixel(u32 height) const noexcept
{
    const f32 pixels = static_cast<f32>(std::max(height, 1u));
    if (m_camera.projection == ViewportProjection::Orthographic)
    {
        return m_camera.orthographicHeight / pixels;
    }
    return 2.0f * m_camera.distance * std::tan(glm::radians(m_camera.fieldOfView) * 0.5f) / pixels;
}

void EditorCamera::advance(f32 deltaSeconds) noexcept
{
    if (!animating())
    {
        return;
    }
    m_elapsed += std::max(deltaSeconds, 0.0f);
    m_camera =
        m_elapsed >= m_duration ? m_target : interpolateCamera(m_from, m_target, m_elapsed / m_duration);
}

void EditorCamera::startDrag(const ViewportInput& input) noexcept
{
    const auto pressed = [&](u32 button) {
        return input.buttons[button] && !m_buttons[button];
    };
    if (!input.hovered)
    {
        return;
    }
    for (u32 button = 0; button < 3; ++button)
    {
        if (!pressed(button))
        {
            continue;
        }
        const NavigationMode mode = navigationMode(m_settings.preset, button, input);
        if (mode != NavigationMode::None)
        {
            m_mode = mode;
            m_modeButton = button;
            return;
        }
    }
}

bool EditorCamera::update(const ViewportInput& input, UVec2 size) noexcept
{
    if (m_mode != NavigationMode::None && !input.buttons[m_modeButton])
    {
        m_mode = NavigationMode::None;
    }
    if (m_mode == NavigationMode::None)
    {
        startDrag(input);
    }
    m_buttons = input.buttons;

    const Vec2 delta = finite(Vec3(input.cursorDelta, 0.0f)) ? input.cursorDelta : Vec2(0.0f);
    const f32 wheel = input.hovered && std::isfinite(input.wheel) ? input.wheel : 0.0f;
    const f32 dt = std::isfinite(input.deltaSeconds) ? std::clamp(input.deltaSeconds, 0.0f, 0.25f) : 0.0f;
    const bool moving = m_mode != NavigationMode::None || wheel != 0.0f;
    if (!moving)
    {
        advance(dt);
        return false;
    }
    // Navigation takes over from an animated move where it is.
    m_target = m_camera;
    m_elapsed = 0.0f;
    m_duration = 0.0f;

    ViewportCamera camera = m_camera;
    switch (m_mode)
    {
    case NavigationMode::Orbit:
        camera.yaw -= delta.x * m_settings.orbitDegreesPerPixel;
        camera.pitch -= delta.y * m_settings.orbitDegreesPerPixel;
        break;
    case NavigationMode::Pan:
    {
        const f32 units = unitsPerPixel(size.y);
        translateCamera(camera, Vec3(-delta.x * units, delta.y * units, 0.0f));
        break;
    }
    case NavigationMode::Dolly:
    {
        const f32 factor = std::exp((delta.y - delta.x) * m_settings.dollyPerPixel);
        camera.distance *= factor;
        camera.orthographicHeight *= factor;
        break;
    }
    case NavigationMode::Fly:
    {
        // Look around the eye: the pivot follows at the same distance.
        const Vec3 eye = viewportEye(camera);
        camera.yaw -= delta.x * m_settings.lookDegreesPerPixel;
        camera.pitch = std::clamp(camera.pitch - delta.y * m_settings.lookDegreesPerPixel, -90.0f, 90.0f);
        camera.pivot = eye + viewportForward(camera) * camera.distance;
        if (wheel != 0.0f)
        {
            m_flySpeed = std::clamp(m_flySpeed * std::pow(1.25f, wheel), kMinFlySpeed, kMaxFlySpeed);
        }
        const Vec3 move = glm::clamp(finite(input.move) ? input.move : Vec3(0.0f), Vec3(-1.0f), Vec3(1.0f));
        const f32 speed = m_flySpeed * (input.shift ? m_settings.fastMultiplier : 1.0f);
        translateCamera(camera, move * speed * dt);
        break;
    }
    case NavigationMode::Walk:
    {
        // Turn around the eye, then move along the ground in the direction the camera faces.
        const Vec3 eye = viewportEye(camera);
        camera.yaw -= delta.x * m_settings.lookDegreesPerPixel;
        camera.pivot = eye + viewportForward(camera) * camera.distance;
        const f32 yaw = glm::radians(camera.yaw);
        const Vec3 ahead(-std::sin(yaw), 0.0f, -std::cos(yaw));
        camera.pivot += ahead * (-delta.y * unitsPerPixel(size.y) * m_settings.walkPerPixel);
        break;
    }
    case NavigationMode::None:
        break;
    }
    if (m_mode != NavigationMode::Fly && wheel != 0.0f)
    {
        const f32 factor = std::pow(m_settings.dollyPerWheelStep, wheel);
        camera.distance *= factor;
        camera.orthographicHeight *= factor;
    }
    m_camera = clampCamera(camera);
    m_target = m_camera;
    return true;
}

} // namespace illusion::editor::viewport

#include <illusion/editor/core/ViewCube.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace illusion::editor
{
namespace
{

// Axis of each face: +x, -x, +y, -y, +z, -z.
constexpr std::array<Vec3, 6> kNormals = {Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f),
                                          Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
                                          Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)};
// The view that looks at each face (from outside, along -normal).
constexpr std::array<ViewportView, 6> kViews = {ViewportView::Right, ViewportView::Left,
                                                ViewportView::Top,   ViewportView::Bottom,
                                                ViewportView::Front, ViewportView::Back};

Vec3 toView(const Quat& rotation, Vec3 point) noexcept
{
    return glm::conjugate(rotation) * point;
}

f32 snap(f32 value) noexcept
{
    if (value >= 1.0f - kViewCubeEdge)
    {
        return 1.0f;
    }
    if (value <= -(1.0f - kViewCubeEdge))
    {
        return -1.0f;
    }
    return 0.0f;
}

} // namespace

Vec2 viewCubeProject(const Quat& rotation, Vec3 point) noexcept
{
    const Vec3 view = toView(rotation, point);
    return Vec2(view.x, -view.y);
}

u32 viewCubeFaces(const Quat& rotation, std::span<ViewCubeFace, 6> out) noexcept
{
    u32 count = 0;
    for (usize i = 0; i < kNormals.size(); ++i)
    {
        const Vec3 normal = kNormals[i];
        // The camera looks along -z in view space: faces towards it have a positive z.
        const f32 facing = toView(rotation, normal).z;
        if (facing <= 1e-4f)
        {
            continue;
        }
        // Two axes in the face's plane.
        const Vec3 u = normal.x != 0.0f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
        const Vec3 v = glm::cross(normal, u);
        ViewCubeFace& face = out[count++];
        face.normal = normal;
        face.view = kViews[i];
        face.facing = std::min(facing, 1.0f);
        face.center = viewCubeProject(rotation, normal);
        face.corners = {viewCubeProject(rotation, normal - u - v), viewCubeProject(rotation, normal + u - v),
                        viewCubeProject(rotation, normal + u + v), viewCubeProject(rotation, normal - u + v)};
    }
    std::sort(out.begin(), out.begin() + count,
              [](const ViewCubeFace& a, const ViewCubeFace& b) { return a.facing < b.facing; });
    return count;
}

std::array<ViewAxisEnd, 6> viewAxisEnds(const Quat& rotation) noexcept
{
    std::array<ViewAxisEnd, 6> ends{};
    for (usize i = 0; i < kNormals.size(); ++i)
    {
        ends[i].direction = kNormals[i];
        ends[i].point = viewCubeProject(rotation, kNormals[i]);
        ends[i].depth = toView(rotation, kNormals[i]).z;
    }
    std::sort(ends.begin(), ends.end(),
              [](const ViewAxisEnd& a, const ViewAxisEnd& b) { return a.depth < b.depth; });
    return ends;
}

std::optional<Vec3> viewCubePick(const Quat& rotation, Vec2 point) noexcept
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y))
    {
        return std::nullopt;
    }
    // A ray from in front of the cube along the view direction, in cube space.
    const Vec3 origin = rotation * Vec3(point.x, -point.y, 4.0f);
    const Vec3 direction = rotation * Vec3(0.0f, 0.0f, -1.0f);
    f32 enter = -std::numeric_limits<f32>::infinity();
    f32 leave = std::numeric_limits<f32>::infinity();
    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(direction[axis]) < 1e-6f)
        {
            if (std::abs(origin[axis]) > 1.0f)
            {
                return std::nullopt;
            }
            continue;
        }
        const f32 a = (-1.0f - origin[axis]) / direction[axis];
        const f32 b = (1.0f - origin[axis]) / direction[axis];
        enter = std::max(enter, std::min(a, b));
        leave = std::min(leave, std::max(a, b));
    }
    if (enter > leave || leave < 0.0f)
    {
        return std::nullopt;
    }
    const Vec3 hit = origin + direction * enter;
    Vec3 out(snap(hit.x), snap(hit.y), snap(hit.z));
    if (out == Vec3(0.0f))
    {
        // Rounding put the hit just inside the face: take its largest component.
        const Vec3 size = glm::abs(hit);
        const int axis = size.x >= size.y && size.x >= size.z ? 0 : (size.y >= size.z ? 1 : 2);
        out[axis] = hit[axis] < 0.0f ? -1.0f : 1.0f;
    }
    return out;
}

std::optional<ViewportView> viewCubeView(Vec3 direction) noexcept
{
    for (usize i = 0; i < kNormals.size(); ++i)
    {
        if (direction == kNormals[i])
        {
            return kViews[i];
        }
    }
    return std::nullopt;
}

ViewportCamera viewCubeCamera(const ViewportCamera& camera, Vec3 direction) noexcept
{
    ViewportCamera out = camera;
    const f32 length = glm::length(direction);
    if (!(length > 1e-6f) || !std::isfinite(length))
    {
        return out;
    }
    // The camera looks from the direction towards the pivot: forward = -direction.
    const Vec3 forward = -direction / length;
    out.pitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
    // Straight up or down keeps the yaw.
    if (std::abs(forward.x) > 1e-5f || std::abs(forward.z) > 1e-5f)
    {
        out.yaw = glm::degrees(std::atan2(-forward.x, -forward.z));
    }
    return out;
}

} // namespace illusion::editor

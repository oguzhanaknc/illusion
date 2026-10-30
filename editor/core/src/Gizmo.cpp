#include "commands/CommandSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/Gizmo.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace illusion::editor
{
namespace
{

constexpr f32 kPi = std::numbers::pi_v<f32>;
// Axes closer to the view direction than kHiddenDot (cosine) are hidden; they fade from kFadeDot.
constexpr f32 kFadeDot = 0.9f;
constexpr f32 kHiddenDot = 0.99f;
// Planes seen more edge on than kPlaneHidden (cosine of their normal) are hidden; they fade until kPlaneFade.
constexpr f32 kPlaneHidden = 0.08f;
constexpr f32 kPlaneFade = 0.25f;
// Rings seen more edge on than this turn with the cursor along their tangent instead of on their plane.
constexpr f32 kRingPlaneDot = 0.2f;
// Inner ends of the axis lines and the plane squares, as fractions of the axis length.
constexpr f32 kTranslateInner = 0.2f;
constexpr f32 kScaleInner = 0.15f;
constexpr f32 kPlaneNear = 0.25f;
constexpr f32 kPlaneFar = 0.5f;
// Ring radii as fractions of the axis length, and the view ring outside them.
constexpr f32 kUniversalRing = 0.75f;
constexpr f32 kViewRing = 1.15f;
// Half sizes of the centre box.
constexpr f32 kCenterBox = 0.08f;

constexpr std::array<Vec3, 3> kUnitAxes = {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
                                           Vec3(0.0f, 0.0f, 1.0f)};

struct Projected
{
    Vec2 pixel{0.0f};
    bool valid = false;
};

Projected project(const GizmoCamera& camera, Vec3 point) noexcept
{
    // Relative to the eye first, so that gizmos far from the origin keep their precision.
    const Vec3 view = glm::inverse(camera.rotation) * (point - camera.eye);
    const Vec4 clip = camera.projection * Vec4(view, 1.0f);
    if (!(clip.w > 1e-6f))
    {
        return {};
    }
    const Vec2 ndc = Vec2(clip) / clip.w;
    const Vec2 size(camera.size);
    return {Vec2((ndc.x * 0.5f + 0.5f) * size.x, (0.5f - ndc.y * 0.5f) * size.y), true};
}

Vec3 forwardOf(const GizmoCamera& camera) noexcept
{
    return camera.rotation * Vec3(0.0f, 0.0f, -1.0f);
}

// Unit direction from @p point towards the camera.
Vec3 towardCamera(const GizmoCamera& camera, Vec3 point) noexcept
{
    if (camera.orthographic)
    {
        return -forwardOf(camera);
    }
    const Vec3 toward = camera.eye - point;
    const f32 length = glm::length(toward);
    return length > 1e-6f ? toward / length : -forwardOf(camera);
}

// World units per image pixel at @p point (0 when it is behind a perspective camera).
f32 worldPerPixel(const GizmoCamera& camera, Vec3 point) noexcept
{
    const f32 scale = camera.projection[1][1] * static_cast<f32>(std::max(camera.size.y, 1u));
    if (!(std::abs(scale) > 1e-12f))
    {
        return 0.0f;
    }
    if (camera.orthographic)
    {
        return 2.0f / std::abs(scale);
    }
    const f32 depth = glm::dot(point - camera.eye, forwardOf(camera));
    return depth > 0.0f ? 2.0f * depth / std::abs(scale) : 0.0f;
}

f32 smoothstep(f32 edge0, f32 edge1, f32 x) noexcept
{
    const f32 t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::optional<Vec3> rayPlane(const GizmoRay& ray, Vec3 point, Vec3 normal, bool behind) noexcept
{
    const f32 denominator = glm::dot(ray.direction, normal);
    if (!(std::abs(denominator) > 1e-6f))
    {
        return std::nullopt;
    }
    const f32 t = glm::dot(point - ray.origin, normal) / denominator;
    if (!std::isfinite(t) || (!behind && t < 0.0f))
    {
        return std::nullopt;
    }
    return ray.origin + ray.direction * t;
}

f32 segmentDistanceSquared(Vec2 point, Vec2 a, Vec2 b) noexcept
{
    const Vec2 ab = b - a;
    const f32 lengthSquared = glm::dot(ab, ab);
    const f32 t =
        lengthSquared > 1e-12f ? std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    const Vec2 away = point - (a + ab * t);
    return glm::dot(away, away);
}

f32 segmentDistance(Vec2 point, Vec2 a, Vec2 b) noexcept
{
    return std::sqrt(segmentDistanceSquared(point, a, b));
}

f32 cross2(Vec2 a, Vec2 b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

bool insideQuad(Vec2 point, const std::array<Vec2, 4>& corners) noexcept
{
    bool positive = false;
    bool negative = false;
    for (usize i = 0; i < corners.size(); ++i)
    {
        const f32 side = cross2(corners[(i + 1) % corners.size()] - corners[i], point - corners[i]);
        positive = positive || side > 0.0f;
        negative = negative || side < 0.0f;
    }
    return !(positive && negative);
}

// Wraps an angle difference into [-pi, pi].
f32 wrapAngle(f32 angle) noexcept
{
    while (angle > kPi)
    {
        angle -= 2.0f * kPi;
    }
    while (angle < -kPi)
    {
        angle += 2.0f * kPi;
    }
    return angle;
}

// Cosines and sines of the ring points and of the middles of the ring segments.
struct RingTable
{
    std::array<f32, kGizmoRingSegments> cosines{};
    std::array<f32, kGizmoRingSegments> sines{};
    std::array<f32, kGizmoRingSegments> middleCosines{};
    std::array<f32, kGizmoRingSegments> middleSines{};
};

const RingTable& ringTable() noexcept
{
    static const RingTable table = [] {
        RingTable out;
        const f32 step = 2.0f * kPi / static_cast<f32>(kGizmoRingSegments);
        for (u32 k = 0; k < kGizmoRingSegments; ++k)
        {
            out.cosines[k] = std::cos(step * static_cast<f32>(k));
            out.sines[k] = std::sin(step * static_cast<f32>(k));
            out.middleCosines[k] = std::cos(step * (static_cast<f32>(k) + 0.5f));
            out.middleSines[k] = std::sin(step * (static_cast<f32>(k) + 0.5f));
        }
        return out;
    }();
    return table;
}

bool hasLines(GizmoTool tool) noexcept
{
    return tool == GizmoTool::Translate || tool == GizmoTool::Scale || tool == GizmoTool::Universal;
}

bool hasRings(GizmoTool tool) noexcept
{
    return tool == GizmoTool::Rotate || tool == GizmoTool::Universal;
}

f32 ringRadius(GizmoTool tool) noexcept
{
    return tool == GizmoTool::Universal ? kUniversalRing : 1.0f;
}

GizmoHandle offset(GizmoHandle first, u32 axis) noexcept
{
    return static_cast<GizmoHandle>(static_cast<u32>(first) + axis);
}

} // namespace

std::optional<u32> gizmoHandleAxis(GizmoHandle handle) noexcept
{
    const u32 value = static_cast<u32>(handle);
    const auto in = [&](GizmoHandle first) {
        return value >= static_cast<u32>(first) && value < static_cast<u32>(first) + 3;
    };
    for (const GizmoHandle first :
         {GizmoHandle::TranslateX, GizmoHandle::TranslateYZ, GizmoHandle::RotateX, GizmoHandle::ScaleX})
    {
        if (in(first))
        {
            return value - static_cast<u32>(first);
        }
    }
    return std::nullopt;
}

GizmoCamera gizmoCamera(const ViewportCamera& camera, UVec2 size, f32 pixelScale) noexcept
{
    GizmoCamera out;
    out.eye = viewportEye(camera);
    out.rotation = viewportRotation(camera);
    out.size = size;
    const f32 aspect = size.y != 0 ? static_cast<f32>(size.x) / static_cast<f32>(size.y) : 1.0f;
    out.projection = viewportProjectionMatrix(camera, aspect);
    out.orthographic = camera.projection == ViewportProjection::Orthographic;
    out.pixelScale = std::isfinite(pixelScale) ? std::clamp(pixelScale, 0.25f, 8.0f) : 1.0f;
    return out;
}

GizmoRay gizmoRay(const GizmoCamera& camera, Vec2 pixel) noexcept
{
    const Vec2 size = glm::max(Vec2(camera.size), Vec2(1.0f));
    const Vec2 ndc((pixel.x / size.x) * 2.0f - 1.0f, 1.0f - (pixel.y / size.y) * 2.0f);
    const Mat4& p = camera.projection;
    const Vec2 view((ndc.x - p[3][0]) / p[0][0], (ndc.y - p[3][1]) / p[1][1]);
    GizmoRay ray;
    if (camera.orthographic)
    {
        ray.origin = camera.eye + camera.rotation * Vec3(view, 0.0f);
        ray.direction = forwardOf(camera);
        return ray;
    }
    ray.origin = camera.eye;
    ray.direction = glm::normalize(camera.rotation * Vec3(view, -1.0f));
    return ray;
}

std::optional<GizmoPose> gizmoPose(std::span<const Transform> placements, usize active, GizmoTool tool,
                                   GizmoSpace space, GizmoPivot pivot, const GizmoCamera& camera) noexcept
{
    if (placements.empty())
    {
        return std::nullopt;
    }
    const Transform& primary = placements[std::min(active, placements.size() - 1)];
    GizmoPose pose;
    if (pivot == GizmoPivot::Active)
    {
        pose.origin = primary.position;
    }
    else if (pivot == GizmoPivot::BoundsCenter)
    {
        Vec3 low = placements[0].position;
        Vec3 high = low;
        for (const Transform& placement : placements)
        {
            low = glm::min(low, placement.position);
            high = glm::max(high, placement.position);
        }
        pose.origin = (low + high) * 0.5f;
    }
    else
    {
        Vec3 sum(0.0f);
        for (const Transform& placement : placements)
        {
            sum += placement.position;
        }
        pose.origin = sum / static_cast<f32>(placements.size());
    }
    if (tool == GizmoTool::Scale || space == GizmoSpace::Local)
    {
        pose.orientation = primary.rotation;
    }
    else if (space == GizmoSpace::View)
    {
        pose.orientation = camera.rotation;
    }
    const f32 length = glm::length(pose.orientation);
    pose.orientation =
        length > 1e-6f && std::isfinite(length) ? pose.orientation / length : Quat(1.0f, 0.0f, 0.0f, 0.0f);
    return pose;
}

GizmoShape gizmoShape(GizmoTool tool, const GizmoCamera& camera, const GizmoPose& pose) noexcept
{
    GizmoShape shape;
    shape.tool = tool;
    if (tool == GizmoTool::Select || tool == GizmoTool::Measure || camera.size.x == 0 || camera.size.y == 0)
    {
        return shape;
    }
    const f32 perPixel = worldPerPixel(camera, pose.origin);
    const Projected center = project(camera, pose.origin);
    if (!(perPixel > 0.0f) || !center.valid)
    {
        return shape;
    }
    shape.visible = true;
    shape.center = center.pixel;
    shape.length = kGizmoSize * camera.pixelScale;
    shape.tolerance = kGizmoHitTolerance * camera.pixelScale;
    shape.worldLength = shape.length * perPixel;
    for (u32 i = 0; i < 3; ++i)
    {
        shape.axes[i] = pose.orientation * kUnitAxes[i];
    }
    const Vec3 toward = towardCamera(camera, pose.origin);
    const f32 world = shape.worldLength;

    if (hasLines(tool))
    {
        const f32 inner = tool == GizmoTool::Scale ? kScaleInner : kTranslateInner;
        for (u32 i = 0; i < 3; ++i)
        {
            const Vec3 axis = shape.axes[i];
            const Projected start = project(camera, pose.origin + axis * (world * inner));
            const Projected end = project(camera, pose.origin + axis * world);
            GizmoAxisShape& line = shape.lines[i];
            line.start = start.pixel;
            line.end = end.pixel;
            line.visibility = start.valid && end.valid
                                  ? 1.0f - smoothstep(kFadeDot, kHiddenDot, std::abs(glm::dot(axis, toward)))
                                  : 0.0f;
        }
    }
    if (tool == GizmoTool::Translate || tool == GizmoTool::Universal)
    {
        for (u32 i = 0; i < 3; ++i)
        {
            const Vec3 u = shape.axes[(i + 1) % 3] * world;
            const Vec3 v = shape.axes[(i + 2) % 3] * world;
            const std::array<Vec3, 4> corners = {
                pose.origin + u * kPlaneNear + v * kPlaneNear, pose.origin + u * kPlaneFar + v * kPlaneNear,
                pose.origin + u * kPlaneFar + v * kPlaneFar, pose.origin + u * kPlaneNear + v * kPlaneFar};
            GizmoPlaneShape& plane = shape.planes[i];
            bool valid = true;
            for (usize k = 0; k < corners.size(); ++k)
            {
                const Projected corner = project(camera, corners[k]);
                plane.corners[k] = corner.pixel;
                valid = valid && corner.valid;
            }
            plane.facing = std::abs(glm::dot(shape.axes[i], toward));
            plane.visibility = valid ? smoothstep(kPlaneHidden, kPlaneFade, plane.facing) : 0.0f;
        }
    }
    if (hasRings(tool))
    {
        // Ring points are projected from view space: the pivot and the ring's axes are rotated once, the
        // points only scale them (this keeps the shape within the gizmo/hit-test budget).
        const RingTable& table = ringTable();
        const f32 radius = world * ringRadius(tool);
        const Quat toView = glm::inverse(camera.rotation);
        const Vec3 centerView = toView * (pose.origin - camera.eye);
        const Vec2 size(camera.size);
        for (u32 i = 0; i < 3; ++i)
        {
            const Vec3 u = shape.axes[(i + 1) % 3];
            const Vec3 v = shape.axes[(i + 2) % 3];
            const Vec4 uClip = camera.projection * Vec4(toView * u * radius, 0.0f);
            const Vec4 vClip = camera.projection * Vec4(toView * v * radius, 0.0f);
            const Vec4 centerClip = camera.projection * Vec4(centerView, 1.0f);
            const f32 uToward = glm::dot(u, toward);
            const f32 vToward = glm::dot(v, toward);
            GizmoRingShape& ring = shape.rings[i];
            ring.visibility = 1.0f;
            std::array<bool, kGizmoRingSegments> valid{};
            for (u32 k = 0; k < kGizmoRingSegments; ++k)
            {
                const Vec4 clip = centerClip + uClip * table.cosines[k] + vClip * table.sines[k];
                valid[k] = clip.w > 1e-6f;
                const Vec2 ndc = valid[k] ? Vec2(clip) / clip.w : Vec2(0.0f);
                ring.points[k] = Vec2((ndc.x * 0.5f + 0.5f) * size.x, (0.5f - ndc.y * 0.5f) * size.y);
            }
            ring.low = ring.points[0];
            ring.high = ring.points[0];
            for (const Vec2 point : ring.points)
            {
                ring.low = glm::min(ring.low, point);
                ring.high = glm::max(ring.high, point);
            }
            for (u32 k = 0; k < kGizmoRingSegments; ++k)
            {
                const f32 facing = table.middleCosines[k] * uToward + table.middleSines[k] * vToward;
                ring.segments[k] = !valid[k] || !valid[(k + 1) % kGizmoRingSegments] ? GizmoSegment::Hidden
                                   : facing >= 0.0f                                  ? GizmoSegment::Front
                                                                                     : GizmoSegment::Back;
            }
        }
    }
    if (tool == GizmoTool::Rotate)
    {
        shape.viewRing = shape.length * kViewRing;
        shape.trackball = shape.length;
    }
    if (tool != GizmoTool::Rotate)
    {
        shape.centerBox = shape.length * kCenterBox;
    }
    return shape;
}

GizmoHandle gizmoHitTest(const GizmoShape& shape, Vec2 cursor) noexcept
{
    if (!shape.visible || shape.tool == GizmoTool::Select || shape.tool == GizmoTool::Measure)
    {
        return GizmoHandle::None;
    }
    const bool translate = shape.tool == GizmoTool::Translate;
    if (shape.centerBox > 0.0f)
    {
        const Vec2 offsetFromCenter = glm::abs(cursor - shape.center);
        if (std::max(offsetFromCenter.x, offsetFromCenter.y) <= shape.centerBox + shape.tolerance * 0.5f)
        {
            return translate ? GizmoHandle::TranslateScreen : GizmoHandle::ScaleUniform;
        }
    }
    GizmoHandle best = GizmoHandle::None;
    f32 facing = -1.0f;
    for (u32 i = 0; i < 3; ++i)
    {
        const GizmoPlaneShape& plane = shape.planes[i];
        if (plane.visibility > 0.0f && plane.facing > facing && insideQuad(cursor, plane.corners))
        {
            facing = plane.facing;
            best = offset(GizmoHandle::TranslateYZ, i);
        }
    }
    if (best != GizmoHandle::None)
    {
        return best;
    }
    f32 nearest = shape.tolerance;
    if (hasLines(shape.tool))
    {
        const bool scale = shape.tool == GizmoTool::Scale;
        for (u32 i = 0; i < 3; ++i)
        {
            const GizmoAxisShape& line = shape.lines[i];
            if (!(line.visibility > 0.0f))
            {
                continue;
            }
            const f32 distance = segmentDistance(cursor, line.start, line.end);
            if (distance < nearest)
            {
                nearest = distance;
                best = offset(scale ? GizmoHandle::ScaleX : GizmoHandle::TranslateX, i);
            }
        }
    }
    if (hasRings(shape.tool))
    {
        for (u32 i = 0; i < 3; ++i)
        {
            const GizmoRingShape& ring = shape.rings[i];
            // Rings whose box is farther than the nearest handle so far cannot be nearer.
            if (glm::any(glm::lessThan(cursor, ring.low - nearest)) ||
                glm::any(glm::greaterThan(cursor, ring.high + nearest)))
            {
                continue;
            }
            // Squared distances keep the square root out of the loop over the segments.
            f32 nearestSquared = nearest * nearest;
            bool nearer = false;
            for (u32 k = 0; k < kGizmoRingSegments; ++k)
            {
                if (ring.segments[k] != GizmoSegment::Front)
                {
                    continue;
                }
                const f32 distanceSquared =
                    segmentDistanceSquared(cursor, ring.points[k], ring.points[(k + 1) % kGizmoRingSegments]);
                if (distanceSquared < nearestSquared)
                {
                    nearestSquared = distanceSquared;
                    nearer = true;
                }
            }
            if (nearer)
            {
                nearest = std::sqrt(nearestSquared);
                best = offset(GizmoHandle::RotateX, i);
            }
        }
    }
    const f32 fromCenter = glm::length(cursor - shape.center);
    if (shape.viewRing > 0.0f && std::abs(fromCenter - shape.viewRing) < nearest)
    {
        best = GizmoHandle::RotateView;
    }
    if (best == GizmoHandle::None && shape.trackball > 0.0f && fromCenter < shape.trackball)
    {
        best = GizmoHandle::RotateTrackball;
    }
    return best;
}

std::optional<GizmoDrag> GizmoDrag::begin(GizmoTool tool, GizmoHandle handle, const GizmoCamera& camera,
                                          const GizmoPose& pose, Vec2 cursor) noexcept
{
    if (handle == GizmoHandle::None || camera.size.x == 0 || camera.size.y == 0)
    {
        return std::nullopt;
    }
    const Projected center = project(camera, pose.origin);
    const f32 perPixel = worldPerPixel(camera, pose.origin);
    if (!center.valid || !(perPixel > 0.0f))
    {
        return std::nullopt;
    }
    GizmoDrag drag;
    drag.m_handle = handle;
    drag.m_camera = camera;
    drag.m_pose = pose;
    drag.m_cursor = cursor;
    drag.m_center = center.pixel;
    drag.m_pixels = kGizmoSize * camera.pixelScale;
    const f32 world = drag.m_pixels * perPixel;
    const Vec3 toward = towardCamera(camera, pose.origin);
    const std::optional<u32> axisIndex = gizmoHandleAxis(handle);
    const Vec3 axis = axisIndex ? pose.orientation * kUnitAxes[*axisIndex] : toward;
    drag.m_axis = axis;

    const auto start = [&]() {
        const std::optional<Vec3> hit = drag.hitPlane(cursor);
        if (hit)
        {
            drag.m_start = *hit;
        }
        return hit.has_value();
    };
    switch (handle)
    {
    case GizmoHandle::TranslateX:
    case GizmoHandle::TranslateY:
    case GizmoHandle::TranslateZ:
    case GizmoHandle::ScaleX:
    case GizmoHandle::ScaleY:
    case GizmoHandle::ScaleZ:
    {
        // The plane through the axis that faces the camera best.
        const Vec3 normal = glm::cross(axis, glm::cross(toward, axis));
        const f32 length = glm::length(normal);
        if (!(length > 1e-3f))
        {
            return std::nullopt;
        }
        drag.m_normal = normal / length;
        if (!start())
        {
            return std::nullopt;
        }
        const f32 grab = glm::dot(drag.m_start - pose.origin, axis);
        drag.m_grab = std::abs(grab) > world * 0.1f ? grab : world;
        return drag;
    }
    case GizmoHandle::TranslateYZ:
    case GizmoHandle::TranslateXZ:
    case GizmoHandle::TranslateXY:
        if (!(std::abs(glm::dot(axis, toward)) > 1e-3f))
        {
            return std::nullopt;
        }
        drag.m_normal = axis;
        return start() ? std::optional<GizmoDrag>(drag) : std::nullopt;
    case GizmoHandle::TranslateScreen:
        drag.m_normal = -forwardOf(camera);
        return start() ? std::optional<GizmoDrag>(drag) : std::nullopt;
    case GizmoHandle::RotateX:
    case GizmoHandle::RotateY:
    case GizmoHandle::RotateZ:
    {
        drag.m_normal = axis;
        const f32 radius = world * ringRadius(tool);
        drag.m_radius = drag.m_pixels * ringRadius(tool);
        drag.m_onPlane = std::abs(glm::dot(axis, toward)) >= kRingPlaneDot && start();
        // The ring point nearest the cursor, and the screen direction it moves in when the angle grows.
        const u32 i = *axisIndex;
        const Vec3 u = pose.orientation * kUnitAxes[(i + 1) % 3];
        const Vec3 v = pose.orientation * kUnitAxes[(i + 2) % 3];
        f32 nearest = std::numeric_limits<f32>::max();
        Vec3 radial = u;
        for (u32 k = 0; k < kGizmoRingSegments; ++k)
        {
            const f32 angle = 2.0f * kPi * static_cast<f32>(k) / static_cast<f32>(kGizmoRingSegments);
            const Vec3 direction = u * std::cos(angle) + v * std::sin(angle);
            const Projected point = project(camera, pose.origin + direction * radius);
            const f32 distance = point.valid ? glm::length(point.pixel - cursor) : nearest;
            if (distance < nearest)
            {
                nearest = distance;
                radial = direction;
            }
        }
        const Vec3 at = pose.origin + radial * radius;
        const Projected a = project(camera, at);
        const Projected b = project(camera, at + glm::cross(axis, radial) * (radius * 0.01f));
        const Vec2 tangent = b.pixel - a.pixel;
        const f32 tangentLength = glm::length(tangent);
        drag.m_tangent =
            a.valid && b.valid && tangentLength > 1e-6f ? tangent / tangentLength : Vec2(1.0f, 0.0f);
        return drag;
    }
    case GizmoHandle::RotateView:
    {
        const Vec2 from = cursor - drag.m_center;
        drag.m_previous = -std::atan2(from.y, from.x);
        return drag;
    }
    case GizmoHandle::RotateTrackball:
    case GizmoHandle::ScaleUniform:
        return drag;
    case GizmoHandle::None:
        break;
    }
    return std::nullopt;
}

std::optional<Vec3> GizmoDrag::hitPlane(Vec2 cursor) const noexcept
{
    const std::optional<Vec3> hit =
        rayPlane(gizmoRay(m_camera, cursor), m_pose.origin, m_normal, m_camera.orthographic);
    if (!hit || !(glm::length(*hit - m_pose.origin) <= kViewportMaxExtent))
    {
        return std::nullopt;
    }
    return hit;
}

GizmoDelta GizmoDrag::update(Vec2 cursor, const GizmoSnap* snap) noexcept
{
    GizmoDelta out;
    const std::optional<u32> axisIndex = gizmoHandleAxis(m_handle);
    const auto snapAxes = [&](Vec3 offsetValue, std::span<const u32> axes) {
        Vec3 snapped = offsetValue;
        for (const u32 i : axes)
        {
            const Vec3 axis = m_pose.orientation * kUnitAxes[i];
            const f32 along = glm::dot(offsetValue, axis);
            snapped += axis * (snapToIncrement(along, snap->translate) - along);
        }
        return snapped;
    };
    const auto turn = [&](f32 radians, Vec3 axis) {
        f32 degrees = glm::degrees(radians);
        if (snap != nullptr)
        {
            degrees = snapToIncrement(degrees, snap->rotate);
        }
        out.angle = degrees;
        out.rotation = glm::angleAxis(glm::radians(degrees), axis);
    };
    const auto scaleFactor = [&](f32 factor) {
        if (snap != nullptr)
        {
            factor = 1.0f + snapToIncrement(factor - 1.0f, snap->scale);
        }
        return std::isfinite(factor) ? std::max(factor, kGizmoMinScaleFactor) : 1.0f;
    };
    // Unwraps a raw angle so that turns past half a revolution keep growing.
    const auto unwrap = [&](f32 raw) {
        m_turn += wrapAngle(raw - m_previous);
        m_previous = raw;
        return m_turn;
    };

    switch (m_handle)
    {
    case GizmoHandle::TranslateX:
    case GizmoHandle::TranslateY:
    case GizmoHandle::TranslateZ:
    {
        const std::optional<Vec3> hit = hitPlane(cursor);
        if (!hit)
        {
            return m_last;
        }
        f32 along = glm::dot(*hit - m_start, m_axis);
        if (snap != nullptr)
        {
            along = snapToIncrement(along, snap->translate);
        }
        out.translation = m_axis * along;
        break;
    }
    case GizmoHandle::TranslateYZ:
    case GizmoHandle::TranslateXZ:
    case GizmoHandle::TranslateXY:
    {
        const std::optional<Vec3> hit = hitPlane(cursor);
        if (!hit)
        {
            return m_last;
        }
        Vec3 moved = *hit - m_start;
        moved -= m_axis * glm::dot(moved, m_axis);
        if (snap != nullptr)
        {
            const std::array<u32, 2> inPlane = {(*axisIndex + 1) % 3, (*axisIndex + 2) % 3};
            moved = snapAxes(moved, inPlane);
        }
        out.translation = moved;
        break;
    }
    case GizmoHandle::TranslateScreen:
    {
        const std::optional<Vec3> hit = hitPlane(cursor);
        if (!hit)
        {
            return m_last;
        }
        Vec3 moved = *hit - m_start;
        if (snap != nullptr)
        {
            constexpr std::array<u32, 3> kAll = {0, 1, 2};
            moved = snapAxes(moved, kAll);
        }
        out.translation = moved;
        break;
    }
    case GizmoHandle::RotateX:
    case GizmoHandle::RotateY:
    case GizmoHandle::RotateZ:
    {
        f32 radians = 0.0f;
        if (m_onPlane)
        {
            const std::optional<Vec3> hit = hitPlane(cursor);
            if (!hit)
            {
                return m_last;
            }
            const Vec3 from = m_start - m_pose.origin;
            const Vec3 to = *hit - m_pose.origin;
            radians = unwrap(std::atan2(glm::dot(glm::cross(from, to), m_axis), glm::dot(from, to)));
        }
        else
        {
            radians = glm::dot(cursor - m_cursor, m_tangent) / m_radius;
        }
        turn(radians, m_axis);
        break;
    }
    case GizmoHandle::RotateView:
    {
        const Vec2 from = cursor - m_center;
        turn(unwrap(-std::atan2(from.y, from.x)), m_axis);
        break;
    }
    case GizmoHandle::RotateTrackball:
    {
        const Vec2 moved = cursor - m_cursor;
        const f32 length = glm::length(moved);
        if (length > 1e-6f)
        {
            // Dragging right turns the front towards the right: about the camera's up axis.
            turn(length / m_pixels, m_camera.rotation * (Vec3(moved.y, moved.x, 0.0f) / length));
        }
        break;
    }
    case GizmoHandle::ScaleX:
    case GizmoHandle::ScaleY:
    case GizmoHandle::ScaleZ:
    {
        const std::optional<Vec3> hit = hitPlane(cursor);
        if (!hit)
        {
            return m_last;
        }
        out.scale[static_cast<glm::length_t>(*axisIndex)] =
            scaleFactor(glm::dot(*hit - m_pose.origin, m_axis) / m_grab);
        break;
    }
    case GizmoHandle::ScaleUniform:
    {
        const Vec2 moved = cursor - m_cursor;
        out.scale = Vec3(scaleFactor(1.0f + (moved.x - moved.y) / m_pixels));
        break;
    }
    case GizmoHandle::None:
        break;
    }
    m_last = out;
    return out;
}

GizmoDelta GizmoDrag::typed(const GizmoTypedInput& input) const noexcept
{
    GizmoDelta out;
    const std::optional<u32> axisIndex = gizmoHandleAxis(m_handle);
    const auto value = [&](u32 field, f32 fallback) {
        return input.value(field).value_or(fallback);
    };
    const auto factor = [&](u32 field) {
        return std::max(value(field, 1.0f), kGizmoMinScaleFactor);
    };
    switch (m_handle)
    {
    case GizmoHandle::TranslateX:
    case GizmoHandle::TranslateY:
    case GizmoHandle::TranslateZ:
        out.translation = m_axis * value(0, 0.0f);
        break;
    case GizmoHandle::TranslateYZ:
    case GizmoHandle::TranslateXZ:
    case GizmoHandle::TranslateXY:
    {
        // The two fields go to the plane's axes in x, y, z order.
        const u32 first = *axisIndex == 0 ? 1u : 0u;
        const u32 second = *axisIndex == 2 ? 1u : 2u;
        out.translation =
            m_pose.orientation * (kUnitAxes[first] * value(0, 0.0f) + kUnitAxes[second] * value(1, 0.0f));
        break;
    }
    case GizmoHandle::TranslateScreen:
        out.translation = m_pose.orientation * Vec3(value(0, 0.0f), value(1, 0.0f), value(2, 0.0f));
        break;
    case GizmoHandle::RotateX:
    case GizmoHandle::RotateY:
    case GizmoHandle::RotateZ:
    case GizmoHandle::RotateView:
    case GizmoHandle::RotateTrackball:
        out.angle = value(0, 0.0f);
        out.rotation = glm::angleAxis(glm::radians(out.angle), m_axis);
        break;
    case GizmoHandle::ScaleX:
    case GizmoHandle::ScaleY:
    case GizmoHandle::ScaleZ:
        out.scale[static_cast<glm::length_t>(*axisIndex)] = factor(0);
        break;
    case GizmoHandle::ScaleUniform:
        out.scale = Vec3(factor(0));
        break;
    case GizmoHandle::None:
        break;
    }
    return out;
}

bool GizmoTypedInput::type(char character) noexcept
{
    std::array<char, kMaxLength>& text = m_text[m_field];
    u8& length = m_length[m_field];
    const auto end = text.begin() + length;
    if (character == '-')
    {
        if (length > 0 && text[0] == '-')
        {
            std::copy(text.begin() + 1, end, text.begin());
            --length;
            return true;
        }
        if (length == kMaxLength)
        {
            return false;
        }
        std::copy_backward(text.begin(), end, end + 1);
        text[0] = '-';
        ++length;
        return true;
    }
    const bool digit = character >= '0' && character <= '9';
    const bool point = character == '.' && std::find(text.begin(), end, '.') == end;
    if ((!digit && !point) || length == kMaxLength)
    {
        return false;
    }
    text[length++] = character;
    return true;
}

void GizmoTypedInput::erase() noexcept
{
    if (m_length[m_field] > 0)
    {
        --m_length[m_field];
    }
    else if (m_field > 0)
    {
        --m_field;
    }
}

void GizmoTypedInput::next() noexcept
{
    m_field = (m_field + 1) % kFields;
}

void GizmoTypedInput::clear() noexcept
{
    m_length = {};
    m_field = 0;
}

bool GizmoTypedInput::active() const noexcept
{
    return std::any_of(m_length.begin(), m_length.end(), [](u8 length) { return length > 0; });
}

std::optional<f32> GizmoTypedInput::value(u32 field) const noexcept
{
    const std::string_view typed = text(field);
    const bool negative = !typed.empty() && typed.front() == '-';
    f64 whole = 0.0;
    f64 place = 1.0;
    bool fraction = false;
    bool digits = false;
    for (const char character : typed.substr(negative ? 1 : 0))
    {
        if (character == '.')
        {
            fraction = true;
            continue;
        }
        digits = true;
        const f64 digit = static_cast<f64>(character - '0');
        if (fraction)
        {
            place *= 0.1;
            whole += digit * place;
        }
        else
        {
            whole = whole * 10.0 + digit;
        }
    }
    if (!digits)
    {
        return std::nullopt;
    }
    return static_cast<f32>(negative ? -whole : whole);
}

std::string_view GizmoTypedInput::text(u32 field) const noexcept
{
    if (field >= kFields)
    {
        return {};
    }
    return std::string_view(m_text[field].data(), m_length[field]);
}

std::optional<GizmoModal> GizmoModal::begin(GizmoModalOperation operation, const GizmoCamera& camera,
                                            Vec3 pivot, Quat local, Vec2 cursor) noexcept
{
    if (camera.size.x == 0 || camera.size.y == 0)
    {
        return std::nullopt;
    }
    const Projected center = project(camera, pivot);
    if (!center.valid)
    {
        return std::nullopt;
    }
    GizmoModal modal;
    modal.m_operation = operation;
    modal.m_camera = camera;
    modal.m_pose.origin = pivot;
    const f32 length = glm::length(local);
    modal.m_localRotation =
        length > 1e-6f && std::isfinite(length) ? local / length : Quat(1.0f, 0.0f, 0.0f, 0.0f);
    modal.m_cursor = cursor;
    modal.m_center = center.pixel;
    // Starting on the pivot would make every scale huge: measure from at least the hit tolerance away.
    modal.m_startRadius =
        std::max(glm::length(cursor - center.pixel), kGizmoHitTolerance * camera.pixelScale);
    modal.restart();
    return modal;
}

void GizmoModal::constrain(u32 axis, bool plane) noexcept
{
    if (axis > 2)
    {
        return;
    }
    const u32 key = axis + (plane ? 3u : 0u) + 1u;
    if (m_key != key)
    {
        m_key = key;
        m_local = false;
    }
    else if (!m_local)
    {
        m_local = true;
    }
    else
    {
        m_key = 0;
        m_local = false;
    }
    m_axes = m_key == 0 ? 0u : plane ? (7u & ~(1u << axis)) : (1u << axis);
    restart();
}

void GizmoModal::restart() noexcept
{
    m_pose.orientation = m_local ? m_localRotation : Quat(1.0f, 0.0f, 0.0f, 0.0f);
    // The axis of a single-axis constraint, and the axis a plane constraint leaves out (3: none).
    const u32 single = m_axes == 1u ? 0u : m_axes == 2u ? 1u : m_axes == 4u ? 2u : 3u;
    const u32 excluded = m_axes == 6u ? 0u : m_axes == 5u ? 1u : m_axes == 3u ? 2u : 3u;
    GizmoHandle handle = GizmoHandle::None;
    if (m_operation == GizmoModalOperation::Translate)
    {
        handle = m_axes == 0  ? GizmoHandle::TranslateScreen
                 : single < 3 ? offset(GizmoHandle::TranslateX, single)
                              : offset(GizmoHandle::TranslateYZ, excluded);
    }
    else if (m_operation == GizmoModalOperation::Rotate)
    {
        handle = m_axes == 0 ? GizmoHandle::RotateView
                             : offset(GizmoHandle::RotateX, single < 3 ? single : excluded);
    }
    const GizmoTool tool =
        m_operation == GizmoModalOperation::Rotate ? GizmoTool::Rotate : GizmoTool::Translate;
    m_drag = handle == GizmoHandle::None ? std::nullopt
                                         : GizmoDrag::begin(tool, handle, m_camera, m_pose, m_cursor);
}

GizmoDelta GizmoModal::update(Vec2 cursor, const GizmoSnap* snap, const GizmoTypedInput& typed) noexcept
{
    if (m_operation != GizmoModalOperation::Scale)
    {
        // An axis that points at the camera cannot be dragged along: nothing moves until it changes.
        if (!m_drag)
        {
            return GizmoDelta{};
        }
        return typed.active() ? m_drag->typed(typed) : m_drag->update(cursor, snap);
    }
    f32 factor = 1.0f;
    if (typed.active())
    {
        factor = std::max(typed.value(0).value_or(1.0f), kGizmoMinScaleFactor);
    }
    else
    {
        factor = glm::length(cursor - m_center) / m_startRadius;
        if (snap != nullptr)
        {
            factor = 1.0f + snapToIncrement(factor - 1.0f, snap->scale);
        }
        factor = std::isfinite(factor) ? std::max(factor, kGizmoMinScaleFactor) : 1.0f;
    }
    GizmoDelta out;
    for (u32 i = 0; i < 3; ++i)
    {
        if (m_axes == 0 || (m_axes & (1u << i)) != 0)
        {
            out.scale[static_cast<glm::length_t>(i)] = factor;
        }
    }
    return out;
}

void applyGizmoDelta(std::span<const Transform> start, const GizmoPose& pose, GizmoPivot pivot,
                     const GizmoDelta& delta, std::span<Transform> out) noexcept
{
    const usize count = std::min(start.size(), out.size());
    const Quat inverse = glm::inverse(pose.orientation);
    const bool individual = pivot == GizmoPivot::Individual;
    for (usize i = 0; i < count; ++i)
    {
        Transform placed = start[i];
        const Vec3 center = individual ? placed.position : pose.origin;
        if (delta.scale != Vec3(1.0f))
        {
            placed.position =
                center + pose.orientation * (delta.scale * (inverse * (placed.position - center)));
            placed.scale *= delta.scale;
        }
        placed.position = center + delta.rotation * (placed.position - center);
        placed.rotation = glm::normalize(delta.rotation * placed.rotation);
        placed.position += delta.translation;
        out[i] = placed;
    }
}

void gizmoTargets(EditorContext& editor, std::vector<GizmoTarget>& out)
{
    out.clear();
    World& world = editor.world();
    EntityIndex& index = editor.entities();
    std::vector<Entity> entities;
    for (const EntityGuid guid : editor.selection().items())
    {
        const Entity entity = index.find(guid);
        if (!entity.isNull() && (world.has<Transform>(entity) || world.has<LocalTransform>(entity)))
        {
            entities.push_back(entity);
        }
    }
    detail::dropDescendants(world, entities);
    const Entity primary = index.find(editor.selection().primary());
    for (const Entity entity : entities)
    {
        if (entity != primary)
        {
            out.push_back(GizmoTarget{index.guidOf(entity), detail::worldTransformOf(world, entity)});
        }
    }
    if (std::find(entities.begin(), entities.end(), primary) != entities.end())
    {
        out.push_back(GizmoTarget{index.guidOf(primary), detail::worldTransformOf(world, primary)});
    }
}

f32 snapToIncrement(f32 value, f32 increment) noexcept
{
    if (!(increment > 0.0f) || !std::isfinite(increment))
    {
        return value;
    }
    return std::round(value / increment) * increment;
}

} // namespace illusion::editor

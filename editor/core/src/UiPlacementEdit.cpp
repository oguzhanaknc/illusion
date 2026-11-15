#include <illusion/editor/core/UiPlacementEdit.hpp>

#include <algorithm>
#include <cmath>

namespace illusion::editor
{
namespace
{

constexpr std::array<UiAnchors, kUiAnchorPresetCount> kPresetAnchors{{
    {{0.0f, 0.0f}, {0.0f, 0.0f}}, // TopLeft
    {{0.5f, 0.0f}, {0.5f, 0.0f}}, // Top
    {{1.0f, 0.0f}, {1.0f, 0.0f}}, // TopRight
    {{0.0f, 0.5f}, {0.0f, 0.5f}}, // Left
    {{0.5f, 0.5f}, {0.5f, 0.5f}}, // Center
    {{1.0f, 0.5f}, {1.0f, 0.5f}}, // Right
    {{0.0f, 1.0f}, {0.0f, 1.0f}}, // BottomLeft
    {{0.5f, 1.0f}, {0.5f, 1.0f}}, // Bottom
    {{1.0f, 1.0f}, {1.0f, 1.0f}}, // BottomRight
    {{0.0f, 0.0f}, {1.0f, 0.0f}}, // StretchTop
    {{0.0f, 0.5f}, {1.0f, 0.5f}}, // StretchMiddle
    {{0.0f, 1.0f}, {1.0f, 1.0f}}, // StretchBottom
    {{0.0f, 0.0f}, {0.0f, 1.0f}}, // StretchLeft
    {{0.5f, 0.0f}, {0.5f, 1.0f}}, // StretchCenter
    {{1.0f, 0.0f}, {1.0f, 1.0f}}, // StretchRight
    {{0.0f, 0.0f}, {1.0f, 1.0f}}, // Stretch
}};

bool finite(Vec2 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

bool finite(const UiRect& rect) noexcept
{
    return finite(rect.min) && finite(rect.max);
}

bool finite(const UiPlacement& placement) noexcept
{
    return finite(placement.anchorMin) && finite(placement.anchorMax) && finite(placement.offsetMin) &&
           finite(placement.offsetMax);
}

bool finite(const UiTransform2D& transform) noexcept
{
    return finite(transform.x) && finite(transform.y) && finite(transform.translation);
}

// The point that @p transform maps to @p point; false when it cannot be inverted.
bool untransform(const UiTransform2D& transform, Vec2 point, Vec2& local) noexcept
{
    const f32 det = transform.x.x * transform.y.y - transform.y.x * transform.x.y;
    if (!(std::abs(det) > 1e-12f) || !std::isfinite(det))
    {
        return false;
    }
    const Vec2 d = point - transform.translation;
    local = Vec2((d.x * transform.y.y - d.y * transform.y.x) / det,
                 (d.y * transform.x.x - d.x * transform.x.y) / det);
    return finite(local);
}

// Whether @p point lies inside the clip of @p element, as UiWorld clips: the bounds of its rectangle after an
// axis-aligned transform, the rectangle itself otherwise.
bool insideClip(const UiLayoutElement& element, Vec2 point) noexcept
{
    UiRect clip = element.rect;
    if (element.transform.x.y == 0.0f && element.transform.y.x == 0.0f)
    {
        const Vec2 a = element.transform.apply(clip.min);
        const Vec2 b = element.transform.apply(clip.max);
        clip = {glm::min(a, b), glm::max(a, b)};
    }
    return clip.contains(point);
}

f32 distanceToSegment(Vec2 point, Vec2 a, Vec2 b) noexcept
{
    const Vec2 ab = b - a;
    const f32 length2 = glm::dot(ab, ab);
    const f32 t = length2 > 0.0f ? std::clamp(glm::dot(point - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
    return glm::length(point - (a + ab * t));
}

f32 snapped(f32 value, f32 snap) noexcept
{
    return snap > 0.0f ? std::round(value / snap) * snap : value;
}

bool movesBody(UiHandle handle) noexcept
{
    return handle == UiHandle::Body;
}

bool movesAnchors(UiHandle handle) noexcept
{
    return handle == UiHandle::AnchorMin || handle == UiHandle::AnchorMax || handle == UiHandle::Anchors;
}

bool movesLeft(UiHandle handle) noexcept
{
    return handle == UiHandle::Left || handle == UiHandle::TopLeft || handle == UiHandle::BottomLeft;
}

bool movesRight(UiHandle handle) noexcept
{
    return handle == UiHandle::Right || handle == UiHandle::TopRight || handle == UiHandle::BottomRight;
}

bool movesTop(UiHandle handle) noexcept
{
    return handle == UiHandle::Top || handle == UiHandle::TopLeft || handle == UiHandle::TopRight;
}

bool movesBottom(UiHandle handle) noexcept
{
    return handle == UiHandle::Bottom || handle == UiHandle::BottomLeft || handle == UiHandle::BottomRight;
}

// The transform the pointer of @p handle is read through.
const UiTransform2D& spaceOf(const UiHandleFrame& frame, UiHandle handle) noexcept
{
    return movesBody(handle) || movesAnchors(handle) ? frame.parentTransform : frame.transform;
}

} // namespace

UiAnchors anchorsOf(UiAnchorPreset preset) noexcept
{
    const auto index = static_cast<usize>(preset);
    return index < kPresetAnchors.size() ? kPresetAnchors[index] : kPresetAnchors[0];
}

std::optional<UiAnchorPreset> anchorPresetOf(const UiPlacement& placement) noexcept
{
    for (u32 i = 0; i < kUiAnchorPresetCount; ++i)
    {
        if (kPresetAnchors[i].min == placement.anchorMin && kPresetAnchors[i].max == placement.anchorMax)
        {
            return static_cast<UiAnchorPreset>(i);
        }
    }
    return std::nullopt;
}

UiPlacement withAnchors(const UiPlacement& placement, const UiRect& parent, UiAnchors anchors,
                        bool keepRect) noexcept
{
    if (!finite(anchors.min) || !finite(anchors.max) || !finite(placement) || !finite(parent))
    {
        return placement;
    }
    const Vec2 min = glm::clamp(anchors.min, Vec2(0.0f), Vec2(1.0f));
    const Vec2 max = glm::max(glm::clamp(anchors.max, Vec2(0.0f), Vec2(1.0f)), min);
    UiPlacement out = placement;
    out.anchorMin = min;
    out.anchorMax = max;
    if (keepRect)
    {
        const UiRect rect = resolvePlacement(placement, parent);
        const Vec2 size = parent.size();
        out.offsetMin = rect.min - parent.min - min * size;
        out.offsetMax = rect.max - parent.min - max * size;
    }
    return out;
}

UiPlacement placementFor(const UiPlacement& placement, const UiRect& parent, const UiRect& rect) noexcept
{
    if (!finite(placement) || !finite(parent) || !finite(rect))
    {
        return placement;
    }
    const Vec2 size = parent.size();
    UiPlacement out = placement;
    out.offsetMin = rect.min - parent.min - placement.anchorMin * size;
    out.offsetMax = glm::max(rect.max, rect.min) - parent.min - placement.anchorMax * size;
    return out;
}

Entity pickUiElement(const UiLayout& layout, Vec2 point) noexcept
{
    if (!finite(point))
    {
        return {};
    }
    const usize count = layout.elements.size();
    for (usize i = count; i-- > 0;)
    {
        const UiLayoutElement& element = layout.elements[i];
        Vec2 local(0.0f);
        if (!element.visible || !untransform(element.transform, point, local) ||
            !element.rect.contains(local))
        {
            continue;
        }
        bool clipped = false;
        for (u32 at = element.parent; at != UiLayoutElement::kNoParent && at < count && !clipped;
             at = layout.elements[at].parent)
        {
            const UiLayoutElement& ancestor = layout.elements[at];
            clipped = ancestor.clipsChildren && !insideClip(ancestor, point);
        }
        if (!clipped)
        {
            return element.entity;
        }
    }
    return {};
}

std::optional<UiHandleFrame> handleFrameOf(const UiLayout& layout, u32 element, const UiPlacement& placement,
                                           Vec2 size, bool placed) noexcept
{
    if (element >= layout.elements.size())
    {
        return std::nullopt;
    }
    const UiLayoutElement& self = layout.elements[element];
    UiHandleFrame frame;
    frame.rect = self.rect;
    frame.transform = self.transform;
    if (self.parent < layout.elements.size())
    {
        const UiLayoutElement& parent = layout.elements[self.parent];
        frame.parent = parent.rect;
        frame.parentTransform = parent.transform;
    }
    else if (self.canvas < layout.canvases.size())
    {
        frame.parent = layout.canvases[self.canvas].area;
    }
    frame.placement = placement;
    frame.size = size;
    frame.placed = placed;
    return frame;
}

std::array<Vec2, 4> handleCorners(const UiHandleFrame& frame) noexcept
{
    const UiRect& r = frame.rect;
    return {frame.transform.apply(r.min), frame.transform.apply(Vec2(r.max.x, r.min.y)),
            frame.transform.apply(r.max), frame.transform.apply(Vec2(r.min.x, r.max.y))};
}

UiAnchors anchorMarkers(const UiHandleFrame& frame) noexcept
{
    const Vec2 size = frame.parent.size();
    return {frame.parentTransform.apply(frame.parent.min + frame.placement.anchorMin * size),
            frame.parentTransform.apply(frame.parent.min + frame.placement.anchorMax * size)};
}

UiHandle uiHandleHitTest(const UiHandleFrame& frame, Vec2 point, f32 radius) noexcept
{
    if (!finite(point) || !std::isfinite(radius) || radius < 0.0f || !finite(frame.rect) ||
        !finite(frame.transform))
    {
        return UiHandle::None;
    }
    if (frame.placed && finite(frame.parentTransform) && finite(frame.parent) && finite(frame.placement))
    {
        const UiAnchors markers = anchorMarkers(frame);
        const f32 toMin = glm::length(point - markers.min);
        const f32 toMax = glm::length(point - markers.max);
        if (frame.placement.anchorMin == frame.placement.anchorMax)
        {
            if (toMin <= radius)
            {
                return UiHandle::Anchors;
            }
        }
        else if (toMin <= radius || toMax <= radius)
        {
            return toMin <= toMax ? UiHandle::AnchorMin : UiHandle::AnchorMax;
        }
    }

    const std::array<Vec2, 4> corners = handleCorners(frame);
    constexpr std::array<UiHandle, 4> kCornerHandles{UiHandle::TopLeft, UiHandle::TopRight,
                                                     UiHandle::BottomRight, UiHandle::BottomLeft};
    // The edge from corner i to corner i + 1.
    constexpr std::array<UiHandle, 4> kEdgeHandles{UiHandle::Top, UiHandle::Right, UiHandle::Bottom,
                                                   UiHandle::Left};
    UiHandle best = UiHandle::None;
    f32 bestDistance = radius;
    for (usize i = 0; i < 4; ++i)
    {
        const f32 distance = glm::length(point - corners[i]);
        if (distance <= bestDistance)
        {
            best = kCornerHandles[i];
            bestDistance = distance;
        }
    }
    if (best != UiHandle::None)
    {
        return best;
    }
    bestDistance = radius;
    for (usize i = 0; i < 4; ++i)
    {
        const f32 distance = distanceToSegment(point, corners[i], corners[(i + 1) % 4]);
        if (distance <= bestDistance)
        {
            best = kEdgeHandles[i];
            bestDistance = distance;
        }
    }
    if (best != UiHandle::None)
    {
        return best;
    }
    Vec2 local(0.0f);
    if (frame.placed && untransform(frame.transform, point, local) && frame.rect.contains(local))
    {
        return UiHandle::Body;
    }
    return UiHandle::None;
}

bool UiHandleDrag::begin(const UiHandleFrame& frame, UiHandle handle, Vec2 point) noexcept
{
    m_handle = UiHandle::None;
    m_frame = frame;
    if (handle == UiHandle::None || handle > UiHandle::Anchors ||
        (!frame.placed && (movesBody(handle) || movesAnchors(handle))) || !finite(point) ||
        !finite(frame.rect) || !finite(frame.parent) || !finite(frame.placement) || !finite(frame.size))
    {
        return false;
    }
    Vec2 start(0.0f);
    if (!untransform(spaceOf(frame, handle), point, start))
    {
        return false;
    }
    m_handle = handle;
    m_start = start;
    return true;
}

UiHandleEdit UiHandleDrag::update(Vec2 point, f32 snap) const noexcept
{
    UiHandleEdit edit{.placement = m_frame.placement, .size = m_frame.size};
    Vec2 local(0.0f);
    if (m_handle == UiHandle::None || !untransform(spaceOf(m_frame, m_handle), point, local))
    {
        return edit;
    }
    if (!std::isfinite(snap) || snap < 0.0f)
    {
        snap = 0.0f;
    }
    const Vec2 delta = local - m_start;
    UiPlacement& placement = edit.placement;

    if (movesBody(m_handle))
    {
        const Vec2 offsetMin(snapped(placement.offsetMin.x + delta.x, snap),
                             snapped(placement.offsetMin.y + delta.y, snap));
        const Vec2 shift = offsetMin - placement.offsetMin;
        placement.offsetMin += shift;
        placement.offsetMax += shift;
        return edit;
    }

    if (movesAnchors(m_handle))
    {
        const Vec2 parentSize = m_frame.parent.size();
        Vec2 moved(0.0f);
        for (int axis = 0; axis < 2; ++axis)
        {
            moved[axis] = parentSize[axis] > 0.0f ? delta[axis] / parentSize[axis] : 0.0f;
        }
        UiAnchors anchors{.min = placement.anchorMin, .max = placement.anchorMax};
        if (m_handle == UiHandle::AnchorMin)
        {
            anchors.min = glm::clamp(anchors.min + moved, Vec2(0.0f), anchors.max);
        }
        else if (m_handle == UiHandle::AnchorMax)
        {
            anchors.max = glm::clamp(anchors.max + moved, anchors.min, Vec2(1.0f));
        }
        else
        {
            anchors.min = glm::clamp(anchors.min + moved, Vec2(0.0f), Vec2(1.0f));
            anchors.max = anchors.min;
        }
        placement = withAnchors(m_frame.placement, m_frame.parent, anchors, true);
        return edit;
    }

    const Vec2 extent = glm::max(m_frame.rect.size(), Vec2(0.0f));
    if (!m_frame.placed)
    {
        // The layout places the element: only its size changes, never below zero.
        Vec2 size = m_frame.size;
        if (movesLeft(m_handle) || movesRight(m_handle))
        {
            const f32 width = extent.x + (movesLeft(m_handle) ? -delta.x : delta.x);
            size.x = std::max(snapped(width, snap), 0.0f);
        }
        if (movesTop(m_handle) || movesBottom(m_handle))
        {
            const f32 height = extent.y + (movesTop(m_handle) ? -delta.y : delta.y);
            size.y = std::max(snapped(height, snap), 0.0f);
        }
        edit.size = size;
        return edit;
    }

    // Each moved edge keeps to its side of the opposite one.
    const UiPlacement& from = m_frame.placement;
    if (movesLeft(m_handle))
    {
        placement.offsetMin.x =
            std::min(snapped(from.offsetMin.x + delta.x, snap), from.offsetMin.x + extent.x);
    }
    if (movesRight(m_handle))
    {
        placement.offsetMax.x =
            std::max(snapped(from.offsetMax.x + delta.x, snap), from.offsetMax.x - extent.x);
    }
    if (movesTop(m_handle))
    {
        placement.offsetMin.y =
            std::min(snapped(from.offsetMin.y + delta.y, snap), from.offsetMin.y + extent.y);
    }
    if (movesBottom(m_handle))
    {
        placement.offsetMax.y =
            std::max(snapped(from.offsetMax.y + delta.y, snap), from.offsetMax.y - extent.y);
    }
    return edit;
}

} // namespace illusion::editor

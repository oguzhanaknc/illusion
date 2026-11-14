#include <illusion/ui/UiGeometry.hpp>

#include <algorithm>
#include <cmath>

namespace illusion
{
namespace
{

constexpr f32 kMinUserScale = 0.5f;
constexpr f32 kMaxUserScale = 3.0f;

bool positiveFinite(f32 value) noexcept
{
    return std::isfinite(value) && value > 0.0f;
}

f32 insetPixels(f32 value) noexcept
{
    return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
}

} // namespace

UiRect intersect(const UiRect& a, const UiRect& b) noexcept
{
    const UiRect shared{glm::max(a.min, b.min), glm::min(a.max, b.max)};
    return shared.empty() ? UiRect{a.min, a.min} : shared;
}

UiRect inset(const UiRect& rect, const UiEdges& edges) noexcept
{
    const Vec2 min = rect.min + Vec2(edges.left, edges.top);
    return {min, glm::max(rect.max - Vec2(edges.right, edges.bottom), min)};
}

UiRect resolvePlacement(const UiPlacement& placement, const UiRect& parent) noexcept
{
    const Vec2 size = parent.size();
    const Vec2 min = parent.min + placement.anchorMin * size + placement.offsetMin;
    const Vec2 max = parent.min + placement.anchorMax * size + placement.offsetMax;
    return {min, glm::max(max, min)};
}

UiCanvasMetrics canvasMetrics(const UiCanvasScale& scale, UVec2 targetPixels,
                              const UiEdges& safeInsetPixels) noexcept
{
    const Vec2 target(targetPixels);
    f32 pixelsPerUnit = 1.0f;
    if (targetPixels.x > 0 && targetPixels.y > 0 && positiveFinite(scale.referenceSize.x) &&
        positiveFinite(scale.referenceSize.y) && positiveFinite(scale.userScale))
    {
        const Vec2 ratio = target / scale.referenceSize;
        switch (scale.mode)
        {
        case UiScaleMode::MatchHeight:
            pixelsPerUnit = ratio.y;
            break;
        case UiScaleMode::MatchWidth:
            pixelsPerUnit = ratio.x;
            break;
        case UiScaleMode::Pixels:
            pixelsPerUnit = 1.0f;
            break;
        case UiScaleMode::Fit:
        default:
            pixelsPerUnit = std::min(ratio.x, ratio.y);
            break;
        }
        pixelsPerUnit *= std::clamp(scale.userScale, kMinUserScale, kMaxUserScale);
    }
    UiCanvasMetrics metrics;
    metrics.pixelsPerUnit = pixelsPerUnit;
    metrics.size = target / pixelsPerUnit;
    const f32 unitsPerPixel = 1.0f / pixelsPerUnit;
    metrics.safeArea =
        inset({Vec2(0.0f), metrics.size}, {insetPixels(safeInsetPixels.left) * unitsPerPixel,
                                           insetPixels(safeInsetPixels.top) * unitsPerPixel,
                                           insetPixels(safeInsetPixels.right) * unitsPerPixel,
                                           insetPixels(safeInsetPixels.bottom) * unitsPerPixel});
    return metrics;
}

UiTransform2D makeUiTransform(Vec2 pivot, f32 radians, Vec2 scale) noexcept
{
    const f32 c = std::cos(radians);
    const f32 s = std::sin(radians);
    UiTransform2D transform;
    transform.x = Vec2(c, s) * scale.x;
    transform.y = Vec2(-s, c) * scale.y;
    transform.translation = pivot - transform.x * pivot.x - transform.y * pivot.y;
    return transform;
}

UiTransform2D combine(const UiTransform2D& parent, const UiTransform2D& child) noexcept
{
    UiTransform2D result;
    result.x = parent.x * child.x.x + parent.y * child.x.y;
    result.y = parent.x * child.y.x + parent.y * child.y.y;
    result.translation = parent.apply(child.translation);
    return result;
}

} // namespace illusion

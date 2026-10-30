#include <illusion/editor/core/Arrange.hpp>

#include <algorithm>
#include <numeric>
#include <vector>

namespace illusion::editor
{
namespace
{

f32 edgeOf(const Aabb& box, u32 axis, AlignEdge edge) noexcept
{
    const glm::length_t i = static_cast<glm::length_t>(axis);
    switch (edge)
    {
    case AlignEdge::Min:
        return box.min[i];
    case AlignEdge::Max:
        return box.max[i];
    case AlignEdge::Center:
        break;
    }
    return (box.min[i] + box.max[i]) * 0.5f;
}

} // namespace

void alignOffsets(std::span<const Aabb> boxes, u32 axis, AlignEdge edge, const Aabb& target,
                  std::span<f32> out) noexcept
{
    const usize count = std::min(boxes.size(), out.size());
    const bool valid = axis < 3 && target.isValid();
    const f32 goal = valid ? edgeOf(target, axis, edge) : 0.0f;
    for (usize i = 0; i < count; ++i)
    {
        out[i] = valid && boxes[i].isValid() ? goal - edgeOf(boxes[i], axis, edge) : 0.0f;
    }
}

void distributeOffsets(std::span<const Aabb> boxes, u32 axis, DistributeMode mode, std::span<f32> out)
{
    const usize count = std::min(boxes.size(), out.size());
    std::fill(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count), 0.0f);
    const bool valid = std::all_of(boxes.begin(), boxes.begin() + static_cast<std::ptrdiff_t>(count),
                                   [](const Aabb& box) { return box.isValid(); });
    if (count < 3 || axis > 2 || !valid)
    {
        return;
    }
    const glm::length_t a = static_cast<glm::length_t>(axis);
    std::vector<usize> order(count);
    std::iota(order.begin(), order.end(), usize{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](usize l, usize r) { return boxes[l].center()[a] < boxes[r].center()[a]; });
    const Aabb& first = boxes[order.front()];
    const Aabb& last = boxes[order.back()];
    const f32 steps = static_cast<f32>(count - 1);
    if (mode == DistributeMode::Centers)
    {
        const f32 from = first.center()[a];
        const f32 step = (last.center()[a] - from) / steps;
        for (usize k = 1; k + 1 < count; ++k)
        {
            const usize i = order[k];
            out[i] = from + step * static_cast<f32>(k) - boxes[i].center()[a];
        }
        return;
    }
    f32 sizes = 0.0f;
    for (usize k = 0; k < count; ++k)
    {
        sizes += boxes[order[k]].max[a] - boxes[order[k]].min[a];
    }
    const f32 gap = (last.max[a] - first.min[a] - sizes) / steps;
    f32 at = first.max[a] + gap;
    for (usize k = 1; k + 1 < count; ++k)
    {
        const usize i = order[k];
        out[i] = at - boxes[i].min[a];
        at += boxes[i].max[a] - boxes[i].min[a] + gap;
    }
}

f32 restingHeight(const Aabb& box, std::span<const Aabb> surfaces, f32 ground) noexcept
{
    if (!box.isValid())
    {
        return ground;
    }
    const f32 bottom = box.min.y + kArrangeTolerance;
    bool found = false;
    f32 height = ground;
    for (const Aabb& surface : surfaces)
    {
        if (!surface.isValid() || surface.max.y > bottom || !(box.min.x < surface.max.x) ||
            !(box.max.x > surface.min.x) || !(box.min.z < surface.max.z) || !(box.max.z > surface.min.z))
        {
            continue;
        }
        if (!found || surface.max.y > height)
        {
            height = surface.max.y;
            found = true;
        }
    }
    return height;
}

} // namespace illusion::editor

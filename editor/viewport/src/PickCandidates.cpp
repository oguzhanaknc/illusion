#include <illusion/core/Assert.hpp>
#include <illusion/editor/viewport/PickCandidates.hpp>

#include <algorithm>

namespace illusion::editor::viewport
{

void appendVisibleCandidates(std::span<const EntityIdTexel> texels, UVec2 windowSize, UVec2 center,
                             std::vector<Entity>& out)
{
    ILL_ASSERT(texels.size() >= static_cast<usize>(windowSize.x) * windowSize.y,
               "appendVisibleCandidates() needs a texel per window pixel");
    // Visit the texels in rings of growing distance; within a ring in memory order.
    const u32 maxRing = std::max(windowSize.x, windowSize.y);
    for (u32 ring = 0; ring <= maxRing; ++ring)
    {
        for (u32 y = 0; y < windowSize.y; ++y)
        {
            for (u32 x = 0; x < windowSize.x; ++x)
            {
                const u32 dx = x > center.x ? x - center.x : center.x - x;
                const u32 dy = y > center.y ? y - center.y : center.y - y;
                if (std::max(dx, dy) != ring)
                {
                    continue;
                }
                const EntityIdTexel& texel = texels[static_cast<usize>(y) * windowSize.x + x];
                if (texel.index == ~0u)
                {
                    continue;
                }
                const Entity entity{texel.index, texel.generation};
                if (std::find(out.begin(), out.end(), entity) == out.end())
                {
                    out.push_back(entity);
                }
            }
        }
    }
}

u32 chooseCandidate(std::span<const Entity> candidates, Entity previous, bool cycle) noexcept
{
    ILL_ASSERT(!candidates.empty(), "chooseCandidate() needs candidates");
    if (!cycle || previous.isNull())
    {
        return 0;
    }
    const auto found = std::find(candidates.begin(), candidates.end(), previous);
    if (found == candidates.end())
    {
        return 0;
    }
    return static_cast<u32>((static_cast<usize>(found - candidates.begin()) + 1) % candidates.size());
}

} // namespace illusion::editor::viewport

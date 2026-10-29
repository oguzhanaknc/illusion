#pragma once

/// @file
/// Ordering of the entities under the cursor for viewport picking (GPU-free).

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <span>
#include <vector>

namespace illusion::editor::viewport
{

/// Half size of the square of ID texels a pick reads: 2 gives the 5 x 5 tolerance.
inline constexpr u32 kPickRadius = 2;

/// One texel of the renderer's Entity ID target (RG32_UINT); index ~0u is the background.
struct EntityIdTexel
{
    u32 index = ~0u;    ///< Entity index.
    u32 generation = 0; ///< Entity generation.
};

/// Appends to @p out the entities of a @p windowSize window of ID texels (rows top to bottom), nearest to
/// the texel @p center first (ties: the earlier texel), each once and skipping the background and entities
/// already in @p out.
void appendVisibleCandidates(std::span<const EntityIdTexel> texels, UVec2 windowSize, UVec2 center,
                             std::vector<Entity>& out);

/// The candidate a pick returns: the one after @p previous when cycling (wrapping around), else the first.
/// @p candidates must not be empty.
[[nodiscard]] u32 chooseCandidate(std::span<const Entity> candidates, Entity previous, bool cycle) noexcept;

} // namespace illusion::editor::viewport

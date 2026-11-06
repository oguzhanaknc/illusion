#pragma once

/// @file
/// The renderer settings of an editor viewport from the user's ViewportQuality preferences (the Rendering
/// panel). GPU-free, so the mapping is tested without a device.

#include <illusion/editor/core/Preferences.hpp>
#include <illusion/render/PostProcess.hpp>
#include <illusion/render/Shadows.hpp>

namespace illusion::editor
{

/// Shadow cascades of a secondary Scene view at most.
inline constexpr u32 kSecondaryShadowCascades = 2;
/// Shadow map size of a secondary Scene view at most.
inline constexpr u32 kSecondaryShadowResolution = 1024;

/// @p base with the shadow quality of @p quality; a @p secondary Scene view keeps at most
/// kSecondaryShadowCascades cascades of kSecondaryShadowResolution texels.
[[nodiscard]] ShadowSettings shadowSettingsOf(const ViewportQuality& quality, ShadowSettings base,
                                              bool secondary) noexcept;

/// @p base with the ambient occlusion, bloom and temporal anti-aliasing of @p quality.
[[nodiscard]] PostProcessSettings postProcessSettingsOf(const ViewportQuality& quality,
                                                        PostProcessSettings base) noexcept;

} // namespace illusion::editor

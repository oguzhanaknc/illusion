#include <illusion/editor/viewport/RenderQuality.hpp>

#include <algorithm>

namespace illusion::editor
{

ShadowSettings shadowSettingsOf(const ViewportQuality& quality, ShadowSettings base, bool secondary) noexcept
{
    base.enabled = quality.shadows;
    base.cascadeCount = std::clamp(quality.shadowCascades, 1u, kMaxShadowCascades);
    base.resolution = std::clamp(quality.shadowResolution, 512u, 4096u);
    base.maxDistance = std::clamp(quality.shadowDistance, 5.0f, 1000.0f);
    if (secondary)
    {
        base.cascadeCount = std::min(base.cascadeCount, kSecondaryShadowCascades);
        base.resolution = std::min(base.resolution, kSecondaryShadowResolution);
    }
    return base;
}

PostProcessSettings postProcessSettingsOf(const ViewportQuality& quality, PostProcessSettings base) noexcept
{
    base.ambientOcclusion.enabled = quality.ambientOcclusion;
    base.bloom.enabled = quality.bloom;
    base.bloom.intensity = std::clamp(quality.bloomIntensity, 0.0f, 1.0f);
    base.temporalAntiAliasing.enabled = quality.temporalAntiAliasing;
    return base;
}

} // namespace illusion::editor

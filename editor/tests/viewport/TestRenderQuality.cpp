#include <illusion/editor/viewport/RenderQuality.hpp>

#include <doctest/doctest.h>

using namespace illusion;
using namespace illusion::editor;

TEST_CASE("the viewport quality preferences set the renderer's shadows and keep its other settings")
{
    ViewportQuality quality;
    quality.shadows = false;
    quality.shadowCascades = 3;
    quality.shadowResolution = 4096;
    quality.shadowDistance = 150.0f;
    ShadowSettings base;
    base.depthBias = 0.5f;
    const ShadowSettings main = shadowSettingsOf(quality, base, false);
    CHECK_FALSE(main.enabled);
    CHECK(main.cascadeCount == 3);
    CHECK(main.resolution == 4096);
    CHECK(main.maxDistance == 150.0f);
    CHECK(main.depthBias == 0.5f);

    // Secondary Scene views keep their reduced shadows, and out-of-range values are clamped.
    const ShadowSettings secondary = shadowSettingsOf(quality, base, true);
    CHECK(secondary.cascadeCount == kSecondaryShadowCascades);
    CHECK(secondary.resolution == kSecondaryShadowResolution);
    quality.shadowCascades = 1;
    quality.shadowResolution = 512;
    CHECK(shadowSettingsOf(quality, base, true).cascadeCount == 1);
    CHECK(shadowSettingsOf(quality, base, true).resolution == 512);
    quality.shadowCascades = 9;
    quality.shadowResolution = 1;
    quality.shadowDistance = 1.0e9f;
    const ShadowSettings clamped = shadowSettingsOf(quality, base, false);
    CHECK(clamped.cascadeCount == kMaxShadowCascades);
    CHECK(clamped.resolution == 512);
    CHECK(clamped.maxDistance == 1000.0f);
}

TEST_CASE("the viewport quality preferences switch post-processing effects and keep their tuning")
{
    ViewportQuality quality;
    quality.ambientOcclusion = false;
    quality.bloom = true;
    quality.bloomIntensity = 0.2f;
    quality.temporalAntiAliasing = true;
    PostProcessSettings base;
    base.ambientOcclusion.radius = 2.0f;
    base.bloom.maxLevels = 3;
    const PostProcessSettings post = postProcessSettingsOf(quality, base);
    CHECK_FALSE(post.ambientOcclusion.enabled);
    CHECK(post.ambientOcclusion.radius == 2.0f);
    CHECK(post.bloom.enabled);
    CHECK(post.bloom.intensity == 0.2f);
    CHECK(post.bloom.maxLevels == 3);
    CHECK(post.temporalAntiAliasing.enabled);
    // The defaults are the renderer's defaults.
    const ViewportQuality defaults;
    const PostProcessSettings renderer;
    const PostProcessSettings same = postProcessSettingsOf(defaults, renderer);
    CHECK(same.ambientOcclusion.enabled == renderer.ambientOcclusion.enabled);
    CHECK(same.bloom.enabled == renderer.bloom.enabled);
    CHECK(same.bloom.intensity == renderer.bloom.intensity);
    CHECK(same.temporalAntiAliasing.enabled == renderer.temporalAntiAliasing.enabled);
    const ShadowSettings shadows;
    const ShadowSettings sameShadows = shadowSettingsOf(defaults, shadows, false);
    CHECK(sameShadows.enabled == shadows.enabled);
    CHECK(sameShadows.cascadeCount == shadows.cascadeCount);
    CHECK(sameShadows.resolution == shadows.resolution);
    CHECK(sameShadows.maxDistance == shadows.maxDistance);
}

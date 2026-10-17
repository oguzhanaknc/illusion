// Shadow lookup for lit surfaces: cascade selection with dithered transitions, then PCSS (percentage-
// closer soft shadows, Fernando 2005): a blocker search estimates the penumbra from the light's angular
// size, and a rotated Vogel-disc PCF filters with that radius. Requires ViewConstants g_view.

Texture2DArray<float> g_shadowMap : register(t4, space0);
SamplerComparisonState g_shadowCompare : register(s1, space0); // NVRHI compares with LESS: 1 = lit

static const uint kBlockerSamples = 16;
static const uint kFilterSamples = 24;

// Interleaved gradient noise (Jimenez 2014), stable per pixel.
float interleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float2 vogelDisk(uint index, uint count, float rotation)
{
    const float goldenAngle = 2.39996323;
    const float r = sqrt((float(index) + 0.5) / float(count));
    const float theta = float(index) * goldenAngle + rotation;
    return float2(cos(theta), sin(theta)) * r;
}

float sampleCascade(uint cascade, float3 position, float3 normal, float noise)
{
    const ShadowCascadeData data = g_view.cascades[cascade];
    const float resolution = g_view.shadow.w;
    const float worldSize = data.params.y;
    const float depthRange = data.params.z;
    const float texelWorld = worldSize / resolution;

    const float3 offsetPosition = position + normal * (g_view.shadowBias.y * texelWorld);
    const float4 clip = mul(data.viewProjection, float4(offsetPosition, 1.0));
    const float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    const float receiverDepth = clip.z - g_view.shadowBias.x / depthRange;
    if (any(uv < 0.0) || any(uv > 1.0) || receiverDepth >= 1.0)
    {
        return 1.0;
    }
    const float rotation = noise * 6.2831853;

    // Blocker search: everything between the light and the receiver within the light's cone.
    const float tanHalfAngle = g_view.shadow.z;
    const float receiverDistance = max(receiverDepth, 0.0) * depthRange;
    const float searchRadius = clamp(tanHalfAngle * receiverDistance / worldSize, 1.5 / resolution, 0.05);
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (uint i = 0; i < kBlockerSamples; ++i)
    {
        const float2 sampleUv = uv + vogelDisk(i, kBlockerSamples, rotation) * searchRadius;
        const int2 texel = int2(saturate(sampleUv) * (resolution - 1.0));
        const float depth = g_shadowMap.Load(int4(texel, int(cascade), 0));
        if (depth < receiverDepth)
        {
            blockerSum += depth;
            blockerCount += 1.0;
        }
    }
    if (blockerCount == 0.0)
    {
        return 1.0;
    }
    const float blockerDistance = blockerSum / blockerCount * depthRange;

    // Penumbra width grows with the distance between blocker and receiver.
    const float penumbra = 2.0 * tanHalfAngle * (receiverDistance - blockerDistance) / worldSize;
    const float filterRadius = clamp(penumbra, 1.0 / resolution, 0.05);
    float lit = 0.0;
    for (uint i = 0; i < kFilterSamples; ++i)
    {
        const float2 sampleUv = uv + vogelDisk(i, kFilterSamples, rotation) * filterRadius;
        lit += g_shadowMap.SampleCmpLevelZero(g_shadowCompare, float3(sampleUv, float(cascade)), receiverDepth);
    }
    return lit / float(kFilterSamples);
}

// Visibility (0 = fully shadowed, 1 = lit) of the shadowed directional light at `position`.
float shadowVisibility(float3 position, float3 normal, float2 pixel)
{
    const uint count = uint(g_view.shadow.x);
    if (count == 0)
    {
        return 1.0;
    }
    const float viewDepth = dot(position - g_view.cameraPosition.xyz, g_view.cameraForward.xyz);
    const float maxDistance = g_view.shadowBias.z;
    if (viewDepth >= maxDistance)
    {
        return 1.0;
    }
    uint cascade = 0;
    while (cascade + 1 < count && viewDepth > g_view.cascades[cascade].params.x)
    {
        ++cascade;
    }
    const float noise = interleavedGradientNoise(pixel);
    // Dither into the next cascade over the last 10% of this one to hide the seam.
    const float splitFar = g_view.cascades[cascade].params.x;
    const float splitNear = cascade == 0 ? 0.0 : g_view.cascades[cascade - 1].params.x;
    const float blendStart = lerp(splitNear, splitFar, 0.9);
    if (cascade + 1 < count && viewDepth > blendStart && noise < (viewDepth - blendStart) / (splitFar - blendStart))
    {
        ++cascade;
    }
    const float visibility = sampleCascade(cascade, position, normal, noise);
    // Fade out over the last 10% of the shadow distance.
    const float fade = saturate((maxDistance - viewDepth) / (0.1 * maxDistance));
    return lerp(1.0, visibility, fade);
}

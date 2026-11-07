// Shadow lookup for lit surfaces: cascade selection with dithered transitions, then PCSS (percentage-
// closer soft shadows, Fernando 2005): a blocker search estimates the penumbra from the light's angular
// size, and a rotated Vogel-disc PCF filters with that radius. Point lights with a cube shadow map
// (ADR 0051) use the same filter in the face that holds the receiver. Requires ViewConstants g_view.

Texture2DArray<float> g_shadowMap : register(t4, space0);
Texture2DArray<float> g_pointShadowMap : register(t9, space0); // reversed depth: 1 - comparison = lit
SamplerComparisonState g_shadowCompare : register(s1, space0); // NVRHI compares with LESS: 1 = lit

static const uint kBlockerSamples = 16;
static const uint kFilterSamples = 24;
static const uint kPointBlockerSamples = 12;
static const uint kPointFilterSamples = 16;
static const float kPointShadowFaceScale = 16.0 / 15.0; // Shadows.hpp
static const float kPointShadowBorder = 1.0 / 32.0;     // the border of a face beyond 90 degrees, in UV

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

// Visibility (0 = fully shadowed, 1 = lit) of the point light with shadow slot @p slot at @p position: a
// blocker search over the face border's reach, then a filter as wide as the penumbra that a light of
// PointLight::sourceRadius casts.
float pointShadowVisibility(uint slot, float3 position, float3 normal, float2 pixel)
{
    const PointShadowData data = g_view.pointShadows[slot];
    const float nearPlane = data.positionNear.w;
    const float resolution = data.params.w;
    const float3 fromLight = position - data.positionNear.xyz;
    const float axisDistance = max(max(abs(fromLight.x), abs(fromLight.y)), abs(fromLight.z));
    const float texelWorld = 2.0 * kPointShadowFaceScale * axisDistance / resolution;
    uint face;
    float2 uv;
    const float distance =
        pointShadowProject(data, position + normal * (data.params.y * texelWorld), face, uv);
    if (distance <= nearPlane)
    {
        return 1.0;
    }
    const float receiverDepth = nearPlane / max(distance - data.params.x, nearPlane);
    const float layer = float(slot * kPointShadowFaces + face);
    const float rotation = interleavedGradientNoise(pixel) * 6.2831853;

    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (uint i = 0; i < kPointBlockerSamples; ++i)
    {
        const float2 sampleUv = uv + vogelDisk(i, kPointBlockerSamples, rotation) * kPointShadowBorder;
        const int2 texel = int2(saturate(sampleUv) * (resolution - 1.0));
        const float depth = g_pointShadowMap.Load(int4(texel, int(layer), 0));
        if (depth > receiverDepth)
        {
            blockerSum += nearPlane / depth;
            blockerCount += 1.0;
        }
    }
    if (blockerCount == 0.0)
    {
        return 1.0;
    }
    const float blockerDistance = blockerSum / blockerCount;
    // Penumbra at the receiver in metres, then in face UV (a face is 2 * scale * distance wide there).
    const float penumbra = data.params.z * max(distance - blockerDistance, 0.0) / blockerDistance;
    const float filterRadius =
        clamp(penumbra / (2.0 * kPointShadowFaceScale * distance), 1.0 / resolution, kPointShadowBorder);
    float lit = 0.0;
    for (uint i = 0; i < kPointFilterSamples; ++i)
    {
        const float2 sampleUv = saturate(uv + vogelDisk(i, kPointFilterSamples, rotation) * filterRadius);
        lit += g_pointShadowMap.SampleCmpLevelZero(g_shadowCompare, float3(sampleUv, layer), receiverDepth);
    }
    return 1.0 - lit / float(kPointFilterSamples);
}

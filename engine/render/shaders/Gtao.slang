// Ground-truth ambient occlusion (Jimenez et al. 2016): for a few screen-space slices around each pixel,
// find the horizon on both sides from the depth buffer and integrate the visible cosine-weighted arc.
// Normals are reconstructed from depth. Output: unclamped visibility (GtaoBlur.slang clamps it).

#include "PostCommon.slang"

Texture2D<float> g_depth : register(t0);
[[vk::image_format("r32f")]] RWTexture2D<float> g_output : register(u0);

static const uint kSlices = 2;
static const uint kSteps = 4;

float depthAt(int2 pixel)
{
    return g_depth.Load(int3(clamp(pixel, int2(0, 0), int2(g_post.size.xy) - 1), 0));
}

float3 positionAt(int2 pixel)
{
    return viewPosition((float2(pixel) + 0.5) * g_post.size.zw, depthAt(pixel));
}

// Position at a sub-pixel location (pixel centres at +0.5). Reversed-Z depth is linear in screen space
// across a plane, so bilinear depth keeps samples on the slice line: snapping them to pixel centres
// would move them off it and raise false horizons on surfaces seen at grazing angles. Interpolated by
// hand because linear filtering of depth formats is optional.
float3 positionAt(float2 location)
{
    const float2 texel = location - 0.5;
    const int2 base = int2(floor(texel));
    const float2 f = texel - float2(base);
    const float top = lerp(depthAt(base), depthAt(base + int2(1, 0)), f.x);
    const float bottom = lerp(depthAt(base + int2(0, 1)), depthAt(base + int2(1, 1)), f.x);
    return viewPosition(location * g_post.size.zw, lerp(top, bottom, f.y));
}

// Normal from the depth buffer: for each axis use the neighbour closer in depth to avoid smearing edges.
float3 reconstructNormal(int2 pixel, float3 centre)
{
    const float3 left = positionAt(pixel + int2(-1, 0));
    const float3 right = positionAt(pixel + int2(1, 0));
    const float3 up = positionAt(pixel + int2(0, -1));
    const float3 down = positionAt(pixel + int2(0, 1));
    const float3 dx = abs(right.z - centre.z) < abs(centre.z - left.z) ? right - centre : centre - left;
    const float3 dy = abs(down.z - centre.z) < abs(centre.z - up.z) ? down - centre : centre - up;
    // Screen y points down, so dx x dy points away from the camera; flip to face it.
    return normalize(cross(dy, dx));
}

float integrateArc(float h, float n)
{
    return 0.25 * (-cos(2.0 * h - n) + cos(n) + 2.0 * h * sin(n));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void gtaoMain(uint3 id: SV_DispatchThreadID)
{
    if (any(id.xy >= uint2(g_post.size.xy)))
    {
        return;
    }
    const int2 pixel = int2(id.xy);
    const float depth = g_depth.Load(int3(pixel, 0));
    if (depth <= 0.0) // sky
    {
        g_output[id.xy] = 1.0;
        return;
    }
    const float3 position = viewPosition((float2(pixel) + 0.5) * g_post.size.zw, depth);
    const float3 normal = reconstructNormal(pixel, position);
    const float3 view = g_post.temporal.z > 0.5 ? float3(0, 0, 1) : normalize(-position);
    const float radius = g_post.ao.x;
    const float radiusPixels = radius * g_post.ao.z / (g_post.temporal.z > 0.5 ? 1.0 : max(-position.z, 1e-4));
    if (radiusPixels < 1.0)
    {
        g_output[id.xy] = 1.0;
        return;
    }

    const float noise = frac(interleavedGradientNoise(float2(pixel)) + g_post.ao.w);
    const float stepNoise = frac(noise * 7.0 + 0.5);
    float visibility = 0.0;
    for (uint slice = 0; slice < kSlices; ++slice)
    {
        const float phi = (float(slice) + noise) * kPi / float(kSlices);
        const float2 screenDirection = float2(cos(phi), sin(phi)); // pixels, y down
        const float3 direction = float3(screenDirection.x, -screenDirection.y, 0.0);
        const float3 orthoDirection = normalize(direction - dot(direction, view) * view);
        const float3 axis = normalize(cross(orthoDirection, view));
        const float3 projectedNormal = normal - axis * dot(normal, axis);
        const float projectedLength = length(projectedNormal);
        const float cosNormal = saturate(dot(projectedNormal, view) / max(projectedLength, 1e-4));
        const float n = sign(dot(orthoDirection, projectedNormal)) * acos(cosNormal);

        // Horizons start at the tangent plane and rise towards the view vector.
        float horizonPositive = cos(n + kPi * 0.5);
        float horizonNegative = cos(n - kPi * 0.5);
        for (uint step = 0; step < kSteps; ++step)
        {
            const float t = (float(step) + stepNoise) / float(kSteps);
            const float2 offset = screenDirection * (t * radiusPixels);
            for (int side = 0; side < 2; ++side)
            {
                const float2 samplePixel = float2(pixel) + 0.5 + (side == 0 ? offset : -offset);
                const float3 delta = positionAt(samplePixel) - position;
                const float distance = length(delta);
                const float weight = saturate((radius - distance) / (0.4 * radius));
                const float lowest = side == 0 ? cos(n + kPi * 0.5) : cos(n - kPi * 0.5);
                const float horizon = lerp(lowest, dot(delta, view) / max(distance, 1e-4), weight);
                if (side == 0)
                {
                    horizonPositive = max(horizonPositive, horizon);
                }
                else
                {
                    horizonNegative = max(horizonNegative, horizon);
                }
            }
        }
        const float h1 = n + clamp(acos(horizonPositive) - n, -kPi * 0.5, kPi * 0.5);
        const float h0 = n + clamp(-acos(horizonNegative) - n, -kPi * 0.5, kPi * 0.5);
        visibility += projectedLength * (integrateArc(h0, n) + integrateArc(h1, n));
    }
    // A single slice can exceed 1 (it is normalized only on average over all slice directions), so the
    // value is clamped after the blur averages neighbouring slice directions.
    g_output[id.xy] = max(visibility / float(kSlices), 0.0);
}

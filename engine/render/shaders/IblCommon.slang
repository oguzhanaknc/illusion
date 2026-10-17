// Shared code of the image-based lighting bake passes (Ibl*.slang). Every pass uses one binding layout
// (EnvironmentBaker.cpp): t0 source, s0 sampler, u0 output, push constants b0.

static const float kPi = 3.14159265358979;

struct BakeParams
{
    uint size;        // output size in texels (per face)
    float roughness;  // prefilter: perceptual roughness of this mip
    float sourceSize; // prefilter: source cube face size (mip 0)
    uint sampleCount; // prefilter / BRDF: number of samples
};

[[vk::push_constant]] ConstantBuffer<BakeParams> g_params;

// Direction through texel `xy` of cube face `face` (Vulkan/D3D face order +X, -X, +Y, -Y, +Z, -Z).
float3 cubeDirection(uint2 xy, uint face, uint size)
{
    const float2 st = (float2(xy) + 0.5) / float(size) * 2.0 - 1.0;
    const float s = st.x;
    const float t = st.y;
    float3 d;
    switch (face)
    {
    case 0: d = float3(1.0, -t, -s); break;
    case 1: d = float3(-1.0, -t, s); break;
    case 2: d = float3(s, 1.0, t); break;
    case 3: d = float3(s, -1.0, -t); break;
    case 4: d = float3(s, -t, 1.0); break;
    default: d = float3(-s, -t, -1.0); break;
    }
    return normalize(d);
}

// Equirectangular coordinates of a direction; must match equirectUv() in assets/Environment.cpp.
float2 equirectUv(float3 d)
{
    const float phi = atan2(d.x, -d.z);
    const float theta = acos(clamp(d.y, -1.0, 1.0));
    return float2(phi / (2.0 * kPi) + 0.5, theta / kPi);
}

float2 hammersley(uint i, uint count)
{
    return float2(float(i) / float(count), float(reversebits(i)) * 2.3283064365386963e-10);
}

// Half vector sampled from the GGX distribution around `n` (alpha = roughness^2).
float3 importanceSampleGgx(float2 xi, float3 n, float alpha)
{
    const float phi = 2.0 * kPi * xi.x;
    const float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (alpha * alpha - 1.0) * xi.y));
    const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    const float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    const float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, n));
    const float3 bitangent = cross(n, tangent);
    return tangent * h.x + bitangent * h.y + n * h.z;
}

float distributionGgx(float nDotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float visibilitySmithGgxCorrelated(float nDotV, float nDotL, float alpha)
{
    const float a2 = alpha * alpha;
    const float ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    const float ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-7);
}

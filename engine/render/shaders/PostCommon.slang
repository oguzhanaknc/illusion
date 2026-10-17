// Shared definitions of the post-processing passes. Layout must match PostProcessor.cpp.

static const float kPi = 3.14159265358979;

struct PostConstants
{
    float4x4 inverseProjection;      // clip -> view (jittered), for view-space reconstruction
    float4x4 inverseViewProjection;  // clip -> world (jittered)
    float4x4 previousViewProjection; // world -> clip of the previous frame (unjittered)
    float4 size;     // xy: render size in pixels, zw: 1 / size
    float4 ao;       // x: radius (world), y: intensity, z: pixels per world unit at view distance 1, w: noise offset
    float4 temporal; // x: weight of the current frame, y: 1 if the history is valid
    float4 bloom;    // x: number of bloom levels, y: intensity, z: 1 if bloom is on
    float4 output;   // x: exposure, y: 1 if the output needs sRGB encoding
};

struct PostPush
{
    uint level;    // bloom level being written
    uint firstPass; // bloom: 1 when downsampling from the full-resolution image
    uint2 padding;
};

ConstantBuffer<PostConstants> g_post : register(b0);
[[vk::push_constant]] ConstantBuffer<PostPush> g_push;

// View-space position of the pixel at `uv` (origin top-left) with reversed-Z depth `depth`.
float3 viewPosition(float2 uv, float depth)
{
    const float4 position = mul(g_post.inverseProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    return position.xyz / position.w;
}

float luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// Interleaved gradient noise (Jimenez 2014).
float interleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

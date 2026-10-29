// Adaptive infinite ground grid of the editor viewports: a full-screen triangle that intersects each pixel's
// view ray with an axis plane, draws minor and major lines with screen-space antialiasing, colours the two
// axes, fades with distance and hides behind scene surfaces by sampling the scene's reversed-Z depth.

struct Constants
{
    float4x4 inverseViewProjection; // unjittered clip to world
    float4x4 viewProjection;        // the scene's (jittered) world to clip, for the depth test
    float4 sizes;                   // render xy, output zw
    float4 eye;                     // xyz eye, w: plane normal axis (0 x, 1 y, 2 z)
    float4 scale;                   // x: minor cell size, y: minor fade-out (0..1), z: fade distance (0: none),
                                    // w: 1 to fade at grazing angles
    float4 color;                   // grid lines
    float4 axisU;                   // the line where the second plane coordinate is 0
    float4 axisV;                   // the line where the first plane coordinate is 0
};

ConstantBuffer<Constants> g_grid : register(b0);
Texture2D<float> g_depth : register(t0);

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId: SV_VertexID)
{
    const float2 clip = float2(float((vertexId << 1) & 2), float(vertexId & 2)) * 2.0 - 1.0;
    VertexOutput output;
    output.position = float4(clip, 0.0, 1.0);
    output.uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    return output;
}

float lineCoverage(float2 coord, float2 width)
{
    const float2 distance = abs(frac(coord - 0.5) - 0.5) / max(width, float2(1e-6));
    return 1.0 - min(min(distance.x, distance.y), 1.0);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
    float4 nearPoint = mul(g_grid.inverseViewProjection, float4(ndc, 1.0, 1.0));
    float4 midPoint = mul(g_grid.inverseViewProjection, float4(ndc, 0.5, 1.0));
    nearPoint /= nearPoint.w;
    midPoint /= midPoint.w;
    const float3 direction = midPoint.xyz - nearPoint.xyz;
    const uint axis = uint(g_grid.eye.w);
    const float denominator = direction[axis];
    const bool parallel = abs(denominator) < 1e-8;
    const float t = parallel ? -1.0 : -nearPoint[axis] / denominator;
    const float3 hit = nearPoint.xyz + direction * max(t, 0.0);
    const float2 coord = axis == 0 ? hit.yz : (axis == 1 ? hit.xz : hit.xy);

    // Derivatives first: the discards below make control flow divergent.
    const float cell = g_grid.scale.x;
    const float2 minorWidth = fwidth(coord / cell);
    const float2 majorWidth = fwidth(coord / (cell * 10.0));
    const float2 axisWidth = fwidth(coord);
    if (parallel || t <= 0.0)
    {
        discard;
    }
    const float4 clipHit = mul(g_grid.viewProjection, float4(hit, 1.0));
    const int2 pixel = clamp(int2(input.position.xy * g_grid.sizes.xy / g_grid.sizes.zw), int2(0),
                             int2(g_grid.sizes.xy) - 1);
    if (clipHit.w <= 0.0 || clipHit.z / clipHit.w + 1e-6 < g_depth.Load(int3(pixel, 0)))
    {
        discard;
    }

    const float minor = lineCoverage(coord / cell, minorWidth) * (1.0 - g_grid.scale.y) * 0.5;
    const float major = lineCoverage(coord / (cell * 10.0), majorWidth);
    const float alongU = 1.0 - min(abs(coord.y) / max(axisWidth.y, 1e-6) * 0.5, 1.0);
    const float alongV = 1.0 - min(abs(coord.x) / max(axisWidth.x, 1e-6) * 0.5, 1.0);
    float fade = 1.0;
    if (g_grid.scale.z > 0.0)
    {
        fade *= 1.0 - smoothstep(0.25 * g_grid.scale.z, g_grid.scale.z, length(hit - g_grid.eye.xyz));
    }
    if (g_grid.scale.w > 0.5)
    {
        fade *= saturate(abs(normalize(direction)[axis]) * 6.0);
    }
    float4 color = float4(g_grid.color.rgb, g_grid.color.a * max(minor, major));
    if (alongU > 0.0 || alongV > 0.0)
    {
        const float4 axisColor = alongU >= alongV ? g_grid.axisU : g_grid.axisV;
        const float coverage = max(alongU, alongV);
        color = float4(lerp(color.rgb, axisColor.rgb, coverage), max(color.a, axisColor.a * coverage));
    }
    color.a *= fade;
    if (color.a <= 0.001)
    {
        discard;
    }
    return color;
}

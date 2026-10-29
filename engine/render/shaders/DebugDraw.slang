// World-space line segments expanded into screen-space quads; never attaches the sampled scene depth.
struct Line { float4 start; float4 end; float4 color; };
struct Constants { float4x4 viewProjection; float4 sizes; float4 options; };
ConstantBuffer<Constants> g_view : register(b0);
StructuredBuffer<Line> g_lines : register(t0);
Texture2D<float> g_depth : register(t1);
struct VertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
    nointerpolation float onTop : TEXCOORD0;
};

bool clipPlane(inout float4 a, inout float4 b, float da, float db)
{
    if (da < 0 && db < 0) { return false; }
    if (da < 0) { a = lerp(a, b, da / (da - db)); }
    else if (db < 0) { b = lerp(a, b, da / (da - db)); }
    return true;
}

[shader("vertex")]
VertexOutput vertexMain(uint vertex: SV_VertexID, uint instance: SV_InstanceID)
{
    const Line line = g_lines[instance];
    float4 a = mul(g_view.viewProjection, float4(line.start.xyz, 1));
    float4 b = mul(g_view.viewProjection, float4(line.end.xyz, 1));
    VertexOutput output;
    output.color = line.color;
    output.onTop = line.end.w;
    output.position = float4(2, 2, 0, 1);
    // Clip in homogeneous coordinates before dividing; reversed-Z near is z=w, far is z=0.
    if (!clipPlane(a, b, a.w - a.z, b.w - b.z) || !clipPlane(a, b, a.z, b.z) ||
        a.w <= 0.000001 || b.w <= 0.000001) { return output; }
    const float2 direction = (b.xy / b.w - a.xy / a.w) * g_view.sizes.zw;
    const float lengthSquared = dot(direction, direction);
    if (lengthSquared < 0.000001) { return output; }
    const float2 offset = float2(-direction.y, direction.x) * rsqrt(lengthSquared) * line.start.w / g_view.sizes.zw;
    const uint ends[6] = {0, 0, 1, 1, 0, 1};
    const float sides[6] = {-1, 1, -1, -1, 1, 1};
    output.position = ends[vertex] == 0 ? a : b;
    output.position.xy += offset * sides[vertex] * output.position.w;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    const int2 pixel = clamp(int2(input.position.xy * g_view.sizes.xy / g_view.sizes.zw),
                            int2(0), int2(g_view.sizes.xy) - 1);
    if (input.onTop < 0.5 && input.position.z + 0.000001 < g_depth.Load(int3(pixel, 0))) { discard; }
    float3 color = max(input.color.rgb, float3(0));
    if (g_view.options.x > 0.5)
    {
        color = select(color <= 0.0031308, color * 12.92, 1.055 * pow(color, 1.0 / 2.4) - 0.055);
    }
    return float4(color, input.color.a);
}

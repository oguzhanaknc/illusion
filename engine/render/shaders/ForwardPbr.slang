// Forward PBR shading (glTF metallic-roughness): Cook-Torrance GGX specular with height-correlated
// Smith visibility and Schlick Fresnel, Lambert diffuse, directional and point lights, constant ambient,
// exposure and ACES tonemapping. Must match the C++ layouts in SceneRenderer.cpp and RenderResources.cpp.

static const uint kMaxDirectionalLights = 4;
static const uint kMaxPointLights = 16;
static const float kPi = 3.14159265358979;

struct DirectionalLightData
{
    float4 direction; // xyz: direction the light travels (normalized)
    float4 color;     // rgb: colour * intensity
};

struct PointLightData
{
    float4 positionRange; // xyz: position, w: range
    float4 color;         // rgb: colour * intensity
};

struct ViewConstants
{
    float4x4 viewProjection;
    float4 cameraPosition; // xyz; w: exposure
    float4 ambient;        // rgb: ambient light; w: 1 if the shader must encode sRGB itself
    uint directionalCount;
    uint pointCount;
    uint2 padding;
    DirectionalLightData directional[kMaxDirectionalLights];
    PointLightData points[kMaxPointLights];
};

struct InstanceData
{
    float4x4 world;
    float4x4 normalMatrix;
};

struct PushConstants
{
    uint instanceOffset;
};

struct MaterialConstants
{
    float4 baseColorFactor;
    float4 emissiveFactor;  // rgb; w: normal scale
    float metallicFactor;
    float roughnessFactor;
    float occlusionStrength;
    float alphaCutoff;      // < 0 when alpha masking is off
};

ConstantBuffer<ViewConstants> g_view : register(b0, space0);
StructuredBuffer<InstanceData> g_instances : register(t0, space0);
[[vk::push_constant]] ConstantBuffer<PushConstants> g_push;

ConstantBuffer<MaterialConstants> g_material : register(b0, space1);
Texture2D g_baseColorTexture : register(t0, space1);
Texture2D g_metallicRoughnessTexture : register(t1, space1);
Texture2D g_normalTexture : register(t2, space1);
Texture2D g_occlusionTexture : register(t3, space1);
Texture2D g_emissiveTexture : register(t4, space1);
SamplerState g_sampler : register(s0, space1);

struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION1;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uint instanceId: SV_InstanceID)
{
    const InstanceData instance = g_instances[g_push.instanceOffset + instanceId];
    const float4 world = mul(instance.world, float4(input.position, 1.0));
    VertexOutput output;
    output.position = mul(g_view.viewProjection, world);
    output.worldPosition = world.xyz;
    output.normal = mul((float3x3)instance.normalMatrix, input.normal);
    output.tangent = float4(mul((float3x3)instance.world, input.tangent.xyz), input.tangent.w);
    output.uv = input.uv;
    return output;
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
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

float3 fresnelSchlick(float3 f0, float vDotH)
{
    return f0 + (1.0 - f0) * pow(1.0 - vDotH, 5.0);
}

struct Surface
{
    float3 albedo;
    float3 f0;
    float alpha; // roughness squared
    float3 normal;
    float3 view;
};

float3 shade(Surface surface, float3 lightDirection, float3 radiance)
{
    const float3 l = lightDirection;
    const float3 h = normalize(surface.view + l);
    const float nDotL = saturate(dot(surface.normal, l));
    const float nDotV = max(dot(surface.normal, surface.view), 1e-4);
    const float nDotH = saturate(dot(surface.normal, h));
    const float vDotH = saturate(dot(surface.view, h));
    const float3 fresnel = fresnelSchlick(surface.f0, vDotH);
    const float3 specular = distributionGgx(nDotH, surface.alpha) *
                            visibilitySmithGgxCorrelated(nDotV, nDotL, surface.alpha) * fresnel;
    const float3 diffuse = (1.0 - fresnel) * surface.albedo / kPi;
    return (diffuse + specular) * radiance * nDotL;
}

// Normal from the normal map; without vertex tangents the tangent frame comes from screen-space
// derivatives of position and UV.
float3 surfaceNormal(VertexOutput input, bool frontFace)
{
    float3 n = normalize(input.normal);
    if (!frontFace)
    {
        n = -n;
    }
    float3 sampled = g_normalTexture.Sample(g_sampler, input.uv).xyz * 2.0 - 1.0;
    sampled.xy *= g_material.emissiveFactor.w;
    float3 t;
    float3 b;
    if (input.tangent.w != 0.0)
    {
        t = normalize(input.tangent.xyz - n * dot(n, input.tangent.xyz));
        b = cross(n, t) * input.tangent.w;
    }
    else
    {
        const float3 dp1 = ddx(input.worldPosition);
        const float3 dp2 = ddy(input.worldPosition);
        const float2 duv1 = ddx(input.uv);
        const float2 duv2 = ddy(input.uv);
        const float3 dp2perp = cross(dp2, n);
        const float3 dp1perp = cross(n, dp1);
        t = dp2perp * duv1.x + dp1perp * duv2.x;
        b = dp2perp * duv1.y + dp1perp * duv2.y;
        const float invMax = rsqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
        t *= invMax;
        b *= -invMax; // UV origin is top-left
    }
    return normalize(t * sampled.x + b * sampled.y + n * sampled.z);
}

float3 tonemapAces(float3 x)
{
    // Narkowicz 2015 fit of the ACES filmic curve.
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 linearToSrgb(float3 c)
{
    return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFace: SV_IsFrontFace) : SV_Target
{
    const float4 baseColor = g_baseColorTexture.Sample(g_sampler, input.uv) * g_material.baseColorFactor;
    if (g_material.alphaCutoff >= 0.0 && baseColor.a < g_material.alphaCutoff)
    {
        discard;
    }
    const float4 metallicRoughness = g_metallicRoughnessTexture.Sample(g_sampler, input.uv);
    const float metallic = saturate(metallicRoughness.b * g_material.metallicFactor);
    const float roughness = clamp(metallicRoughness.g * g_material.roughnessFactor, 0.045, 1.0);
    const float occlusion = lerp(1.0, g_occlusionTexture.Sample(g_sampler, input.uv).r, g_material.occlusionStrength);
    const float3 emissive = g_emissiveTexture.Sample(g_sampler, input.uv).rgb * g_material.emissiveFactor.rgb;

    Surface surface;
    surface.albedo = baseColor.rgb * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    surface.alpha = roughness * roughness;
    surface.normal = surfaceNormal(input, frontFace);
    surface.view = normalize(g_view.cameraPosition.xyz - input.worldPosition);

    float3 color = g_view.ambient.rgb * baseColor.rgb * occlusion;
    for (uint i = 0; i < g_view.directionalCount; ++i)
    {
        const DirectionalLightData light = g_view.directional[i];
        color += shade(surface, -light.direction.xyz, light.color.rgb);
    }
    for (uint i = 0; i < g_view.pointCount; ++i)
    {
        const PointLightData light = g_view.points[i];
        const float3 toLight = light.positionRange.xyz - input.worldPosition;
        const float distanceSquared = max(dot(toLight, toLight), 1e-4);
        // Inverse-square falloff windowed to reach zero at the range (Karis 2013).
        const float ratio = distanceSquared / (light.positionRange.w * light.positionRange.w);
        const float window = saturate(1.0 - ratio * ratio);
        const float attenuation = window * window / distanceSquared;
        color += shade(surface, toLight * rsqrt(distanceSquared), light.color.rgb * attenuation);
    }
    color += emissive;

    float3 mapped = tonemapAces(color * g_view.cameraPosition.w);
    if (g_view.ambient.w > 0.5)
    {
        mapped = linearToSrgb(mapped);
    }
    return float4(mapped, baseColor.a);
}

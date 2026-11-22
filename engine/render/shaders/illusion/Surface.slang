// The surface API of material shaders (ADR 0063). A `.shader` file is Slang that the shader compiler places
// between this file and the engine's passes (illusion/MaterialPass.slang); it declares
//
//     [ShadingModel("Default")]           // optional: Default, Unlit, Subsurface, TwoSidedFoliage, ClearCoat
//     struct Parameters                   // required, may be empty: the material's parameters, at most 256 bytes
//     {
//         [Color(1, 0.5, 0.2, 1)] float4 tint;               // colour (float3 or float4)
//         [Default(2)] [Range(0, 10)] float intensity;      // float, int or bool
//         [Default2(0.5, 0)] float2 scroll;                 // Default2/3/4 for float2/3/4
//         [Texture("white", "color")] Texture2DSlot mask;   // at most 8 textures
//     };
//
//     SurfaceOutput surface(SurfaceInput input);          // required
//     float3 worldOffset(WorldOffsetInput input);         // optional: world-space vertex offset
//
// Parameters without an attribute default to zero. A texture's default is "white", "black", "normal" (a flat
// normal map) or an asset path; its usage ("color", "linear", "normal" or "mask") picks how the image is
// imported. Read the parameters with parameters(input) and sample textures with sampleTexture().

#pragma once

#include "../Common.slang"

// Attributes of Parameters and its fields, read from the compiler's reflection.
[__AttributeUsage(_AttributeTargets.Struct)]
struct ShadingModelAttribute
{
    string name;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct DefaultAttribute
{
    float value;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct Default2Attribute
{
    float x;
    float y;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct Default3Attribute
{
    float x;
    float y;
    float z;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct Default4Attribute
{
    float x;
    float y;
    float z;
    float w;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct ColorAttribute
{
    float r;
    float g;
    float b;
    float a;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct RangeAttribute
{
    float minimum;
    float maximum;
};

[__AttributeUsage(_AttributeTargets.Var)]
struct TextureAttribute
{
    string fallback;
    string usage;
};

// A texture of the material: its slot (bound passes) or its descriptor index (bindless passes).
typealias Texture2DSlot = uint;

// What the surface function knows about the pixel it shades.
struct SurfaceInput
{
    float3 worldPosition;
    float3 normal;        // world-space geometric normal, facing the viewer, normalized
    float3 tangent;       // world-space tangent frame, normalized (derived from UVs without vertex tangents)
    float3 bitangent;
    float2 uv;
    float4 color;         // vertex colour (white: the engine's meshes have none)
    float3 viewDirection; // from the surface to the camera, normalized
    float time;           // seconds, the same in every pass of a frame
    float4 customData;    // the Entity's custom data (zero until the renderer provides it)
    bool frontFace;
    uint material;        // used by parameters() and sampleTexture()
};

// What the surface function returns; start from defaultSurface().
struct SurfaceOutput
{
    float3 baseColor;
    float3 normal; // tangent space, (0, 0, 1) for the geometric normal
    float metallic;
    float roughness;
    float occlusion;
    float3 emission; // linear radiance added to the lit colour
    float opacity;   // blended materials: coverage; masked materials: compared with opacityMask
    float opacityMask;
    float3 subsurfaceColor; // Subsurface: scatter colour; TwoSidedFoliage: transmitted colour
    float thickness;        // Subsurface and TwoSidedFoliage: 0 (thin, much light through) to 1 (opaque)
    float clearCoat;        // ClearCoat: strength of the second specular lobe
    float clearCoatRoughness;
};

// What worldOffset() knows about the vertex it moves. It runs for the current and the previous frame, with
// that frame's time, so it must depend on nothing else that changes over time.
struct WorldOffsetInput
{
    float3 worldPosition;
    float3 objectOrigin; // world position of the object's origin
    float3 normal;       // world-space normal
    float2 uv;
    float time; // seconds, of the frame being evaluated
    float4 customData;
    uint material;
};

// The defaults of SurfaceOutput: white, dielectric, rough 0.5, opaque.
SurfaceOutput defaultSurface()
{
    SurfaceOutput output;
    output.baseColor = float3(1.0, 1.0, 1.0);
    output.normal = float3(0.0, 0.0, 1.0);
    output.metallic = 0.0;
    output.roughness = 0.5;
    output.occlusion = 1.0;
    output.emission = float3(0.0, 0.0, 0.0);
    output.opacity = 1.0;
    output.opacityMask = 0.5;
    output.subsurfaceColor = float3(1.0, 1.0, 1.0);
    output.thickness = 1.0;
    output.clearCoat = 0.0;
    output.clearCoatRoughness = 0.1;
    return output;
}

// The parameters of the material being drawn. The passes define illusionMaterialParameters().
Parameters parameters(SurfaceInput input)
{
    return illusionMaterialParameters(input.material);
}

// The parameters of the material being drawn, in worldOffset().
Parameters parameters(WorldOffsetInput input)
{
    return illusionMaterialParameters(input.material);
}

// Samples a texture of the material (with derivatives: only in surface()).
float4 sampleTexture(SurfaceInput input, Texture2DSlot texture, float2 uv)
{
    return illusionSampleTexture(input.material, texture, uv);
}

// Samples mip @p level of a texture of the material (in surface() and worldOffset()).
float4 sampleTextureLevel(uint material, Texture2DSlot texture, float2 uv, float level)
{
    return illusionSampleTextureLevel(material, texture, uv, level);
}

// A tangent-space normal from a normal map texel (red and green; z rebuilt), with the strength @p scale.
float3 unpackNormal(float4 texel, float scale)
{
    return decodeNormalMap(texel.xy, scale);
}

// Hash of a cell to [0, 1).
float hash12(float2 p)
{
    float3 q = frac(float3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return frac((q.x + q.y) * q.z);
}

// Hash of a cell to [0, 1).
float hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return frac((p.x + p.y) * p.z);
}

// Smooth value noise in [0, 1].
float valueNoise(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    const float a = hash12(cell);
    const float b = hash12(cell + float2(1.0, 0.0));
    const float c = hash12(cell + float2(0.0, 1.0));
    const float d = hash12(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Smooth value noise in [0, 1].
float valueNoise(float3 p)
{
    const float3 cell = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0 - 2.0 * f);
    const float x00 = lerp(hash13(cell), hash13(cell + float3(1, 0, 0)), u.x);
    const float x10 = lerp(hash13(cell + float3(0, 1, 0)), hash13(cell + float3(1, 1, 0)), u.x);
    const float x01 = lerp(hash13(cell + float3(0, 0, 1)), hash13(cell + float3(1, 0, 1)), u.x);
    const float x11 = lerp(hash13(cell + float3(0, 1, 1)), hash13(cell + float3(1, 1, 1)), u.x);
    return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z);
}

// Fractal noise: @p octaves of valueNoise(), each at twice the frequency and half the amplitude, in [0, 1].
float fractalNoise(float3 p, uint octaves)
{
    float sum = 0.0;
    float amplitude = 0.5;
    float total = 0.0;
    for (uint i = 0; i < octaves; ++i)
    {
        sum += valueNoise(p) * amplitude;
        total += amplitude;
        p *= 2.0;
        amplitude *= 0.5;
    }
    return total > 0.0 ? sum / total : 0.0;
}

// Samples @p texture projected along the three world axes at @p scale texels per metre, blended by the normal
// (a higher @p sharpness gives narrower transitions).
float4 triplanar(SurfaceInput input, Texture2DSlot texture, float scale, float sharpness)
{
    float3 weights = pow(abs(input.normal), max(sharpness, 1.0));
    weights /= max(weights.x + weights.y + weights.z, 1e-5);
    const float3 p = input.worldPosition * scale;
    return sampleTexture(input, texture, p.zy) * weights.x + sampleTexture(input, texture, p.xz) * weights.y +
           sampleTexture(input, texture, p.xy) * weights.z;
}

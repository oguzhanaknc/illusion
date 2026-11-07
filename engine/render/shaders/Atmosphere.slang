// The atmosphere model of engine/assets/src/Atmosphere.cpp, for the sky background and the reflection cube
// map of a SkyAtmosphere (ADR 0049). Keep the two in step. Directions are normalized by the CPU.

struct AtmosphereData
{
    float4 sun;    // xyz: towards the sun, w: sun radiance scale (AtmosphereDesc::sunRadiance)
    float4 moon;   // xyz: towards the moon, w: moon radiance scale
    float4 params; // x: turbidity (1 to 10)
    float4 ground; // rgb: ground albedo
};

static const float kAtmospherePi = 3.14159265358979;
static const float3 kRayleighDepth = float3(0.0464, 0.108, 0.265);
static const float kMieDepthPerTurbidity = 0.005;
static const float3 kOzoneDepth = float3(0.03, 0.08, 0.004);
static const float kMieAsymmetry = 0.76;
static const float kSkyScale = 30.0;
static const float3 kMoonTint = float3(0.55, 0.7, 1.0);
static const float3 kNightGlow = float3(0.0006, 0.0009, 0.0018);

float atmosphereAirMass(float up)
{
    const float cosine = clamp(up, 0.0, 1.0);
    const float degrees = acos(cosine) * (180.0 / kAtmospherePi);
    return 1.0 / (cosine + 0.15 * pow(max(93.885 - degrees, 1e-3), -1.253));
}

float3 atmosphereMieDepth(AtmosphereData a)
{
    return float3(kMieDepthPerTurbidity * clamp(a.params.x, 1.0, 10.0));
}

float3 atmosphereScattered(AtmosphereData a, float3 view, float3 light)
{
    const float twilight = smoothstep(-0.15, 0.02, light.y);
    if (twilight <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }
    const float cosine = dot(view, light);
    const float rayleighPhase = 3.0 / (16.0 * kAtmospherePi) * (1.0 + cosine * cosine);
    const float g = kMieAsymmetry;
    const float miePhase = (1.0 - g * g) / (4.0 * kAtmospherePi * pow(max(1.0 + g * g - 2.0 * g * cosine, 1e-4), 1.5));
    const float3 mie = atmosphereMieDepth(a);
    const float3 extinction = kRayleighDepth + mie + kOzoneDepth;
    const float3 lightThrough = exp(-extinction * atmosphereAirMass(light.y));
    const float3 viewDepth = extinction * atmosphereAirMass(view.y);
    const float3 albedo = (kRayleighDepth * rayleighPhase + mie * miePhase) / extinction;
    return twilight * lightThrough * albedo * (1.0 - exp(-viewDepth));
}

float3 atmosphereSky(AtmosphereData a, float3 view)
{
    const float sunScale = kSkyScale * max(a.sun.w, 0.0);
    const float moonScale = kSkyScale * max(a.moon.w, 0.0);
    return atmosphereScattered(a, view, a.sun.xyz) * sunScale +
           atmosphereScattered(a, view, a.moon.xyz) * (moonScale * kMoonTint) + kNightGlow * max(a.sun.w, 0.0);
}

float3 atmosphereTransmittance(AtmosphereData a, float3 direction)
{
    return exp(-(kRayleighDepth + atmosphereMieDepth(a) + kOzoneDepth) * atmosphereAirMass(direction.y));
}

// Radiance of the sky towards the unit direction `view`, without discs and stars.
float3 atmosphereRadiance(AtmosphereData a, float3 view)
{
    if (view.y >= 0.0)
    {
        return atmosphereSky(a, view);
    }
    const float2 across = view.xz;
    const float acrossLength = length(across);
    const float3 horizonDirection = acrossLength > 1e-6 ? float3(across.x / acrossLength, 0.0, across.y / acrossLength)
                                                        : float3(1.0, 0.0, 0.0);
    const float3 horizon = atmosphereSky(a, horizonDirection);
    const float3 zenith = atmosphereSky(a, float3(0.0, 1.0, 0.0));
    const float3 sun = a.sun.xyz;
    const float3 sunlight = atmosphereTransmittance(a, sun) *
                            (smoothstep(-0.05, 0.1, sun.y) * max(sun.y, 0.0) * kSkyScale * 0.05 * max(a.sun.w, 0.0));
    const float3 ground = max(a.ground.rgb, 0.0) * (sunlight + zenith);
    return lerp(ground, horizon, exp(view.y * 12.0));
}

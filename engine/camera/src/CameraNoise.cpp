#include <illusion/camera/CameraNoise.hpp>

#include <cmath>

namespace illusion
{

namespace
{

constexpr f64 kTwoPi = 6.283185307179586;
constexpr f64 kSecondHarmonic = 2.17;

// MurmurHash3's finaliser.
u32 mix(u32 value) noexcept
{
    value ^= value >> 16;
    value *= 0x85EBCA6Bu;
    value ^= value >> 13;
    value *= 0xC2B2AE35u;
    value ^= value >> 16;
    return value;
}

// sin(2 pi cycles + phase), with the whole cycles removed first so that long playing times keep precision.
f64 wave(f64 cycles, f32 phase) noexcept
{
    return std::sin(kTwoPi * (cycles - std::floor(cycles)) + static_cast<f64>(phase));
}

} // namespace

f32 cameraNoisePhase(u32 seed, u32 channel) noexcept
{
    const u32 bits = mix(seed * 0x9E3779B9u + channel * 0x632BE5ABu + 0x2545F491u) >> 8;
    return static_cast<f32>(static_cast<f64>(bits) * (kTwoPi / 16777216.0));
}

CameraNoiseOffset sampleCameraNoise(const CameraNoiseSettings& settings, u32 seed, f64 time) noexcept
{
    const auto channel = [&](u32 index, f32 amplitude, f32 frequency) {
        const f64 cycles = static_cast<f64>(frequency) * static_cast<f64>(settings.frequencyGain) * time;
        const f64 value = 0.6 * wave(cycles, cameraNoisePhase(seed, index)) +
                          0.4 * wave(kSecondHarmonic * cycles, cameraNoisePhase(seed, index + 6));
        return static_cast<f32>(static_cast<f64>(amplitude) * static_cast<f64>(settings.amplitudeGain) *
                                value);
    };
    CameraNoiseOffset offset;
    for (u32 axis = 0; axis < 3; ++axis)
    {
        const auto i = static_cast<glm::length_t>(axis);
        offset.position[i] = channel(axis, settings.positionAmplitude[i], settings.positionFrequency[i]);
        offset.rotation[i] = channel(axis + 3, settings.rotationAmplitude[i], settings.rotationFrequency[i]);
    }
    return offset;
}

CameraNoiseOffset stepCameraNoise(const CameraNoiseSettings& settings, CameraNoiseState& state,
                                  f32 dt) noexcept
{
    if (dt > 0.0f)
    {
        state.time += static_cast<f64>(dt);
    }
    return sampleCameraNoise(settings, state.seed, state.time);
}

CameraPose applyCameraNoise(const CameraPose& pose, const CameraNoiseOffset& offset) noexcept
{
    CameraPose result = pose;
    result.position += pose.rotation * offset.position;
    result.rotation =
        glm::normalize(pose.rotation * glm::angleAxis(offset.rotation.y, Vec3(0.0f, 1.0f, 0.0f)) *
                       glm::angleAxis(offset.rotation.x, Vec3(1.0f, 0.0f, 0.0f)));
    result.dutch += offset.rotation.z;
    return result;
}

} // namespace illusion

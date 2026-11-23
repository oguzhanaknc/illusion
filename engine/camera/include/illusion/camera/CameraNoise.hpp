#pragma once

/// @file
/// Handheld noise of a virtual camera (docs/adr/0064-virtual-cameras.md): a deterministic sum of two sines
/// per channel, phased by a seed, added on top of the body and aim stages.

#include <illusion/camera/CameraPose.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// Settings of stepCameraNoise(): amplitudes and frequencies per axis of the camera's own space.
struct CameraNoiseSettings
{
    Vec3 positionAmplitude{0.0f};   ///< Largest offset along x, y and z, in metres.
    Vec3 positionFrequency{1.0f};   ///< Base frequency per position axis, in hertz.
    Vec3 rotationAmplitude{0.005f}; ///< Largest pitch, yaw and roll, in radians.
    Vec3 rotationFrequency{0.5f};   ///< Base frequency per rotation axis, in hertz.
    f32 amplitudeGain = 1.0f;       ///< Scales every amplitude.
    f32 frequencyGain = 1.0f;       ///< Scales every frequency.
};

/// Per-camera state of stepCameraNoise().
struct CameraNoiseState
{
    f64 time = 0.0; ///< Seconds of noise played.
    u32 seed = 0;   ///< Chooses the phases, so that cameras with the same settings shake differently.
};

/// A noise sample, in the camera's own space.
struct CameraNoiseOffset
{
    Vec3 position{0.0f}; ///< Offset along the camera's x, y and z, in metres.
    Vec3 rotation{0.0f}; ///< Pitch, yaw and roll (about the camera's x, y and z), in radians.
};

/// Phase in radians in [0, 2 pi) of noise channel @p channel (0 to 11) for @p seed.
[[nodiscard]] f32 cameraNoisePhase(u32 seed, u32 channel) noexcept;

/// The noise at @p time. Channel c (position x, y, z, then rotation x, y, z) with amplitude A and frequency f
/// is A (0.6 sin(2 pi f t + p(c)) + 0.4 sin(2 pi 2.17 f t + p(c + 6))), with p = cameraNoisePhase(seed, ...)
/// and the gains applied to A and f.
[[nodiscard]] CameraNoiseOffset sampleCameraNoise(const CameraNoiseSettings& settings, u32 seed,
                                                  f64 time) noexcept;

/// Advances the state's time by @p dt and samples the noise there.
[[nodiscard]] CameraNoiseOffset stepCameraNoise(const CameraNoiseSettings& settings, CameraNoiseState& state,
                                                f32 dt) noexcept;

/// @p pose moved and turned by @p offset in its own space; the roll adds to the Dutch angle.
[[nodiscard]] CameraPose applyCameraNoise(const CameraPose& pose, const CameraNoiseOffset& offset) noexcept;

} // namespace illusion

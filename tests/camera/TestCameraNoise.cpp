// Handheld camera noise (ADR 0064): the documented two-sine formula, seeds and phases, bounds, stepping and
// how an offset applies to a pose.

#include <illusion/camera/CameraNoise.hpp>

#include <doctest/doctest.h>

#include <cmath>

using namespace illusion;

namespace
{

constexpr f64 kTwoPi = 6.283185307179586;

CameraNoiseSettings handheld()
{
    CameraNoiseSettings settings;
    settings.positionAmplitude = Vec3(0.02f, 0.03f, 0.01f);
    settings.positionFrequency = Vec3(0.7f, 1.1f, 0.4f);
    settings.rotationAmplitude = Vec3(0.01f, 0.02f, 0.005f);
    settings.rotationFrequency = Vec3(0.3f, 0.5f, 0.9f);
    return settings;
}

} // namespace

TEST_CASE("Camera noise follows its documented two-sine formula")
{
    CameraNoiseSettings settings = handheld();
    settings.amplitudeGain = 2.0f;
    settings.frequencyGain = 1.5f;
    const u32 seed = 42;
    const f64 time = 3.25;
    const CameraNoiseOffset offset = sampleCameraNoise(settings, seed, time);
    // Position y is channel 1, rotation z channel 5.
    const auto expected = [&](u32 channel, f32 amplitude, f32 frequency) {
        const f64 f = static_cast<f64>(frequency) * 1.5;
        return 2.0 * static_cast<f64>(amplitude) *
               (0.6 * std::sin(kTwoPi * f * time + static_cast<f64>(cameraNoisePhase(seed, channel))) +
                0.4 * std::sin(kTwoPi * 2.17 * f * time +
                               static_cast<f64>(cameraNoisePhase(seed, channel + 6))));
    };
    CHECK(offset.position.y == doctest::Approx(expected(1, 0.03f, 1.1f)).epsilon(1e-4));
    CHECK(offset.rotation.z == doctest::Approx(expected(5, 0.005f, 0.9f)).epsilon(1e-4));
}

TEST_CASE("Camera noise phases depend on seed and channel and lie between 0 and 2 pi")
{
    for (u32 seed = 0; seed < 8; ++seed)
    {
        for (u32 channel = 0; channel < 12; ++channel)
        {
            const f32 phase = cameraNoisePhase(seed, channel);
            CHECK(phase >= 0.0f);
            CHECK(static_cast<f64>(phase) < kTwoPi);
        }
    }
    CHECK(cameraNoisePhase(1, 0) != cameraNoisePhase(2, 0));
    CHECK(cameraNoisePhase(1, 0) != cameraNoisePhase(1, 1));
    CHECK(cameraNoisePhase(7, 3) == cameraNoisePhase(7, 3));
}

TEST_CASE("Camera noise stays within its amplitudes and is zero without them")
{
    const CameraNoiseSettings settings = handheld();
    for (int i = 0; i < 500; ++i)
    {
        const CameraNoiseOffset offset = sampleCameraNoise(settings, 9, 0.037 * static_cast<f64>(i));
        for (glm::length_t axis = 0; axis < 3; ++axis)
        {
            CHECK(std::abs(offset.position[axis]) <= settings.positionAmplitude[axis] + 1e-7f);
            CHECK(std::abs(offset.rotation[axis]) <= settings.rotationAmplitude[axis] + 1e-7f);
        }
    }
    CameraNoiseSettings silent;
    silent.rotationAmplitude = Vec3(0.0f);
    const CameraNoiseOffset none = sampleCameraNoise(silent, 3, 12.5);
    CHECK(none.position == Vec3(0.0f));
    CHECK(none.rotation == Vec3(0.0f));
}

TEST_CASE("Stepping camera noise advances its time and seeds give different shakes")
{
    const CameraNoiseSettings settings = handheld();
    CameraNoiseState state{.time = 0.0, .seed = 5};
    static_cast<void>(stepCameraNoise(settings, state, 0.5f));
    const CameraNoiseOffset stepped = stepCameraNoise(settings, state, 0.5f);
    CHECK(state.time == 1.0);
    const CameraNoiseOffset sampled = sampleCameraNoise(settings, 5, 1.0);
    CHECK(stepped.position == sampled.position);
    CHECK(stepped.rotation == sampled.rotation);
    CHECK(sampleCameraNoise(settings, 6, 1.0).position != sampled.position);
}

TEST_CASE("A noise offset moves and turns the pose in its own space and adds its roll to the Dutch angle")
{
    CameraPose pose;
    pose.position = Vec3(1.0f, 2.0f, 3.0f);
    pose.rotation = glm::angleAxis(0.5f * 3.14159265f, Vec3(0.0f, 1.0f, 0.0f));
    pose.dutch = 0.1f;
    CameraNoiseOffset offset;
    offset.position = Vec3(1.0f, 0.0f, 0.0f);
    offset.rotation = Vec3(0.0f, 0.2f, 0.05f);
    const CameraPose shaken = applyCameraNoise(pose, offset);
    // The camera's x axis points along world -z after the quarter turn.
    CHECK(shaken.position.x == doctest::Approx(1.0f));
    CHECK(shaken.position.z == doctest::Approx(2.0f));
    CHECK(shaken.dutch == doctest::Approx(0.15f));
    const Quat expected = pose.rotation * glm::angleAxis(0.2f, Vec3(0.0f, 1.0f, 0.0f));
    CHECK(std::abs(glm::dot(shaken.rotation, expected)) == doctest::Approx(1.0f));
    CHECK(shaken.fovY == pose.fovY);
}

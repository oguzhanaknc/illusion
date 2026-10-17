#pragma once

// Shared helpers for the audio tests.

#include "TestSupport.hpp"

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

namespace illusion::test
{

inline constexpr u32 kRate = 48000;

// Encodes interleaved samples as a 16-bit PCM WAV file.
inline std::vector<std::byte> makeWav(std::span<const f32> samples, u32 channels, u32 sampleRate)
{
    std::vector<std::byte> out;
    const auto put = [&out](u32 value, u32 bytes) {
        for (u32 i = 0; i < bytes; ++i)
        {
            out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFu));
        }
    };
    const auto tag = [&out](std::string_view text) {
        for (const char c : text)
        {
            out.push_back(static_cast<std::byte>(c));
        }
    };
    const auto dataBytes = static_cast<u32>(samples.size() * 2);
    tag("RIFF");
    put(36 + dataBytes, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2); // PCM
    put(channels, 2);
    put(sampleRate, 4);
    put(sampleRate * channels * 2, 4);
    put(channels * 2, 2);
    put(16, 2);
    tag("data");
    put(dataBytes, 4);
    for (const f32 sample : samples)
    {
        const auto value = static_cast<i32>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        put(static_cast<u32>(value) & 0xFFFFu, 2);
    }
    return out;
}

inline std::vector<f32> constant(f32 value, u32 frames, u32 channels = 1)
{
    return std::vector<f32>(static_cast<usize>(frames) * channels, value);
}

inline std::vector<f32> sine(f32 frequency, u32 frames)
{
    std::vector<f32> samples(frames);
    for (u32 i = 0; i < frames; ++i)
    {
        samples[i] = 0.5f * std::sin(2.0f * std::numbers::pi_v<f32> * frequency * static_cast<f32>(i) /
                                     static_cast<f32>(kRate));
    }
    return samples;
}

// Root mean square of one channel of interleaved stereo frames [begin, end).
inline f32 rms(std::span<const f32> stereo, u32 channel, usize begin, usize end)
{
    f64 sum = 0.0;
    for (usize frame = begin; frame < end; ++frame)
    {
        const auto sample = static_cast<f64>(stereo[frame * 2 + channel]);
        sum += sample * sample;
    }
    return static_cast<f32>(std::sqrt(sum / static_cast<f64>(end - begin)));
}

inline f32 peak(std::span<const f32> stereo)
{
    f32 result = 0.0f;
    for (const f32 sample : stereo)
    {
        result = std::max(result, std::abs(sample));
    }
    return result;
}

// Sign changes of one channel over frames [begin, end).
inline u32 zeroCrossings(std::span<const f32> stereo, u32 channel, usize begin, usize end)
{
    u32 crossings = 0;
    for (usize frame = begin + 1; frame < end; ++frame)
    {
        if ((stereo[(frame - 1) * 2 + channel] < 0.0f) != (stereo[frame * 2 + channel] < 0.0f))
        {
            ++crossings;
        }
    }
    return crossings;
}

inline Transform at(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

struct Fixture
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    World world;
    std::unique_ptr<AudioWorld> audio;

    explicit Fixture(u32 maxVoices = 16, AssetStreamer* streamer = nullptr)
    {
        REQUIRE(files.mount("test", directory.path, MountAccess::ReadWrite) == FileError::None);
        AudioWorldDesc desc;
        desc.output = AudioOutput::Offline;
        desc.sampleRate = kRate;
        desc.maxVoices = maxVoices;
        desc.streamer = streamer;
        audio = std::make_unique<AudioWorld>(world, files, desc);
    }

    void writeWav(std::string_view path, std::span<const f32> samples, u32 channels = 1,
                  u32 sampleRate = kRate)
    {
        const std::vector<std::byte> wav = makeWav(samples, channels, sampleRate);
        REQUIRE(files.writeFile(path, wav) == FileError::None);
    }

    // Publishes the game state and mixes @p frames frames.
    std::vector<f32> mix(u32 frames, f64 deltaSeconds = 0.0)
    {
        audio->update(deltaSeconds);
        std::vector<f32> out(static_cast<usize>(frames) * 2);
        audio->render(out);
        return out;
    }
};

inline AudioSource sourceFor(std::string_view clip)
{
    AudioSource source;
    source.clip = clip;
    return source;
}

} // namespace illusion::test

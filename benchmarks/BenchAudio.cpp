#include "Budget.hpp"

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <random>
#include <vector>

using namespace illusion;

namespace
{

constexpr u32 kRate = 48000;

// A one-second 440 Hz mono tone as a 16-bit PCM WAV file.
std::vector<std::byte> makeToneWav()
{
    std::vector<std::byte> out;
    const auto put = [&out](u32 value, u32 bytes) {
        for (u32 i = 0; i < bytes; ++i)
        {
            out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFFu));
        }
    };
    const auto tag = [&out](const char* text) {
        for (; *text != '\0'; ++text)
        {
            out.push_back(static_cast<std::byte>(*text));
        }
    };
    const u32 dataBytes = kRate * 2;
    tag("RIFF");
    put(36 + dataBytes, 4);
    tag("WAVEfmt ");
    put(16, 4);
    put(1, 2);
    put(1, 2);
    put(kRate, 4);
    put(kRate * 2, 4);
    put(2, 2);
    put(16, 2);
    tag("data");
    put(dataBytes, 4);
    for (u32 i = 0; i < kRate; ++i)
    {
        const f32 sample = 0.25f * std::sin(2.0f * std::numbers::pi_v<f32> * 440.0f * static_cast<f32>(i) /
                                            static_cast<f32>(kRate));
        put(static_cast<u32>(static_cast<i32>(std::lround(sample * 32767.0f))) & 0xFFFFu, 2);
    }
    return out;
}

// A temporary directory with tone.wav, mounted as "bench:".
struct ToneFiles
{
    std::filesystem::path root = std::filesystem::temp_directory_path() /
                                 std::format("illusion-bench-audio-{}", std::random_device{}());
    VirtualFileSystem files;
    bool ready = false;

    ToneFiles()
    {
        std::filesystem::create_directories(root);
        ready = files.mount("bench", root, MountAccess::ReadWrite) == FileError::None &&
                files.writeFile("bench:/tone.wav", makeToneWav()) == FileError::None;
    }
    ~ToneFiles()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
    ToneFiles(const ToneFiles&) = delete;
    ToneFiles& operator=(const ToneFiles&) = delete;
};

AudioWorldDesc offline(u32 maxVoices)
{
    AudioWorldDesc desc;
    desc.output = AudioOutput::Offline;
    desc.sampleRate = kRate;
    desc.maxVoices = maxVoices;
    return desc;
}

// Looping spatial sources spread around the listener, optionally through a low-pass filter and the reverb.
void createSources(World& world, u32 count, bool effects = false)
{
    AudioSource source;
    source.clip = "bench:/tone.wav";
    source.loop = true;
    if (effects)
    {
        source.lowPassCutoff = 2000.0f;
        source.reverbSend = 0.3f;
    }
    for (u32 i = 0; i < count; ++i)
    {
        const f32 angle = static_cast<f32>(i) * 0.37f;
        Transform transform;
        transform.position = Vec3(std::cos(angle), 0.0f, std::sin(angle)) * (2.0f + static_cast<f32>(i % 16));
        world.create(transform, source);
    }
}

} // namespace

// Budgets are roughly 5-10x a desktop result; see Budget.hpp.

ILL_BENCHMARK("audio/mix", 60.0)
{
    constexpr u32 kVoices = 64;
    constexpr u32 kFrames = 512;
    ToneFiles tone;
    if (!tone.ready)
    {
        return;
    }
    World world;
    AudioWorld audio(world, tone.files, offline(kVoices));
    createSources(world, kVoices);
    audio.update(0.0);
    std::vector<f32> out(kFrames * AudioWorld::channelCount());
    bench.batch(kVoices * kFrames).unit("voice-frame").run("mix 64 spatial voices, 512 frames", [&] {
        audio.render(out);
        ankerl::nanobench::doNotOptimizeAway(out.data());
    });
}

ILL_BENCHMARK("audio/mix-effects", 120.0)
{
    constexpr u32 kVoices = 64;
    constexpr u32 kFrames = 512;
    ToneFiles tone;
    if (!tone.ready)
    {
        return;
    }
    World world;
    AudioWorld audio(world, tone.files, offline(kVoices));
    createSources(world, kVoices, true);
    audio.setBusFilter(AudioBus::Sfx, AudioBusFilter{8000.0f, 60.0f});
    audio.update(0.0);
    std::vector<f32> out(kFrames * AudioWorld::channelCount());
    bench.batch(kVoices * kFrames)
        .unit("voice-frame")
        .run("mix 64 spatial voices with low-pass, bus filters and reverb, 512 frames", [&] {
            audio.render(out);
            ankerl::nanobench::doNotOptimizeAway(out.data());
        });
}

ILL_BENCHMARK("audio/update", 120.0)
{
    constexpr u32 kSources = 256;
    ToneFiles tone;
    if (!tone.ready)
    {
        return;
    }
    World world;
    AudioWorld audio(world, tone.files, offline(kSources));
    createSources(world, kSources);
    audio.update(0.0);
    // Looping sources never finish, so update() does the same work every time without the mixer running.
    bench.batch(kSources).unit("source").run("update 256 playing sources", [&] { audio.update(1.0 / 60.0); });
}

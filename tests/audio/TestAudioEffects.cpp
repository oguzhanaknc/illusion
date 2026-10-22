#include "AudioTestSupport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/SceneSerializer.hpp>

#include <doctest/doctest.h>

#include <numbers>
#include <vector>

using namespace illusion;
using namespace illusion::test;

namespace
{

// Frames skipped before measuring a filtered signal, so that the filter has settled.
constexpr usize kSettle = 2048;

// Level of a one-shot of @p clip with @p params, measured on the left channel after the filter settled.
// The sound is stopped afterwards.
f32 levelOf(Fixture& f, AudioClipHandle clip, const AudioPlayParams& params = {})
{
    const AudioVoiceHandle voice = f.audio->playOneShot(clip, params);
    REQUIRE(voice.valid());
    const std::vector<f32> out = f.mix(8192);
    f.audio->stop(voice);
    f.mix(256);
    return rms(out, 0, kSettle, 8192);
}

// Peak of the mix over @p frames frames, rendered in blocks like a device would.
f32 peakOver(Fixture& f, u32 frames)
{
    f32 result = 0.0f;
    for (u32 done = 0; done < frames; done += 4800)
    {
        result = std::max(result, peak(f.mix(4800)));
    }
    return result;
}

} // namespace

TEST_CASE("a low-pass filter on a sound removes high frequencies and keeps low ones")
{
    Fixture f;
    const AudioClipHandle high = f.audio->createClip(sine(8000.0f, kRate), 1, kRate);
    const AudioClipHandle low = f.audio->createClip(sine(100.0f, kRate), 1, kRate);
    const f32 highDry = levelOf(f, high);
    const f32 lowDry = levelOf(f, low);
    REQUIRE(highDry > 0.1f);
    REQUIRE(lowDry > 0.1f);

    AudioPlayParams muffled;
    muffled.lowPassCutoff = 500.0f;
    CHECK(levelOf(f, high, muffled) < highDry * 0.05f);
    muffled.lowPassCutoff = 2000.0f;
    CHECK(levelOf(f, low, muffled) == doctest::Approx(lowDry).epsilon(0.05));
}

TEST_CASE("an AudioSource's low-pass cutoff can change while it plays")
{
    Fixture f;
    f.writeWav("test:/hiss.wav", sine(8000.0f, kRate));
    AudioSource source = sourceFor("test:/hiss.wav");
    source.spatial = false;
    source.loop = true;
    const Entity entity = f.world.create(source);
    const f32 dry = rms(f.mix(8192), 0, kSettle, 8192);
    REQUIRE(dry > 0.1f);
    f.world.get<AudioSource>(entity).lowPassCutoff = 500.0f;
    CHECK(rms(f.mix(8192), 0, kSettle, 8192) < dry * 0.05f);
    f.world.get<AudioSource>(entity).lowPassCutoff = 0.0f;
    CHECK(rms(f.mix(8192), 0, kSettle, 8192) == doctest::Approx(dry).epsilon(0.05));
}

TEST_CASE("bus filters apply to every sound on the bus")
{
    Fixture f;
    const AudioClipHandle high = f.audio->createClip(sine(8000.0f, kRate), 1, kRate);
    const AudioClipHandle low = f.audio->createClip(sine(100.0f, kRate), 1, kRate);
    const f32 highDry = levelOf(f, high);
    const f32 lowDry = levelOf(f, low);

    f.audio->setBusFilter(AudioBus::Sfx, AudioBusFilter{500.0f, 0.0f});
    CHECK(f.audio->busFilter(AudioBus::Sfx).lowPassCutoff == 500.0f);
    CHECK(levelOf(f, high) < highDry * 0.05f);
    AudioPlayParams music;
    music.bus = AudioBus::Music;
    CHECK(levelOf(f, high, music) == doctest::Approx(highDry).epsilon(0.05)); // other buses are untouched

    f.audio->setBusFilter(AudioBus::Sfx, AudioBusFilter{0.0f, 2000.0f});
    CHECK(levelOf(f, low) < lowDry * 0.05f);
    CHECK(levelOf(f, high) == doctest::Approx(highDry).epsilon(0.05));

    f.audio->setBusFilter(AudioBus::Sfx, AudioBusFilter{});
    CHECK(levelOf(f, low) == doctest::Approx(lowDry).epsilon(0.05));
}

TEST_CASE("the reverb rings after a sound that feeds it has ended and then goes silent")
{
    Fixture f;
    const AudioClipHandle burst = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    REQUIRE(f.audio->playOneShot(burst).valid());
    f.mix(480);
    CHECK(peakOver(f, 4800) == 0.0f); // dry only: silence after the burst

    AudioPlayParams wet;
    wet.reverbSend = 1.0f;
    REQUIRE(f.audio->playOneShot(burst, wet).valid());
    f.mix(480);
    CHECK(peakOver(f, 4800) > 0.001f); // the tail

    // The reverb disconnects once its tail has decayed, so the output becomes exactly silent.
    peakOver(f, 20 * kRate);
    CHECK(peak(f.mix(4800)) == 0.0f);
}

TEST_CASE("the reverb follows its settings and the volume of the bus that feeds it")
{
    Fixture f;
    const AudioClipHandle burst = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    AudioPlayParams wet;
    wet.reverbSend = 1.0f;
    const auto tail = [&] {
        REQUIRE(f.audio->playOneShot(burst, wet).valid());
        f.mix(480);
        const f32 level = peakOver(f, 9600);
        peakOver(f, 20 * kRate); // let it decay
        return level;
    };
    const f32 full = tail();
    REQUIRE(full > 0.001f);

    AudioReverbSettings settings;
    settings.volume = 0.5f;
    settings.roomSize = 0.9f;
    f.audio->setReverb(settings);
    CHECK(f.audio->reverb().roomSize == 0.9f);
    CHECK(tail() < full * 0.75f);

    settings.volume = 0.0f;
    f.audio->setReverb(settings);
    CHECK(tail() == 0.0f);

    f.audio->setReverb(AudioReverbSettings{});
    f.audio->setBusVolume(AudioBus::Sfx, 0.0f);
    CHECK(tail() == 0.0f);
}

TEST_CASE("a paused bus stops feeding the reverb and keeps its sounds in place")
{
    Fixture f;
    const AudioClipHandle clip = f.audio->createClip(constant(0.25f, 4800), 1, kRate);
    AudioPlayParams wet;
    wet.reverbSend = 1.0f;
    const AudioVoiceHandle voice = f.audio->playOneShot(clip, wet);
    REQUIRE(voice.valid());
    f.mix(1000);
    f.audio->setBusPaused(AudioBus::Sfx, true);
    f.mix(kRate); // far longer than the clip
    f.audio->update(0.0);
    CHECK(f.audio->isPlaying(voice));
    f.audio->setBusPaused(AudioBus::Sfx, false);
    f.mix(4800);
    f.audio->update(0.0);
    CHECK_FALSE(f.audio->isPlaying(voice));
}

TEST_CASE("sound cones make sources quieter behind them")
{
    Fixture f;
    f.writeWav("test:/horn.wav", constant(0.5f, kRate));
    f.world.create(Transform{}, AudioListener{});
    AudioSource source = sourceFor("test:/horn.wav");
    source.loop = true;
    source.attenuation = AudioAttenuation::None;
    source.coneInnerAngle = std::numbers::pi_v<f32> / 2.0f;
    source.coneOuterAngle = std::numbers::pi_v<f32>;
    source.coneOuterVolume = 0.1f;
    // In front of the listener; its forward axis (-z) points away from the listener.
    const Entity horn = f.world.create(at(Vec3(0.0f, 0.0f, -2.0f)), source);
    const f32 away = rms(f.mix(4800), 0, 2400, 4800);
    // Turned around, it faces the listener.
    f.world.get<Transform>(horn).rotation = glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
    f.mix(2400);
    const f32 facing = rms(f.mix(4800), 0, 2400, 4800);
    REQUIRE(facing > 0.05f);
    CHECK(away == doctest::Approx(facing * 0.1f).epsilon(0.1));
}

TEST_CASE("effect parameters are clamped")
{
    Fixture f;
    f.audio->setReverb(AudioReverbSettings{2.0f, -1.0f, 0.5f, -3.0f});
    CHECK(f.audio->reverb().roomSize == 1.0f);
    CHECK(f.audio->reverb().damping == 0.0f);
    CHECK(f.audio->reverb().width == 0.5f);
    CHECK(f.audio->reverb().volume == 0.0f);
    f.audio->setBusFilter(AudioBus::Music, AudioBusFilter{-5.0f, 1.0e9f});
    CHECK(f.audio->busFilter(AudioBus::Music).lowPassCutoff == 0.0f);
    // Cutoffs above the mixing range are clamped when mixing; the request is kept as given.
    CHECK(f.audio->busFilter(AudioBus::Music).highPassCutoff == 1.0e9f);
    AudioPlayParams params;
    params.bus = AudioBus::Music;
    const AudioClipHandle clip = f.audio->createClip(sine(100.0f, kRate), 1, kRate);
    CHECK(levelOf(f, clip, params) < 0.05f); // a high-pass at the top of the range removes almost all
}

TEST_CASE("mixing with filters and the reverb does not allocate once the voice pools are warm")
{
    Fixture f(8);
    const AudioClipHandle mono = f.audio->createClip(constant(0.1f, 240), 1, kRate);
    const AudioClipHandle stereo = f.audio->createClip(constant(0.1f, 240, 2), 2, kRate);
    const u64 warm = f.audio->stats().mixerAllocations;
    AudioPlayParams effects;
    effects.spatial = true;
    effects.position = Vec3(1.0f, 0.0f, -2.0f);
    for (int frame = 0; frame < 50; ++frame)
    {
        effects.lowPassCutoff = frame % 2 == 0 ? 800.0f : 0.0f;
        effects.reverbSend = frame % 3 == 0 ? 0.5f : 0.0f;
        f.audio->setBusFilter(AudioBus::Sfx, AudioBusFilter{frame % 4 == 0 ? 3000.0f : 0.0f, 100.0f});
        f.audio->playOneShot(mono, effects);
        f.audio->playOneShot(stereo, effects);
        f.mix(256, 1.0 / 60.0);
    }
    CHECK(f.audio->stats().mixerAllocations == warm);
}

TEST_CASE("effect and cone settings of AudioSources are saved in scenes")
{
    reflectCoreComponents();
    reflectAudioComponents();
    World world;
    AudioSource source = sourceFor("game:/fan.ogg");
    source.lowPassCutoff = 1200.0f;
    source.reverbSend = 0.4f;
    source.coneInnerAngle = 1.0f;
    source.coneOuterAngle = 2.0f;
    source.coneOuterVolume = 0.25f;
    world.create(source);
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 1);
    const AudioSource& copy = loaded.get<AudioSource>(result.entities[0]);
    CHECK(copy.lowPassCutoff == 1200.0f);
    CHECK(copy.reverbSend == 0.4f);
    CHECK(copy.coneInnerAngle == 1.0f);
    CHECK(copy.coneOuterAngle == 2.0f);
    CHECK(copy.coneOuterVolume == 0.25f);
}

namespace
{

struct StreamingFixture
{
    JobSystem jobs{JobSystemDesc{2, 256}};
    test::TempDirectory directory;
    VirtualFileSystem files;
    std::unique_ptr<AssetStreamer> streamer;
    World world;
    std::unique_ptr<AudioWorld> audio;

    StreamingFixture()
    {
        REQUIRE(files.mount("test", directory.path, MountAccess::ReadWrite) == FileError::None);
        AssetStreamerDesc streamerDesc;
        streamerDesc.cacheMount.clear();
        streamer = std::make_unique<AssetStreamer>(files, jobs, streamerDesc);
        AudioWorldDesc desc;
        desc.output = AudioOutput::Offline;
        desc.sampleRate = kRate;
        desc.maxVoices = 8;
        desc.streamer = streamer.get();
        audio = std::make_unique<AudioWorld>(world, files, desc);
    }

    ~StreamingFixture()
    {
        audio.reset(); // before the streamer it uses
        streamer.reset();
    }

    StreamingFixture(const StreamingFixture&) = delete;
    StreamingFixture& operator=(const StreamingFixture&) = delete;

    void writeWav(std::string_view path, std::span<const f32> samples)
    {
        REQUIRE(files.writeFile(path, makeWav(samples, 1, kRate)) == FileError::None);
    }

    // Finishes every background load and lets the AudioWorld see the results.
    void finishLoads()
    {
        streamer->waitIdle();
        audio->update(0.0);
    }

    std::vector<f32> mix(u32 frames)
    {
        audio->update(0.0);
        std::vector<f32> out(static_cast<usize>(frames) * 2);
        audio->render(out);
        return out;
    }
};

} // namespace

TEST_CASE("requestClip loads clips in the background through the asset streamer")
{
    StreamingFixture f;
    f.writeWav("test:/beep.wav", constant(0.5f, 4800));
    const AudioClipHandle clip = f.audio->requestClip("test:/beep.wav");
    REQUIRE(clip.valid());
    CHECK(f.audio->clipState(clip) == AssetState::Loading);
    CHECK_FALSE(f.audio->clipInfo(clip));
    CHECK_FALSE(f.audio->playOneShot(clip).valid()); // not ready yet
    CHECK(f.audio->requestClip("test:/beep.wav") == clip);
    CHECK(f.audio->loadClip("test:/beep.wav") == clip); // does not load it a second time
    CHECK(f.audio->stats().loadingClips == 1);

    f.finishLoads();
    CHECK(f.audio->clipState(clip) == AssetState::Ready);
    CHECK(f.audio->stats().loadingClips == 0);
    CHECK(f.audio->stats().clips == 1);
    const std::optional<AudioClipInfo> info = f.audio->clipInfo(clip);
    REQUIRE(info);
    CHECK(info->frames == 4800);
    CHECK(info->memoryBytes == 4800 * sizeof(f32));
    REQUIRE(f.audio->playOneShot(clip).valid());
    CHECK(peak(f.mix(1024)) == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("streamed clips can be loaded in the background")
{
    StreamingFixture f;
    f.writeWav("test:/music.wav", sine(440.0f, kRate));
    const AudioClipHandle clip = f.audio->requestClip("test:/music.wav", AudioClipMode::Stream);
    f.finishLoads();
    const std::optional<AudioClipInfo> info = f.audio->clipInfo(clip);
    REQUIRE(info);
    CHECK(info->mode == AudioClipMode::Stream);
    REQUIRE(f.audio->playOneShot(clip).valid());
    CHECK(rms(f.mix(4800), 0, 0, 4800) > 0.1f);
}

TEST_CASE("AudioSources wait for their clip to load in the background and then start")
{
    StreamingFixture f;
    f.writeWav("test:/engine.wav", constant(0.25f, kRate));
    AudioSource source = sourceFor("test:/engine.wav");
    source.spatial = false;
    const Entity entity = f.world.create(source);
    f.audio->update(0.0);
    CHECK_FALSE(f.audio->isPlaying(entity));
    CHECK(f.world.get<AudioPlayback>(entity).pendingStart);

    f.finishLoads();
    CHECK(f.audio->isPlaying(entity));
    CHECK_FALSE(f.world.get<AudioPlayback>(entity).pendingStart);
    CHECK(peak(f.mix(1024)) == doctest::Approx(0.25f).epsilon(0.01));

    // stop() also cancels a start that waits for a clip.
    AudioSource other = sourceFor("test:/other.wav");
    f.writeWav("test:/other.wav", constant(0.25f, 480));
    const Entity waiting = f.world.create(other);
    f.audio->update(0.0);
    f.audio->stop(waiting);
    f.finishLoads();
    CHECK_FALSE(f.audio->isPlaying(waiting));
}

TEST_CASE("clips that fail to load in the background are reported and their sources stay silent")
{
    StreamingFixture f;
    REQUIRE(f.files.writeFile("test:/garbage.wav", std::vector<std::byte>(64, std::byte{7})) ==
            FileError::None);
    const AudioClipHandle missing = f.audio->requestClip("test:/missing.wav");
    const AudioClipHandle garbage = f.audio->requestClip("test:/garbage.wav");
    CHECK_FALSE(f.audio->requestClip("../outside.wav").valid()); // not a virtual path: fails at once
    const Entity entity = f.world.create(sourceFor("test:/missing.wav"));
    f.audio->update(0.0); // the source waits for the clip
    CHECK(f.world.get<AudioPlayback>(entity).pendingStart);
    f.finishLoads();
    CHECK(f.audio->clipState(missing) == AssetState::Failed);
    CHECK(f.audio->clipState(garbage) == AssetState::Failed);
    CHECK_FALSE(f.audio->isPlaying(entity));
    CHECK_FALSE(f.world.get<AudioPlayback>(entity).pendingStart);
    CHECK(f.audio->stats().clips == 0);
    CHECK(f.audio->stats().loadingClips == 0);

    // Once the file exists, a new request loads it.
    f.writeWav("test:/missing.wav", constant(0.25f, 480));
    const AudioClipHandle retry = f.audio->requestClip("test:/missing.wav");
    f.finishLoads();
    CHECK(f.audio->clipState(retry) == AssetState::Ready);
}

TEST_CASE("a clip unloaded while it loads in the background is dropped")
{
    StreamingFixture f;
    f.writeWav("test:/beep.wav", constant(0.5f, 480));
    const AudioClipHandle clip = f.audio->requestClip("test:/beep.wav");
    f.audio->unloadClip(clip);
    CHECK(f.audio->clipState(clip) == AssetState::Failed);
    f.finishLoads();
    f.audio->update(0.0);
    CHECK(f.audio->stats().clips == 0);
    CHECK(f.audio->stats().loadingClips == 0);
    CHECK(f.audio->clipState(clip) == AssetState::Failed);

    // A source waiting for a clip that is unloaded gives up.
    f.writeWav("test:/hum.wav", constant(0.5f, 480));
    const Entity entity = f.world.create(sourceFor("test:/hum.wav"));
    f.audio->update(0.0);
    REQUIRE(f.world.get<AudioPlayback>(entity).pendingStart);
    f.audio->unloadClip(f.world.get<AudioPlayback>(entity).clip);
    f.audio->update(0.0);
    CHECK_FALSE(f.world.get<AudioPlayback>(entity).pendingStart);
    f.finishLoads();
    CHECK_FALSE(f.audio->isPlaying(entity));
}

TEST_CASE("without a streamer, requestClip loads at once")
{
    Fixture f;
    f.writeWav("test:/beep.wav", constant(0.5f, 480));
    const AudioClipHandle clip = f.audio->requestClip("test:/beep.wav");
    CHECK(f.audio->clipState(clip) == AssetState::Ready);
    CHECK(f.audio->clipState(AudioClipHandle{}) == AssetState::Failed);
}

#include "AudioTestSupport.hpp"

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
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

using namespace illusion;
using namespace illusion::test;

TEST_CASE("an offline AudioWorld mixes silence when nothing plays")
{
    Fixture f;
    CHECK(f.audio->output() == AudioOutput::Offline);
    CHECK(f.audio->sampleRate() == kRate);
    CHECK(AudioWorld::channelCount() == 2);
    const std::vector<f32> out = f.mix(1024);
    CHECK(peak(out) == 0.0f);
    CHECK(f.audio->stats().activeVoices == 0);
}

TEST_CASE("createClip converts the sample rate and downmixes to stereo")
{
    Fixture f;
    const std::vector<f32> half = constant(0.25f, 24000);
    const AudioClipHandle mono = f.audio->createClip(half, 1, 24000);
    REQUIRE(mono.valid());
    const std::optional<AudioClipInfo> monoInfo = f.audio->clipInfo(mono);
    REQUIRE(monoInfo);
    CHECK(monoInfo->channels == 1);
    CHECK(monoInfo->frames >= 47900);
    CHECK(monoInfo->frames <= 48000);
    CHECK(monoInfo->mode == AudioClipMode::Decode);

    const std::vector<f32> quad = constant(0.1f, 480, 4);
    const std::optional<AudioClipInfo> quadInfo = f.audio->clipInfo(f.audio->createClip(quad, 4, kRate));
    REQUIRE(quadInfo);
    CHECK(quadInfo->channels == 2);
    CHECK(quadInfo->frames == 480);
    CHECK(f.audio->stats().clips == 2);
}

TEST_CASE("loadClip decodes WAV files through the VFS and caches them by path")
{
    Fixture f;
    f.writeWav("test:/beep.wav", constant(0.5f, 4800, 2), 2);
    const AudioClipHandle clip = f.audio->loadClip("test:/beep.wav");
    REQUIRE(clip.valid());
    const std::optional<AudioClipInfo> info = f.audio->clipInfo(clip);
    REQUIRE(info);
    CHECK(info->channels == 2);
    CHECK(info->frames == 4800);
    CHECK(info->memoryBytes == 4800 * 2 * sizeof(f32));
    CHECK(f.audio->loadClip("test:/beep.wav") == clip);
    CHECK(f.audio->stats().clips == 1);

    SUBCASE("a clip at another rate is resampled to the mixing rate")
    {
        f.writeWav("test:/slow.wav", constant(0.5f, 2205), 1, 22050);
        const std::optional<AudioClipInfo> slow = f.audio->clipInfo(f.audio->loadClip("test:/slow.wav"));
        REQUIRE(slow);
        CHECK(slow->frames >= 4790);
        CHECK(slow->frames <= 4800);
    }
}

TEST_CASE("loadClip returns an invalid handle for missing and undecodable files")
{
    Fixture f;
    CHECK_FALSE(f.audio->loadClip("test:/missing.wav").valid());
    const std::vector<std::byte> garbage(256, std::byte{0x5A});
    REQUIRE(f.files.writeFile("test:/garbage.ogg", garbage) == FileError::None);
    CHECK_FALSE(f.audio->loadClip("test:/garbage.ogg").valid());
    CHECK_FALSE(f.audio->loadClip("test:/garbage.ogg", AudioClipMode::Stream).valid());
    CHECK_FALSE(f.audio->clipInfo(AudioClipHandle{}).has_value());
    CHECK_FALSE(f.audio->playOneShot(AudioClipHandle{}).valid());
    CHECK(f.audio->stats().clips == 0);
}

TEST_CASE("a one-shot plays its clip at its volume and releases its voice at the end")
{
    Fixture f;
    const AudioClipHandle clip = f.audio->createClip(constant(0.5f, 4800), 1, kRate);
    AudioPlayParams params;
    params.volume = 0.5f;
    const AudioVoiceHandle voice = f.audio->playOneShot(clip, params);
    REQUIRE(voice.valid());
    CHECK(f.audio->isPlaying(voice));
    CHECK(f.audio->stats().activeVoices == 1);

    const std::vector<f32> out = f.mix(9600);
    // A mono sound that is not spatial reaches both channels unchanged.
    CHECK(rms(out, 0, 1000, 4000) == doctest::Approx(0.25).epsilon(0.01));
    CHECK(rms(out, 1, 1000, 4000) == doctest::Approx(0.25).epsilon(0.01));
    CHECK(rms(out, 0, 4900, 9600) == 0.0f);

    f.audio->update(0.0);
    CHECK_FALSE(f.audio->isPlaying(voice));
    CHECK(f.audio->stats().activeVoices == 0);
}

TEST_CASE("looping sounds keep playing until they are stopped")
{
    Fixture f;
    const AudioClipHandle clip = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    AudioPlayParams params;
    params.loop = true;
    const AudioVoiceHandle voice = f.audio->playOneShot(clip, params);
    const std::vector<f32> looped = f.mix(4800);
    CHECK(rms(looped, 0, 100, 4800) == doctest::Approx(0.5).epsilon(0.01));
    f.audio->update(0.0);
    CHECK(f.audio->isPlaying(voice));

    f.audio->stop(voice);
    CHECK_FALSE(f.audio->isPlaying(voice));
    const std::vector<f32> stopped = f.mix(480);
    CHECK(peak(stopped) == 0.0f);
}

TEST_CASE("master and bus volumes scale the mix and paused buses keep their sounds")
{
    Fixture f;
    const AudioClipHandle clip = f.audio->createClip(constant(0.8f, 480), 1, kRate);
    AudioPlayParams params;
    params.bus = AudioBus::Music;
    params.loop = true;
    const AudioVoiceHandle voice = f.audio->playOneShot(clip, params);
    f.audio->playOneShot(clip, AudioPlayParams{}); // a short sound on the Sfx bus
    CHECK(rms(f.mix(2400), 0, 600, 2400) == doctest::Approx(0.8).epsilon(0.01));

    f.audio->setBusVolume(AudioBus::Music, 0.5f);
    CHECK(f.audio->busVolume(AudioBus::Music) == 0.5f);
    CHECK(rms(f.mix(2400), 0, 600, 2400) == doctest::Approx(0.4).epsilon(0.01));

    f.audio->setMasterVolume(0.5f);
    CHECK(f.audio->masterVolume() == 0.5f);
    CHECK(rms(f.mix(2400), 0, 600, 2400) == doctest::Approx(0.2).epsilon(0.01));

    f.audio->setBusPaused(AudioBus::Music, true);
    CHECK(f.audio->isBusPaused(AudioBus::Music));
    CHECK(peak(f.mix(2400)) == 0.0f);
    f.audio->update(0.0);
    CHECK(f.audio->isPlaying(voice));

    f.audio->setBusPaused(AudioBus::Music, false);
    CHECK(rms(f.mix(2400), 0, 600, 2400) == doctest::Approx(0.2).epsilon(0.01));
}

TEST_CASE("spatial sounds pan toward the side of the listener they are on")
{
    Fixture f;
    const AudioClipHandle clip = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    const Entity listener = f.world.create(Transform{}, AudioListener{});

    AudioPlayParams params;
    params.loop = true;
    params.spatial = true;
    params.position = Vec3(3.0f, 0.0f, 0.0f);
    const AudioVoiceHandle right = f.audio->playOneShot(clip, params);
    std::vector<f32> out = f.mix(9600);
    CHECK(rms(out, 1, 4800, 9600) > 2.0f * rms(out, 0, 4800, 9600));
    f.audio->stop(right);

    params.position = Vec3(-3.0f, 0.0f, 0.0f);
    const AudioVoiceHandle left = f.audio->playOneShot(clip, params);
    out = f.mix(9600);
    CHECK(rms(out, 0, 4800, 9600) > 2.0f * rms(out, 1, 4800, 9600));
    f.audio->stop(left);

    // Without distance attenuation the sound is still panned.
    params.attenuation = AudioAttenuation::None;
    params.position = Vec3(30.0f, 0.0f, 0.0f);
    const AudioVoiceHandle distant = f.audio->playOneShot(clip, params);
    out = f.mix(9600);
    CHECK(rms(out, 1, 4800, 9600) > 2.0f * rms(out, 0, 4800, 9600));
    f.audio->stop(distant);

    // Turned around (looking along +z), the listener hears a sound at +x on its left.
    f.world.get<Transform>(listener).rotation =
        glm::angleAxis(std::numbers::pi_v<f32>, Vec3(0.0f, 1.0f, 0.0f));
    params.position = Vec3(3.0f, 0.0f, 0.0f);
    f.audio->playOneShot(clip, params);
    out = f.mix(9600);
    CHECK(rms(out, 0, 4800, 9600) > 2.0f * rms(out, 1, 4800, 9600));
}

TEST_CASE("distance attenuation follows the selected model")
{
    struct Case
    {
        AudioAttenuation model;
        f32 distance;
        f32 expectedGain;
    };
    // minDistance 1, maxDistance 11, rolloff 1; the reference is a sound within minDistance.
    const Case cases[] = {
        {AudioAttenuation::Inverse, 4.0f, 0.25f},          // 1 / (1 + 3)
        {AudioAttenuation::Linear, 6.0f, 0.5f},            // 1 - 5 / 10
        {AudioAttenuation::Exponential, 2.0f, 0.5f},       // 2^-1
        {AudioAttenuation::None, 8.0f, 1.0f},              //
        {AudioAttenuation::Inverse, 100.0f, 1.0f / 11.0f}, // clamped to maxDistance
    };
    for (const Case& c : cases)
    {
        CAPTURE(static_cast<int>(c.model));
        CAPTURE(c.distance);
        Fixture f;
        const AudioClipHandle clip = f.audio->createClip(constant(0.5f, 480), 1, kRate);
        AudioPlayParams params;
        params.loop = true;
        params.spatial = true;
        params.attenuation = c.model;
        params.minDistance = 1.0f;
        params.maxDistance = 11.0f;
        params.position = Vec3(0.0f, 0.0f, -0.5f);
        const AudioVoiceHandle close = f.audio->playOneShot(clip, params);
        std::vector<f32> out = f.mix(9600);
        const f32 reference = rms(out, 0, 4800, 9600);
        REQUIRE(reference > 0.0f);
        f.audio->stop(close);

        params.position = Vec3(0.0f, 0.0f, -c.distance);
        f.audio->playOneShot(clip, params);
        out = f.mix(9600);
        CHECK(rms(out, 0, 4800, 9600) / reference ==
              doctest::Approx(static_cast<f64>(c.expectedGain)).epsilon(0.02));
        CHECK(rms(out, 0, 4800, 9600) ==
              doctest::Approx(static_cast<f64>(rms(out, 1, 4800, 9600))).epsilon(0.01));
    }
}

TEST_CASE("the doppler effect raises the pitch of a sound that approaches the listener")
{
    Fixture f;
    f.writeWav("test:/tone.wav", sine(1000.0f, kRate));
    f.world.create(Transform{}, AudioListener{});
    AudioSource source = sourceFor("test:/tone.wav");
    source.loop = true;
    source.attenuation = AudioAttenuation::None;

    constexpr f64 kDelta = 0.1;
    const Entity still = f.world.create(at(Vec3(0.0f, 0.0f, -50.0f)), source);
    std::vector<f32> out;
    for (int i = 0; i < 3; ++i)
    {
        out = f.mix(4800, kDelta);
    }
    const u32 stillCrossings = zeroCrossings(out, 0, 0, 4800);
    CHECK(static_cast<f64>(stillCrossings) == doctest::Approx(200.0).epsilon(0.02)); // 1000 Hz for 0.1 s
    f.world.destroy(still);

    // 40 m/s toward the listener: the pitch rises by c / (c - v).
    const Entity moving = f.world.create(at(Vec3(0.0f, 0.0f, -50.0f)), source);
    for (int i = 0; i < 3; ++i)
    {
        f.world.get<Transform>(moving).position.z += 4.0f;
        out = f.mix(4800, kDelta);
    }
    const f64 expected = 343.3 / (343.3 - 40.0);
    CHECK(static_cast<f64>(zeroCrossings(out, 0, 0, 4800)) / stillCrossings ==
          doctest::Approx(expected).epsilon(0.03));
}

TEST_CASE("Inactive sources fall silent and start again when they become active")
{
    Fixture f;
    HierarchyIndex hierarchy;
    f.writeWav("test:/hum.wav", constant(0.5f, 480));
    AudioSource source = sourceFor("test:/hum.wav");
    source.loop = true;
    source.spatial = false;
    const Entity speaker = f.world.create(Transform{});
    const Entity emitter = f.world.create(Transform{}, source);
    REQUIRE(setParent(f.world, emitter, speaker));
    updateTransformHierarchy(f.world, hierarchy);
    CHECK(peak(f.mix(2400)) > 0.4f);

    setEnabled(f.world, speaker, false); // disabling the parent silences the child
    updateTransformHierarchy(f.world, hierarchy);
    f.mix(480);
    CHECK(peak(f.mix(2400)) == 0.0f);
    CHECK_FALSE(f.audio->isPlaying(emitter));
    CHECK_FALSE(f.world.has<AudioPlayback>(emitter));
    CHECK(f.audio->stats().activeVoices == 0);

    setEnabled(f.world, speaker, true);
    updateTransformHierarchy(f.world, hierarchy);
    f.mix(480);
    CHECK(f.audio->isPlaying(emitter));
    CHECK(peak(f.mix(2400)) > 0.4f);
}

TEST_CASE("AudioSource Entities start on update and stop when destroyed or when the source is removed")
{
    Fixture f;
    f.writeWav("test:/hum.wav", constant(0.5f, 480));
    AudioSource source = sourceFor("test:/hum.wav");
    source.loop = true;
    source.spatial = false;
    const Entity first = f.world.create(Transform{}, source);
    const Entity second = f.world.create(source); // no Transform: still plays
    CHECK_FALSE(f.audio->isPlaying(first));

    const std::vector<f32> out = f.mix(2400);
    CHECK(f.audio->isPlaying(first));
    CHECK(f.audio->isPlaying(second));
    CHECK(f.world.has<AudioPlayback>(first));
    CHECK(rms(out, 0, 600, 2400) == doctest::Approx(1.0).epsilon(0.01)); // two sounds of 0.5
    CHECK(f.audio->stats().clips == 1);

    f.world.destroy(first);
    f.world.remove<AudioSource>(second);
    CHECK(peak(f.mix(480)) == 0.0f);
    CHECK_FALSE(f.audio->isPlaying(second));
    CHECK_FALSE(f.world.has<AudioPlayback>(second));
    CHECK(f.audio->stats().activeVoices == 0);
}

TEST_CASE("play() starts and restarts sources and stop() stops them")
{
    Fixture f;
    f.writeWav("test:/blip.wav", constant(0.5f, 4800));
    AudioSource source = sourceFor("test:/blip.wav");
    source.playOnStart = false;
    source.spatial = false;
    const Entity entity = f.world.create(source);
    CHECK(peak(f.mix(480)) == 0.0f);
    CHECK(f.world.has<AudioPlayback>(entity));
    CHECK(f.world.get<AudioPlayback>(entity).clip.valid());

    f.audio->play(entity);
    CHECK(f.audio->isPlaying(entity));
    std::vector<f32> out = f.mix(2400);
    CHECK(rms(out, 0, 100, 2400) > 0.4f);

    // Restarting plays the whole clip again: 4800 more frames.
    f.audio->play(entity);
    out = f.mix(4800);
    CHECK(rms(out, 0, 100, 4700) > 0.4f);
    out = f.mix(480);
    CHECK(peak(out) == 0.0f);

    f.audio->play(entity);
    f.audio->stop(entity);
    CHECK_FALSE(f.audio->isPlaying(entity));
    CHECK(peak(f.mix(480)) == 0.0f);

    SUBCASE("a source with a missing clip does not play")
    {
        f.world.get<AudioSource>(entity).clip = AssetRef("test:/missing.wav");
        f.audio->play(entity);
        CHECK_FALSE(f.audio->isPlaying(entity));
        CHECK_FALSE(f.world.get<AudioPlayback>(entity).clip.valid());
    }
}

TEST_CASE("voices go to higher priorities and sounds are dropped when every voice is more important")
{
    Fixture f(2);
    const AudioClipHandle clip = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    AudioPlayParams params;
    params.loop = true;
    const AudioVoiceHandle oldest = f.audio->playOneShot(clip, params);
    const AudioVoiceHandle newer = f.audio->playOneShot(clip, params);

    params.priority = 200;
    const AudioVoiceHandle important = f.audio->playOneShot(clip, params);
    CHECK(important.valid());
    CHECK_FALSE(f.audio->isPlaying(oldest));
    CHECK(f.audio->isPlaying(newer));
    CHECK(f.audio->stats().stolenVoices == 1);

    params.priority = 50;
    CHECK_FALSE(f.audio->playOneShot(clip, params).valid());
    CHECK(f.audio->stats().droppedSounds == 1);
    CHECK(f.audio->stats().activeVoices == 2);
}

TEST_CASE("an unloaded clip keeps playing on its voices and is freed afterwards")
{
    Fixture f;
    const AudioClipHandle clip = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    AudioPlayParams params;
    params.loop = true;
    const AudioVoiceHandle voice = f.audio->playOneShot(clip, params);
    f.mix(480);

    f.audio->unloadClip(clip);
    CHECK_FALSE(f.audio->clipInfo(clip).has_value());
    CHECK_FALSE(f.audio->playOneShot(clip).valid());
    CHECK(f.audio->stats().clips == 0);
    CHECK(rms(f.mix(480), 0, 0, 480) == doctest::Approx(0.5).epsilon(0.01));

    f.audio->stop(voice);
    f.mix(480); // the mixer drops the voice
    f.audio->update(0.0);
    CHECK(f.audio->stats().clipBytes == 0);

    // The slot is reused with a new generation.
    const AudioClipHandle next = f.audio->createClip(constant(0.5f, 480), 1, kRate);
    CHECK(next.index == clip.index);
    CHECK(next.generation != clip.generation);
    CHECK_FALSE(f.audio->clipInfo(clip).has_value());
}

TEST_CASE("streamed clips stay compressed and decode while playing")
{
    Fixture f;
    f.writeWav("test:/music.wav", constant(0.5f, 9600, 2), 2);
    const AudioClipHandle clip = f.audio->loadClip("test:/music.wav", AudioClipMode::Stream);
    REQUIRE(clip.valid());
    const std::optional<AudioClipInfo> info = f.audio->clipInfo(clip);
    REQUIRE(info);
    CHECK(info->mode == AudioClipMode::Stream);
    CHECK(info->channels == 2);
    CHECK(info->frames == 9600);
    CHECK(info->memoryBytes == 44 + 9600 * 2 * 2); // the 16-bit WAV file

    const AudioVoiceHandle first = f.audio->playOneShot(clip);
    const AudioVoiceHandle second = f.audio->playOneShot(clip); // each voice has its own decoder
    const std::vector<f32> out = f.mix(14400);
    CHECK(rms(out, 0, 100, 9500) == doctest::Approx(1.0).epsilon(0.01));
    CHECK(peak(std::span<const f32>(out).subspan(9700 * 2)) == 0.0f);
    f.audio->update(0.0);
    CHECK_FALSE(f.audio->isPlaying(first));
    CHECK_FALSE(f.audio->isPlaying(second));
}

TEST_CASE("mixing does not allocate once the voice pools are warm")
{
    Fixture f(8);
    const AudioClipHandle mono = f.audio->createClip(constant(0.1f, 240), 1, kRate);
    const AudioClipHandle stereo = f.audio->createClip(constant(0.1f, 240, 2), 2, kRate);
    const u64 warm = f.audio->stats().mixerAllocations;
    CHECK(warm > 0);
    AudioPlayParams spatial;
    spatial.spatial = true;
    spatial.position = Vec3(1.0f, 0.0f, -2.0f);
    for (int frame = 0; frame < 50; ++frame)
    {
        f.audio->playOneShot(mono, spatial);
        f.audio->playOneShot(stereo);
        f.mix(256, 1.0 / 60.0);
    }
    CHECK(f.audio->stats().mixerAllocations == warm);
}

TEST_CASE("audio components are saved in scenes and the runtime state is not")
{
    reflectCoreComponents();
    reflectAudioComponents();
    Fixture f;
    f.writeWav("test:/wind.wav", constant(0.1f, 480));
    AudioSource source = sourceFor("test:/wind.wav");
    source.bus = AudioBus::Ambience;
    source.attenuation = AudioAttenuation::Linear;
    source.volume = 0.75f;
    source.loop = true;
    source.priority = 7;
    f.world.create(Transform{}, source);
    f.world.create(Transform{}, AudioListener{});
    f.audio->update(0.0);

    assignMissingGuids(f.world);
    const SceneSaveResult saved = saveScene(f.world, {.order = SceneOrder::AsGiven});
    REQUIRE(saved.ok());
    CHECK(saved.skippedComponents == 1); // AudioPlayback
    CHECK(saved.json.find("\"bus\": \"Ambience\"") != std::string::npos);
    CHECK(saved.json.find("\"attenuation\": \"Linear\"") != std::string::npos);
    CHECK(saved.json.find("illusion.AudioListener") != std::string::npos);

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 2);
    const AudioSource& copy = loaded.get<AudioSource>(result.entities[0]);
    CHECK(copy.clip.path() == "test:/wind.wav");
    CHECK(copy.bus == AudioBus::Ambience);
    CHECK(copy.attenuation == AudioAttenuation::Linear);
    CHECK(copy.volume == 0.75f);
    CHECK(copy.loop);
    CHECK(copy.priority == 7);
    CHECK(loaded.has<AudioListener>(result.entities[1]));
}

TEST_CASE("the null output mixes on its own thread")
{
    VirtualFileSystem files;
    World world;
    AudioWorldDesc desc;
    desc.output = AudioOutput::Null;
    desc.maxVoices = 4;
    AudioWorld audio(world, files, desc);
    CHECK(audio.output() == AudioOutput::Null);
    CHECK(audio.sampleRate() == 48000);

    const AudioClipHandle clip = audio.createClip(constant(0.5f, 480), 1, kRate);
    AudioPlayParams params;
    params.loop = true;
    const AudioVoiceHandle voice = audio.playOneShot(clip, params);
    for (int i = 0; i < 10; ++i)
    {
        audio.update(1.0 / 60.0);
    }
    CHECK(audio.isPlaying(voice));
    audio.stop(voice);
    audio.update(1.0 / 60.0);
    CHECK_FALSE(audio.isPlaying(voice));

#if ILL_ASSERTS_ENABLED
    std::vector<f32> out(64);
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS_AS(audio.render(out), test::AssertionFailure);
#endif
}

TEST_CASE("version 1 scenes with an AudioSource clip path still load")
{
    reflectCoreComponents();
    reflectAudioComponents();
    constexpr std::string_view kVersion1 = R"({
  "format": "illusion.scene",
  "version": 1,
  "types": {"illusion.AudioSource": 1},
  "entities": [{"illusion.AudioSource": {"clip": "test:/audio/../wind.wav", "volume": 0.5}}]
})";
    World world;
    const SceneLoadResult result = loadScene(world, kVersion1);
    REQUIRE_MESSAGE(result.ok(), result.error);
    REQUIRE(result.entities.size() == 1);
    const AudioSource& source = world.get<AudioSource>(result.entities[0]);
    CHECK(source.clip.path() == "test:/wind.wav");
    CHECK(source.volume == 0.5f);
}

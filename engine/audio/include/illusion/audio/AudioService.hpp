#pragma once

/// @file
/// The audio part of the scripting API: what a game module may ask of the host's AudioWorld.
///
/// @code
/// if (illusion::AudioService* audio = context.service<illusion::AudioService>())
/// {
///     const auto hit = audio->loadClip("game:/hit.wav", illusion::AudioClipMode::Decode);
///     audio->playOneShot(hit, illusion::AudioPlayParams{});
/// }
/// @endcode

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/audio/AudioTypes.hpp>
#include <illusion/core/AssetState.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/ecs/Entity.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

/// Sound of one Simulation World, implemented by the host (AudioWorldService over an AudioWorld) and
/// found by modules with `context.service<AudioService>()`. Clips and voices belong to the host, so they
/// stay valid across hot reloads of the module (ADR 0041). The functions behave like the AudioWorld
/// functions of the same name. Adding a function changes the module API version.
/// @ingroup scripting_api
class AudioService : NonCopyable
{
public:
    /// Loads a clip now (Decode) or opens it for streaming; invalid if the file cannot be read.
    virtual AudioClipHandle loadClip(std::string_view virtualPath, AudioClipMode mode) = 0;
    /// Starts loading a clip in the background; see clipState().
    virtual AudioClipHandle requestClip(std::string_view virtualPath, AudioClipMode mode) = 0;
    /// Ready once a clip can play, Loading while it loads in the background, Failed otherwise.
    [[nodiscard]] virtual AssetState clipState(AudioClipHandle clip) const = 0;
    /// Creates a clip from interleaved samples in [-1, 1].
    virtual AudioClipHandle createClip(std::span<const f32> samples, u32 channels, u32 sampleRate) = 0;
    /// Unloads a clip; voices playing it stop.
    virtual void unloadClip(AudioClipHandle clip) = 0;
    /// Channels, length and memory of a loaded clip.
    [[nodiscard]] virtual std::optional<AudioClipInfo> clipInfo(AudioClipHandle clip) const = 0;

    /// Starts the AudioSource of @p entity.
    virtual void play(Entity entity) = 0;
    /// Stops the AudioSource of @p entity.
    virtual void stop(Entity entity) = 0;
    /// True while the AudioSource of @p entity plays.
    [[nodiscard]] virtual bool isPlaying(Entity entity) const = 0;
    /// Plays a clip without an Entity.
    virtual AudioVoiceHandle playOneShot(AudioClipHandle clip, const AudioPlayParams& params) = 0;
    /// Stops a sound from playOneShot().
    virtual void stopVoice(AudioVoiceHandle voice) = 0;
    /// True while a sound from playOneShot() plays.
    [[nodiscard]] virtual bool isVoicePlaying(AudioVoiceHandle voice) const = 0;

    /// Sets the linear gain of the whole mix.
    virtual void setMasterVolume(f32 volume) = 0;
    /// Linear gain of the whole mix.
    [[nodiscard]] virtual f32 masterVolume() const = 0;
    /// Sets the linear gain of a bus.
    virtual void setBusVolume(AudioBus bus, f32 volume) = 0;
    /// Linear gain of a bus.
    [[nodiscard]] virtual f32 busVolume(AudioBus bus) const = 0;
    /// Pauses or resumes every sound on a bus.
    virtual void setBusPaused(AudioBus bus, bool paused) = 0;
    /// True if a bus is paused.
    [[nodiscard]] virtual bool isBusPaused(AudioBus bus) const = 0;
    /// Sets the low- and high-pass filter of a bus.
    virtual void setBusFilter(AudioBus bus, const AudioBusFilter& filter) = 0;
    /// Filter of a bus.
    [[nodiscard]] virtual AudioBusFilter busFilter(AudioBus bus) const = 0;
    /// Sets the reverb that sounds send to.
    virtual void setReverb(const AudioReverbSettings& settings) = 0;
    /// The reverb settings.
    [[nodiscard]] virtual AudioReverbSettings reverb() const = 0;

protected:
    AudioService() = default;
    virtual ~AudioService() = default;
};

} // namespace illusion

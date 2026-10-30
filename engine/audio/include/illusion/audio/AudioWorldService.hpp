#pragma once

/// @file
/// The host's implementation of AudioService over AudioWorld.
///
/// @code
/// illusion::AudioWorld audio(world, files);
/// illusion::AudioWorldService audioService(audio);
/// services.provide<illusion::AudioService>(audioService);
/// @endcode

#include <illusion/audio/AudioService.hpp>
#include <illusion/audio/AudioWorld.hpp>

namespace illusion
{

/// AudioService that forwards every call to an AudioWorld, which must outlive it. Created by the host; a
/// shared module that constructs one fails to link, since only the host has the library's code (ADR 0041).
class AudioWorldService final : public AudioService
{
public:
    /// Serves @p audio.
    explicit AudioWorldService(AudioWorld& audio) noexcept
        : m_audio(audio)
    {
    }
    ~AudioWorldService() override;

    /// The AudioWorld it serves.
    [[nodiscard]] AudioWorld& audio() const noexcept { return m_audio; }

    /// @name AudioService
    /// @{
    AudioClipHandle loadClip(std::string_view virtualPath, AudioClipMode mode) override;
    AudioClipHandle requestClip(std::string_view virtualPath, AudioClipMode mode) override;
    [[nodiscard]] AssetState clipState(AudioClipHandle clip) const override;
    AudioClipHandle createClip(std::span<const f32> samples, u32 channels, u32 sampleRate) override;
    void unloadClip(AudioClipHandle clip) override;
    [[nodiscard]] std::optional<AudioClipInfo> clipInfo(AudioClipHandle clip) const override;
    void play(Entity entity) override;
    void stop(Entity entity) override;
    [[nodiscard]] bool isPlaying(Entity entity) const override;
    AudioVoiceHandle playOneShot(AudioClipHandle clip, const AudioPlayParams& params) override;
    void stopVoice(AudioVoiceHandle voice) override;
    [[nodiscard]] bool isVoicePlaying(AudioVoiceHandle voice) const override;
    void setMasterVolume(f32 volume) override;
    [[nodiscard]] f32 masterVolume() const override;
    void setBusVolume(AudioBus bus, f32 volume) override;
    [[nodiscard]] f32 busVolume(AudioBus bus) const override;
    void setBusPaused(AudioBus bus, bool paused) override;
    [[nodiscard]] bool isBusPaused(AudioBus bus) const override;
    void setBusFilter(AudioBus bus, const AudioBusFilter& filter) override;
    [[nodiscard]] AudioBusFilter busFilter(AudioBus bus) const override;
    void setReverb(const AudioReverbSettings& settings) override;
    [[nodiscard]] AudioReverbSettings reverb() const override;
    /// @}

private:
    AudioWorld& m_audio;
};

} // namespace illusion

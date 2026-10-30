#include <illusion/audio/AudioWorldService.hpp>

namespace illusion
{

// Out of line so that the vtable is emitted in the library, which only the host links.
AudioWorldService::~AudioWorldService() = default;

AudioClipHandle AudioWorldService::loadClip(std::string_view virtualPath, AudioClipMode mode)
{
    return m_audio.loadClip(virtualPath, mode);
}

AudioClipHandle AudioWorldService::requestClip(std::string_view virtualPath, AudioClipMode mode)
{
    return m_audio.requestClip(virtualPath, mode);
}

AssetState AudioWorldService::clipState(AudioClipHandle clip) const
{
    return m_audio.clipState(clip);
}

AudioClipHandle AudioWorldService::createClip(std::span<const f32> samples, u32 channels, u32 sampleRate)
{
    return m_audio.createClip(samples, channels, sampleRate);
}

void AudioWorldService::unloadClip(AudioClipHandle clip)
{
    m_audio.unloadClip(clip);
}

std::optional<AudioClipInfo> AudioWorldService::clipInfo(AudioClipHandle clip) const
{
    return m_audio.clipInfo(clip);
}

void AudioWorldService::play(Entity entity)
{
    m_audio.play(entity);
}

void AudioWorldService::stop(Entity entity)
{
    m_audio.stop(entity);
}

bool AudioWorldService::isPlaying(Entity entity) const
{
    return m_audio.isPlaying(entity);
}

AudioVoiceHandle AudioWorldService::playOneShot(AudioClipHandle clip, const AudioPlayParams& params)
{
    return m_audio.playOneShot(clip, params);
}

void AudioWorldService::stopVoice(AudioVoiceHandle voice)
{
    m_audio.stop(voice);
}

bool AudioWorldService::isVoicePlaying(AudioVoiceHandle voice) const
{
    return m_audio.isPlaying(voice);
}

void AudioWorldService::setMasterVolume(f32 volume)
{
    m_audio.setMasterVolume(volume);
}

f32 AudioWorldService::masterVolume() const
{
    return m_audio.masterVolume();
}

void AudioWorldService::setBusVolume(AudioBus bus, f32 volume)
{
    m_audio.setBusVolume(bus, volume);
}

f32 AudioWorldService::busVolume(AudioBus bus) const
{
    return m_audio.busVolume(bus);
}

void AudioWorldService::setBusPaused(AudioBus bus, bool paused)
{
    m_audio.setBusPaused(bus, paused);
}

bool AudioWorldService::isBusPaused(AudioBus bus) const
{
    return m_audio.isBusPaused(bus);
}

void AudioWorldService::setBusFilter(AudioBus bus, const AudioBusFilter& filter)
{
    m_audio.setBusFilter(bus, filter);
}

AudioBusFilter AudioWorldService::busFilter(AudioBus bus) const
{
    return m_audio.busFilter(bus);
}

void AudioWorldService::setReverb(const AudioReverbSettings& settings)
{
    m_audio.setReverb(settings);
}

AudioReverbSettings AudioWorldService::reverb() const
{
    return m_audio.reverb();
}

} // namespace illusion

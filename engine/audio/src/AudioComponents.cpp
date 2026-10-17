#include <illusion/audio/AudioComponents.hpp>

namespace illusion
{

void reflectAudioComponents(ComponentRegistry& registry)
{
    reflect<AudioSource>("illusion.AudioSource", 1, registry)
        .field("clip", &AudioSource::clip)
        .field("bus", &AudioSource::bus)
        .field("volume", &AudioSource::volume)
        .field("pitch", &AudioSource::pitch)
        .field("pan", &AudioSource::pan)
        .field("loop", &AudioSource::loop)
        .field("playOnStart", &AudioSource::playOnStart)
        .field("spatial", &AudioSource::spatial)
        .field("stream", &AudioSource::stream)
        .field("attenuation", &AudioSource::attenuation)
        .field("minDistance", &AudioSource::minDistance)
        .field("maxDistance", &AudioSource::maxDistance)
        .field("rolloff", &AudioSource::rolloff)
        .field("dopplerFactor", &AudioSource::dopplerFactor)
        .field("priority", &AudioSource::priority)
        .field("lowPassCutoff", &AudioSource::lowPassCutoff)
        .field("reverbSend", &AudioSource::reverbSend)
        .field("coneInnerAngle", &AudioSource::coneInnerAngle)
        .field("coneOuterAngle", &AudioSource::coneOuterAngle)
        .field("coneOuterVolume", &AudioSource::coneOuterVolume);
    reflect<AudioListener>("illusion.AudioListener", 1, registry);
}

} // namespace illusion

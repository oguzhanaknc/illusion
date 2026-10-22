#include <illusion/audio/AudioComponents.hpp>
#include <illusion/audio/AudioWorld.hpp>
#include <illusion/ecs/StructRegistry.hpp>

namespace illusion
{
namespace
{

constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};
constexpr f64 kFullCone = static_cast<f64>(kAudioFullCone);

} // namespace

void reflectAudioComponents(ComponentRegistry& registry)
{
    StructRegistry& structs = registry.structs();
    reflectStruct<AudioReverbSettings>("illusion.AudioReverbSettings", 1, structs)
        .meta({.displayName = "Reverb", .docsAnchor = "manual/audio.md#effects"})
        .field("roomSize", &AudioReverbSettings::roomSize,
               {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("damping", &AudioReverbSettings::damping, {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("width", &AudioReverbSettings::width, {.min = 0.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("volume", &AudioReverbSettings::volume, {.min = 0.0, .softMax = 2.0});
    reflectStruct<AudioBusFilter>("illusion.AudioBusFilter", 1, structs)
        .meta({.displayName = "Bus filter", .docsAnchor = "manual/audio.md#effects"})
        .field("lowPassCutoff", &AudioBusFilter::lowPassCutoff,
               {.description = "0 turns the filter off.",
                .unit = Unit::Hertz,
                .min = 0.0,
                .softMax = 22000.0,
                .hint = FieldHint::LogSlider})
        .field("highPassCutoff", &AudioBusFilter::highPassCutoff,
               {.description = "0 turns the filter off.",
                .unit = Unit::Hertz,
                .min = 0.0,
                .softMax = 22000.0,
                .hint = FieldHint::LogSlider});

    reflect<AudioSource>("illusion.AudioSource", 1, registry)
        .meta({.displayName = "Audio source",
               .category = "Audio",
               .icon = "volume_up",
               .description = "A sound emitted by the Entity.",
               .docsAnchor = "manual/audio.md#sources",
               .requiredComponents = kRequiresTransform})
        .field("clip", &AudioSource::clip,
               {.description = "The clip to play.", .assetKind = AssetKind::AudioClip})
        .field("bus", &AudioSource::bus)
        .field("volume", &AudioSource::volume, {.min = 0.0, .softMax = 2.0, .hint = FieldHint::Slider})
        .field("pitch", &AudioSource::pitch, {.min = 0.0625, .max = 16.0, .hint = FieldHint::LogSlider})
        .field("pan", &AudioSource::pan, {.min = -1.0, .max = 1.0, .hint = FieldHint::Slider})
        .field("loop", &AudioSource::loop)
        .field("playOnStart", &AudioSource::playOnStart)
        .field("spatial", &AudioSource::spatial)
        .field("stream", &AudioSource::stream)
        .field("attenuation", &AudioSource::attenuation, {.category = "3D sound"})
        .field("minDistance", &AudioSource::minDistance,
               {.category = "3D sound", .unit = Unit::Meters, .min = 0.0})
        .field("maxDistance", &AudioSource::maxDistance,
               {.category = "3D sound", .unit = Unit::Meters, .min = 0.0})
        .field("rolloff", &AudioSource::rolloff, {.category = "3D sound", .min = 0.0})
        .field("dopplerFactor", &AudioSource::dopplerFactor, {.category = "3D sound", .min = 0.0})
        .field("priority", &AudioSource::priority, {.advanced = true})
        .field("lowPassCutoff", &AudioSource::lowPassCutoff,
               {.description = "0 turns the filter off.",
                .unit = Unit::Hertz,
                .min = 0.0,
                .softMax = 22000.0,
                .hint = FieldHint::LogSlider})
        .field("reverbSend", &AudioSource::reverbSend,
               {.unit = Unit::Ratio,
                .displayUnit = Unit::Percent,
                .min = 0.0,
                .max = 1.0,
                .hint = FieldHint::Slider})
        .field("coneInnerAngle", &AudioSource::coneInnerAngle,
               {.category = "3D sound",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = kFullCone,
                .hint = FieldHint::Angle})
        .field("coneOuterAngle", &AudioSource::coneOuterAngle,
               {.category = "3D sound",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = kFullCone,
                .hint = FieldHint::Angle})
        .field("coneOuterVolume", &AudioSource::coneOuterVolume,
               {.category = "3D sound", .min = 0.0, .max = 1.0, .hint = FieldHint::Slider});
    reflect<AudioListener>("illusion.AudioListener", 1, registry)
        .meta({.displayName = "Audio listener",
               .category = "Audio",
               .icon = "hearing",
               .description = "The ears that hear spatial sounds; put it on the camera Entity.",
               .docsAnchor = "manual/audio.md#3d-sound",
               .requiredComponents = kRequiresTransform,
               .singleton = true});
    reflectRuntime<AudioPlayback>("illusion.AudioPlayback", {.displayName = "Audio playback"}, registry);
}

} // namespace illusion

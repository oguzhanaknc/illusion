#pragma once

/// @file
/// ECS components played by AudioWorld: AudioSource (a sound emitted by an Entity) and AudioListener (the
/// ears, usually on the camera Entity).

#include <illusion/core/Math.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>
#include <string>

namespace illusion
{

/// Mixer bus. Every sound plays through one bus; each bus has its own volume and can be paused (see
/// AudioWorld::setBusVolume()), for example for a settings menu or a pause screen.
enum class AudioBus : u8
{
    Sfx,      ///< Sound effects.
    Music,    ///< Music.
    Ambience, ///< Ambient loops (wind, rooms, crowds).
    Dialogue, ///< Voices.
    Ui,       ///< Menu and interface sounds (usually not paused with the game).
};

/// Number of mixer buses.
inline constexpr u32 kAudioBusCount = 5;

/// How the volume of a spatial sound falls off with its distance d from the listener. Distances are
/// clamped to [minDistance, maxDistance] first.
enum class AudioAttenuation : u8
{
    None,        ///< No attenuation.
    Inverse,     ///< minDistance / (minDistance + rolloff * (d - minDistance)); sounds natural.
    Linear,      ///< 1 - rolloff * (d - minDistance) / (maxDistance - minDistance); silent at maxDistance
                 ///< when rolloff is 1.
    Exponential, ///< (d / minDistance)^-rolloff.
};

/// Marks the Entity whose Transform hears spatial sounds (forward is -z, up is +y). Put it on the camera
/// Entity. If several Entities have one, the first one found is used; without any, the listener is at the
/// origin looking along -z.
struct AudioListener
{
};

/// Handle to a clip loaded or created by an AudioWorld.
struct AudioClipHandle
{
    u32 index = 0;      ///< Slot in the AudioWorld's clip table.
    u32 generation = 0; ///< 0 for the invalid handle; changes when the slot is reused.

    /// True unless this is the default (invalid) handle. Use AudioWorld::clipInfo() to check that the
    /// clip is still loaded.
    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0; }

    /// Handles are equal when index and generation match.
    friend constexpr bool operator==(const AudioClipHandle&, const AudioClipHandle&) noexcept = default;
};

/// A sound emitted by an Entity. Spatial sounds are positioned at the Entity's Transform; AudioWorld
/// derives their velocity (for the doppler effect) from how the Transform moves.
///
/// Changes to the fields take effect on the next AudioWorld::update(); a new clip path takes effect on
/// the next AudioWorld::play().
struct AudioSource
{
    std::string clip;             ///< Virtual path of the clip (WAV, FLAC, MP3, Ogg Vorbis).
    AudioBus bus = AudioBus::Sfx; ///< Mixer bus.
    f32 volume = 1.0f;            ///< Linear gain (at least 0).
    f32 pitch = 1.0f;             ///< Playback speed, clamped to [1/16, 16].
    f32 pan = 0.0f;               ///< Stereo balance in [-1, 1] (left to right).
    bool loop = false;            ///< Restarts at the end instead of stopping.
    bool playOnStart = true;      ///< Starts when the AudioWorld first sees the Entity.
    bool spatial = true;          ///< 3D sound at the Transform; false plays it as is.
    bool stream = false;          ///< Decodes while playing instead of on load (music).
    AudioAttenuation attenuation = AudioAttenuation::Inverse; ///< Distance attenuation model.
    f32 minDistance = 1.0f;    ///< Distance in m within which the volume is full.
    f32 maxDistance = 1000.0f; ///< Distance in m beyond which it no longer falls.
    f32 rolloff = 1.0f;        ///< Steepness of the attenuation.
    f32 dopplerFactor = 1.0f;  ///< Strength of the doppler effect (0 disables it).
    u32 priority = 128; ///< When all voices are in use, a sound takes the voice of a lower or equal one.
};

/// Runtime state of an AudioSource. Added and removed by AudioWorld; not saved in scenes.
struct AudioPlayback
{
    static constexpr u32 kNoVoice = 0xFFFFFFFFu; ///< Not playing.

    AudioClipHandle clip;        ///< The resolved clip (invalid if it could not be loaded).
    u32 voice = kNoVoice;        ///< Voice playing the sound.
    u32 serial = 0;              ///< Identifies the playback on that voice.
    Vec3 previousPosition{0.0f}; ///< Transform position at the previous update (for the velocity).
};

/// Reflects AudioSource ("illusion.AudioSource") and AudioListener ("illusion.AudioListener").
void reflectAudioComponents(ComponentRegistry& registry = ComponentRegistry::instance());

} // namespace illusion

/// Names of AudioBus values in scene files.
template <>
struct illusion::EnumReflection<illusion::AudioBus>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Sfx", 0}, illusion::EnumValue{"Music", 1},
                                        illusion::EnumValue{"Ambience", 2},
                                        illusion::EnumValue{"Dialogue", 3}, illusion::EnumValue{"Ui", 4}};
};

/// Names of AudioAttenuation values in scene files.
template <>
struct illusion::EnumReflection<illusion::AudioAttenuation>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"None", 0}, illusion::EnumValue{"Inverse", 1},
                                        illusion::EnumValue{"Linear", 2},
                                        illusion::EnumValue{"Exponential", 3}};
};

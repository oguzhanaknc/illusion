#pragma once

/// @file
/// Plain parameters and handles of audio playback, shared by AudioWorld and AudioService.

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

namespace illusion
{

/// How a clip is kept in memory.
enum class AudioClipMode : u8
{
    Decode, ///< Decoded to PCM when loaded; any number of voices can play it cheaply (effects).
    Stream, ///< Kept compressed and decoded while playing; much less memory (music, long ambience).
};

/// Properties of a loaded clip.
struct AudioClipInfo
{
    u32 channels = 0;                           ///< 1 (mono) or 2 (stereo); more channels are downmixed.
    u64 frames = 0;                             ///< Length in frames at the mixing rate (0 if unknown).
    AudioClipMode mode = AudioClipMode::Decode; ///< How the clip is kept.
    usize memoryBytes = 0;                      ///< Memory held by the clip's audio data.
};

/// Parameters of a sound started with AudioWorld::playOneShot(). The fields mean the same as in
/// AudioSource.
struct AudioPlayParams
{
    AudioBus bus = AudioBus::Sfx;                             ///< Mixer bus.
    f32 volume = 1.0f;                                        ///< Linear gain.
    f32 pitch = 1.0f;                                         ///< Playback speed.
    f32 pan = 0.0f;                                           ///< Stereo balance.
    bool loop = false;                                        ///< Restarts at the end.
    bool spatial = false;                                     ///< 3D sound at @ref position.
    Vec3 position{0.0f};                                      ///< World position of a spatial sound.
    AudioAttenuation attenuation = AudioAttenuation::Inverse; ///< Distance attenuation model.
    f32 minDistance = 1.0f;                                   ///< Full volume within this distance.
    f32 maxDistance = 1000.0f;                                ///< No further attenuation beyond it.
    f32 rolloff = 1.0f;                                       ///< Steepness of the attenuation.
    u32 priority = 128;                                       ///< Voice priority.
    f32 lowPassCutoff = 0.0f;                                 ///< Low-pass filter cutoff in Hz; 0 is off.
    f32 reverbSend = 0.0f;                                    ///< Share sent to the reverb, in [0, 1].
};

/// The reverb that sounds feed through AudioSource::reverbSend (a Freeverb-style room reverb).
struct AudioReverbSettings
{
    f32 roomSize = 0.5f; ///< Size of the room in [0, 1]: larger rooms ring longer.
    f32 damping = 0.25f; ///< Absorption of high frequencies in [0, 1].
    f32 width = 1.0f;    ///< Stereo width of the reverb in [0, 1].
    f32 volume = 1.0f;   ///< Linear gain of the reverb (at least 0).
};

/// Filters applied to everything a bus plays, for example to muffle the game under a pause menu or to
/// make dialogue sound like a radio.
struct AudioBusFilter
{
    f32 lowPassCutoff = 0.0f;  ///< Removes frequencies above this cutoff in Hz; 0 is off.
    f32 highPassCutoff = 0.0f; ///< Removes frequencies below this cutoff in Hz; 0 is off.
};

/// Handle to a sound started with AudioWorld::playOneShot().
struct AudioVoiceHandle
{
    u32 voice = 0xFFFFFFFFu; ///< Voice index.
    u32 serial = 0;          ///< Identifies the playback on that voice (0 for the invalid handle).

    /// True if the sound was started (it may have finished since).
    [[nodiscard]] constexpr bool valid() const noexcept { return serial != 0; }
};

} // namespace illusion

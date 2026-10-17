#pragma once

/// @file
/// Sound playback for a World: clips, a mixer with buses, 3D sound and the audio device.
///
/// @code
/// illusion::AudioWorld audio(world, files);
/// world.create(illusion::Transform{}, illusion::AudioListener{}, camera);
/// illusion::AudioSource fire;
/// fire.clip = "game:/audio/fire.ogg";
/// fire.loop = true;
/// world.create(fireTransform, fire);
/// // every frame:
/// audio.update(frameSeconds); // starts new sources, moves 3D sounds, hands the mix to the audio thread
/// @endcode

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

class VirtualFileSystem;
class World;

/// Where mixed audio goes.
enum class AudioOutput : u8
{
    Device,  ///< The default audio device; falls back to Null if none can be opened.
    Null,    ///< No sound card: a device thread consumes the mix in real time (servers, CI).
    Offline, ///< No device: the caller pulls the mix with AudioWorld::render() (tests, offline rendering).
};

/// Creation parameters for AudioWorld.
struct AudioWorldDesc
{
    AudioOutput output = AudioOutput::Device; ///< Where the mix goes.
    u32 sampleRate = 0;          ///< Mixing rate in Hz; 0 uses the device's rate (48000 without a device).
    u32 maxVoices = 128;         ///< Sounds that can play at the same time.
    u32 periodMilliseconds = 10; ///< Device buffer period: lower means less latency, more wake-ups.
    f32 speedOfSound = 343.3f;   ///< In m/s, for the doppler effect.
    /// Loads the clips of AudioSources and of AudioWorld::requestClip() in the background. Null loads
    /// them on the calling thread. The streamer must outlive the AudioWorld, and the game calls its
    /// update() (which publishes finished loads) before AudioWorld::update().
    AssetStreamer* streamer = nullptr;
};

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

/// Counters for profiling and debugging.
struct AudioStats
{
    u32 activeVoices = 0;     ///< Voices playing (or paused on a bus).
    u32 droppedSounds = 0;    ///< Sounds that could not start because every voice had a higher priority.
    u32 stolenVoices = 0;     ///< Voices taken from lower-priority sounds.
    u32 clips = 0;            ///< Loaded clips.
    u32 loadingClips = 0;     ///< Clips loading in the background.
    usize clipBytes = 0;      ///< Memory held by loaded clips.
    u64 mixerAllocations = 0; ///< Blocks the mixer took from the system allocator; constant after start-up.
};

/// Plays the AudioSource Entities of a World and one-shot sounds through a mixer with buses.
///
/// The game thread calls update() once per frame; mixing runs on the audio device's thread (or in
/// render() for AudioOutput::Offline) and never allocates memory or waits for the game thread. Playback
/// and mixer calls change what the game thread asks for; the audio thread hears about it at the next
/// update(). The World and the VirtualFileSystem must outlive the AudioWorld. Not thread-safe: call it
/// from one thread, outside of World queries.
class AudioWorld : NonCopyable
{
public:
    /// Opens the output and creates the mixer. Clips are read through @p files.
    AudioWorld(World& world, const VirtualFileSystem& files, const AudioWorldDesc& desc = {});

    /// Stops the output, frees every clip and removes the AudioPlayback components.
    ~AudioWorld();

    /// Synchronizes the mixer with the World and publishes the result to the audio thread:
    /// - AudioSource Entities seen for the first time get an AudioPlayback component and load their clip
    ///   (in the background with AudioWorldDesc::streamer); those with AudioSource::playOnStart start
    ///   playing, or start once their clip has loaded.
    /// - Clips that finished loading in the background become ready.
    /// - Sounds of destroyed Entities, or of Entities that lost their AudioSource, stop.
    /// - Volumes, pitches, buses and the positions and velocities of spatial sounds and of the listener
    ///   are updated. Velocities are the Transform movement divided by @p deltaSeconds.
    /// - Finished sounds release their voices.
    void update(f64 deltaSeconds);

    /// @name Clips
    /// @{

    /// Loads (or returns the already loaded) clip at @p virtualPath. Returns an invalid handle and logs a
    /// warning if the file cannot be read or decoded. Clips stay loaded until unloadClip(). If the clip is
    /// still loading in the background (requestClip()), returns its handle without waiting.
    AudioClipHandle loadClip(std::string_view virtualPath, AudioClipMode mode = AudioClipMode::Decode);

    /// Starts loading the clip at @p virtualPath through AudioWorldDesc::streamer and returns its handle
    /// at once; clipState() tells when it is ready (after a later update()). Without a streamer this is
    /// loadClip(). Requests for a clip that is loaded or loading return the same handle.
    AudioClipHandle requestClip(std::string_view virtualPath, AudioClipMode mode = AudioClipMode::Decode);

    /// Loading state of @p clip: Ready once it can play, Loading while it loads in the background, and
    /// Failed for invalid or unloaded handles and clips that could not be loaded.
    [[nodiscard]] AssetState clipState(AudioClipHandle clip) const noexcept;

    /// Creates a clip from interleaved PCM samples in [-1, 1] with @p channels channels at @p sampleRate Hz
    /// (converted to the mixing rate; more than two channels are downmixed to stereo).
    AudioClipHandle createClip(std::span<const f32> samples, u32 channels, u32 sampleRate);

    /// Unloads a clip. Its handle becomes invalid at once; its memory is freed when no voice plays it.
    void unloadClip(AudioClipHandle clip);

    /// Properties of a loaded clip, or std::nullopt for an invalid or unloaded handle (or a clip that is
    /// not ready yet).
    [[nodiscard]] std::optional<AudioClipInfo> clipInfo(AudioClipHandle clip) const;

    /// @}

    /// @name Playback
    /// @{

    /// Starts (or restarts) the AudioSource of @p entity, reloading its clip path if it changed.
    void play(Entity entity);

    /// Stops the sound of @p entity.
    void stop(Entity entity);

    /// True if the sound of @p entity is playing, as of the last update().
    [[nodiscard]] bool isPlaying(Entity entity) const;

    /// Plays @p clip without an Entity. Returns an invalid handle if the clip is invalid or not ready yet,
    /// or if no voice is free for its priority.
    AudioVoiceHandle playOneShot(AudioClipHandle clip, const AudioPlayParams& params = {});

    /// Stops a sound started with playOneShot().
    void stop(AudioVoiceHandle voice);

    /// True if the sound is playing, as of the last update().
    [[nodiscard]] bool isPlaying(AudioVoiceHandle voice) const;

    /// @}

    /// @name Mixer
    /// @{

    /// Sets the master volume (linear gain, at least 0).
    void setMasterVolume(f32 volume);

    /// Master volume.
    [[nodiscard]] f32 masterVolume() const noexcept;

    /// Sets the volume of @p bus (linear gain, at least 0).
    void setBusVolume(AudioBus bus, f32 volume);

    /// Volume of @p bus.
    [[nodiscard]] f32 busVolume(AudioBus bus) const noexcept;

    /// Pauses or resumes every sound on @p bus; paused sounds keep their position.
    void setBusPaused(AudioBus bus, bool paused);

    /// True if @p bus is paused.
    [[nodiscard]] bool isBusPaused(AudioBus bus) const noexcept;

    /// Sets the filters of @p bus (cutoffs are clamped below half the mixing rate).
    void setBusFilter(AudioBus bus, const AudioBusFilter& filter);

    /// Filters of @p bus.
    [[nodiscard]] AudioBusFilter busFilter(AudioBus bus) const noexcept;

    /// Sets the reverb that AudioSource::reverbSend and AudioPlayParams::reverbSend feed. Sounds on a
    /// paused bus stop feeding it; the tail of the reverb keeps ringing.
    void setReverb(const AudioReverbSettings& settings);

    /// The reverb settings.
    [[nodiscard]] const AudioReverbSettings& reverb() const noexcept;

    /// @}

    /// The output in use (Device falls back to Null when no device can be opened).
    [[nodiscard]] AudioOutput output() const noexcept;

    /// Mixing rate in Hz.
    [[nodiscard]] u32 sampleRate() const noexcept;

    /// Channels of the mix (always 2: stereo).
    [[nodiscard]] static constexpr u32 channelCount() noexcept { return 2; }

    /// Counters for profiling and debugging.
    [[nodiscard]] AudioStats stats() const noexcept;

    /// AudioOutput::Offline only: mixes the next @p samples.size() / 2 frames into @p samples
    /// (interleaved stereo), as the audio thread does with a device. Applies the state published by the
    /// last update(). May run on another thread than update(), but not concurrently with itself.
    void render(std::span<f32> samples);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

#include "AudioDecoder.hpp"
#include "AudioMixer.hpp"

#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <miniaudio.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace illusion
{
namespace
{

constexpr u32 kDefaultSampleRate = 48000;
constexpr u32 kMaxVoices = 4096;
constexpr u32 kNoVoice = AudioPlayback::kNoVoice;
constexpr f32 kMinPitch = 1.0f / 16.0f;
constexpr f32 kMaxPitch = 16.0f;
constexpr f32 kMinDistance = 0.01f;
constexpr u32 kMaxRenderFrames = 1u << 20;
constexpr std::string_view kSupportedFormats = "supported: WAV, FLAC, MP3, Ogg Vorbis";

// Values from game code and scene files are clamped; non-finite values fall back to @p fallback.
f32 sanitize(f32 value, f32 low, f32 high, f32 fallback) noexcept
{
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

AudioBus sanitizeBus(AudioBus bus) noexcept
{
    return static_cast<u32>(bus) < kAudioBusCount ? bus : AudioBus::Sfx;
}

struct StringHash
{
    using is_transparent = void;

    usize operator()(std::string_view text) const noexcept { return std::hash<std::string_view>{}(text); }
};

void logFromMiniaudio(void*, ma_uint32 level, const char* message)
{
    std::string_view text(message);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
    {
        text.remove_suffix(1);
    }
    if (level == MA_LOG_LEVEL_ERROR)
    {
        ILL_LOG_ERROR("Audio", "{}", text);
    }
    else if (level == MA_LOG_LEVEL_WARNING)
    {
        ILL_LOG_WARN("Audio", "{}", text);
    }
}

std::string_view outputName(AudioOutput output) noexcept
{
    switch (output)
    {
    case AudioOutput::Device:
        return "device";
    case AudioOutput::Null:
        return "null device";
    case AudioOutput::Offline:
        break;
    }
    return "offline";
}

// Writes the playback parameters shared by AudioSource and AudioPlayParams.
template <class Params>
void writeParams(detail::VoiceState& state, const Params& params) noexcept
{
    state.bus = sanitizeBus(params.bus);
    state.volume = sanitize(params.volume, 0.0f, FLT_MAX, 0.0f);
    state.pitch = sanitize(params.pitch, kMinPitch, kMaxPitch, 1.0f);
    state.pan = sanitize(params.pan, -1.0f, 1.0f, 0.0f);
    state.loop = params.loop;
    state.spatial = params.spatial;
    state.attenuation = params.attenuation;
    state.minDistance = sanitize(params.minDistance, kMinDistance, FLT_MAX, 1.0f);
    state.maxDistance = sanitize(params.maxDistance, state.minDistance, FLT_MAX, FLT_MAX);
    state.rolloff = sanitize(params.rolloff, 0.0f, FLT_MAX, 1.0f);
    state.lowPassCutoff = sanitize(params.lowPassCutoff, 0.0f, FLT_MAX, 0.0f);
    state.reverbSend = sanitize(params.reverbSend, 0.0f, 1.0f, 0.0f);
    if constexpr (std::is_same_v<Params, AudioSource>)
    {
        state.dopplerFactor = sanitize(params.dopplerFactor, 0.0f, FLT_MAX, 0.0f);
        state.coneInnerAngle = sanitize(params.coneInnerAngle, 0.0f, kAudioFullCone, kAudioFullCone);
        state.coneOuterAngle =
            sanitize(params.coneOuterAngle, state.coneInnerAngle, kAudioFullCone, kAudioFullCone);
        state.coneOuterVolume = sanitize(params.coneOuterVolume, 0.0f, 1.0f, 0.0f);
    }
}

// Spatial state of a sound taken from its Entity's Transform: position and forward axis (-z).
void writePose(detail::VoiceState& state, const Transform& transform) noexcept
{
    state.position = transform.position;
    state.direction = transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
}

AudioReverbSettings sanitizeReverb(const AudioReverbSettings& settings) noexcept
{
    AudioReverbSettings result;
    result.roomSize = sanitize(settings.roomSize, 0.0f, 1.0f, 0.5f);
    result.damping = sanitize(settings.damping, 0.0f, 1.0f, 0.25f);
    result.width = sanitize(settings.width, 0.0f, 1.0f, 1.0f);
    result.volume = sanitize(settings.volume, 0.0f, FLT_MAX, 0.0f);
    return result;
}

} // namespace

struct AudioWorld::Impl
{
    struct Clip
    {
        std::string path;                    // empty for clips made with createClip()
        detail::ClipData owned;              // data of clips loaded on the game thread
        AssetHandle<detail::ClipData> asset; // data of clips loaded by the streamer
        AudioClipMode mode = AudioClipMode::Decode;
        u32 generation = 1;
        u32 users = 0;       // voices playing the clip
        bool loaded = false; // false for free slots and unloaded clips waiting to be freed
        bool ready = true;   // false while the streamer loads the clip
        u64 freeAfter = 0;   // snapshot serial after which an unloaded clip can be freed

        [[nodiscard]] const detail::ClipData& data() const noexcept
        {
            const detail::ClipData* streamed = asset.get();
            return streamed != nullptr ? *streamed : owned;
        }
    };

    // Game-thread bookkeeping of one voice.
    struct VoiceRecord
    {
        u32 serial = 0; // 0 = free
        Entity owner;   // null for one-shots
        u32 clip = 0;
        u32 priority = 0;
        u64 order = 0; // start order, for stealing the oldest sound
        detail::DecoderPtr decoder;
    };

    struct RetiredDecoder
    {
        u64 freeAfter = 0;
        detail::DecoderPtr decoder;
    };

    Impl(World& targetWorld, const VirtualFileSystem& fileSystem, const AudioWorldDesc& settings)
        : world(targetWorld)
        , files(fileSystem)
        , desc(settings)
    {
        ILL_ASSERT(desc.maxVoices >= 1 && desc.maxVoices <= kMaxVoices, "maxVoices must be in [1, {}]",
                   kMaxVoices);
        ma_log_init(nullptr, &log);
        ma_log_register_callback(&log, ma_log_callback_init(&logFromMiniaudio, nullptr));

        output = desc.output;
        if (output != AudioOutput::Offline)
        {
            bool opened = output == AudioOutput::Device && openDevice(false, desc.sampleRate);
            if (!opened)
            {
                if (output == AudioOutput::Device)
                {
                    ILL_LOG_WARN("Audio", "no audio device could be opened; using the null device");
                }
                output = AudioOutput::Null;
                opened = openDevice(true, desc.sampleRate != 0 ? desc.sampleRate : kDefaultSampleRate);
            }
            if (!opened)
            {
                ILL_LOG_ERROR("Audio", "the null audio device could not be opened; audio output is offline");
                output = AudioOutput::Offline;
            }
        }

        const u32 rate =
            device ? device->sampleRate : (desc.sampleRate != 0 ? desc.sampleRate : kDefaultSampleRate);
        mixer = std::make_unique<detail::Mixer>(rate, desc.maxVoices, desc.speedOfSound);
        ILL_CHECK(mixer->valid(), "cannot create the audio mixer (out of memory)");

        voices.resize(desc.maxVoices);
        records.resize(desc.maxVoices);
        freeVoices.reserve(desc.maxVoices);
        for (u32 i = desc.maxVoices; i > 0; --i)
        {
            freeVoices.push_back(i - 1); // voice 0 is used first
        }
        retired.reserve(desc.maxVoices);
        scratch.reserve(256);
        busVolumes.fill(1.0f);

        if (device && ma_device_start(device.get()) != MA_SUCCESS)
        {
            const bool wasDevice = output == AudioOutput::Device;
            closeDevice();
            if (wasDevice && openDevice(true, rate) && ma_device_start(device.get()) == MA_SUCCESS)
            {
                ILL_LOG_WARN("Audio", "the audio device could not be started; using the null device");
                output = AudioOutput::Null;
            }
            else
            {
                closeDevice();
                ILL_LOG_ERROR("Audio", "the audio output could not be started; audio output is offline");
                output = AudioOutput::Offline;
            }
        }
        ILL_LOG_INFO("Audio", "mixing {} voices at {} Hz to the {}", desc.maxVoices, rate,
                     outputName(output));
    }

    ~Impl()
    {
        closeDevice(); // stops the audio thread first
        mixer.reset();
        records.clear();
        retired.clear();
        ma_log_uninit(&log);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // --- Output ---------------------------------------------------------------------------------------

    static void deviceData(ma_device* audioDevice, void* out, const void*, ma_uint32 frameCount)
    {
        static_cast<Impl*>(audioDevice->pUserData)->mixer->render(static_cast<f32*>(out), frameCount);
    }

    // Creates (without starting) the default device, or the null device.
    bool openDevice(bool nullBackend, u32 sampleRate)
    {
        context = std::make_unique<ma_context>();
        ma_context_config contextConfig = ma_context_config_init();
        contextConfig.pLog = &log;
        const std::array<ma_backend, 1> nullOnly{ma_backend_null};
        if (ma_context_init(nullBackend ? nullOnly.data() : nullptr, nullBackend ? 1u : 0u, &contextConfig,
                            context.get()) != MA_SUCCESS)
        {
            context.reset();
            return false;
        }
        device = std::make_unique<ma_device>();
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = sampleRate;
        config.periodSizeInMilliseconds = desc.periodMilliseconds;
        config.dataCallback = &deviceData;
        config.pUserData = this;
        if (ma_device_init(context.get(), &config, device.get()) != MA_SUCCESS)
        {
            device.reset();
            ma_context_uninit(context.get());
            context.reset();
            return false;
        }
        return true;
    }

    void closeDevice()
    {
        if (device)
        {
            ma_device_uninit(device.get());
            device.reset();
        }
        if (context)
        {
            ma_context_uninit(context.get());
            context.reset();
        }
    }

    // --- Clips ----------------------------------------------------------------------------------------

    // The clip of @p handle if it is loaded or loading.
    [[nodiscard]] const Clip* slotOf(AudioClipHandle handle) const noexcept
    {
        if (handle.index >= clips.size())
        {
            return nullptr;
        }
        const Clip& clip = clips[handle.index];
        return clip.loaded && clip.generation == handle.generation ? &clip : nullptr;
    }

    // The clip of @p handle if it is ready to play.
    [[nodiscard]] const Clip* clipOf(AudioClipHandle handle) const noexcept
    {
        const Clip* clip = slotOf(handle);
        return clip != nullptr && clip->ready ? clip : nullptr;
    }

    [[nodiscard]] AssetState stateOf(AudioClipHandle handle) const noexcept
    {
        const Clip* clip = slotOf(handle);
        if (clip == nullptr)
        {
            return AssetState::Failed;
        }
        return clip->ready ? AssetState::Ready : AssetState::Loading;
    }

    AudioClipHandle handleOf(u32 index) const noexcept
    {
        return AudioClipHandle{index, clips[index].generation};
    }

    AudioClipHandle addClip(Clip&& clip)
    {
        u32 index = 0;
        if (!freeClips.empty())
        {
            index = freeClips.back();
            freeClips.pop_back();
            clip.generation = clips[index].generation;
            clips[index] = std::move(clip);
        }
        else
        {
            index = static_cast<u32>(clips.size());
            clips.push_back(std::move(clip));
        }
        clips[index].loaded = true;
        if (!clips[index].path.empty())
        {
            clipsByPath.emplace(clips[index].path, index);
        }
        return handleOf(index);
    }

    AudioClipHandle loadClip(std::string_view path, AudioClipMode mode)
    {
        if (const auto found = clipsByPath.find(path); found != clipsByPath.end())
        {
            return handleOf(found->second);
        }
        std::vector<std::byte> bytes;
        if (const FileError error = files.readFile(path, bytes); error != FileError::None)
        {
            ILL_LOG_WARN("Audio", "cannot read clip '{}': {}", path, fileErrorName(error));
            return {};
        }
        Clip clip;
        clip.path = path;
        clip.mode = mode;
        if (!detail::loadClipData(std::move(bytes), mode == AudioClipMode::Stream, mixer->sampleRate(),
                                  clip.owned))
        {
            ILL_LOG_WARN("Audio", "cannot decode clip '{}' ({})", path, kSupportedFormats);
            return {};
        }
        return addClip(std::move(clip));
    }

    AudioClipHandle requestClip(std::string_view path, AudioClipMode mode)
    {
        if (desc.streamer == nullptr)
        {
            return loadClip(path, mode);
        }
        if (const auto found = clipsByPath.find(path); found != clipsByPath.end())
        {
            return handleOf(found->second);
        }
        const u32 rate = mixer->sampleRate();
        const bool stream = mode == AudioClipMode::Stream;
        Clip clip;
        clip.path = path;
        clip.mode = mode;
        clip.asset = desc.streamer->loadFile<detail::ClipData>(
            path, std::format("illusion.audio.{}@{}", stream ? "stream" : "decode", rate),
            [rate, stream](std::vector<std::byte>&& bytes,
                           std::string& error) -> std::optional<detail::ClipData> {
                detail::ClipData data;
                if (!detail::loadClipData(std::move(bytes), stream, rate, data))
                {
                    error = std::format("cannot decode the clip ({})", kSupportedFormats);
                    return std::nullopt;
                }
                return data;
            });
        if (clip.asset.state() == AssetState::Failed)
        {
            ILL_LOG_WARN("Audio", "cannot load clip '{}': {}", path, clip.asset.error());
            return {};
        }
        clip.ready = clip.asset.ready();
        const AudioClipHandle handle = addClip(std::move(clip));
        if (!clips[handle.index].ready)
        {
            loadingClips.push_back(handle);
        }
        return handle;
    }

    // Publishes clips the streamer finished. Returns true if any finished (ready, failed or unloaded), so
    // that the sources waiting for them can start or give up.
    bool pollLoadingClips()
    {
        bool finished = false;
        std::erase_if(loadingClips, [&](AudioClipHandle handle) {
            if (slotOf(handle) == nullptr)
            {
                finished = true; // unloaded while loading
                return true;
            }
            const u32 index = handle.index;
            Clip& clip = clips[index];
            const AssetState state = clip.asset.state();
            if (state == AssetState::Loading)
            {
                return false;
            }
            finished = true;
            if (state == AssetState::Ready)
            {
                clip.ready = true;
                return true;
            }
            ILL_LOG_WARN("Audio", "cannot load clip '{}': {}", clip.path, clip.asset.error());
            clip.asset = {}; // so that the streamer forgets the request and a later one tries again
            unloadClip(handle);
            return true;
        });
        return finished;
    }

    void unloadClip(AudioClipHandle handle)
    {
        if (slotOf(handle) == nullptr)
        {
            return;
        }
        Clip& clip = clips[handle.index];
        clip.loaded = false;
        clip.generation = clip.generation == 0xFFFFFFFFu ? 1 : clip.generation + 1;
        if (!clip.path.empty())
        {
            clipsByPath.erase(clip.path);
        }
        if (clip.users == 0)
        {
            scheduleClipFree(handle.index);
        }
    }

    // The clip is no longer referenced by the next snapshot; free it once the mixer has applied that one.
    void scheduleClipFree(u32 index)
    {
        clips[index].freeAfter = published + 1;
        clipsToFree.push_back(index);
    }

    // --- Voices ---------------------------------------------------------------------------------------

    [[nodiscard]] bool isActive(const AudioPlayback& playback) const noexcept
    {
        return playback.voice < records.size() && playback.serial != 0 &&
               records[playback.voice].serial == playback.serial;
    }

    u32 newSerial() noexcept
    {
        nextSerial = nextSerial == 0xFFFFFFFFu ? 1 : nextSerial + 1;
        return nextSerial;
    }

    // A free voice, or the voice of the lowest-priority (then oldest) sound if its priority is not higher
    // than @p priority. kNoVoice if every voice plays something more important.
    u32 allocateVoice(u32 priority)
    {
        if (!freeVoices.empty())
        {
            const u32 voice = freeVoices.back();
            freeVoices.pop_back();
            return voice;
        }
        u32 victim = 0;
        for (u32 i = 1; i < records.size(); ++i)
        {
            const VoiceRecord& candidate = records[i];
            const VoiceRecord& best = records[victim];
            if (candidate.priority < best.priority ||
                (candidate.priority == best.priority && candidate.order < best.order))
            {
                victim = i;
            }
        }
        if (records[victim].priority > priority)
        {
            ++droppedSounds;
            return kNoVoice;
        }
        ++stolenVoices;
        releaseVoice(victim);
        freeVoices.pop_back(); // the victim
        return victim;
    }

    // Starts @p clipIndex on the allocated @p voice. Returns false (and frees the voice) if a stream cannot
    // be opened.
    bool beginVoice(u32 voice, u32 clipIndex, Entity owner, u32 priority)
    {
        Clip& clip = clips[clipIndex];
        const detail::ClipData& data = clip.data();
        VoiceRecord& record = records[voice];
        detail::VoiceState& state = voices[voice];
        state = detail::VoiceState{};
        if (clip.mode == AudioClipMode::Stream)
        {
            record.decoder = detail::openAudioStream(data.encoded, data.channels, mixer->sampleRate());
            if (!record.decoder)
            {
                ILL_LOG_WARN("Audio", "cannot stream clip '{}'", clip.path);
                freeVoices.push_back(voice);
                return false;
            }
            state.decoder = record.decoder.get();
        }
        else
        {
            state.samples = data.samples.data();
            state.frames = data.frames;
        }
        state.channels = data.channels;
        state.serial = newSerial();
        record.serial = state.serial;
        record.owner = owner;
        record.clip = clipIndex;
        record.priority = priority;
        record.order = nextOrder++;
        ++clip.users;
        return true;
    }

    void releaseVoice(u32 voice)
    {
        VoiceRecord& record = records[voice];
        if (record.serial == 0)
        {
            return;
        }
        Clip& clip = clips[record.clip];
        if (--clip.users == 0 && !clip.loaded)
        {
            scheduleClipFree(record.clip);
        }
        if (record.decoder)
        {
            retired.push_back(RetiredDecoder{published + 1, std::move(record.decoder)});
        }
        if (!record.owner.isNull() && world.isAlive(record.owner))
        {
            AudioPlayback* playback = world.tryGet<AudioPlayback>(record.owner);
            if (playback != nullptr && playback->serial == record.serial)
            {
                playback->voice = kNoVoice;
            }
        }
        record = VoiceRecord{};
        voices[voice] = detail::VoiceState{};
        freeVoices.push_back(voice);
    }

    // --- Entities -------------------------------------------------------------------------------------

    AudioClipHandle resolveClip(const AudioSource& source, Entity entity)
    {
        if (source.clip.empty())
        {
            ILL_LOG_WARN("Audio", "the AudioSource of entity {} has no clip", entity.index);
            return {};
        }
        return requestClip(source.clip, source.stream ? AudioClipMode::Stream : AudioClipMode::Decode);
    }

    void startSource(Entity entity)
    {
        const AudioSource& source = world.get<AudioSource>(entity);
        AudioPlayback& playback = world.get<AudioPlayback>(entity);
        if (isActive(playback))
        {
            releaseVoice(playback.voice);
        }
        playback.clip = resolveClip(source, entity);
        playback.pendingStart = stateOf(playback.clip) == AssetState::Loading;
        if (clipOf(playback.clip) == nullptr)
        {
            return;
        }
        const u32 voice = allocateVoice(source.priority);
        if (voice == kNoVoice || !beginVoice(voice, playback.clip.index, entity, source.priority))
        {
            return;
        }
        detail::VoiceState& state = voices[voice];
        writeParams(state, source);
        if (const Transform* transform = world.tryGet<Transform>(entity))
        {
            writePose(state, *transform);
        }
        playback.voice = voice;
        playback.serial = state.serial;
    }

    void collectFinished()
    {
        for (u32 voice = 0; voice < records.size(); ++voice)
        {
            const u32 serial = records[voice].serial;
            if (serial != 0 && mixer->finishedSerial(voice) == serial)
            {
                releaseVoice(voice);
            }
        }
    }

    void syncListener(f64 deltaSeconds)
    {
        bool found = false;
        Transform transform;
        world.query<const Transform>().with<AudioListener>().each([&](const Transform& candidate) {
            if (!found)
            {
                found = true;
                transform = candidate;
            }
        });
        if (!found)
        {
            listener = detail::ListenerState{};
            listenerSeen = false;
            return;
        }
        listener.velocity = listenerSeen && deltaSeconds > 0.0
                                ? (transform.position - listener.position) / static_cast<f32>(deltaSeconds)
                                : Vec3(0.0f);
        listener.position = transform.position;
        listener.forward = transform.rotation * Vec3(0.0f, 0.0f, -1.0f);
        listener.up = transform.rotation * Vec3(0.0f, 1.0f, 0.0f);
        listenerSeen = true;
    }

    void removeStaleSources()
    {
        for (u32 voice = 0; voice < records.size(); ++voice)
        {
            const Entity owner = records[voice].owner;
            if (records[voice].serial != 0 && !owner.isNull() &&
                (!world.isAlive(owner) || !world.has<AudioSource>(owner)))
            {
                releaseVoice(voice);
            }
        }
        scratch.clear();
        world.query<const AudioPlayback>().without<AudioSource>().each(
            [&](Entity entity, const AudioPlayback&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            world.remove<AudioPlayback>(entity);
        }
    }

    void addNewSources()
    {
        scratch.clear();
        world.query<const AudioSource>().without<AudioPlayback>().each(
            [&](Entity entity, const AudioSource&) { scratch.push_back(entity); });
        for (const Entity entity : scratch)
        {
            AudioPlayback playback;
            if (const Transform* transform = world.tryGet<Transform>(entity))
            {
                playback.previousPosition = transform->position;
            }
            world.emplace<AudioPlayback>(entity, playback);
            if (world.get<AudioSource>(entity).playOnStart)
            {
                startSource(entity);
            }
            else
            {
                // Load the clip now so that a later play() starts without reading the file.
                world.get<AudioPlayback>(entity).clip = resolveClip(world.get<AudioSource>(entity), entity);
            }
        }
    }

    void updateSources(f64 deltaSeconds)
    {
        const f32 inverseDelta = deltaSeconds > 0.0 ? static_cast<f32>(1.0 / deltaSeconds) : 0.0f;
        world.query<const AudioSource, AudioPlayback, const Transform>().each(
            [&](const AudioSource& source, AudioPlayback& playback, const Transform& transform) {
                const Vec3 velocity = (transform.position - playback.previousPosition) * inverseDelta;
                playback.previousPosition = transform.position;
                if (isActive(playback))
                {
                    detail::VoiceState& state = voices[playback.voice];
                    writeParams(state, source);
                    writePose(state, transform);
                    state.velocity = velocity;
                }
            });
        world.query<const AudioSource, AudioPlayback>().without<Transform>().each(
            [&](const AudioSource& source, AudioPlayback& playback) {
                if (isActive(playback))
                {
                    writeParams(voices[playback.voice], source);
                }
            });
    }

    // Starts the sources that waited for a clip that has now loaded (or gives up on failed ones).
    void startPendingSources()
    {
        scratch.clear();
        world.query<const AudioPlayback>().with<AudioSource>().each(
            [&](Entity entity, const AudioPlayback& playback) {
                if (playback.pendingStart && stateOf(playback.clip) != AssetState::Loading)
                {
                    scratch.push_back(entity);
                }
            });
        for (const Entity entity : scratch)
        {
            AudioPlayback& playback = world.get<AudioPlayback>(entity);
            playback.pendingStart = false;
            if (stateOf(playback.clip) == AssetState::Ready)
            {
                startSource(entity);
            }
        }
    }

    void publish()
    {
        detail::MixerSnapshot& snapshot = mixer->exchange().back();
        snapshot.serial = ++published;
        snapshot.listener = listener;
        snapshot.masterVolume = masterVolume;
        snapshot.busVolumes = busVolumes;
        snapshot.busPaused = busPaused;
        snapshot.busFilters = busFilters;
        snapshot.reverb = reverb;
        std::copy(voices.begin(), voices.end(), snapshot.voices.begin());
        mixer->exchange().publish();
    }

    void freeRetired()
    {
        const u64 acknowledged = mixer->acknowledgedSnapshot();
        std::erase_if(
            retired, [acknowledged](const RetiredDecoder& entry) { return entry.freeAfter <= acknowledged; });
        std::erase_if(clipsToFree, [&](u32 index) {
            Clip& clip = clips[index];
            if (clip.freeAfter > acknowledged)
            {
                return false;
            }
            const u32 generation = clip.generation;
            clip = Clip{};
            clip.generation = generation;
            freeClips.push_back(index);
            return true;
        });
    }

    World& world;
    const VirtualFileSystem& files;
    AudioWorldDesc desc;
    AudioOutput output = AudioOutput::Offline;
    ma_log log{};
    std::unique_ptr<ma_context> context;
    std::unique_ptr<ma_device> device;
    std::unique_ptr<detail::Mixer> mixer;

    std::vector<Clip> clips;
    std::vector<u32> freeClips;
    std::vector<u32> clipsToFree;
    std::unordered_map<std::string, u32, StringHash, std::equal_to<>> clipsByPath;

    std::vector<detail::VoiceState> voices; // what the mixer should play; copied into every snapshot
    std::vector<VoiceRecord> records;
    std::vector<u32> freeVoices;
    std::vector<RetiredDecoder> retired;

    detail::ListenerState listener;
    bool listenerSeen = false;
    f32 masterVolume = 1.0f;
    std::array<f32, kAudioBusCount> busVolumes{};
    std::array<bool, kAudioBusCount> busPaused{};
    std::array<AudioBusFilter, kAudioBusCount> busFilters{};
    AudioReverbSettings reverb;
    std::vector<AudioClipHandle> loadingClips; // clips the streamer is loading

    u32 nextSerial = 0;
    u64 nextOrder = 0;
    u64 published = 0;
    u32 droppedSounds = 0;
    u32 stolenVoices = 0;
    std::vector<Entity> scratch;
};

AudioWorld::AudioWorld(World& world, const VirtualFileSystem& files, const AudioWorldDesc& desc)
    : m_impl(std::make_unique<Impl>(world, files, desc))
{
}

AudioWorld::~AudioWorld()
{
    Impl& impl = *m_impl;
    impl.scratch.clear();
    impl.world.query<const AudioPlayback>().each(
        [&](Entity entity, const AudioPlayback&) { impl.scratch.push_back(entity); });
    for (const Entity entity : impl.scratch)
    {
        impl.world.remove<AudioPlayback>(entity);
    }
}

void AudioWorld::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    impl.collectFinished();
    impl.syncListener(deltaSeconds);
    impl.removeStaleSources();
    if (impl.pollLoadingClips())
    {
        impl.startPendingSources();
    }
    impl.addNewSources();
    impl.updateSources(deltaSeconds);
    impl.publish();
    impl.freeRetired();
}

AudioClipHandle AudioWorld::loadClip(std::string_view virtualPath, AudioClipMode mode)
{
    return m_impl->loadClip(virtualPath, mode);
}

AudioClipHandle AudioWorld::requestClip(std::string_view virtualPath, AudioClipMode mode)
{
    return m_impl->requestClip(virtualPath, mode);
}

AssetState AudioWorld::clipState(AudioClipHandle clip) const noexcept
{
    return m_impl->stateOf(clip);
}

AudioClipHandle AudioWorld::createClip(std::span<const f32> samples, u32 channels, u32 sampleRate)
{
    ILL_ASSERT(channels > 0 && !samples.empty() && samples.size() % channels == 0,
               "createClip needs a whole number of frames with at least one channel");
    ILL_ASSERT(sampleRate > 0, "createClip needs a sample rate");
    detail::PcmAudio pcm;
    detail::convertAudio(samples, channels, sampleRate, m_impl->mixer->sampleRate(), pcm);
    if (pcm.frames == 0)
    {
        return {};
    }
    Impl::Clip clip;
    clip.owned.samples = std::move(pcm.samples);
    clip.owned.channels = pcm.channels;
    clip.owned.frames = pcm.frames;
    return m_impl->addClip(std::move(clip));
}

void AudioWorld::unloadClip(AudioClipHandle clip)
{
    m_impl->unloadClip(clip);
}

std::optional<AudioClipInfo> AudioWorld::clipInfo(AudioClipHandle clip) const
{
    const Impl::Clip* found = m_impl->clipOf(clip);
    if (found == nullptr)
    {
        return std::nullopt;
    }
    const detail::ClipData& data = found->data();
    AudioClipInfo info;
    info.channels = data.channels;
    info.frames = data.frames;
    info.mode = found->mode;
    info.memoryBytes = data.memoryBytes();
    return info;
}

void AudioWorld::play(Entity entity)
{
    Impl& impl = *m_impl;
    if (!impl.world.isAlive(entity) || !impl.world.has<AudioSource>(entity))
    {
        return;
    }
    if (!impl.world.has<AudioPlayback>(entity))
    {
        AudioPlayback playback;
        if (const Transform* transform = impl.world.tryGet<Transform>(entity))
        {
            playback.previousPosition = transform->position;
        }
        impl.world.emplace<AudioPlayback>(entity, playback);
    }
    impl.startSource(entity);
}

void AudioWorld::stop(Entity entity)
{
    Impl& impl = *m_impl;
    if (!impl.world.isAlive(entity))
    {
        return;
    }
    if (AudioPlayback* playback = impl.world.tryGet<AudioPlayback>(entity))
    {
        playback->pendingStart = false;
        if (impl.isActive(*playback))
        {
            impl.releaseVoice(playback->voice);
        }
    }
}

bool AudioWorld::isPlaying(Entity entity) const
{
    const Impl& impl = *m_impl;
    if (!impl.world.isAlive(entity))
    {
        return false;
    }
    const AudioPlayback* playback = impl.world.tryGet<AudioPlayback>(entity);
    return playback != nullptr && impl.isActive(*playback);
}

AudioVoiceHandle AudioWorld::playOneShot(AudioClipHandle clip, const AudioPlayParams& params)
{
    Impl& impl = *m_impl;
    if (impl.clipOf(clip) == nullptr)
    {
        return {};
    }
    const u32 voice = impl.allocateVoice(params.priority);
    if (voice == kNoVoice || !impl.beginVoice(voice, clip.index, kNullEntity, params.priority))
    {
        return {};
    }
    detail::VoiceState& state = impl.voices[voice];
    writeParams(state, params);
    state.position = params.position;
    return AudioVoiceHandle{voice, state.serial};
}

void AudioWorld::stop(AudioVoiceHandle voice)
{
    if (isPlaying(voice))
    {
        m_impl->releaseVoice(voice.voice);
    }
}

bool AudioWorld::isPlaying(AudioVoiceHandle voice) const
{
    const Impl& impl = *m_impl;
    return voice.serial != 0 && voice.voice < impl.records.size() &&
           impl.records[voice.voice].serial == voice.serial;
}

void AudioWorld::setMasterVolume(f32 volume)
{
    m_impl->masterVolume = sanitize(volume, 0.0f, FLT_MAX, 0.0f);
}

f32 AudioWorld::masterVolume() const noexcept
{
    return m_impl->masterVolume;
}

void AudioWorld::setBusVolume(AudioBus bus, f32 volume)
{
    ILL_ASSERT(static_cast<u32>(bus) < kAudioBusCount, "invalid audio bus");
    m_impl->busVolumes[static_cast<u32>(bus)] = sanitize(volume, 0.0f, FLT_MAX, 0.0f);
}

f32 AudioWorld::busVolume(AudioBus bus) const noexcept
{
    return m_impl->busVolumes[static_cast<u32>(sanitizeBus(bus))];
}

void AudioWorld::setBusPaused(AudioBus bus, bool paused)
{
    ILL_ASSERT(static_cast<u32>(bus) < kAudioBusCount, "invalid audio bus");
    m_impl->busPaused[static_cast<u32>(bus)] = paused;
}

bool AudioWorld::isBusPaused(AudioBus bus) const noexcept
{
    return m_impl->busPaused[static_cast<u32>(sanitizeBus(bus))];
}

void AudioWorld::setBusFilter(AudioBus bus, const AudioBusFilter& filter)
{
    ILL_ASSERT(static_cast<u32>(bus) < kAudioBusCount, "invalid audio bus");
    AudioBusFilter& target = m_impl->busFilters[static_cast<u32>(bus)];
    target.lowPassCutoff = sanitize(filter.lowPassCutoff, 0.0f, FLT_MAX, 0.0f);
    target.highPassCutoff = sanitize(filter.highPassCutoff, 0.0f, FLT_MAX, 0.0f);
}

AudioBusFilter AudioWorld::busFilter(AudioBus bus) const noexcept
{
    return m_impl->busFilters[static_cast<u32>(sanitizeBus(bus))];
}

void AudioWorld::setReverb(const AudioReverbSettings& settings)
{
    m_impl->reverb = sanitizeReverb(settings);
}

const AudioReverbSettings& AudioWorld::reverb() const noexcept
{
    return m_impl->reverb;
}

AudioOutput AudioWorld::output() const noexcept
{
    return m_impl->output;
}

u32 AudioWorld::sampleRate() const noexcept
{
    return m_impl->mixer->sampleRate();
}

AudioStats AudioWorld::stats() const noexcept
{
    const Impl& impl = *m_impl;
    AudioStats stats;
    stats.activeVoices = static_cast<u32>(impl.records.size() - impl.freeVoices.size());
    stats.droppedSounds = impl.droppedSounds;
    stats.stolenVoices = impl.stolenVoices;
    stats.mixerAllocations = impl.mixer->systemAllocations();
    for (const Impl::Clip& clip : impl.clips)
    {
        if (clip.loaded && clip.ready)
        {
            ++stats.clips;
            stats.clipBytes += clip.data().memoryBytes();
        }
    }
    stats.loadingClips = static_cast<u32>(impl.loadingClips.size());
    return stats;
}

void AudioWorld::render(std::span<f32> samples)
{
    ILL_ASSERT(m_impl->output == AudioOutput::Offline, "render() is only for AudioOutput::Offline");
    ILL_ASSERT(samples.size() % channelCount() == 0, "render() needs whole stereo frames");
    usize frames = samples.size() / channelCount();
    f32* out = samples.data();
    while (frames > 0)
    {
        const u32 chunk = static_cast<u32>(std::min<usize>(frames, kMaxRenderFrames));
        m_impl->mixer->render(out, chunk);
        out += static_cast<usize>(chunk) * channelCount();
        frames -= chunk;
    }
}

} // namespace illusion

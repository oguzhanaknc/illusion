#include "AudioMixer.hpp"

#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

// Clears the delay lines of a reverb node (MiniaudioImpl.c).
extern "C" void illusion_clear_reverb(ma_reverb_node* node);

namespace illusion::detail
{
namespace
{

// Each block starts with its size; 16 bytes keep the alignment the system allocator guarantees.
constexpr usize kBlockHeader = 16;

constexpr u32 kFilterOrder = 2;        // 12 dB per octave
constexpr f32 kMinCutoff = 10.0f;      // Hz
constexpr f32 kMaxCutoffRatio = 0.45f; // of the mixing rate (below Nyquist)
constexpr u32 kMinReverbRate = 22050;  // verblib's lowest supported rate

// AudioAttenuation::None is applied as Inverse with no rolloff: miniaudio's "none" model would also turn
// off panning and the doppler effect.
ma_attenuation_model toMiniaudio(AudioAttenuation attenuation) noexcept
{
    switch (attenuation)
    {
    case AudioAttenuation::Linear:
        return ma_attenuation_model_linear;
    case AudioAttenuation::Exponential:
        return ma_attenuation_model_exponential;
    case AudioAttenuation::None:
    case AudioAttenuation::Inverse:
        break;
    }
    return ma_attenuation_model_inverse;
}

} // namespace

SnapshotExchange::SnapshotExchange(u32 voiceCount)
{
    for (MixerSnapshot& buffer : m_buffers)
    {
        buffer.voices.resize(voiceCount);
        buffer.busVolumes.fill(1.0f);
    }
}

void SnapshotExchange::publish() noexcept
{
    m_back = m_middle.exchange(m_back | kFresh, std::memory_order_acq_rel) & kIndexMask;
}

const MixerSnapshot* SnapshotExchange::acquire() noexcept
{
    if ((m_middle.load(std::memory_order_relaxed) & kFresh) == 0)
    {
        return nullptr;
    }
    m_front = m_middle.exchange(m_front, std::memory_order_acq_rel) & kIndexMask;
    return &m_buffers[m_front];
}

VoiceHeap::VoiceHeap()
{
    m_bins.reserve(32); // miniaudio uses a handful of block sizes per channel layout
}

VoiceHeap::~VoiceHeap()
{
    for (Bin& bin : m_bins)
    {
        while (bin.head != nullptr)
        {
            void* next = *static_cast<void**>(bin.head);
            std::free(static_cast<std::byte*>(bin.head) - kBlockHeader);
            bin.head = next;
        }
    }
}

ma_allocation_callbacks VoiceHeap::callbacks() noexcept
{
    ma_allocation_callbacks callbacks{};
    callbacks.pUserData = this;
    callbacks.onMalloc = [](size_t size, void* heap) -> void* {
        return static_cast<VoiceHeap*>(heap)->allocate(size);
    };
    callbacks.onRealloc = [](void* block, size_t size, void* heap) -> void* {
        return static_cast<VoiceHeap*>(heap)->reallocate(block, size);
    };
    callbacks.onFree = [](void* block, void* heap) {
        static_cast<VoiceHeap*>(heap)->release(block);
    };
    return callbacks;
}

void* VoiceHeap::allocate(usize size)
{
    for (Bin& bin : m_bins)
    {
        if (bin.size == size && bin.head != nullptr)
        {
            void* block = bin.head;
            bin.head = *static_cast<void**>(block);
            return block;
        }
    }
    // Free blocks store the next pointer in their first bytes.
    auto* raw = static_cast<std::byte*>(std::malloc(kBlockHeader + std::max(size, sizeof(void*))));
    if (raw == nullptr)
    {
        return nullptr;
    }
    m_systemAllocations.fetch_add(1, std::memory_order_relaxed);
    std::memcpy(raw, &size, sizeof(size));
    return raw + kBlockHeader;
}

void* VoiceHeap::reallocate(void* block, usize size)
{
    if (block == nullptr)
    {
        return allocate(size);
    }
    usize oldSize = 0;
    std::memcpy(&oldSize, static_cast<std::byte*>(block) - kBlockHeader, sizeof(oldSize));
    if (oldSize == size)
    {
        return block;
    }
    void* moved = allocate(size);
    if (moved != nullptr)
    {
        std::memcpy(moved, block, std::min(oldSize, size));
        release(block);
    }
    return moved;
}

void VoiceHeap::release(void* block) noexcept
{
    if (block == nullptr)
    {
        return;
    }
    usize size = 0;
    std::memcpy(&size, static_cast<std::byte*>(block) - kBlockHeader, sizeof(size));
    auto bin = std::find_if(m_bins.begin(), m_bins.end(),
                            [size](const Bin& candidate) { return candidate.size == size; });
    if (bin == m_bins.end())
    {
        if (m_bins.size() == m_bins.capacity())
        {
            // Never expected (the pools are warmed for every size miniaudio uses); give the block back
            // rather than allocating on the audio thread.
            std::free(static_cast<std::byte*>(block) - kBlockHeader);
            return;
        }
        m_bins.push_back(Bin{size, nullptr});
        bin = m_bins.end() - 1;
    }
    *static_cast<void**>(block) = bin->head;
    bin->head = block;
}

Mixer::Mixer(u32 sampleRate, u32 maxVoices, f32 speedOfSound)
    : m_voices(std::make_unique<Voice[]>(maxVoices))
    , m_finished(std::make_unique<std::atomic<u32>[]>(maxVoices))
    , m_reverb(std::make_unique<ma_reverb_node>())
    , m_voiceCount(maxVoices)
    , m_sampleRate(sampleRate)
    , m_exchange(maxVoices)
{
    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE; // the AudioWorld owns the device and calls render()
    config.channels = 2;
    config.sampleRate = sampleRate;
    config.listenerCount = 1;
    config.allocationCallbacks = m_heap.callbacks();
    if (ma_engine_init(&config, &m_engine) != MA_SUCCESS)
    {
        return;
    }
    m_engineReady = true;
    ma_spatializer_listener_set_speed_of_sound(&m_engine.listeners[0], speedOfSound);
    ma_node_graph* graph = ma_engine_get_node_graph(&m_engine);
    const ma_allocation_callbacks callbacks = m_heap.callbacks();
    const f64 initialCutoff = static_cast<f64>(sampleRate) * 0.25;
    for (; m_busCount < kAudioBusCount; ++m_busCount)
    {
        if (ma_sound_group_init(&m_engine, MA_SOUND_FLAG_NO_PITCH, nullptr, &m_buses[m_busCount].group) !=
            MA_SUCCESS)
        {
            return;
        }
    }
    for (; m_busFilterCount < kAudioBusCount; ++m_busFilterCount)
    {
        Bus& bus = m_buses[m_busFilterCount];
        const ma_lpf_node_config lowPass =
            ma_lpf_node_config_init(2, sampleRate, initialCutoff, kFilterOrder);
        if (ma_lpf_node_init(graph, &lowPass, &callbacks, &bus.lowPass) != MA_SUCCESS)
        {
            return;
        }
        const ma_hpf_node_config highPass =
            ma_hpf_node_config_init(2, sampleRate, initialCutoff, kFilterOrder);
        if (ma_hpf_node_init(graph, &highPass, &callbacks, &bus.highPass) != MA_SUCCESS)
        {
            ma_lpf_node_uninit(&bus.lowPass, &callbacks);
            return;
        }
        ma_node_detach_output_bus(&bus.lowPass, 0);
        ma_node_detach_output_bus(&bus.highPass, 0);
    }
    for (; m_voiceNodeCount < maxVoices; ++m_voiceNodeCount)
    {
        Voice& voice = m_voices[m_voiceNodeCount];
        const ma_lpf_node_config lowPass =
            ma_lpf_node_config_init(2, sampleRate, initialCutoff, kFilterOrder);
        if (ma_lpf_node_init(graph, &lowPass, &callbacks, &voice.lowPass) != MA_SUCCESS)
        {
            return;
        }
        const ma_splitter_node_config splitter = ma_splitter_node_config_init(2);
        if (ma_splitter_node_init(graph, &splitter, &callbacks, &voice.splitter) != MA_SUCCESS)
        {
            ma_lpf_node_uninit(&voice.lowPass, &callbacks);
            return;
        }
        // Idle voices are disconnected so that the graph does not process them.
        ma_node_detach_output_bus(&voice.lowPass, 0);
        ma_node_detach_output_bus(&voice.splitter, 0);
        ma_node_detach_output_bus(&voice.splitter, 1);
    }
    // verblib supports 22050 Hz and up; below that sounds simply do not reach a reverb.
    if (sampleRate >= kMinReverbRate)
    {
        ma_reverb_node_config reverb = ma_reverb_node_config_init(2, sampleRate);
        reverb.dryVolume = 0.0f; // the dry signal reaches the output through the buses
        if (ma_reverb_node_init(graph, &reverb, &callbacks, m_reverb.get()) == MA_SUCCESS)
        {
            m_reverbReady = true;
            ma_node_detach_output_bus(m_reverb.get(), 0);
            applyReverb(AudioReverbSettings{});
        }
    }
    warmPools(maxVoices);
    m_valid = true;
}

Mixer::~Mixer()
{
    const ma_allocation_callbacks callbacks = m_heap.callbacks();
    for (u32 i = 0; i < m_voiceCount; ++i)
    {
        if (m_voices[i].active)
        {
            stop(m_voices[i]);
        }
    }
    if (m_reverbReady)
    {
        ma_reverb_node_uninit(m_reverb.get(), &callbacks);
    }
    for (u32 i = m_voiceNodeCount; i > 0; --i)
    {
        ma_splitter_node_uninit(&m_voices[i - 1].splitter, &callbacks);
        ma_lpf_node_uninit(&m_voices[i - 1].lowPass, &callbacks);
    }
    for (u32 bus = m_busFilterCount; bus > 0; --bus)
    {
        ma_hpf_node_uninit(&m_buses[bus - 1].highPass, &callbacks);
        ma_lpf_node_uninit(&m_buses[bus - 1].lowPass, &callbacks);
    }
    for (u32 bus = m_busCount; bus > 0; --bus)
    {
        ma_sound_group_uninit(&m_buses[bus - 1].group);
    }
    if (m_engineReady)
    {
        ma_engine_uninit(&m_engine);
    }
}

f32 Mixer::clampCutoff(f32 cutoff) const noexcept
{
    return std::clamp(cutoff, kMinCutoff, static_cast<f32>(m_sampleRate) * kMaxCutoffRatio);
}

void Mixer::warmPools(u32 maxVoices)
{
    // Starting a sound allocates its processing node and cache. Starting every voice once with each
    // channel layout fills the pools, so playback never reaches the system allocator afterwards.
    static constexpr std::array<f32, 2> kSilence{};
    for (u32 channels = 1; channels <= 2; ++channels)
    {
        VoiceState state;
        state.serial = 1;
        state.samples = kSilence.data();
        state.frames = 1;
        state.channels = channels;
        for (u32 i = 0; i < maxVoices; ++i)
        {
            start(i, state);
        }
        for (u32 i = 0; i < maxVoices; ++i)
        {
            if (m_voices[i].active)
            {
                stop(m_voices[i]);
            }
            m_voices[i].serial = 0;
            m_finished[i].store(0, std::memory_order_relaxed);
        }
    }
}

void Mixer::render(f32* out, u32 frameCount) noexcept
{
    ILL_PROFILE_SCOPE();
    if (const MixerSnapshot* snapshot = m_exchange.acquire())
    {
        apply(*snapshot);
        m_applied = snapshot->serial;
    }
    connectReverb(frameCount);
    ma_uint64 read = 0;
    ma_engine_read_pcm_frames(&m_engine, out, frameCount, &read);
    if (read < frameCount)
    {
        std::fill(out + read * 2, out + static_cast<usize>(frameCount) * 2, 0.0f);
    }
    for (u32 i = 0; i < m_voiceCount; ++i)
    {
        Voice& voice = m_voices[i];
        if (voice.active && ma_sound_at_end(&voice.sound))
        {
            stop(voice);
            m_finished[i].store(voice.serial, std::memory_order_release);
        }
    }
    m_acknowledged.store(m_applied, std::memory_order_release);
}

void Mixer::connectReverb(u32 frameCount) noexcept
{
    if (!m_reverbReady)
    {
        return;
    }
    // The reverb runs only while sounds feed it and until its tail has decayed below audibility.
    if (m_sendingVoices > 0)
    {
        m_reverbTailFrames = verblib_get_decay_time_in_frames(&m_reverb->reverb);
        if (!m_reverbConnected)
        {
            ma_node_attach_output_bus(m_reverb.get(), 0, ma_engine_get_endpoint(&m_engine), 0);
            m_reverbConnected = true;
        }
    }
    else if (m_reverbConnected)
    {
        if (m_reverbTailFrames > frameCount)
        {
            m_reverbTailFrames -= frameCount;
        }
        else
        {
            // The tail is inaudible now; clearing what is left keeps it from returning on the next send.
            ma_node_detach_output_bus(m_reverb.get(), 0);
            illusion_clear_reverb(m_reverb.get());
            m_reverbConnected = false;
            m_reverbTailFrames = 0;
        }
    }
}

void Mixer::apply(const MixerSnapshot& snapshot) noexcept
{
    const ListenerState& listener = snapshot.listener;
    ma_engine_listener_set_position(&m_engine, 0, listener.position.x, listener.position.y,
                                    listener.position.z);
    ma_engine_listener_set_direction(&m_engine, 0, listener.forward.x, listener.forward.y,
                                     listener.forward.z);
    ma_engine_listener_set_world_up(&m_engine, 0, listener.up.x, listener.up.y, listener.up.z);
    ma_engine_listener_set_velocity(&m_engine, 0, listener.velocity.x, listener.velocity.y,
                                    listener.velocity.z);
    ma_engine_set_volume(&m_engine, snapshot.masterVolume);
    for (u32 index = 0; index < kAudioBusCount; ++index)
    {
        Bus& bus = m_buses[index];
        bus.volume = snapshot.busVolumes[index];
        ma_sound_group_set_volume(&bus.group, bus.volume);
        if (snapshot.busPaused[index] != bus.paused)
        {
            bus.paused = snapshot.busPaused[index];
            if (bus.paused)
            {
                ma_sound_group_stop(&bus.group);
            }
            else
            {
                ma_sound_group_start(&bus.group);
            }
        }
        routeBus(bus, snapshot.busFilters[index]);
    }
    if (m_reverbReady)
    {
        applyReverb(snapshot.reverb);
    }
    for (u32 i = 0; i < m_voiceCount; ++i)
    {
        const VoiceState& state = snapshot.voices[i];
        Voice& voice = m_voices[i];
        if (state.serial != voice.serial)
        {
            if (voice.active)
            {
                stop(voice);
            }
            voice.serial = state.serial;
            if (state.serial != 0)
            {
                start(i, state);
            }
        }
        else if (voice.active)
        {
            update(voice, state);
        }
    }
}

void Mixer::routeBus(Bus& bus, const AudioBusFilter& filter) noexcept
{
    const f32 lowPass = filter.lowPassCutoff > 0.0f ? clampCutoff(filter.lowPassCutoff) : 0.0f;
    const f32 highPass = filter.highPassCutoff > 0.0f ? clampCutoff(filter.highPassCutoff) : 0.0f;
    if (lowPass == bus.filter.lowPassCutoff && highPass == bus.filter.highPassCutoff)
    {
        return;
    }
    if (lowPass > 0.0f && lowPass != bus.filter.lowPassCutoff)
    {
        const ma_lpf_config config =
            ma_lpf_config_init(ma_format_f32, 2, m_sampleRate, static_cast<f64>(lowPass), kFilterOrder);
        ma_lpf_node_reinit(&config, &bus.lowPass);
    }
    if (highPass > 0.0f && highPass != bus.filter.highPassCutoff)
    {
        const ma_hpf_config config =
            ma_hpf_config_init(ma_format_f32, 2, m_sampleRate, static_cast<f64>(highPass), kFilterOrder);
        ma_hpf_node_reinit(&config, &bus.highPass);
    }
    // group -> [low-pass] -> [high-pass] -> endpoint
    ma_node* endpoint = ma_engine_get_endpoint(&m_engine);
    ma_node* next = endpoint;
    if (highPass > 0.0f)
    {
        ma_node_attach_output_bus(&bus.highPass, 0, next, 0);
        next = &bus.highPass;
    }
    else
    {
        ma_node_detach_output_bus(&bus.highPass, 0);
    }
    if (lowPass > 0.0f)
    {
        ma_node_attach_output_bus(&bus.lowPass, 0, next, 0);
        next = &bus.lowPass;
    }
    else
    {
        ma_node_detach_output_bus(&bus.lowPass, 0);
    }
    ma_node_attach_output_bus(&bus.group, 0, next, 0);
    bus.filter = AudioBusFilter{lowPass, highPass};
}

void Mixer::applyReverb(const AudioReverbSettings& settings) noexcept
{
    verblib& reverb = m_reverb->reverb;
    if (settings.roomSize != m_reverbSettings.roomSize)
    {
        verblib_set_room_size(&reverb, settings.roomSize);
    }
    if (settings.damping != m_reverbSettings.damping)
    {
        verblib_set_damping(&reverb, settings.damping);
    }
    if (settings.width != m_reverbSettings.width)
    {
        verblib_set_width(&reverb, settings.width);
    }
    if (settings.volume != m_reverbSettings.volume)
    {
        verblib_set_wet(&reverb, settings.volume / verblib_scalewet);
    }
    m_reverbSettings = settings;
}

void Mixer::start(u32 index, const VoiceState& state) noexcept
{
    Voice& voice = m_voices[index];
    voice.serial = state.serial;
    ma_data_source* source = state.decoder;
    if (source == nullptr)
    {
        ma_audio_buffer_ref_init(ma_format_f32, state.channels, state.samples, state.frames, &voice.buffer);
        voice.buffer.sampleRate = m_sampleRate;
        source = &voice.buffer;
    }
    ma_sound_config config = ma_sound_config_init_2(&m_engine);
    config.pDataSource = source;
    config.pInitialAttachment = &voice.splitter;
    if (ma_sound_init_ex(&m_engine, &config, &voice.sound) != MA_SUCCESS)
    {
        // Out of memory: report the sound as finished so the game thread frees the voice.
        m_finished[index].store(state.serial, std::memory_order_release);
        return;
    }
    voice.active = true;
    voice.bus = state.bus;
    voice.lowPassCutoff = 0.0f;
    voice.sending = false;
    ma_node_attach_output_bus(&voice.splitter, 0, &m_buses[static_cast<u32>(state.bus)].group, 0);
    ma_sound_set_positioning(&voice.sound, ma_positioning_absolute);
    update(voice, state);
    ma_sound_start(&voice.sound);
}

void Mixer::stop(Voice& voice) noexcept
{
    ma_sound_uninit(&voice.sound);
    ma_node_detach_output_bus(&voice.lowPass, 0);
    ma_node_detach_output_bus(&voice.splitter, 0);
    if (voice.sending)
    {
        ma_node_detach_output_bus(&voice.splitter, 1);
        voice.sending = false;
        --m_sendingVoices;
    }
    voice.lowPassCutoff = 0.0f;
    voice.active = false;
}

void Mixer::update(Voice& voice, const VoiceState& state) noexcept
{
    ma_sound* sound = &voice.sound;
    ma_sound_set_volume(sound, state.volume);
    ma_sound_set_pitch(sound, state.pitch);
    ma_sound_set_pan(sound, state.pan);
    ma_sound_set_looping(sound, state.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(sound, state.spatial ? MA_TRUE : MA_FALSE);
    if (state.spatial)
    {
        ma_sound_set_position(sound, state.position.x, state.position.y, state.position.z);
        ma_sound_set_velocity(sound, state.velocity.x, state.velocity.y, state.velocity.z);
        ma_sound_set_direction(sound, state.direction.x, state.direction.y, state.direction.z);
        ma_sound_set_cone(sound, state.coneInnerAngle, state.coneOuterAngle, state.coneOuterVolume);
        ma_sound_set_attenuation_model(sound, toMiniaudio(state.attenuation));
        ma_sound_set_rolloff(sound, state.attenuation == AudioAttenuation::None ? 0.0f : state.rolloff);
        ma_sound_set_min_distance(sound, state.minDistance);
        ma_sound_set_max_distance(sound, state.maxDistance);
        ma_sound_set_doppler_factor(sound, state.dopplerFactor);
    }
    if (state.bus != voice.bus)
    {
        ma_node_attach_output_bus(&voice.splitter, 0, &m_buses[static_cast<u32>(state.bus)].group, 0);
        voice.bus = state.bus;
    }
    routeVoice(voice, state);
}

void Mixer::routeVoice(Voice& voice, const VoiceState& state) noexcept
{
    // sound -> [low-pass] -> splitter
    const f32 cutoff = state.lowPassCutoff > 0.0f ? clampCutoff(state.lowPassCutoff) : 0.0f;
    if (cutoff != voice.lowPassCutoff)
    {
        if (cutoff > 0.0f)
        {
            const ma_lpf_config config =
                ma_lpf_config_init(ma_format_f32, 2, m_sampleRate, static_cast<f64>(cutoff), kFilterOrder);
            ma_lpf_node_reinit(&config, &voice.lowPass);
            if (voice.lowPassCutoff == 0.0f)
            {
                ma_lpf_clear_cache(&voice.lowPass.lpf); // no history from an earlier sound
                ma_node_attach_output_bus(&voice.lowPass, 0, &voice.splitter, 0);
                ma_node_attach_output_bus(&voice.sound, 0, &voice.lowPass, 0);
            }
        }
        else
        {
            ma_node_attach_output_bus(&voice.sound, 0, &voice.splitter, 0);
            ma_node_detach_output_bus(&voice.lowPass, 0);
        }
        voice.lowPassCutoff = cutoff;
    }
    // splitter output 1 -> reverb, scaled like the bus so that bus volumes and pauses apply to the send.
    const Bus& bus = m_buses[static_cast<u32>(voice.bus)];
    const f32 sendVolume = state.reverbSend * bus.volume;
    const bool send = m_reverbReady && sendVolume > 0.0f && !bus.paused;
    if (send)
    {
        if (!voice.sending)
        {
            ma_node_attach_output_bus(&voice.splitter, 1, m_reverb.get(), 0);
            voice.sending = true;
            ++m_sendingVoices;
        }
        ma_node_set_output_bus_volume(&voice.splitter, 1, sendVolume);
    }
    else if (voice.sending)
    {
        ma_node_detach_output_bus(&voice.splitter, 1);
        voice.sending = false;
        --m_sendingVoices;
    }
}

} // namespace illusion::detail

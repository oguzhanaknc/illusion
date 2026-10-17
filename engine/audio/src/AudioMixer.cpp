#include "AudioMixer.hpp"

#include <illusion/core/Profiler.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace illusion::detail
{
namespace
{

// Each block starts with its size; 16 bytes keep the alignment the system allocator guarantees.
constexpr usize kBlockHeader = 16;

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
    for (; m_busCount < kAudioBusCount; ++m_busCount)
    {
        if (ma_sound_group_init(&m_engine, MA_SOUND_FLAG_NO_PITCH, nullptr, &m_buses[m_busCount]) !=
            MA_SUCCESS)
        {
            return;
        }
    }
    warmPools(maxVoices);
    m_valid = true;
}

Mixer::~Mixer()
{
    for (u32 i = 0; i < m_voiceCount; ++i)
    {
        if (m_voices[i].active)
        {
            stop(m_voices[i]);
        }
    }
    for (u32 bus = m_busCount; bus > 0; --bus)
    {
        ma_sound_group_uninit(&m_buses[bus - 1]);
    }
    if (m_engineReady)
    {
        ma_engine_uninit(&m_engine);
    }
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
    for (u32 bus = 0; bus < kAudioBusCount; ++bus)
    {
        ma_sound_group_set_volume(&m_buses[bus], snapshot.busVolumes[bus]);
        if (snapshot.busPaused[bus] != m_busPaused[bus])
        {
            m_busPaused[bus] = snapshot.busPaused[bus];
            if (m_busPaused[bus])
            {
                ma_sound_group_stop(&m_buses[bus]);
            }
            else
            {
                ma_sound_group_start(&m_buses[bus]);
            }
        }
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
    config.pInitialAttachment = &m_buses[static_cast<u32>(state.bus)];
    if (ma_sound_init_ex(&m_engine, &config, &voice.sound) != MA_SUCCESS)
    {
        // Out of memory: report the sound as finished so the game thread frees the voice.
        m_finished[index].store(state.serial, std::memory_order_release);
        return;
    }
    voice.active = true;
    voice.bus = state.bus;
    ma_sound_set_positioning(&voice.sound, ma_positioning_absolute);
    update(voice, state);
    ma_sound_start(&voice.sound);
}

void Mixer::stop(Voice& voice) noexcept
{
    ma_sound_uninit(&voice.sound);
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
        ma_sound_set_attenuation_model(sound, toMiniaudio(state.attenuation));
        ma_sound_set_rolloff(sound, state.attenuation == AudioAttenuation::None ? 0.0f : state.rolloff);
        ma_sound_set_min_distance(sound, state.minDistance);
        ma_sound_set_max_distance(sound, state.maxDistance);
        ma_sound_set_doppler_factor(sound, state.dopplerFactor);
    }
    if (state.bus != voice.bus)
    {
        ma_node_attach_output_bus(sound, 0, &m_buses[static_cast<u32>(state.bus)], 0);
        voice.bus = state.bus;
    }
}

} // namespace illusion::detail

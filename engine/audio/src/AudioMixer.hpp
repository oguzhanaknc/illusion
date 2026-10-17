#pragma once

// The mixer: a miniaudio engine owned by the audio thread (internal to illusion::audio).
//
// The game thread never touches miniaudio objects after construction. It describes what should play in a
// MixerSnapshot and publishes it through a lock-free triple buffer; the audio thread applies the newest
// snapshot at the start of each render() (starting, stopping and updating sounds), mixes, and reports
// voices that reached their end and the newest snapshot it applied through atomics.
//
// Graph: sound -> [low-pass] -> splitter -> bus group -> [bus low-pass] -> [bus high-pass] -> endpoint, and
// the splitter's second output -> reverb -> endpoint. Bracketed filters are bypassed while off, and the
// reverb is only connected while sounds feed it or its tail rings.

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/audio/AudioWorld.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <miniaudio.h>

#include <array>
#include <atomic>
#include <extras/nodes/ma_reverb_node/ma_reverb_node.h>
#include <memory>
#include <vector>

namespace illusion::detail
{

// What one voice should play. Idle when serial is 0.
struct VoiceState
{
    u32 serial = 0;                // Changes whenever the voice starts a new sound.
    const f32* samples = nullptr;  // Decoded clip, or
    ma_decoder* decoder = nullptr; // a streamed clip (fresh decoder owned by the game thread).
    u64 frames = 0;                // Decoded clips: length in frames.
    u32 channels = 0;              // 1 or 2.
    AudioBus bus = AudioBus::Sfx;
    AudioAttenuation attenuation = AudioAttenuation::Inverse;
    bool loop = false;
    bool spatial = false;
    f32 volume = 1.0f;
    f32 pitch = 1.0f;
    f32 pan = 0.0f;
    f32 minDistance = 1.0f;
    f32 maxDistance = 1000.0f;
    f32 rolloff = 1.0f;
    f32 dopplerFactor = 1.0f;
    f32 lowPassCutoff = 0.0f; // Hz; 0 is off
    f32 reverbSend = 0.0f;
    f32 coneInnerAngle = kAudioFullCone;
    f32 coneOuterAngle = kAudioFullCone;
    f32 coneOuterVolume = 0.0f;
    Vec3 position{0.0f};
    Vec3 velocity{0.0f};
    Vec3 direction{0.0f, 0.0f, -1.0f};
};

struct ListenerState
{
    Vec3 position{0.0f};
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    Vec3 velocity{0.0f};
};

struct MixerSnapshot
{
    u64 serial = 0;
    ListenerState listener;
    f32 masterVolume = 1.0f;
    std::array<f32, kAudioBusCount> busVolumes{};
    std::array<bool, kAudioBusCount> busPaused{};
    std::array<AudioBusFilter, kAudioBusCount> busFilters{};
    AudioReverbSettings reverb;
    std::vector<VoiceState> voices; // One entry per voice; sized once.
};

// Lock-free single-producer, single-consumer triple buffer: the producer always has a buffer to write, the
// consumer always gets the newest published one, and neither ever waits.
class SnapshotExchange : NonCopyable
{
public:
    explicit SnapshotExchange(u32 voiceCount);

    // Game thread: the buffer to fill, then publish() it.
    [[nodiscard]] MixerSnapshot& back() noexcept { return m_buffers[m_back]; }
    void publish() noexcept;

    // Audio thread: the newest snapshot if one was published since the last call, otherwise null.
    [[nodiscard]] const MixerSnapshot* acquire() noexcept;

private:
    static constexpr u32 kIndexMask = 3;
    static constexpr u32 kFresh = 4;

    std::array<MixerSnapshot, 3> m_buffers;
    std::atomic<u32> m_middle{2};
    u32 m_back = 0;  // owned by the game thread
    u32 m_front = 1; // owned by the audio thread
};

// Recycles freed blocks by exact size, so miniaudio's per-sound allocations stop reaching the system
// allocator once every size has been seen. Not thread-safe: the engine uses it only during construction,
// on the audio thread and during destruction, never at the same time.
class VoiceHeap : NonCopyable
{
public:
    VoiceHeap();
    ~VoiceHeap();

    [[nodiscard]] ma_allocation_callbacks callbacks() noexcept;

    // Blocks taken from the system allocator (stays constant once the pools are warm).
    [[nodiscard]] u64 systemAllocations() const noexcept
    {
        return m_systemAllocations.load(std::memory_order_relaxed);
    }

private:
    struct Bin
    {
        usize size;
        void* head;
    };

    void* allocate(usize size);
    void* reallocate(void* block, usize size);
    void release(void* block) noexcept;

    std::vector<Bin> m_bins;
    std::atomic<u64> m_systemAllocations{0}; // read by the game thread for statistics
};

class Mixer : NonCopyable
{
public:
    // Creates the engine, the buses and warms the allocation pools for maxVoices mono and stereo sounds.
    // Check valid() afterwards.
    Mixer(u32 sampleRate, u32 maxVoices, f32 speedOfSound);
    ~Mixer();

    [[nodiscard]] bool valid() const noexcept { return m_valid; }
    [[nodiscard]] u32 sampleRate() const noexcept { return m_sampleRate; }
    [[nodiscard]] SnapshotExchange& exchange() noexcept { return m_exchange; }

    // Audio thread: applies the newest snapshot and mixes frameCount stereo frames into out.
    void render(f32* out, u32 frameCount) noexcept;

    // Game thread: the serial of the last sound on voice that reached its end.
    [[nodiscard]] u32 finishedSerial(u32 voice) const noexcept
    {
        return m_finished[voice].load(std::memory_order_acquire);
    }

    // Game thread: the serial of the newest snapshot the audio thread applied and mixed. Resources that
    // newer snapshots no longer reference can be freed once this reaches them.
    [[nodiscard]] u64 acknowledgedSnapshot() const noexcept
    {
        return m_acknowledged.load(std::memory_order_acquire);
    }

    // Blocks the mixer took from the system allocator (for tests).
    [[nodiscard]] u64 systemAllocations() const noexcept { return m_heap.systemAllocations(); }

    // True if the reverb could be created (it needs a mixing rate of at least 22050 Hz).
    [[nodiscard]] bool hasReverb() const noexcept { return m_reverbReady; }

    // Audio thread (tests): true while the reverb is connected to the output.
    [[nodiscard]] bool reverbConnected() const noexcept { return m_reverbConnected; }

private:
    struct Voice
    {
        ma_sound sound;
        ma_audio_buffer_ref buffer;
        ma_lpf_node lowPass;
        ma_splitter_node splitter; // output 0: the bus; output 1: the reverb
        u32 serial = 0;            // serial of the last sound started on this voice
        bool active = false;
        AudioBus bus = AudioBus::Sfx;
        f32 lowPassCutoff = 0.0f; // in effect; 0 while the filter is bypassed
        bool sending = false;     // output 1 is connected to the reverb
    };

    struct Bus
    {
        ma_sound_group group;
        ma_lpf_node lowPass;
        ma_hpf_node highPass;
        AudioBusFilter filter; // in effect
        bool paused = false;
        f32 volume = 1.0f;
    };

    void apply(const MixerSnapshot& snapshot) noexcept;
    void start(u32 index, const VoiceState& state) noexcept;
    void stop(Voice& voice) noexcept;
    void update(Voice& voice, const VoiceState& state) noexcept;
    void routeVoice(Voice& voice, const VoiceState& state) noexcept;
    void routeBus(Bus& bus, const AudioBusFilter& filter) noexcept;
    void applyReverb(const AudioReverbSettings& settings) noexcept;
    void connectReverb(u32 frameCount) noexcept;
    [[nodiscard]] f32 clampCutoff(f32 cutoff) const noexcept;
    void warmPools(u32 maxVoices);

    VoiceHeap m_heap;
    ma_engine m_engine{};
    std::array<Bus, kAudioBusCount> m_buses{};
    std::unique_ptr<Voice[]> m_voices; // fixed addresses: miniaudio's node graph points into them
    std::unique_ptr<std::atomic<u32>[]> m_finished;
    std::unique_ptr<ma_reverb_node> m_reverb; // large (fixed delay lines): kept off the Mixer object
    AudioReverbSettings m_reverbSettings;     // in effect
    u32 m_voiceCount = 0;
    u32 m_sampleRate = 0;
    u32 m_busCount = 0;         // bus groups initialized so far
    u32 m_busFilterCount = 0;   // buses whose filter nodes are initialized
    u32 m_voiceNodeCount = 0;   // voices whose filter and splitter nodes are initialized
    u32 m_sendingVoices = 0;    // voices connected to the reverb
    u64 m_reverbTailFrames = 0; // frames the reverb stays connected after the last send stops
    bool m_engineReady = false;
    bool m_reverbReady = false;
    bool m_reverbConnected = false;
    bool m_valid = false;
    SnapshotExchange m_exchange;
    std::atomic<u64> m_acknowledged{0};
    u64 m_applied = 0; // audio thread: serial of the snapshot in effect
};

} // namespace illusion::detail

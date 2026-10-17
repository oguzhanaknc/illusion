#pragma once

// Decoding and conversion of audio data with miniaudio (internal to illusion::audio).

#include <illusion/core/Base.hpp>

#include <miniaudio.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace illusion::detail
{

// Interleaved f32 PCM with one or two channels.
struct PcmAudio
{
    std::vector<f32> samples;
    u32 channels = 0;
    u64 frames = 0;
};

// Decodes a whole WAV, FLAC, MP3 or Ogg Vorbis file to PCM at sampleRate; more than two channels are
// downmixed to stereo. Returns false if the data cannot be decoded or holds no audio.
bool decodeAudio(std::span<const std::byte> data, u32 sampleRate, PcmAudio& out);

// Converts interleaved PCM to sampleRate with at most two channels.
void convertAudio(std::span<const f32> samples, u32 channels, u32 sourceRate, u32 sampleRate, PcmAudio& out);

// Checks that encoded data can be streamed and reports its channel count after downmixing (1 or 2) and its
// length in frames at sampleRate (0 if the format cannot tell).
bool probeAudio(std::span<const std::byte> data, u32 sampleRate, u32& channels, u64& frames);

struct DecoderDeleter
{
    void operator()(ma_decoder* decoder) const noexcept;
};

using DecoderPtr = std::unique_ptr<ma_decoder, DecoderDeleter>;

// Opens a decoder that streams data (which must outlive it) as f32 with the given channel count and rate.
// Returns null on failure.
DecoderPtr openAudioStream(std::span<const std::byte> data, u32 channels, u32 sampleRate);

} // namespace illusion::detail

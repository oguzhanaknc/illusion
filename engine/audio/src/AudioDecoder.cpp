#include "AudioDecoder.hpp"

#include <algorithm>

namespace illusion::detail
{
namespace
{

ma_result initDecoder(std::span<const std::byte> data, u32 channels, u32 sampleRate, ma_decoder& decoder)
{
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, sampleRate);
    return ma_decoder_init_memory(data.data(), data.size(), &config, &decoder);
}

// Opens a decoder with the source's channel count, or stereo if the source has more channels.
bool openDecoder(std::span<const std::byte> data, u32 sampleRate, ma_decoder& decoder)
{
    if (data.empty() || initDecoder(data, 0, sampleRate, decoder) != MA_SUCCESS)
    {
        return false;
    }
    if (decoder.outputChannels > 2)
    {
        ma_decoder_uninit(&decoder);
        return initDecoder(data, 2, sampleRate, decoder) == MA_SUCCESS;
    }
    return true;
}

} // namespace

bool decodeAudio(std::span<const std::byte> data, u32 sampleRate, PcmAudio& out)
{
    ma_decoder decoder{};
    if (!openDecoder(data, sampleRate, decoder))
    {
        return false;
    }
    const u32 channels = decoder.outputChannels;
    out.channels = channels;
    out.samples.clear();

    // The length is only a hint (it is unknown for some formats); cap it so a corrupted header cannot
    // reserve gigabytes.
    constexpr u64 kMaxReservedSamples = u64{1} << 26;
    ma_uint64 length = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS)
    {
        out.samples.reserve(static_cast<usize>(std::min<u64>(length * channels, kMaxReservedSamples)));
    }

    constexpr ma_uint64 kChunkFrames = 4096;
    bool ok = true;
    for (;;)
    {
        const usize offset = out.samples.size();
        out.samples.resize(offset + static_cast<usize>(kChunkFrames) * channels);
        ma_uint64 read = 0;
        const ma_result result =
            ma_decoder_read_pcm_frames(&decoder, out.samples.data() + offset, kChunkFrames, &read);
        out.samples.resize(offset + static_cast<usize>(read) * channels);
        if (result == MA_AT_END || (result == MA_SUCCESS && read == 0))
        {
            break;
        }
        if (result != MA_SUCCESS)
        {
            ok = false;
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    out.frames = out.samples.size() / channels;
    return ok && out.frames > 0;
}

void convertAudio(std::span<const f32> samples, u32 channels, u32 sourceRate, u32 sampleRate, PcmAudio& out)
{
    const u32 outChannels = std::min(channels, 2u);
    const ma_uint64 inFrames = samples.size() / channels;
    out.channels = outChannels;
    if (outChannels == channels && sourceRate == sampleRate)
    {
        out.samples.assign(samples.begin(), samples.end());
        out.frames = inFrames;
        return;
    }
    const ma_uint64 capacity =
        ma_convert_frames(nullptr, 0, ma_format_f32, outChannels, sampleRate, samples.data(), inFrames,
                          ma_format_f32, channels, sourceRate);
    out.samples.resize(static_cast<usize>(capacity) * outChannels);
    const ma_uint64 frames =
        ma_convert_frames(out.samples.data(), capacity, ma_format_f32, outChannels, sampleRate,
                          samples.data(), inFrames, ma_format_f32, channels, sourceRate);
    out.samples.resize(static_cast<usize>(frames) * outChannels);
    out.frames = frames;
}

bool probeAudio(std::span<const std::byte> data, u32 sampleRate, u32& channels, u64& frames)
{
    ma_decoder decoder{};
    if (!openDecoder(data, sampleRate, decoder))
    {
        return false;
    }
    channels = decoder.outputChannels;
    ma_uint64 length = 0;
    frames = ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS ? length : 0;
    // Decode the first block so that files with a valid header but broken audio are rejected up front.
    std::vector<f32> probe(static_cast<usize>(256) * channels);
    ma_uint64 read = 0;
    const ma_result result = ma_decoder_read_pcm_frames(&decoder, probe.data(), 256, &read);
    ma_decoder_uninit(&decoder);
    return (result == MA_SUCCESS || result == MA_AT_END) && read > 0;
}

void DecoderDeleter::operator()(ma_decoder* decoder) const noexcept
{
    ma_decoder_uninit(decoder);
    delete decoder;
}

DecoderPtr openAudioStream(std::span<const std::byte> data, u32 channels, u32 sampleRate)
{
    auto* decoder = new ma_decoder{};
    if (initDecoder(data, channels, sampleRate, *decoder) != MA_SUCCESS)
    {
        delete decoder;
        return nullptr;
    }
    return DecoderPtr(decoder);
}

bool loadClipData(std::vector<std::byte>&& bytes, bool stream, u32 sampleRate, ClipData& out)
{
    if (stream)
    {
        if (!probeAudio(bytes, sampleRate, out.channels, out.frames))
        {
            return false;
        }
        out.encoded = std::move(bytes);
        return true;
    }
    PcmAudio pcm;
    if (!decodeAudio(bytes, sampleRate, pcm))
    {
        return false;
    }
    out.samples = std::move(pcm.samples);
    out.channels = pcm.channels;
    out.frames = pcm.frames;
    return true;
}

} // namespace illusion::detail

#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Version.hpp>
#include <illusion/save/SaveFile.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace illusion
{
namespace
{

constexpr std::array<std::byte, 4> kMagic{std::byte{'I'}, std::byte{'L'}, std::byte{'S'}, std::byte{'V'}};

// Header: magic, container version (u32), payload size (u64), xxhash64 of the payload (u64).
constexpr usize kVersionOffset = 4;
constexpr usize kSizeOffset = 8;
constexpr usize kChecksumOffset = 16;
static_assert(kChecksumOffset + 8 == kSaveHeaderBytes);

// Payload: the metadata section's size (u32), the metadata JSON, the World section.
constexpr usize kSectionSizeBytes = 4;

// Far above what valid metadata needs (every character of title and custom escaped as \uXXXX).
constexpr usize kMaxMetadataBytes = 16 * 1024;

// The longest engine version string read back.
constexpr usize kMaxEngineVersionBytes = 64;

template <class T>
void storeLittleEndian(std::byte* out, T value) noexcept
{
    for (usize i = 0; i < sizeof(T); ++i)
    {
        out[i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
    }
}

template <class T>
T loadLittleEndian(const std::byte* in) noexcept
{
    T value = 0;
    for (usize i = 0; i < sizeof(T); ++i)
    {
        value |= static_cast<T>(static_cast<T>(in[i]) << (8 * i));
    }
    return value;
}

// The payload of @p bytes after checking the header and the checksum.
Expected<std::span<const std::byte>, SaveError> checkedPayload(std::span<const std::byte> bytes) noexcept
{
    if (bytes.size() < kVersionOffset + 4 || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
    {
        return unexpected(SaveError::Corrupt);
    }
    const u32 version = loadLittleEndian<u32>(bytes.data() + kVersionOffset);
    if (version > kSaveFormatVersion)
    {
        return unexpected(SaveError::NewerVersion);
    }
    if (version == 0 || bytes.size() < kSaveHeaderBytes)
    {
        return unexpected(SaveError::Corrupt);
    }
    const u64 size = loadLittleEndian<u64>(bytes.data() + kSizeOffset);
    if (size != bytes.size() - kSaveHeaderBytes)
    {
        return unexpected(SaveError::Corrupt);
    }
    if (size > kMaxSavePayloadBytes)
    {
        return unexpected(SaveError::TooLarge);
    }
    const std::span<const std::byte> payload = bytes.subspan(kSaveHeaderBytes);
    if (xxhash64(payload) != loadLittleEndian<u64>(bytes.data() + kChecksumOffset))
    {
        return unexpected(SaveError::Corrupt);
    }
    return payload;
}

std::string_view textOf(std::span<const std::byte> bytes) noexcept
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::optional<std::string_view> stringMember(JsonValue object, std::string_view key, usize maxBytes)
{
    const std::optional<std::string_view> text = object.find(key).asString();
    return text && text->size() <= maxBytes ? text : std::nullopt;
}

// Reads the metadata section of @p payload into @p out and returns the World section.
Expected<std::string_view, SaveError> readMetadata(std::span<const std::byte> payload, SaveFileMetadata& out)
{
    if (payload.size() < kSectionSizeBytes)
    {
        return unexpected(SaveError::Corrupt);
    }
    const u32 size = loadLittleEndian<u32>(payload.data());
    if (size > kMaxMetadataBytes || size > payload.size() - kSectionSizeBytes)
    {
        return unexpected(SaveError::Corrupt);
    }
    JsonReadOptions options;
    options.maxDepth = 4;
    options.maxBytes = kMaxMetadataBytes;
    const Expected<JsonDocument, JsonError> doc =
        JsonDocument::parse(textOf(payload.subspan(kSectionSizeBytes, size)), options);
    if (!doc || !doc->root().isObject())
    {
        return unexpected(SaveError::Corrupt);
    }
    const JsonValue root = doc->root();
    const std::optional<std::string_view> title = stringMember(root, "title", kMaxSaveTitleBytes);
    const std::optional<std::string_view> custom = stringMember(root, "custom", kMaxSaveCustomBytes);
    const std::optional<std::string_view> engine =
        stringMember(root, "engineVersion", kMaxEngineVersionBytes);
    const std::optional<f64> playSeconds = root.find("playSeconds").asF64();
    const std::optional<u64> gameVersion = root.find("gameVersion").asU64();
    const std::optional<i64> savedAt = root.find("savedAt").asI64();
    if (!title || !custom || !engine || !playSeconds || !(*playSeconds >= 0.0) || !gameVersion ||
        *gameVersion > std::numeric_limits<u32>::max() || !savedAt)
    {
        return unexpected(SaveError::Corrupt);
    }
    out.title = *title;
    out.custom = *custom;
    out.engineVersion = *engine;
    out.playSeconds = *playSeconds;
    out.gameVersion = static_cast<u32>(*gameVersion);
    out.savedAt = *savedAt;
    return textOf(payload.subspan(kSectionSizeBytes + size));
}

} // namespace

Expected<void, SaveError> encodeSaveFile(const SaveMetadata& metadata, i64 savedAt, std::string_view world,
                                         std::vector<std::byte>& out)
{
    ILL_PROFILE_SCOPE();
    if (!isValidSaveMetadata(metadata))
    {
        return unexpected(SaveError::InvalidValue);
    }
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("title", doc.makeString(metadata.title))
        .add("playSeconds", doc.makeF64(metadata.playSeconds))
        .add("gameVersion", doc.makeU64(metadata.gameVersion))
        .add("custom", doc.makeString(metadata.custom))
        .add("savedAt", doc.makeI64(savedAt))
        .add("engineVersion", doc.makeString(engineVersion().toString()));
    std::string section;
    if (!doc.write(section, JsonWriteOptions{.pretty = false}) || section.size() > kMaxMetadataBytes)
    {
        return unexpected(SaveError::InvalidValue);
    }
    const u64 payloadSize = u64{kSectionSizeBytes} + section.size() + world.size();
    if (payloadSize > kMaxSavePayloadBytes)
    {
        return unexpected(SaveError::TooLarge);
    }
    out.resize(kSaveHeaderBytes + static_cast<usize>(payloadSize));
    std::copy(kMagic.begin(), kMagic.end(), out.begin());
    storeLittleEndian(out.data() + kVersionOffset, kSaveFormatVersion);
    storeLittleEndian(out.data() + kSizeOffset, payloadSize);
    std::byte* payload = out.data() + kSaveHeaderBytes;
    storeLittleEndian(payload, static_cast<u32>(section.size()));
    std::transform(section.begin(), section.end(), payload + kSectionSizeBytes,
                   [](char c) { return static_cast<std::byte>(c); });
    std::transform(world.begin(), world.end(), payload + kSectionSizeBytes + section.size(),
                   [](char c) { return static_cast<std::byte>(c); });
    storeLittleEndian(out.data() + kChecksumOffset,
                      xxhash64(std::span<const std::byte>(payload, static_cast<usize>(payloadSize))));
    return {};
}

Expected<DecodedSave, SaveError> decodeSaveFile(std::span<const std::byte> bytes)
{
    ILL_PROFILE_SCOPE();
    const Expected<std::span<const std::byte>, SaveError> payload = checkedPayload(bytes);
    if (!payload)
    {
        return unexpected(payload.error());
    }
    DecodedSave decoded;
    const Expected<std::string_view, SaveError> world = readMetadata(*payload, decoded.metadata);
    if (!world)
    {
        return unexpected(world.error());
    }
    decoded.world = *world;
    return decoded;
}

Expected<SaveFileMetadata, SaveError> decodeSaveMetadata(std::span<const std::byte> bytes)
{
    ILL_PROFILE_SCOPE();
    const Expected<std::span<const std::byte>, SaveError> payload = checkedPayload(bytes);
    if (!payload)
    {
        return unexpected(payload.error());
    }
    SaveFileMetadata metadata;
    const Expected<std::string_view, SaveError> world = readMetadata(*payload, metadata);
    if (!world)
    {
        return unexpected(world.error());
    }
    return metadata;
}

} // namespace illusion

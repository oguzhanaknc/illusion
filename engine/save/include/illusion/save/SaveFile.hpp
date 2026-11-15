#pragma once

/// @file
/// The slot file of a saved game (docs/adr/0058-saved-games.md): a fixed little-endian header (magic
/// `ILSV`, container version, payload size, xxhash64 of the payload) and a payload of two sections, the
/// metadata as JSON and the World section as scene JSON.
///
/// @code
/// const illusion::SaveMetadata metadata{.title = "Chapter 2", .playSeconds = 3600.0, .gameVersion = 3};
/// std::vector<std::byte> file;
/// if (illusion::encodeSaveFile(metadata, savedAt, capture->world, file))
/// {
///     static_cast<void>(files.writeFileAtomic("user:/saves/slot1.save", file));
/// }
/// const auto decoded = illusion::decodeSaveFile(file); // decoded->world views file
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// Version of the slot file container written by encodeSaveFile().
inline constexpr u32 kSaveFormatVersion = 1;

/// Size of the slot file header in bytes.
inline constexpr usize kSaveHeaderBytes = 24;

/// Largest payload of a slot file (metadata and World sections), in bytes (64 MiB).
inline constexpr u64 kMaxSavePayloadBytes = 64ull * 1024 * 1024;

/// Longest slot name, in bytes.
inline constexpr usize kMaxSaveSlotNameLength = 32;

/// Longest SaveMetadata::title, in bytes.
inline constexpr usize kMaxSaveTitleBytes = 128;

/// Longest SaveMetadata::custom, in bytes.
inline constexpr usize kMaxSaveCustomBytes = 1024;

/// Why a save operation failed.
enum class SaveError : u8
{
    Unavailable,  ///< Saving cannot be done here: no save system or no writable directory.
    InvalidName,  ///< The slot name is not valid (isValidSaveSlotName()).
    InvalidValue, ///< The metadata is not valid (isValidSaveMetadata()).
    NotFound,     ///< No slot has that name.
    Corrupt,      ///< The file has a wrong magic, size or checksum, or unreadable metadata.
    NewerVersion, ///< The file was written by a newer container version than this program reads.
    TooLarge,     ///< The payload is larger than kMaxSavePayloadBytes.
    IoError,      ///< Reading or writing the file failed.
    InvalidScene, ///< The World section could not be captured or applied.
};

/// Stable name of @p error, e.g. "NewerVersion".
[[nodiscard]] constexpr std::string_view saveErrorName(SaveError error) noexcept
{
    switch (error)
    {
    case SaveError::Unavailable:
        return "Unavailable";
    case SaveError::InvalidName:
        return "InvalidName";
    case SaveError::InvalidValue:
        return "InvalidValue";
    case SaveError::NotFound:
        return "NotFound";
    case SaveError::Corrupt:
        return "Corrupt";
    case SaveError::NewerVersion:
        return "NewerVersion";
    case SaveError::TooLarge:
        return "TooLarge";
    case SaveError::IoError:
        return "IoError";
    case SaveError::InvalidScene:
        return "InvalidScene";
    }
    return "IoError";
}

/// True if @p name can name a slot: 1 to kMaxSaveSlotNameLength ASCII letters, digits, '_' and '-'.
[[nodiscard]] constexpr bool isValidSaveSlotName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMaxSaveSlotNameLength)
    {
        return false;
    }
    for (const char c : name)
    {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_' && c != '-')
        {
            return false;
        }
    }
    return true;
}

namespace detail
{

/// True if @p text is valid UTF-8 (no overlong forms, surrogates or code points above U+10FFFF).
[[nodiscard]] constexpr bool isUtf8(std::string_view text) noexcept
{
    usize i = 0;
    while (i < text.size())
    {
        const auto lead = static_cast<u8>(text[i]);
        if (lead < 0x80)
        {
            ++i;
            continue;
        }
        usize length = 0;
        u32 smallest = 0;
        u32 code = 0;
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            smallest = 0x80;
            code = lead & 0x1Fu;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            smallest = 0x800;
            code = lead & 0x0Fu;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            smallest = 0x10000;
            code = lead & 0x07u;
        }
        else
        {
            return false;
        }
        if (text.size() - i < length)
        {
            return false;
        }
        for (usize k = 1; k < length; ++k)
        {
            const auto next = static_cast<u8>(text[i + k]);
            if ((next & 0xC0) != 0x80)
            {
                return false;
            }
            code = (code << 6) | (next & 0x3Fu);
        }
        if (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
        {
            return false;
        }
        i += length;
    }
    return true;
}

} // namespace detail

/// What a game writes about a saved game; the strings are views that must outlive the call they are passed
/// to.
struct SaveMetadata
{
    std::string_view title{};  ///< Text for the player, UTF-8, at most kMaxSaveTitleBytes.
    f64 playSeconds = 0.0;     ///< Time played, in seconds (finite, not negative).
    u32 gameVersion = 0;       ///< A number the game chooses and gets back, for its own fix-ups after a load.
    std::string_view custom{}; ///< UTF-8 the game defines, at most kMaxSaveCustomBytes.
};

/// True if @p metadata can be saved: UTF-8 strings within their limits and a finite, non-negative
/// playSeconds.
[[nodiscard]] constexpr bool isValidSaveMetadata(const SaveMetadata& metadata) noexcept
{
    return metadata.title.size() <= kMaxSaveTitleBytes && metadata.custom.size() <= kMaxSaveCustomBytes &&
           detail::isUtf8(metadata.title) && detail::isUtf8(metadata.custom) && metadata.playSeconds >= 0.0 &&
           metadata.playSeconds <= std::numeric_limits<f64>::max();
}

/// The metadata section of a slot file as decoded: the game's SaveMetadata and what the host wrote.
struct SaveFileMetadata
{
    std::string title;         ///< SaveMetadata::title.
    f64 playSeconds = 0.0;     ///< SaveMetadata::playSeconds.
    u32 gameVersion = 0;       ///< SaveMetadata::gameVersion.
    std::string custom;        ///< SaveMetadata::custom.
    i64 savedAt = 0;           ///< When the slot was written, in seconds since the Unix epoch (UTC).
    std::string engineVersion; ///< Version of the engine that wrote it, e.g. "0.9.0".

    /// The game's part, viewing this object's strings.
    [[nodiscard]] SaveMetadata metadata() const noexcept
    {
        return SaveMetadata{title, playSeconds, gameVersion, custom};
    }
};

/// A slot file read by decodeSaveFile().
struct DecodedSave
{
    SaveFileMetadata metadata; ///< The metadata section.
    std::string_view world;    ///< The World section (scene JSON), a view of the decoded bytes.
};

/// Writes the slot file of @p metadata, the host's @p savedAt (seconds since the Unix epoch, UTC), the
/// engine's version and @p world (scene JSON from captureSave()) into @p out (replacing its contents). Fails
/// with InvalidValue (see isValidSaveMetadata()) or TooLarge (payload above kMaxSavePayloadBytes).
Expected<void, SaveError> encodeSaveFile(const SaveMetadata& metadata, i64 savedAt, std::string_view world,
                                         std::vector<std::byte>& out);

/// Reads the slot file @p bytes, which may be anything: Corrupt for a wrong magic, size or checksum or an
/// unreadable metadata section, NewerVersion for a container version above kSaveFormatVersion, TooLarge for a
/// payload above kMaxSavePayloadBytes. The World section is checked only by applySave(). A flipped byte of
/// the version field reads as NewerVersion (or Corrupt if it becomes 0): the header of a newer container
/// cannot be checked.
[[nodiscard]] Expected<DecodedSave, SaveError> decodeSaveFile(std::span<const std::byte> bytes);

/// decodeSaveFile() without the World section: checks the whole file the same way and returns its metadata.
[[nodiscard]] Expected<SaveFileMetadata, SaveError> decodeSaveMetadata(std::span<const std::byte> bytes);

} // namespace illusion

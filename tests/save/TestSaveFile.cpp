#include <illusion/core/Hash.hpp>
#include <illusion/core/Version.hpp>
#include <illusion/save/SaveFile.hpp>

#include <doctest/doctest.h>

#include <limits>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kWorld = R"({"format": "illusion.scene", "version": 2, "entities": []})";

std::vector<std::byte> smallFile()
{
    const SaveMetadata metadata{
        .title = "Chapter 2", .playSeconds = 61.5, .gameVersion = 7, .custom = "level=2"};
    std::vector<std::byte> file;
    REQUIRE(encodeSaveFile(metadata, 1'700'000'000, kWorld, file).hasValue());
    return file;
}

// A slot file with a valid header around @p payload.
std::vector<std::byte> fileWithPayload(std::string_view payload)
{
    std::vector<std::byte> file(kSaveHeaderBytes + payload.size());
    const auto put = [&](usize offset, u64 value, usize bytes) {
        for (usize i = 0; i < bytes; ++i)
        {
            file[offset + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
        }
    };
    file[0] = std::byte{'I'};
    file[1] = std::byte{'L'};
    file[2] = std::byte{'S'};
    file[3] = std::byte{'V'};
    put(4, kSaveFormatVersion, 4);
    put(8, payload.size(), 8);
    for (usize i = 0; i < payload.size(); ++i)
    {
        file[kSaveHeaderBytes + i] = static_cast<std::byte>(payload[i]);
    }
    put(16, xxhash64(std::span<const std::byte>(file).subspan(kSaveHeaderBytes)), 8);
    return file;
}

// A payload whose metadata section is @p metadata.
std::string payloadWith(std::string_view metadata, std::string_view world = kWorld)
{
    std::string payload(4, '\0');
    for (usize i = 0; i < 4; ++i)
    {
        payload[i] = static_cast<char>((metadata.size() >> (8 * i)) & 0xFF);
    }
    return payload.append(metadata).append(world);
}

} // namespace

TEST_CASE("slot names are 1 to 32 ASCII letters, digits, underscores and hyphens")
{
    CHECK(isValidSaveSlotName("slot1"));
    CHECK(isValidSaveSlotName("Auto_save-2"));
    CHECK(isValidSaveSlotName(std::string(32, 'a')));
    CHECK_FALSE(isValidSaveSlotName(""));
    CHECK_FALSE(isValidSaveSlotName(std::string(33, 'a')));
    for (const char* bad : {"a.b", "a b", "a/b", "a\\b", "..", "slot:1", "\xC3\xA7"})
    {
        CHECK_MESSAGE(!isValidSaveSlotName(bad), bad);
    }
}

TEST_CASE("every save error has a stable name")
{
    CHECK(saveErrorName(SaveError::Unavailable) == "Unavailable");
    CHECK(saveErrorName(SaveError::InvalidName) == "InvalidName");
    CHECK(saveErrorName(SaveError::InvalidValue) == "InvalidValue");
    CHECK(saveErrorName(SaveError::NotFound) == "NotFound");
    CHECK(saveErrorName(SaveError::Corrupt) == "Corrupt");
    CHECK(saveErrorName(SaveError::NewerVersion) == "NewerVersion");
    CHECK(saveErrorName(SaveError::TooLarge) == "TooLarge");
    CHECK(saveErrorName(SaveError::IoError) == "IoError");
    CHECK(saveErrorName(SaveError::InvalidScene) == "InvalidScene");
}

TEST_CASE("metadata must be UTF-8 within its limits with a finite play time")
{
    CHECK(isValidSaveMetadata({}));
    CHECK(isValidSaveMetadata({.title = std::string(kMaxSaveTitleBytes, 't')}));
    CHECK(isValidSaveMetadata({.title = "\xC3\xA7\xE2\x82\xAC\xF0\x9F\x98\x80", .custom = "{\"level\": 2}"}));
    CHECK_FALSE(isValidSaveMetadata({.title = std::string(kMaxSaveTitleBytes + 1, 't')}));
    CHECK(isValidSaveMetadata({.custom = std::string(kMaxSaveCustomBytes, 'c')}));
    CHECK_FALSE(isValidSaveMetadata({.custom = std::string(kMaxSaveCustomBytes + 1, 'c')}));
    for (const char* bad : {"\xC3", "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xFF", "a\x80"})
    {
        CHECK_FALSE(isValidSaveMetadata({.title = bad}));
        CHECK_FALSE(isValidSaveMetadata({.custom = bad}));
    }
    CHECK_FALSE(isValidSaveMetadata({.playSeconds = -1.0}));
    CHECK_FALSE(isValidSaveMetadata({.playSeconds = std::numeric_limits<f64>::infinity()}));
    CHECK_FALSE(isValidSaveMetadata({.playSeconds = std::numeric_limits<f64>::quiet_NaN()}));

    std::vector<std::byte> file;
    CHECK(encodeSaveFile({.title = "\xFF"}, 0, kWorld, file).error() == SaveError::InvalidValue);
    CHECK(file.empty());
}

TEST_CASE("a slot file round-trips its metadata and World section")
{
    const std::vector<std::byte> file = smallFile();
    CHECK(file[0] == std::byte{'I'});
    CHECK(file[3] == std::byte{'V'});
    CHECK(file[4] == std::byte{1}); // the container version, little-endian
    CHECK(file[5] == std::byte{0});

    const Expected<DecodedSave, SaveError> decoded = decodeSaveFile(file);
    REQUIRE(decoded.hasValue());
    CHECK(decoded->world == kWorld);
    const SaveFileMetadata& metadata = decoded->metadata;
    CHECK(metadata.title == "Chapter 2");
    CHECK(metadata.playSeconds == 61.5);
    CHECK(metadata.gameVersion == 7);
    CHECK(metadata.custom == "level=2");
    CHECK(metadata.savedAt == 1'700'000'000);
    CHECK(metadata.engineVersion == engineVersion().toString());
    CHECK(metadata.metadata().title == "Chapter 2");

    const Expected<SaveFileMetadata, SaveError> only = decodeSaveMetadata(file);
    REQUIRE(only.hasValue());
    CHECK(only->custom == "level=2");
}

TEST_CASE("decoding the metadata does not read the World section")
{
    const std::vector<std::byte> file =
        fileWithPayload(payloadWith(R"({"title": "", "playSeconds": 0, "gameVersion": 0, "custom": "",
                                        "savedAt": -5, "engineVersion": "9.9.9"})",
                                    "not a scene"));
    const Expected<SaveFileMetadata, SaveError> metadata = decodeSaveMetadata(file);
    REQUIRE(metadata.hasValue());
    CHECK(metadata->savedAt == -5);
    const Expected<DecodedSave, SaveError> decoded = decodeSaveFile(file);
    REQUIRE(decoded.hasValue());
    CHECK(decoded->world == "not a scene"); // applySave() reports it
}

TEST_CASE("every flipped byte of a slot file is Corrupt, or NewerVersion in the version field")
{
    const std::vector<std::byte> file = smallFile();
    for (const u8 mask : {u8{0x01}, u8{0x80}, u8{0xFF}})
    {
        for (usize i = 0; i < file.size(); ++i)
        {
            std::vector<std::byte> flipped = file;
            flipped[i] ^= std::byte{mask};
            const Expected<DecodedSave, SaveError> decoded = decodeSaveFile(flipped);
            CAPTURE(i);
            CAPTURE(mask);
            REQUIRE_FALSE(decoded.hasValue());
            const bool versionField = i >= 4 && i < 8;
            // 1 ^ 0x01 is version 0, which no container has.
            const SaveError expected =
                versionField && !(i == 4 && mask == 0x01) ? SaveError::NewerVersion : SaveError::Corrupt;
            CHECK(decoded.error() == expected);
            CHECK(decodeSaveMetadata(flipped).error() == expected);
        }
    }
}

TEST_CASE("every truncation of a slot file is Corrupt")
{
    const std::vector<std::byte> file = smallFile();
    for (usize length = 0; length < file.size(); ++length)
    {
        CAPTURE(length);
        const std::span<const std::byte> truncated(file.data(), length);
        CHECK(decodeSaveFile(truncated).error() == SaveError::Corrupt);
        CHECK(decodeSaveMetadata(truncated).error() == SaveError::Corrupt);
    }
    std::vector<std::byte> longer = file;
    longer.push_back(std::byte{0});
    CHECK(decodeSaveFile(longer).error() == SaveError::Corrupt);
}

TEST_CASE("a newer container version is NewerVersion and is not read further")
{
    std::vector<std::byte> file = smallFile();
    file[4] = std::byte{kSaveFormatVersion + 1};
    CHECK(decodeSaveFile(file).error() == SaveError::NewerVersion);
    file.resize(8); // a newer header may have another size
    CHECK(decodeSaveFile(file).error() == SaveError::NewerVersion);
}

TEST_CASE("a header announcing a huge payload is Corrupt")
{
    std::vector<std::byte> file = smallFile();
    file[15] = std::byte{0x7F}; // payload size of about 2^63 bytes
    CHECK(decodeSaveFile(file).error() == SaveError::Corrupt);
}

TEST_CASE("metadata sections with a valid checksum but wrong content are Corrupt")
{
    constexpr std::string_view kGood = R"({"title": "t", "playSeconds": 1.5, "gameVersion": 1, "custom": "",
                                           "savedAt": 0, "engineVersion": "1.0.0"})";
    REQUIRE(decodeSaveFile(fileWithPayload(payloadWith(kGood))).hasValue());
    const std::string bad[] = {
        "",
        "[]",
        "{",
        R"({"title": "t"})",
        R"({"title": 1, "playSeconds": 0, "gameVersion": 1, "custom": "", "savedAt": 0, "engineVersion": ""})",
        R"({"title": "", "playSeconds": -1, "gameVersion": 1, "custom": "", "savedAt": 0, "engineVersion": ""})",
        R"({"title": "", "playSeconds": 0, "gameVersion": 4294967296, "custom": "", "savedAt": 0,
            "engineVersion": ""})",
        R"({"title": "", "playSeconds": 0, "gameVersion": 1, "custom": "", "savedAt": 0.5, "engineVersion": ""})",
        std::string(R"({"title": ")") + std::string(kMaxSaveTitleBytes + 1, 't') +
            R"(", "playSeconds": 0, "gameVersion": 1, "custom": "", "savedAt": 0, "engineVersion": ""})",
        "{\"title\": \"\xFF\", \"playSeconds\": 0, \"gameVersion\": 1, \"custom\": \"\", \"savedAt\": 0, "
        "\"engineVersion\": \"\"}",
    };
    for (const std::string& metadata : bad)
    {
        CAPTURE(metadata);
        CHECK(decodeSaveFile(fileWithPayload(payloadWith(metadata))).error() == SaveError::Corrupt);
    }
    // A metadata size beyond the payload, and a payload too short to hold one.
    std::string payload = payloadWith(kGood);
    payload[0] = static_cast<char>(0xFF);
    payload[1] = static_cast<char>(0xFF);
    CHECK(decodeSaveFile(fileWithPayload(payload)).error() == SaveError::Corrupt);
    CHECK(decodeSaveFile(fileWithPayload("abc")).error() == SaveError::Corrupt);
}

TEST_CASE("encoding fails with TooLarge above the payload limit")
{
    const std::string world(static_cast<usize>(kMaxSavePayloadBytes), ' ');
    std::vector<std::byte> file;
    CHECK(encodeSaveFile({}, 0, world, file).error() == SaveError::TooLarge);
    CHECK(file.empty());
}

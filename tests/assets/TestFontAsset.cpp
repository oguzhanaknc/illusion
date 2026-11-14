#include "AssetTestProject.hpp"
#include "FontTestSupport.hpp"

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/platform/Archive.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr std::string_view kFont = "game:/fonts/roboto.ttf";

Expected<void, ImportSettingsError> mergeFont(ImportSettings& settings, std::string_view patch)
{
    Expected<JsonDocument, JsonError> document = JsonDocument::parse(patch);
    REQUIRE(document.hasValue());
    return mergeImportSettings(settings, document->root(), ImportSettingsKind::Font);
}

std::string rejectedAt(std::string_view patch)
{
    ImportSettings settings;
    const Expected<void, ImportSettingsError> result = mergeFont(settings, patch);
    CHECK(settings == ImportSettings{});
    return result ? std::string("accepted") : result.error().pointer;
}

void writeFont(test::AssetProject& project)
{
    const std::vector<std::byte> bytes = test::readTestFontFile();
    REQUIRE_FALSE(bytes.empty());
    REQUIRE(project.files.writeFile(kFont, bytes) == FileError::None);
}

void writeSidecar(test::AssetProject& project, std::string_view text)
{
    REQUIRE(project.files.writeText(importSettingsPath(kFont), text) == FileError::None);
}

constexpr std::string_view kAsciiAt32 =
    R"({"format": "illusion.import", "version": 2, "font": {"pixelSize": 32, "ranges": [[32, 126]]}})";

} // namespace

TEST_CASE("fonts have import settings with the bake defaults")
{
    CHECK(importSettingsKindOf("game:/a.ttf") == ImportSettingsKind::Font);
    CHECK(importSettingsKindOf("game:/A.OTF") == ImportSettingsKind::Font);
    CHECK(importSettingsKindOf("game:/a.ttf.import.json") == ImportSettingsKind::None);

    const FontImportSettings defaults;
    const FontBakeSettings bake = fontBakeSettings(defaults);
    CHECK(bake.pixelSize == FontBakeSettings{}.pixelSize);
    CHECK(bake.spread == FontBakeSettings{}.spread);
    REQUIRE(bake.ranges.size() == std::size(kDefaultFontRanges));
    CHECK(bake.ranges[0] == kDefaultFontRanges[0]);
    CHECK(writeImportSettings(ImportSettings{}, ImportSettingsKind::Font).empty());
    CHECK(
        importSettingsValue(ImportSettings{}, ImportSettingsKind::Font) ==
        R"({"font":{"pixelSize":48,"spread":6,"ranges":[[32,126],[160,383],[8208,8231],[8364,8364],[8482,8482]]}})");
}

TEST_CASE("font sidecars round trip pixel size, spread and ranges and reset with null")
{
    ImportSettings settings;
    REQUIRE(
        mergeFont(settings, R"({"font": {"pixelSize": 64, "spread": 8, "ranges": [[48, 57], [65, 90]]}})"));
    CHECK(settings.font.pixelSize == 64);
    CHECK(settings.font.spread == 8);
    REQUIRE(settings.font.ranges.size() == 2);
    CHECK(settings.font.ranges[1] == CodePointRange{65, 90});
    CHECK_FALSE(settings.isDefault());

    const std::string text = writeImportSettings(settings, ImportSettingsKind::Font);
    const Expected<ImportSettings, ImportSettingsError> parsed =
        parseImportSettings(text, ImportSettingsKind::Font);
    REQUIRE(parsed.hasValue());
    CHECK(*parsed == settings);

    REQUIRE(mergeFont(settings, R"({"font": {"ranges": null, "spread": null}})"));
    CHECK(settings.font.pixelSize == 64);
    CHECK(settings.font.spread == 6);
    CHECK(settings.font.ranges == FontImportSettings{}.ranges);
    REQUIRE(mergeFont(settings, R"({"font": null})"));
    CHECK(settings.isDefault());
}

TEST_CASE("invalid font settings are rejected with a pointer and change nothing")
{
    CHECK(rejectedAt(R"({"font": {"pixelSize": 8}})") == "/font/pixelSize");
    CHECK(rejectedAt(R"({"font": {"pixelSize": 200}})") == "/font/pixelSize");
    CHECK(rejectedAt(R"({"font": {"pixelSize": 32.5}})") == "/font/pixelSize");
    CHECK(rejectedAt(R"({"font": {"spread": 1}})") == "/font/spread");
    CHECK(rejectedAt(R"({"font": {"spread": "wide"}})") == "/font/spread");
    CHECK(rejectedAt(R"({"font": {"ranges": []}})") == "/font/ranges");
    CHECK(rejectedAt(R"({"font": {"ranges": {"first": 1}}})") == "/font/ranges");
    CHECK(rejectedAt(R"({"font": {"ranges": [[32, 126], [90, 65]]}})") == "/font/ranges/1");
    CHECK(rejectedAt(R"({"font": {"ranges": [[32]]}})") == "/font/ranges/0");
    CHECK(rejectedAt(R"({"font": {"ranges": [[32, 1114112]]}})") == "/font/ranges/0");
    CHECK(rejectedAt(R"({"font": {"ranges": [[-1, 5]]}})") == "/font/ranges/0");
    CHECK(rejectedAt(R"({"font": {"size": 32}})") == "/font/size");
    CHECK(rejectedAt(R"({"font": 3})") == "/font");
    CHECK(rejectedAt(R"({"texture": {"maxSize": 8}})") == "/texture");
    std::string many = R"({"font": {"ranges": [)";
    for (int i = 0; i < 65; ++i)
    {
        many += i == 0 ? "[32, 33]" : ", [32, 33]";
    }
    many += "]}}";
    CHECK(rejectedAt(many) == "/font/ranges");
}

TEST_CASE("the asset database bakes fonts once, caches them and bakes again when the sidecar changes")
{
    test::AssetProject project;
    writeFont(project);
    writeSidecar(project, kAsciiAt32);
    AssetDatabase database(project.files, "cache");
    std::optional<Font> font = database.loadFont(kFont);
    REQUIRE(font.has_value());
    CHECK(font->pixelSize() == 32);
    CHECK(font->find(U'A') != nullptr);
    CHECK(font->find(U'é') == nullptr);
    CHECK(database.statistics().imports == 1);

    const std::optional<Font> cached = database.loadFont(kFont);
    REQUIRE(cached.has_value());
    CHECK(database.statistics().cacheHits == 1);
    CHECK(cached->glyphs().size() == font->glyphs().size());
    CHECK(cached->atlasSize() == font->atlasSize());

    writeSidecar(project,
                 R"({"format": "illusion.import", "version": 2, "font": {"pixelSize": 24, "spread": 4,
                             "ranges": [[48, 57]]}})");
    font = database.loadFont(kFont);
    REQUIRE(font.has_value());
    CHECK(database.statistics().imports == 2); // the sidecar is a source of the cache entry
    CHECK(font->pixelSize() == 24);
    CHECK(font->spread() == 4);
    CHECK(font->glyphs().size() == 10);

    std::string error;
    writeSidecar(project,
                 R"({"format": "illusion.import", "version": 2, "font": {"ranges": [[57344, 57400]]}})");
    CHECK_FALSE(database.loadFont(kFont, &error).has_value());
    CHECK(error.find("none of the requested code points") != std::string::npos);
    writeSidecar(project, R"({"format": "illusion.import", "version": 2, "font": {"spread": 99}})");
    CHECK_FALSE(database.loadFont(kFont, &error).has_value());
    CHECK(error.find("roboto.ttf.import.json/font/spread") != std::string::npos);
    REQUIRE(project.files.writeText("game:/fonts/broken.otf", "not a font") == FileError::None);
    CHECK_FALSE(database.loadFont("game:/fonts/broken.otf", &error).has_value());
    CHECK(error.find("not a TrueType or OpenType font") != std::string::npos);
    CHECK_FALSE(database.loadFont("game:/fonts/missing.ttf", &error).has_value());
}

TEST_CASE("cooked fonts load through the streamer from an archive without their sources")
{
    test::AssetProject project;
    writeFont(project);
    writeSidecar(project, kAsciiAt32);
    const CookResult result = cookAssets(project.files, "game:/", "cache");
    CHECK(result.fonts == 1);
    for (const std::string& error : result.errors)
    {
        CHECK(error.find("roboto") == std::string::npos);
    }

    const std::filesystem::path archive = project.directory.path / "cooked.illpak";
    REQUIRE(packDirectory(project.directory.path / "cache", archive) == FileError::None);
    VirtualFileSystem shipped;
    REQUIRE(shipped.mountArchive("cache", archive) == FileError::None);
    JobSystem jobs(JobSystemDesc{2, 256});
    AssetStreamer streamer(shipped, jobs, AssetStreamerDesc{.cacheMount = "cache", .verifySources = false});
    const AssetHandle<Font> font = streamer.loadFont(kFont);
    CHECK(font == streamer.loadFont("game:/fonts/../fonts/roboto.ttf"));
    streamer.waitIdle();
    REQUIRE(font.ready());
    CHECK(font.value().pixelSize() == 32);
    CHECK(font.value().find(U'z') != nullptr);
    CHECK(streamer.statistics().imports == 0);
    CHECK(streamer.statistics().cacheHits == 1);
}

TEST_CASE("the asset streamer bakes fonts, uses the cache and bakes again after a sidecar change")
{
    test::AssetProject project;
    writeFont(project);
    writeSidecar(project, kAsciiAt32);
    JobSystem jobs(JobSystemDesc{2, 256});
    {
        AssetStreamer streamer(project.files, jobs);
        const AssetHandle<Font> font = streamer.loadFont(kFont);
        streamer.waitIdle();
        REQUIRE(font.ready());
        CHECK(font.value().pixelSize() == 32);
        CHECK(streamer.statistics().imports == 1);
    }
    {
        AssetStreamer streamer(project.files, jobs);
        const AssetHandle<Font> font = streamer.loadFont(kFont);
        streamer.waitIdle();
        REQUIRE(font.ready());
        CHECK(streamer.statistics().cacheHits == 1);
        CHECK(streamer.statistics().imports == 0);

        writeSidecar(project, R"({"format": "illusion.import", "version": 2, "font": {"pixelSize": 40,
                                 "ranges": [[65, 70]]}})");
        CHECK(streamer.reload(kFont));
        const AssetHandle<Font> changed = streamer.loadFont(kFont);
        CHECK_FALSE(changed == font);
        streamer.waitIdle();
        REQUIRE(changed.ready());
        CHECK(changed.value().pixelSize() == 40);
        CHECK(changed.value().glyphs().size() == 6);
        CHECK(streamer.statistics().imports == 1);

        CHECK(streamer.reload(kFont, AssetReloadMode::Reimport));
        const AssetHandle<Font> again = streamer.loadFont(kFont);
        streamer.waitIdle();
        REQUIRE(again.ready());
        CHECK(streamer.statistics().imports == 2);

        const AssetHandle<Font> missing = streamer.loadFont("game:/fonts/missing.ttf");
        const AssetHandle<Font> invalid = streamer.loadFont("no mount here");
        streamer.waitIdle();
        CHECK(missing.state() == AssetState::Failed);
        CHECK_FALSE(missing.error().empty());
        CHECK(invalid.state() == AssetState::Failed);
    }
}

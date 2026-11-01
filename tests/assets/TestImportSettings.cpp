#include "AssetTestProject.hpp"

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/platform/Archive.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <string>

using namespace illusion;

namespace
{

constexpr std::string_view kTexture = "game:/textures/checker.png";
constexpr std::string_view kModel = "game:/scene/two_nodes.gltf";

// Merges the JSON text @p patch into @p settings.
Expected<void, ImportSettingsError> merge(ImportSettings& settings, std::string_view patch,
                                          ImportSettingsKind kind = ImportSettingsKind::Texture)
{
    Expected<JsonDocument, JsonError> document = JsonDocument::parse(patch);
    REQUIRE(document.hasValue());
    return mergeImportSettings(settings, document->root(), kind);
}

void writeSidecar(test::AssetProject& project, std::string_view asset, std::string_view text)
{
    REQUIRE(project.files.writeText(importSettingsPath(asset), text) == FileError::None);
}

// A solid image of @p width x @p height written as PNG to @p path.
void writeImage(test::AssetProject& project, std::string_view path, u32 width, u32 height)
{
    Image image = makeSolidImage(width, height, Vec4(0.5f, 0.25f, 1.0f, 1.0f), ImageFormat::RGBA8);
    REQUIRE(project.files.writeFile(path, encodePng(image)) == FileError::None);
}

JobSystemDesc smallPool()
{
    return JobSystemDesc{2, 256};
}

} // namespace

TEST_CASE("import settings paths name the sidecar next to an asset")
{
    CHECK(importSettingsPath("assets:/a.png") == "assets:/a.png.import.json");
    CHECK(isImportSettingsPath("assets:/a.png.import.json"));
    CHECK(isImportSettingsPath("assets:/A.PNG.IMPORT.JSON"));
    CHECK_FALSE(isImportSettingsPath(".import.json"));
    CHECK_FALSE(isImportSettingsPath("assets:/a.json"));
    CHECK(importSettingsOwner("assets:/m/ship.glb.import.json") == "assets:/m/ship.glb");

    CHECK(importSettingsKindOf("assets:/a.PNG") == ImportSettingsKind::Texture);
    CHECK(importSettingsKindOf("assets:/sky.hdr") == ImportSettingsKind::Texture);
    CHECK(importSettingsKindOf("assets:/ship.glb") == ImportSettingsKind::Model);
    CHECK(importSettingsKindOf("assets:/ship.gltf") == ImportSettingsKind::Model);
    CHECK(importSettingsKindOf("assets:/red.material") == ImportSettingsKind::None);
    CHECK(importSettingsKindOf("assets:/a.png.import.json") == ImportSettingsKind::None);
}

TEST_CASE("sidecars hold only the settings that differ from their defaults")
{
    ImportSettings settings;
    CHECK(settings.isDefault());
    CHECK(writeImportSettings(settings, ImportSettingsKind::Texture).empty());
    CHECK(writeImportSettings(settings, ImportSettingsKind::Model).empty());

    settings.texture.maxSize = 512;
    const std::string text = writeImportSettings(settings, ImportSettingsKind::Texture);
    CHECK(text.find("\"maxSize\": 512") != std::string::npos);
    CHECK(text.find("colorSpace") == std::string::npos);
    CHECK(text.find("generateMips") == std::string::npos);
    CHECK(writeImportSettings(settings, ImportSettingsKind::Model).empty()); // texture settings of a model

    const Expected<ImportSettings, ImportSettingsError> parsed =
        parseImportSettings(text, ImportSettingsKind::Texture);
    REQUIRE(parsed.hasValue());
    CHECK(*parsed == settings);

    ImportSettings model;
    model.model.scale = 0.01f;
    const Expected<ImportSettings, ImportSettingsError> again =
        parseImportSettings(writeImportSettings(model, ImportSettingsKind::Model), ImportSettingsKind::Model);
    REQUIRE(again.hasValue());
    CHECK(again->model.scale == doctest::Approx(0.01f));

    CHECK(importSettingsValue(settings, ImportSettingsKind::Texture) ==
          R"({"texture":{"colorSpace":"auto","generateMips":true,"maxSize":512}})");
    CHECK(importSettingsValue(settings, ImportSettingsKind::None) == "{}");
}

TEST_CASE("merging import settings follows JSON merge patch rules")
{
    ImportSettings settings;
    REQUIRE(merge(settings, R"({"texture": {"colorSpace": "linear", "maxSize": 256}})"));
    CHECK(settings.texture.colorSpace == ImportColorSpace::Linear);
    CHECK(settings.texture.maxSize == 256);

    REQUIRE(merge(settings, R"({"texture": {"generateMips": false}})")); // other fields stay
    CHECK(settings.texture.colorSpace == ImportColorSpace::Linear);
    CHECK_FALSE(settings.texture.generateMips);

    REQUIRE(merge(settings, R"({"texture": {"maxSize": null}})"));
    CHECK(settings.texture.maxSize == 0);
    CHECK_FALSE(settings.texture.generateMips);

    REQUIRE(merge(settings, R"({"texture": null})"));
    CHECK(settings.isDefault());

    REQUIRE(merge(settings, R"({"model": {"scale": 2}})", ImportSettingsKind::Model));
    CHECK(settings.model.scale == 2.0f);
    REQUIRE(merge(settings, "null", ImportSettingsKind::Model));
    CHECK(settings.isDefault());
    REQUIRE(merge(settings, "{}"));
    CHECK(settings.isDefault());
}

TEST_CASE("invalid import settings are rejected with a pointer and change nothing")
{
    struct Case
    {
        std::string_view patch;
        ImportSettingsKind kind;
        std::string_view pointer;
    };
    const Case cases[] = {
        {R"([1, 2])", ImportSettingsKind::Texture, ""},
        {R"({"texture": 3})", ImportSettingsKind::Texture, "/texture"},
        {R"({"textures": {}})", ImportSettingsKind::Texture, "/textures"},
        {R"({"model": {"scale": 2}})", ImportSettingsKind::Texture, "/model"},
        {R"({"texture": {"maxSize": 4}})", ImportSettingsKind::Model, "/texture"},
        {R"({"texture": {"colorSpace": "rgb"}})", ImportSettingsKind::Texture, "/texture/colorSpace"},
        {R"({"texture": {"colorSpace": 1}})", ImportSettingsKind::Texture, "/texture/colorSpace"},
        {R"({"texture": {"generateMips": "yes"}})", ImportSettingsKind::Texture, "/texture/generateMips"},
        {R"({"texture": {"maxSize": -1}})", ImportSettingsKind::Texture, "/texture/maxSize"},
        {R"({"texture": {"maxSize": 2.5}})", ImportSettingsKind::Texture, "/texture/maxSize"},
        {R"({"texture": {"maxSize": 16385}})", ImportSettingsKind::Texture, "/texture/maxSize"},
        {R"({"texture": {"srgb": true}})", ImportSettingsKind::Texture, "/texture/srgb"},
        {R"({"model": {"scale": 0}})", ImportSettingsKind::Model, "/model/scale"},
        {R"({"model": {"scale": 1001}})", ImportSettingsKind::Model, "/model/scale"},
        {R"({"model": {"scale": "big"}})", ImportSettingsKind::Model, "/model/scale"},
        {R"({"model": {"up": "z"}})", ImportSettingsKind::Model, "/model/up"},
        {R"({})", ImportSettingsKind::None, ""},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.patch);
        ImportSettings settings;
        settings.texture.maxSize = 64;
        const ImportSettings before = settings;
        // A valid member first: a later error must not leave it applied.
        Expected<void, ImportSettingsError> merged = merge(settings, c.patch, c.kind);
        REQUIRE_FALSE(merged.hasValue());
        CHECK(merged.error().pointer == c.pointer);
        CHECK_FALSE(merged.error().message.empty());
        CHECK(settings == before);
    }
    ImportSettings settings;
    CHECK_FALSE(merge(settings, R"({"texture": {"maxSize": 128, "colorSpace": "bad"}})"));
    CHECK(settings.texture.maxSize == 0);
}

TEST_CASE("parsing a sidecar checks its format and version")
{
    const auto parse = [](std::string_view text) {
        return parseImportSettings(text, ImportSettingsKind::Texture);
    };
    CHECK(parse(R"({"format": "illusion.import", "version": 1})").hasValue());
    CHECK(
        parse(R"({"format": "illusion.import", "version": 1, "texture": {"maxSize": 8}})")->texture.maxSize ==
        8);
    CHECK(parse("{ not json").error().message.find("invalid JSON") != std::string::npos);
    CHECK(parse(R"({"version": 1})").error().pointer == "/format");
    CHECK(parse(R"({"format": "illusion.material", "version": 1})").error().pointer == "/format");
    CHECK(parse(R"({"format": "illusion.import", "version": 2})").error().pointer == "/version");
    CHECK(parse(R"({"format": "illusion.import", "version": 0})").error().pointer == "/version");
    CHECK(parse(R"({"format": "illusion.import", "version": 1, "model": {}})").error().pointer == "/model");
    CHECK_FALSE(
        parseImportSettings(R"({"format": "illusion.import", "version": 1})", ImportSettingsKind::None));
}

TEST_CASE("texture settings override what the user of an image asks for")
{
    TextureImportSettings settings;
    CHECK(effectiveImageImport({true, true}, settings).srgb);
    CHECK_FALSE(effectiveImageImport({false, true}, settings).srgb);
    settings.colorSpace = ImportColorSpace::Linear;
    CHECK_FALSE(effectiveImageImport({true, true}, settings).srgb);
    settings.colorSpace = ImportColorSpace::Srgb;
    CHECK(effectiveImageImport({false, true}, settings).srgb);
    // Mips are built only if both the user and the settings want them.
    CHECK(effectiveImageImport({true, true}, settings).generateMips);
    CHECK_FALSE(effectiveImageImport({true, false}, settings).generateMips);
    settings.generateMips = false;
    CHECK_FALSE(effectiveImageImport({true, true}, settings).generateMips);
}

TEST_CASE("limitImageSize halves an image until it fits and keeps its mip chain")
{
    Image image = makeSolidImage(8, 4, Vec4(1.0f, 0.5f, 0.25f, 1.0f), ImageFormat::RGBA8);
    limitImageSize(image, 0);
    CHECK(image.width() == 8);
    limitImageSize(image, 8);
    CHECK(image.width() == 8);
    limitImageSize(image, 3);
    CHECK(image.width() == 2);
    CHECK(image.height() == 1);
    CHECK(image.mips.size() == 1); // it had no chain, so it gets none
    CHECK(image.data.size() == image.mips[0].size);
    CHECK(readPixel(image, 1, 0).r == doctest::Approx(1.0f));

    Image chained = makeSolidImage(16, 16, Vec4(0.0f, 1.0f, 0.0f, 1.0f), ImageFormat::RGBA8Srgb);
    generateMips(chained);
    limitImageSize(chained, 4);
    REQUIRE(chained.mips.size() == 3); // 4, 2, 1
    CHECK(chained.width() == 4);
    CHECK(chained.mips[0].offset == 0);
    CHECK(chained.mips[2].width == 1);
    CHECK(chained.data.size() == chained.mips[2].offset + chained.mips[2].size);
    CHECK(readPixel(chained, 3, 3).g == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("scaleModel scales positions, bounds and node translations")
{
    test::AssetProject project;
    AssetDatabase database(project.files, "");
    std::optional<ModelData> model = database.loadModel(kModel);
    REQUIRE(model.has_value());
    const ModelData original = *model;
    scaleModel(*model, 2.0f);
    REQUIRE(model->meshes.size() == original.meshes.size());
    CHECK(model->meshes[0].vertices[0].position.x ==
          doctest::Approx(original.meshes[0].vertices[0].position.x * 2.0f));
    CHECK(model->meshes[0].bounds.max.x == doctest::Approx(original.meshes[0].bounds.max.x * 2.0f));
    const Aabb before = original.bounds();
    const Aabb after = model->bounds();
    CHECK(after.min.x == doctest::Approx(before.min.x * 2.0f));
    CHECK(after.max.y == doctest::Approx(before.max.y * 2.0f));
    CHECK(after.max.z == doctest::Approx(before.max.z * 2.0f));
}

TEST_CASE("the asset database honours texture sidecars and re-imports when they change")
{
    test::AssetProject project;
    writeImage(project, "game:/textures/big.png", 64, 32);
    AssetDatabase database(project.files, "cache");
    std::optional<Image> image = database.loadImage("game:/textures/big.png");
    REQUIRE(image.has_value());
    CHECK(image->format == ImageFormat::RGBA8Srgb);
    CHECK(image->width() == 64);
    CHECK(image->mips.size() == 7);

    writeSidecar(project, "game:/textures/big.png",
                 R"({"format": "illusion.import", "version": 1,
                     "texture": {"colorSpace": "linear", "maxSize": 16}})");
    image = database.loadImage("game:/textures/big.png");
    REQUIRE(image.has_value());
    CHECK(database.statistics().imports == 2); // the sidecar is a source of the cache entry
    CHECK(image->format == ImageFormat::RGBA8);
    CHECK(image->width() == 16);
    CHECK(image->height() == 8);
    CHECK(image->mips.size() == 5);

    REQUIRE(database.loadImage("game:/textures/big.png").has_value());
    CHECK(database.statistics().cacheHits == 1);

    writeSidecar(project, "game:/textures/big.png",
                 R"({"format": "illusion.import", "version": 1, "texture": {"generateMips": false}})");
    image = database.loadImage("game:/textures/big.png");
    REQUIRE(image.has_value());
    CHECK(database.statistics().imports == 3);
    CHECK(image->format == ImageFormat::RGBA8Srgb);
    CHECK(image->mips.size() == 1);

    REQUIRE(project.files.remove(importSettingsPath("game:/textures/big.png")) == FileError::None);
    image = database.loadImage("game:/textures/big.png");
    REQUIRE(image.has_value());
    CHECK(database.statistics().imports == 4); // removing the sidecar makes the entry stale too
    CHECK(image->mips.size() == 7);
}

TEST_CASE("an invalid sidecar fails the import and names the sidecar")
{
    test::AssetProject project;
    writeSidecar(project, kTexture,
                 R"({"format": "illusion.import", "version": 1, "texture": {"maxSize": "x"}})");
    AssetDatabase database(project.files, "cache");
    std::string error;
    CHECK_FALSE(database.loadImage(kTexture, {}, &error).has_value());
    CHECK(error.find("checker.png.import.json/texture/maxSize") != std::string::npos);

    writeSidecar(project, kModel, R"({"format": "illusion.import", "version": 1, "texture": {}})");
    CHECK_FALSE(database.loadModel(kModel, &error).has_value());
    CHECK(error.find("two_nodes.gltf.import.json/texture") != std::string::npos);
}

TEST_CASE("model sidecars scale the imported model")
{
    test::AssetProject project;
    AssetDatabase database(project.files, "cache");
    const std::optional<ModelData> plain = database.loadModel(kModel);
    REQUIRE(plain.has_value());
    writeSidecar(project, kModel, R"({"format": "illusion.import", "version": 1, "model": {"scale": 0.5}})");
    const std::optional<ModelData> scaled = database.loadModel(kModel);
    REQUIRE(scaled.has_value());
    CHECK(database.statistics().imports == 2);
    CHECK(scaled->bounds().max.x == doctest::Approx(plain->bounds().max.x * 0.5f));
}

TEST_CASE("the asset streamer honours sidecars and reimports on request")
{
    test::AssetProject project;
    writeImage(project, "game:/textures/big.png", 32, 32);
    writeSidecar(project, "game:/textures/big.png",
                 R"({"format": "illusion.import", "version": 1, "texture": {"maxSize": 8}})");
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    AssetHandle<Image> image = streamer.loadImage("game:/textures/big.png");
    streamer.waitIdle();
    REQUIRE(image.ready());
    CHECK(image.value().width() == 8);
    CHECK(streamer.statistics().imports == 1);

    // Unchanged: a reload uses the cache.
    REQUIRE(streamer.reload("game:/textures/big.png"));
    image = streamer.loadImage("game:/textures/big.png");
    streamer.waitIdle();
    REQUIRE(image.ready());
    CHECK(streamer.statistics().imports == 1);
    CHECK(streamer.statistics().cacheHits == 1);

    // Reimport ignores the cache although nothing changed.
    REQUIRE(streamer.reload("game:/textures/big.png", AssetReloadMode::Reimport));
    image = streamer.loadImage("game:/textures/big.png");
    streamer.waitIdle();
    REQUIRE(image.ready());
    CHECK(streamer.statistics().imports == 2);
    CHECK(image.value().width() == 8);

    // A changed sidecar makes the cached entry stale.
    writeSidecar(project, "game:/textures/big.png",
                 R"({"format": "illusion.import", "version": 1, "texture": {"maxSize": 4}})");
    REQUIRE(streamer.reload("game:/textures/big.png"));
    image = streamer.loadImage("game:/textures/big.png");
    streamer.waitIdle();
    REQUIRE(image.ready());
    CHECK(streamer.statistics().imports == 3);
    CHECK(image.value().width() == 4);

    // A broken sidecar fails the load with its error.
    writeSidecar(project, "game:/textures/big.png", "{");
    REQUIRE(streamer.reload("game:/textures/big.png"));
    image = streamer.loadImage("game:/textures/big.png");
    streamer.waitIdle();
    REQUIRE(image.state() == AssetState::Failed);
    CHECK(image.error().find("big.png.import.json") != std::string::npos);

    // Models reimport the same way.
    AssetHandle<ModelData> model = streamer.loadModel(kModel);
    streamer.waitIdle();
    REQUIRE(model.ready());
    const u32 before = streamer.statistics().imports;
    REQUIRE(streamer.reload(kModel, AssetReloadMode::Reimport));
    model = streamer.loadModel(kModel);
    streamer.waitIdle();
    REQUIRE(model.ready());
    CHECK(streamer.statistics().imports == before + 1);
}

TEST_CASE("cookAssets honours sidecars and reports orphans, so a shipped game needs no sidecars")
{
    test::AssetProject project;
    writeImage(project, "game:/textures/big.png", 64, 64);
    writeSidecar(project, "game:/textures/big.png",
                 R"({"format": "illusion.import", "version": 1, "texture": {"maxSize": 32}})");
    REQUIRE(project.files.writeText("game:/gone.png.import.json",
                                    R"({"format": "illusion.import", "version": 1})") == FileError::None);
    REQUIRE(project.files.writeText("game:/notes.txt.import.json", "{}") == FileError::None);
    const CookResult result = cookAssets(project.files, "game:/", "cache");
    CHECK(result.images == 2);
    const auto reported = [&](std::string_view text) {
        return std::any_of(result.errors.begin(), result.errors.end(),
                           [&](const std::string& error) { return error.find(text) != std::string::npos; });
    };
    CHECK(reported("gone.png.import.json: the asset game:/gone.png does not exist"));
    CHECK(reported("notes.txt.import.json: .txt files have no import settings"));

    const std::filesystem::path archive = project.directory.path / "cooked.illpak";
    REQUIRE(packDirectory(project.directory.path / "cache", archive) == FileError::None);
    VirtualFileSystem shipped;
    REQUIRE(shipped.mountArchive("cache", archive) == FileError::None);
    AssetDatabase database(shipped, "cache", false);
    const std::optional<Image> image = database.loadImage("game:/textures/big.png");
    REQUIRE(image.has_value());
    CHECK(image->width() == 32);
    CHECK(database.statistics().imports == 0);
}

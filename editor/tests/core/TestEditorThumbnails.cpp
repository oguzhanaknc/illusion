#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>
#include <illusion/editor/core/ThumbnailCache.hpp>
#include <illusion/editor/core/ThumbnailHost.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

// The thumbnail cache and asset.thumbnail with the GPU-free stand-in renderer (test::FakeThumbnails); the
// real renderer is tested in gpu/TestThumbnailGpu.cpp.

namespace
{

EditorContextOptions pollingOptions()
{
    EditorContextOptions options;
    options.assetWatching = FileWatchBackend::Polling;
    options.assetPollInterval = 0.0;
    return options;
}

std::filesystem::path assetFile(const EditorFixture& fixture, std::string_view relative)
{
    return fixture.directory.path / "project" / "assets" / utf8Path(relative);
}

void writeText(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::optional<Image> readPng(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return decodeImage(std::as_bytes(std::span<const char>(bytes)), true);
}

std::filesystem::path fileOf(const JsonDocument& result)
{
    return utf8Path(result.root().find("file").asString().value_or(""));
}

} // namespace

TEST_CASE("thumbnail kinds come from the extension in any case")
{
    CHECK(thumbnailKindOf("assets:/m/ship.GLB") == ThumbnailKind::Model);
    CHECK(thumbnailKindOf("assets:/m/ship.gltf") == ThumbnailKind::Model);
    CHECK(thumbnailKindOf("assets:/red.material") == ThumbnailKind::Material);
    CHECK(thumbnailKindOf("assets:/t/a.png") == ThumbnailKind::Texture);
    CHECK(thumbnailKindOf("assets:/t/a.JPEG") == ThumbnailKind::Texture);
    CHECK(thumbnailKindOf("assets:/t/a.tga") == ThumbnailKind::Texture);
    CHECK(thumbnailKindOf("assets:/t/a.bmp") == ThumbnailKind::Texture);
    CHECK(thumbnailKindOf("assets:/sky.hdr") == ThumbnailKind::Environment);
    CHECK_FALSE(thumbnailKindOf("assets:/scenes/main.scene").has_value());
    CHECK_FALSE(thumbnailKindOf("assets:/music.ogg").has_value());
    CHECK_FALSE(thumbnailKindOf("assets:/noextension").has_value());
    CHECK(thumbnailKindName(ThumbnailKind::Environment) == "Environment");
    CHECK(thumbnailPath(0xabcull) == "thumbnails:/0000000000000abc.png");
}

TEST_CASE("thumbnail keys change with the asset, its dependencies and its import settings, and only then")
{
    EditorFixture fixture(1, pollingOptions());
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    const std::string material = "assets:/materials/red.material";
    const u64 first = thumbnailKey(editor.assets(), material);
    CHECK(first != 0);
    CHECK(thumbnailKey(editor.assets(), material) == first);
    CHECK(thumbnailKey(editor.assets(), "assets:/textures/red.png") != first);
    CHECK(thumbnailKey(editor.assets(), "assets:/missing.png") == 0);
    CHECK(thumbnailKey(editor.assets(), "assets:/materials") == 0);

    // A changed texture changes the key of the material that uses it.
    writeText(assetFile(fixture, "textures/red.png"), "another not-png");
    editor.endFrame(0.016);
    const u64 second = thumbnailKey(editor.assets(), material);
    CHECK(second != first);

    // So does an import settings sidecar of that texture.
    ok(editor, "asset.set_import_settings",
       R"({"path":"assets:/textures/red.png","value":{"texture":{"maxSize":64}}})");
    editor.endFrame(0.016);
    CHECK(thumbnailKey(editor.assets(), material) != second);

    // Unrelated files do not.
    const u64 third = thumbnailKey(editor.assets(), material);
    writeText(assetFile(fixture, "notes.txt"), "hello");
    editor.endFrame(0.016);
    CHECK(thumbnailKey(editor.assets(), material) == third);
}

TEST_CASE("asset.thumbnail renders once, then answers from the cache until asked to refresh")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    const JsonDocument first = ok(editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material"})");
    REQUIRE(fixture.thumbnails.requests.size() == 1);
    CHECK(fixture.thumbnails.requests[0].path == "assets:/materials/red.material");
    CHECK(fixture.thumbnails.requests[0].kind == ThumbnailKind::Material);
    CHECK(first.root().find("kind").asString() == "Material");
    CHECK(first.root().find("cached").asBool() == false);
    CHECK(first.root().find("width").asU64() == kThumbnailSize);
    const std::string thumbnail(first.root().find("thumbnail").asString().value_or(""));
    CHECK(thumbnail == thumbnailPath(thumbnailKey(editor.assets(), "assets:/materials/red.material")));
    const std::filesystem::path file = fileOf(first);
    CHECK(file.parent_path().filename() == "thumbnails");
    const std::optional<Image> png = readPng(file);
    REQUIRE(png.has_value());
    CHECK(png->width() == kThumbnailSize);
    CHECK(png->height() == kThumbnailSize);
    CHECK(editor.thumbnails().statistics().stored == 1);

    const JsonDocument again = ok(editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material"})");
    CHECK(again.root().find("cached").asBool() == true);
    CHECK(again.root().find("thumbnail").asString() == thumbnail);
    CHECK(fixture.thumbnails.requests.size() == 1);

    // The cache survives a restart of the editor.
    fixture.open(1);
    fixture.thumbnails.requests.clear();
    CHECK(ok(*fixture.editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material"})")
              .root()
              .find("cached")
              .asBool() == true);
    CHECK(fixture.thumbnails.requests.empty());

    const JsonDocument refreshed =
        ok(*fixture.editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material","refresh":true})");
    CHECK(refreshed.root().find("cached").asBool() == false);
    CHECK(fixture.thumbnails.requests.size() == 1);
}

TEST_CASE("a changed asset removes its thumbnail and those of the assets that use it")
{
    EditorFixture fixture(1, pollingOptions());
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    writeText(assetFile(fixture, "textures/blue.png"), "blue");
    editor.endFrame(0.016);
    const std::filesystem::path material =
        fileOf(ok(editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material"})"));
    const std::filesystem::path texture =
        fileOf(ok(editor, "asset.thumbnail", R"({"path":"assets:/textures/red.png"})"));
    const std::filesystem::path other =
        fileOf(ok(editor, "asset.thumbnail", R"({"path":"assets:/textures/blue.png"})"));
    REQUIRE(std::filesystem::exists(material));
    REQUIRE(std::filesystem::exists(texture));
    REQUIRE(std::filesystem::exists(other));

    writeText(assetFile(fixture, "textures/red.png"), "changed outside the editor");
    editor.endFrame(0.016);
    CHECK_FALSE(std::filesystem::exists(material));
    CHECK_FALSE(std::filesystem::exists(texture));
    CHECK(std::filesystem::exists(other));
    CHECK(editor.thumbnails().statistics().invalidated == 2);

    const JsonDocument redone = ok(editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material"})");
    CHECK(redone.root().find("cached").asBool() == false);
    CHECK(fileOf(redone) != material);

    // Removing a file removes its thumbnail too.
    std::filesystem::remove(assetFile(fixture, "textures/blue.png"));
    editor.endFrame(0.016);
    CHECK_FALSE(std::filesystem::exists(other));
}

TEST_CASE("asset.thumbnail renders again when the asset changes while it is made")
{
    EditorFixture fixture(1, pollingOptions());
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    fixture.thumbnails.delay = 1;
    const JsonDocument params = test::parseJson(R"({"path":"assets:/textures/red.png"})");
    Expected<std::unique_ptr<RunningCommand>, CommandError> started =
        editor.startJson("asset.thumbnail", params.root());
    REQUIRE(started.hasValue());
    Expected<std::optional<std::string>, CommandError> step = started.value()->update();
    REQUIRE(step.hasValue());
    CHECK_FALSE(step->has_value());
    writeText(assetFile(fixture, "textures/red.png"), "edited while rendering");
    editor.endFrame(0.016);
    std::optional<std::string> result;
    for (int i = 0; i < 8 && !result; ++i)
    {
        step = started.value()->update();
        REQUIRE(step.hasValue());
        result = *step;
    }
    REQUIRE(result.has_value());
    CHECK(fixture.thumbnails.requests.size() == 2);
    const JsonDocument done = test::parseJson(*result);
    CHECK(done.root().find("thumbnail").asString() ==
          thumbnailPath(thumbnailKey(editor.assets(), "assets:/textures/red.png")));
}

TEST_CASE("cancelling asset.thumbnail cancels its request")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    fixture.thumbnails.delay = 5;
    const JsonDocument params = test::parseJson(R"({"path":"assets:/textures/red.png"})");
    Expected<std::unique_ptr<RunningCommand>, CommandError> started =
        editor.startJson("asset.thumbnail", params.root());
    REQUIRE(started.hasValue());
    REQUIRE(started.value()->update().hasValue());
    started.value().reset();
    CHECK(fixture.thumbnails.cancelled == 1);
    CHECK(editor.thumbnails().statistics().stored == 0);
}

TEST_CASE("asset.thumbnail reports bad paths, unsupported assets, a missing renderer and failed loads")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    CHECK(fails(editor, "asset.thumbnail", R"({"path":"settings:/x.png"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "asset.thumbnail", R"({"path":"assets:/"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "asset.thumbnail", R"({"path":"assets:/scenes/main.scene"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "asset.thumbnail", R"({"path":"assets:/missing.glb"})").kind == ErrorKind::NotFound);
    ok(editor, "asset.create_folder", R"({"path":"assets:/folder.png"})");
    CHECK(fails(editor, "asset.thumbnail", R"({"path":"assets:/folder.png"})").kind ==
          ErrorKind::ValidationFailed);

    fixture.thumbnails.failure =
        CommandError::make(ErrorKind::IoError, "'assets:/textures/red.png' could not be loaded");
    const CommandError failed = fails(editor, "asset.thumbnail", R"({"path":"assets:/textures/red.png"})");
    CHECK(failed.kind == ErrorKind::IoError);
    CHECK(editor.thumbnails().statistics().stored == 0);
    fixture.thumbnails.failure.reset();

    // Without a renderer only cached thumbnails are available.
    ok(editor, "asset.thumbnail", R"({"path":"assets:/textures/red.png"})");
    editor.setThumbnailHost(nullptr);
    CHECK(ok(editor, "asset.thumbnail", R"({"path":"assets:/textures/red.png"})")
              .root()
              .find("cached")
              .asBool() == true);
    CHECK(fails(editor, "asset.thumbnail", R"({"path":"assets:/materials/red.material"})").kind ==
          ErrorKind::Unsupported);
}

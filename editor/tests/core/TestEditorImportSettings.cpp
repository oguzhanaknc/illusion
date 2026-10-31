#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>
#include <illusion/platform/FileWatcher.hpp>

#include <fstream>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

constexpr std::string_view kSidecar = "assets:/textures/red.png.import.json";

std::string read(EditorContext& editor, std::string_view path)
{
    std::string text;
    REQUIRE(editor.files().readText(path, text) == FileError::None);
    return text;
}

// A fixture whose catalog polls the file system on every endFrame().
EditorFixture pollingFixture()
{
    EditorContextOptions options;
    options.assetWatching = FileWatchBackend::Polling;
    options.assetPollInterval = 0.0;
    return EditorFixture(1, options);
}

} // namespace

TEST_CASE("asset.set_import_settings writes a sidecar only for non-default settings")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;

    JsonDocument set = ok(editor, "asset.set_import_settings",
                          R"({"path":"assets:/textures/red.png","value":{"texture":{"maxSize":256}}})");
    CHECK(set.root().find("changed").asBool() == true);
    CHECK(set.root().find("sidecar").asString() == kSidecar);
    CHECK(set.root().pointer("/value/texture/maxSize").asU64() == 256);
    CHECK(set.root().pointer("/value/texture/colorSpace").asString() == "auto");
    const std::string text = read(editor, kSidecar);
    CHECK(text.find("\"maxSize\": 256") != std::string::npos);
    CHECK(text.find("colorSpace") == std::string::npos);

    // Merging keeps the other settings; the same patch again changes nothing.
    ok(editor, "asset.set_import_settings",
       R"({"path":"assets:/textures/red.png","value":{"texture":{"colorSpace":"linear"}}})");
    set = ok(editor, "asset.set_import_settings",
             R"({"path":"assets:/textures/red.png","value":{"texture":{"colorSpace":"linear"}}})");
    CHECK(set.root().find("changed").asBool() == false);
    CHECK(set.root().pointer("/value/texture/maxSize").asU64() == 256);
    const JsonDocument info = ok(editor, "asset.info", R"({"path":"assets:/textures/red.png"})");
    CHECK(info.root().pointer("/importSettings/texture/colorSpace").asString() == "linear");
    CHECK(info.root().pointer("/importSettings/texture/maxSize").asU64() == 256);
    CHECK_FALSE(ok(editor, "asset.info", R"({"path":"assets:/materials/red.material"})")
                    .root()
                    .find("importSettings")
                    .isObject());

    // The sidecar is part of its asset, not an asset of its own.
    const JsonDocument listed = ok(editor, "asset.list", R"({"path":"assets:/textures"})");
    REQUIRE(listed.root().find("assets").size() == 1);
    CHECK(listed.root().pointer("/assets/0/path").asString() == "assets:/textures/red.png");
    CHECK(editor.assets().find(kSidecar) == nullptr);
    CHECK(editor.assets().find("assets:/textures/red.png")->importSettingsHash != 0);

    // Back to the defaults: the sidecar goes away.
    set =
        ok(editor, "asset.set_import_settings",
           R"({"path":"assets:/textures/red.png","value":{"texture":{"maxSize":null,"colorSpace":"auto"}}})");
    CHECK(set.root().find("changed").asBool() == true);
    CHECK(set.root().find("sidecar").asString() == "");
    CHECK_FALSE(editor.files().exists(kSidecar));
    CHECK(editor.assets().find("assets:/textures/red.png")->importSettingsHash == 0);

    // Each change is one undo step.
    ok(editor, "history.undo");
    CHECK(read(editor, kSidecar).find("\"colorSpace\": \"linear\"") != std::string::npos);
    ok(editor, "history.undo");
    ok(editor, "history.undo");
    CHECK_FALSE(editor.files().exists(kSidecar));
    ok(editor, "history.redo");
    CHECK(read(editor, kSidecar) == text);

    // null resets everything.
    ok(editor, "asset.set_import_settings", R"({"path":"assets:/textures/red.png","value":null})");
    CHECK_FALSE(editor.files().exists(kSidecar));

    // Models have a scale.
    ok(editor, "project.file_write", R"({"path":"assets:/models/ship.glb","text":"not a model"})");
    set = ok(editor, "asset.set_import_settings",
             R"({"path":"assets:/models/ship.glb","value":{"model":{"scale":0.01}}})");
    CHECK(set.root().pointer("/value/model/scale").asF64().value_or(0.0) == doctest::Approx(0.01));
    CHECK(editor.files().exists("assets:/models/ship.glb.import.json"));
}

TEST_CASE("asset.set_import_settings validates the asset and every setting")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    const auto pointer = [&](std::string_view params) {
        const CommandError error = fails(editor, "asset.set_import_settings", params);
        REQUIRE_FALSE(error.errors.empty());
        return error.errors.at(0).pointer;
    };
    CHECK(pointer(R"({"path":"assets:/materials/red.material","value":{}})") == "/path");
    CHECK(pointer(R"({"path":"assets:/textures","value":{}})") == "/path");
    CHECK(pointer(R"({"path":"assets:/textures/red.png.import.json","value":{}})") == "/path");
    CHECK(pointer(R"({"path":"settings:/x.png","value":{}})") == "/path");
    CHECK(pointer(R"({"path":"assets:/textures/red.png","value":{"texture":{"maxSize":"big"}}})") ==
          "/value/texture/maxSize");
    CHECK(pointer(R"({"path":"assets:/textures/red.png","value":{"model":{"scale":2}}})") == "/value/model");
    CHECK(pointer(R"({"path":"assets:/textures/red.png","value":[1]})") == "/value");
    CHECK(fails(editor, "asset.set_import_settings", R"({"path":"assets:/none.png","value":{}})").kind ==
          ErrorKind::NotFound);
    CHECK_FALSE(editor.files().exists(kSidecar));

    // A broken sidecar written by hand: only a reset is accepted.
    ok(editor, "project.file_write", R"({"path":"assets:/textures/red.png.import.json","text":"{"})");
    CHECK(fails(editor, "asset.set_import_settings",
                R"({"path":"assets:/textures/red.png","value":{"texture":{"maxSize":8}}})")
              .kind == ErrorKind::PreconditionFailed);
    const JsonDocument info = ok(editor, "asset.info", R"({"path":"assets:/textures/red.png"})");
    CHECK_FALSE(info.root().find("importSettings").isObject());
    CHECK(info.root().pointer("/_editor/warnings").size() == 1);
    ok(editor, "asset.set_import_settings", R"({"path":"assets:/textures/red.png","value":null})");
    CHECK_FALSE(editor.files().exists(kSidecar));
}

TEST_CASE("asset.move and asset.delete carry the sidecar with its asset, and undo restores both")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    ok(editor, "asset.set_import_settings",
       R"({"path":"assets:/textures/red.png","value":{"texture":{"generateMips":false}}})");
    const std::string text = read(editor, kSidecar);

    ok(editor, "asset.move", R"({"from":"assets:/textures/red.png","to":"assets:/textures/crimson.jpg"})");
    CHECK_FALSE(editor.files().exists(kSidecar));
    CHECK(read(editor, "assets:/textures/crimson.jpg.import.json") == text);
    ok(editor, "history.undo");
    CHECK(read(editor, kSidecar) == text);
    CHECK_FALSE(editor.files().exists("assets:/textures/crimson.jpg.import.json"));

    // Folders take their sidecars along anyway.
    ok(editor, "asset.move", R"({"from":"assets:/textures","to":"assets:/images"})");
    CHECK(editor.files().exists("assets:/images/red.png.import.json"));
    ok(editor, "history.undo");

    // Settings the new name could not use, a sidecar in the way, and sidecars as paths are refused.
    CHECK(
        fails(editor, "asset.move", R"({"from":"assets:/textures/red.png","to":"assets:/textures/red.txt"})")
            .kind == ErrorKind::PreconditionFailed);
    ok(editor, "project.file_write", R"({"path":"assets:/textures/blue.png.import.json","text":"{}"})");
    CHECK(
        fails(editor, "asset.move", R"({"from":"assets:/textures/red.png","to":"assets:/textures/blue.png"})")
            .kind == ErrorKind::AlreadyExists);
    CHECK(
        fails(editor, "asset.move",
              R"({"from":"assets:/textures/red.png.import.json","to":"assets:/textures/x.png.import.json"})")
            .errors.at(0)
            .pointer == "/from");
    CHECK(fails(editor, "asset.move", R"({"from":"assets:/textures/red.png","to":"assets:/x.import.json"})")
              .errors.at(0)
              .pointer == "/to");
    CHECK(fails(editor, "asset.delete", R"({"path":"assets:/textures/red.png.import.json"})")
              .errors.at(0)
              .pointer == "/path");
    CHECK(editor.files().exists("assets:/textures/red.png"));

    ok(editor, "asset.delete", R"({"path":"assets:/textures/red.png","force":true})");
    CHECK_FALSE(editor.files().exists("assets:/textures/red.png"));
    CHECK_FALSE(editor.files().exists(kSidecar));
    ok(editor, "history.undo");
    CHECK(read(editor, kSidecar) == text);
    CHECK(editor.files().exists("assets:/textures/red.png"));
}

TEST_CASE("asset.reimport asks for textures and models to import again in asset.changed")
{
    EditorFixture fixture = pollingFixture();
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    ok(editor, "project.file_write", R"({"path":"assets:/models/ship.glb","text":"not a model"})");
    ok(editor, "project.file_write", R"({"path":"assets:/models/sky.HDR","text":"not a picture"})");
    std::vector<std::string> payloads;
    editor.events().subscribe("asset.changed",
                              [&payloads](const EditorEvent& event) { payloads.push_back(event.payload); });
    REQUIRE(editor.assets().scanned());
    editor.endFrame(0.016);
    payloads.clear();

    const JsonDocument one = ok(editor, "asset.reimport", R"({"path":"assets:/textures/red.png"})");
    REQUIRE(one.root().find("assets").size() == 1);
    CHECK(one.root().pointer("/assets/0").asString() == "assets:/textures/red.png");
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == R"({"added":[],"modified":[],"removed":[],"renamed":[],)"
                         R"("reimported":["assets:/textures/red.png"]})");

    const JsonDocument all = ok(editor, "asset.reimport", R"({"path":"assets:/"})");
    REQUIRE(all.root().find("assets").size() == 3);
    CHECK(all.root().pointer("/assets/0").asString() == "assets:/models/ship.glb");
    CHECK(all.root().pointer("/assets/1").asString() == "assets:/models/sky.HDR");
    CHECK(all.root().pointer("/assets/2").asString() == "assets:/textures/red.png");
    ok(editor, "asset.reimport", R"({"path":"assets:/textures/red.png"})"); // posted once
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 2);
    CHECK(payloads[1].find(R"("reimported":["assets:/models/ship.glb","assets:/models/sky.HDR",)"
                           R"("assets:/textures/red.png"])") != std::string::npos);
    editor.endFrame(0.016);
    CHECK(payloads.size() == 2);

    CHECK(fails(editor, "asset.reimport", R"({"path":"assets:/materials/red.material"})")
              .errors.at(0)
              .pointer == "/path");
    CHECK(fails(editor, "asset.reimport", R"({"path":"assets:/none.png"})").kind == ErrorKind::NotFound);
    CHECK(ok(editor, "asset.reimport", R"({"path":"assets:/scenes"})").root().find("assets").size() == 0);
}

TEST_CASE("changing import settings reports the asset as modified in asset.changed")
{
    EditorFixture fixture = pollingFixture();
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;
    std::vector<std::string> payloads;
    editor.events().subscribe("asset.changed",
                              [&payloads](const EditorEvent& event) { payloads.push_back(event.payload); });
    REQUIRE(editor.assets().scanned());
    editor.endFrame(0.016);
    payloads.clear();

    ok(editor, "asset.set_import_settings",
       R"({"path":"assets:/textures/red.png","value":{"texture":{"maxSize":64}}})");
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 1);
    CHECK(
        payloads[0] ==
        R"({"added":[],"modified":["assets:/textures/red.png"],"removed":[],"renamed":[],"reimported":[]})");

    ok(editor, "history.undo"); // the sidecar is deleted again
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 2);
    CHECK(payloads[1] == payloads[0]);

    // A sidecar edited outside the editor counts too.
    const std::filesystem::path assets = fixture.directory.path / "project" / "assets";
    std::ofstream(assets / "textures" / "red.png.import.json")
        << R"({"format":"illusion.import","version":1,"texture":{"generateMips":false}})";
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 3);
    CHECK(payloads[2] == payloads[0]);
}

TEST_CASE("the catalog keeps sidecars on their assets, also before the asset exists")
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    reflectAssetStructs();
    REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
    const auto write = [&](std::string_view relative, std::string_view text) {
        const std::filesystem::path path = directory.path / utf8Path(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    };
    write("a.png", "pixels");
    write("a.png.import.json", "{}");
    write("b.png.import.json", "{}"); // no asset yet
    AssetCatalog catalog;
    const AssetCatalogChanges first = catalog.refresh(files);
    CHECK(first.added == std::vector<std::string>{"assets:/a.png"});
    CHECK(catalog.size() == 1);
    const u64 hash = catalog.find("assets:/a.png")->importSettingsHash;
    CHECK(hash != 0);

    // The asset appears: it has its sidecar at once, and is reported only as added.
    write("b.png", "more pixels");
    AssetCatalogChanges changes =
        catalog.apply(files, std::vector<FileEvent>{{FileEventKind::Created, "b.png"}});
    CHECK(changes.added == std::vector<std::string>{"assets:/b.png"});
    CHECK(changes.modified.empty());
    CHECK(catalog.find("assets:/b.png")->importSettingsHash == hash);

    // Changing or removing a sidecar modifies its asset.
    write("a.png.import.json", R"({"x":1})");
    changes = catalog.apply(files, std::vector<FileEvent>{{FileEventKind::Modified, "a.png.import.json"}});
    CHECK(changes.modified == std::vector<std::string>{"assets:/a.png"});
    CHECK(catalog.find("assets:/a.png")->importSettingsHash != hash);
    std::filesystem::remove(directory.path / "a.png.import.json");
    changes = catalog.refresh(files);
    CHECK(changes.modified == std::vector<std::string>{"assets:/a.png"});
    CHECK(catalog.find("assets:/a.png")->importSettingsHash == 0);
    CHECK(catalog.refresh(files).empty());

    // Removing a folder removes what is inside it, even past a sibling file that sorts in between
    // ("m.png" sorts between "m" and "m/x").
    write("m/x.png", "x");
    write("m/x.png.import.json", "{}");
    write("m.png", "m");
    (void)catalog.refresh(files);
    std::filesystem::remove_all(directory.path / "m");
    changes = catalog.apply(files, std::vector<FileEvent>{{FileEventKind::Removed, "m"}});
    CHECK(changes.removed == std::vector<std::string>{"assets:/m", "assets:/m/x.png"});
    CHECK(catalog.find("assets:/m.png") != nullptr);
    CHECK(catalog.find("assets:/m/x.png") == nullptr);
    write("m/x.png", "x");
    changes = catalog.refresh(files);
    CHECK(catalog.find("assets:/m/x.png")->importSettingsHash == 0); // its old sidecar was forgotten
}

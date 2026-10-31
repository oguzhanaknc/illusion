#include "EditorCoreTestSupport.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/editor/core/AssetCatalog.hpp>
#include <illusion/platform/FileWatcher.hpp>

#include <chrono>
#include <fstream>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::ok;

namespace
{

constexpr std::string_view kMaterial =
    R"({"format":"illusion.material","version":1,"material":{"baseColorFactor":[1,1,1,1]},)"
    R"("textures":{"baseColor":"assets:/textures/red.png"}})";
constexpr std::string_view kScene =
    R"({"format":"illusion.scene","version":2,"entities":[{"id":"00000000000000a1","components":)"
    R"({"test.Look":{"material":"assets:/materials/red.material","extras":["assets:/models/ship.gltf#mesh/0"]}}}]})";
constexpr std::string_view kGltf =
    R"({"asset":{"version":"2.0"},"buffers":[{"uri":"ship.bin","byteLength":4},{"uri":"data:application/)"
    R"(octet-stream;base64,AAAA"}],"images":[{"uri":"../textures/hull%20paint.png"},{"uri":"https://x/y.png"}]})";

// A project-like `assets:` mount on a temporary directory, with files written at fixed stamps.
struct CatalogFixture
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    int stamp = 0;

    CatalogFixture()
    {
        test::reflectEditorTestComponents();
        reflectAssetStructs();
        REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
    }

    std::filesystem::path real(std::string_view relative) const
    {
        return directory.path / utf8Path(relative);
    }

    // Writes @p text to `assets:/<relative>` with a new modification stamp (or @p fixedStamp).
    void write(std::string_view relative, std::string_view text, std::optional<int> fixedStamp = std::nullopt)
    {
        const std::filesystem::path path = real(relative);
        std::filesystem::create_directories(path.parent_path());
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        const int value = fixedStamp.value_or(++stamp);
        std::filesystem::last_write_time(
            path, std::filesystem::file_time_type(std::chrono::seconds(1'000'000 + value)));
    }

    void writeProject()
    {
        write("textures/red.png", "red pixels");
        write("textures/hull paint.png", "hull pixels");
        write("materials/red.material", kMaterial);
        write("scenes/main.scene", kScene);
        write("models/ship.gltf", kGltf);
        write("models/ship.bin", "abcd");
    }
};

u64 hashOf(std::string_view text)
{
    return xxhash64(std::as_bytes(std::span(text.data(), text.size())));
}

} // namespace

TEST_CASE("Asset kinds come from extensions in any case")
{
    CHECK(assetKindOf("assets:/a", true) == "Folder");
    CHECK(assetKindOf("assets:/a.SCENE", false) == "Scene");
    CHECK(assetKindOf("assets:/a.material", false) == "Material");
    CHECK(assetKindOf("assets:/a.glb", false) == "Model");
    CHECK(assetKindOf("assets:/a.Gltf", false) == "Model");
    CHECK(assetKindOf("assets:/a.jpeg", false) == "Texture");
    CHECK(assetKindOf("assets:/a.hdr", false) == "Environment");
    CHECK(assetKindOf("assets:/a.ogg", false) == "AudioClip");
    CHECK(assetKindOf("assets:/a.txt", false) == "Other");
    CHECK(assetKindOf("assets:/noextension", false) == "Other");
}

TEST_CASE("A catalog scan records kinds, sizes, hashes and dependencies")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog catalog;
    CHECK_FALSE(catalog.scanned());
    const AssetCatalogChanges changes = catalog.refresh(f.files);
    CHECK(catalog.scanned());
    CHECK(changes.added ==
          std::vector<std::string>{"assets:/materials", "assets:/materials/red.material", "assets:/models",
                                   "assets:/models/ship.bin", "assets:/models/ship.gltf", "assets:/scenes",
                                   "assets:/scenes/main.scene", "assets:/textures",
                                   "assets:/textures/hull paint.png", "assets:/textures/red.png"});
    CHECK(changes.modified.empty());
    CHECK(changes.removed.empty());
    CHECK(catalog.size() == 10);

    const AssetRecord* folder = catalog.find("assets:/models");
    REQUIRE(folder != nullptr);
    CHECK(folder->kind == "Folder");
    CHECK(folder->contentHash == 0);

    const AssetRecord* material = catalog.find("assets:/materials/red.material");
    REQUIRE(material != nullptr);
    CHECK(material->kind == "Material");
    CHECK(material->error == "");
    CHECK(material->size == kMaterial.size());
    CHECK(material->contentHash == hashOf(kMaterial));
    CHECK(material->dependencies == std::vector<std::string>{"assets:/textures/red.png"});

    // Fragments are dropped and each dependency is listed once.
    const AssetRecord* scene = catalog.find("assets:/scenes/main.scene");
    REQUIRE(scene != nullptr);
    CHECK(scene->dependencies ==
          std::vector<std::string>{"assets:/materials/red.material", "assets:/models/ship.gltf"});

    // External glTF files resolve against the model's folder, with escapes decoded; data and absolute URIs
    // are not files of the project.
    const AssetRecord* model = catalog.find("assets:/models/ship.gltf");
    REQUIRE(model != nullptr);
    CHECK(model->dependencies ==
          std::vector<std::string>{"assets:/models/ship.bin", "assets:/textures/hull paint.png"});
    CHECK(catalog.find("assets:/models/ship.bin")->dependencies.empty());
    CHECK(catalog.find("assets:/missing") == nullptr);
}

TEST_CASE("Catalog scans are deterministic and later ones read only changed files")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog first;
    AssetCatalog second;
    (void)first.refresh(f.files);
    (void)second.refresh(f.files);
    REQUIRE(first.size() == second.size());
    for (const auto& [path, record] : first.records())
    {
        const AssetRecord* other = second.find(path);
        REQUIRE(other != nullptr);
        CHECK(other->contentHash == record.contentHash);
        CHECK(other->dependencies == record.dependencies);
    }

    CHECK(first.refresh(f.files).empty());

    // New contents and a new stamp: modified.
    f.write("textures/red.png", "redder pixels");
    AssetCatalogChanges changes = first.refresh(f.files);
    CHECK(changes.modified == std::vector<std::string>{"assets:/textures/red.png"});
    CHECK(changes.added.empty());
    CHECK(first.find("assets:/textures/red.png")->contentHash == hashOf("redder pixels"));

    // A new stamp with the same contents: not a change, but the new stamp is remembered.
    f.write("textures/red.png", "redder pixels");
    CHECK(first.refresh(f.files).empty());
    CHECK(first.refresh(f.files).empty());

    // A changed dependency list is picked up.
    f.write("materials/red.material",
            R"({"format":"illusion.material","version":1,"material":{},"textures":{}})");
    changes = first.refresh(f.files);
    CHECK(changes.modified == std::vector<std::string>{"assets:/materials/red.material"});
    CHECK(first.find("assets:/materials/red.material")->dependencies.empty());
}

TEST_CASE("A file that disappears and reappears elsewhere with the same contents is a rename")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);

    std::filesystem::create_directories(f.real("art"));
    std::filesystem::rename(f.real("textures/red.png"), f.real("art/red.png"));
    std::filesystem::remove(f.real("models/ship.bin"));
    f.write("models/other.bin", "dcba"); // Same size, other contents: not a rename.
    const AssetCatalogChanges changes = catalog.refresh(f.files);
    CHECK(changes.renamed == std::vector<AssetRename>{{"assets:/textures/red.png", "assets:/art/red.png"}});
    CHECK(changes.added == std::vector<std::string>{"assets:/art", "assets:/models/other.bin"});
    CHECK(changes.removed == std::vector<std::string>{"assets:/models/ship.bin"});
    CHECK(catalog.find("assets:/art/red.png") != nullptr);
    CHECK(catalog.find("assets:/textures/red.png") == nullptr);
}

TEST_CASE("Empty files are never matched as renames")
{
    CatalogFixture f;
    f.write("a.txt", "");
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);
    std::filesystem::rename(f.real("a.txt"), f.real("b.txt"));
    const AssetCatalogChanges changes = catalog.refresh(f.files);
    CHECK(changes.renamed.empty());
    CHECK(changes.added == std::vector<std::string>{"assets:/b.txt"});
    CHECK(changes.removed == std::vector<std::string>{"assets:/a.txt"});
}

TEST_CASE("Applying watcher events reads reported files again even when their stamp did not change")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog catalog;
    (void)catalog.apply(f.files, {}); // Before the first scan, apply() scans.
    REQUIRE(catalog.scanned());
    REQUIRE(catalog.size() == 10);

    // Same size, same stamp, other contents: a full refresh cannot see it, the event can.
    f.write("models/ship.bin", "wxyz", f.stamp);
    const auto touched = std::filesystem::last_write_time(f.real("models/ship.bin"));
    CHECK(catalog.refresh(f.files).empty());
    REQUIRE(std::filesystem::last_write_time(f.real("models/ship.bin")) == touched);
    const std::vector<FileEvent> events{{FileEventKind::Modified, "models/ship.bin"}};
    AssetCatalogChanges changes = catalog.apply(f.files, events);
    CHECK(changes.modified == std::vector<std::string>{"assets:/models/ship.bin"});
    CHECK(catalog.find("assets:/models/ship.bin")->contentHash == hashOf("wxyz"));

    // Only reported paths are looked at.
    f.write("textures/red.png", "unreported");
    CHECK(
        catalog.apply(f.files, std::vector<FileEvent>{{FileEventKind::Modified, "models/ship.bin"}}).empty());

    // A reported path that no longer exists is removed, whatever the event said.
    std::filesystem::remove(f.real("models/ship.bin"));
    changes = catalog.apply(f.files, std::vector<FileEvent>{{FileEventKind::Modified, "models/ship.bin"}});
    CHECK(changes.removed == std::vector<std::string>{"assets:/models/ship.bin"});
}

TEST_CASE("Watcher events for folders remove or scan everything below them")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);

    std::filesystem::rename(f.real("models"), f.real("ships"));
    const AssetCatalogChanges changes =
        catalog.apply(f.files, std::vector<FileEvent>{{FileEventKind::Removed, "models"},
                                                      {FileEventKind::Created, "ships"}});
    CHECK(changes.added == std::vector<std::string>{"assets:/ships"});
    CHECK(changes.removed == std::vector<std::string>{"assets:/models"});
    CHECK(changes.renamed ==
          std::vector<AssetRename>{{"assets:/models/ship.bin", "assets:/ships/ship.bin"},
                                   {"assets:/models/ship.gltf", "assets:/ships/ship.gltf"}});
    CHECK(catalog.find("assets:/ships/ship.gltf") != nullptr);
    CHECK(catalog.find("assets:/models/ship.gltf") == nullptr);

    // A file replaced by a folder of the same name.
    std::filesystem::remove(f.real("textures/red.png"));
    f.write("textures/red.png/inner.txt", "inner");
    const AssetCatalogChanges replaced =
        catalog.apply(f.files, std::vector<FileEvent>{{FileEventKind::Created, "textures/red.png"}});
    CHECK(replaced.modified == std::vector<std::string>{"assets:/textures/red.png"});
    CHECK(replaced.added == std::vector<std::string>{"assets:/textures/red.png/inner.txt"});
    CHECK(catalog.find("assets:/textures/red.png")->kind == "Folder");
}

TEST_CASE("A refresh sees a file replaced by a folder of the same name")
{
    CatalogFixture f;
    f.write("thing", "file");
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);
    std::filesystem::remove(f.real("thing"));
    f.write("thing/inner.txt", "inner");
    const AssetCatalogChanges changes = catalog.refresh(f.files);
    CHECK(changes.modified == std::vector<std::string>{"assets:/thing"});
    CHECK(changes.added == std::vector<std::string>{"assets:/thing/inner.txt"});
    CHECK(catalog.find("assets:/thing")->kind == "Folder");
}

TEST_CASE("An overflow event makes the catalog scan everything")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);
    f.write("new.txt", "new");
    const AssetCatalogChanges changes =
        catalog.apply(f.files, std::vector<FileEvent>{{FileEventKind::Overflow, ""}});
    CHECK(changes.added == std::vector<std::string>{"assets:/new.txt"});
}

TEST_CASE("The catalog answers which files refer to an asset and which dependencies are missing")
{
    CatalogFixture f;
    f.writeProject();
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);

    CHECK(catalog.referrers("assets:/textures/red.png") ==
          std::vector<std::string>{"assets:/materials/red.material"});
    CHECK(catalog.referrers("assets:/textures") ==
          std::vector<std::string>{"assets:/materials/red.material", "assets:/models/ship.gltf"});
    CHECK(catalog.referrers("assets:/models/ship.gltf#mesh/0") ==
          std::vector<std::string>{"assets:/scenes/main.scene"});
    CHECK(catalog.referrers("assets:/scenes/main.scene").empty());
    CHECK(catalog.missing().empty());

    std::filesystem::remove(f.real("textures/hull paint.png"));
    std::filesystem::remove(f.real("materials/red.material"));
    (void)catalog.refresh(f.files);
    CHECK(catalog.missing() ==
          std::vector<MissingDependency>{{"assets:/models/ship.gltf", "assets:/textures/hull paint.png"},
                                         {"assets:/scenes/main.scene", "assets:/materials/red.material"}});
    // The reverse index follows the change.
    CHECK(catalog.referrers("assets:/textures/red.png").empty());

    catalog.clear();
    CHECK_FALSE(catalog.scanned());
    CHECK(catalog.size() == 0);
    CHECK(catalog.referrers("assets:/textures").empty());
}

TEST_CASE("Files that cannot be parsed are kept with their error and no dependencies")
{
    CatalogFixture f;
    f.write("broken.material", "{ not json");
    f.write("broken.gltf", "[");
    AssetCatalog catalog;
    (void)catalog.refresh(f.files);
    REQUIRE(catalog.find("assets:/broken.material") != nullptr);
    CHECK_FALSE(catalog.find("assets:/broken.material")->error.empty());
    CHECK(catalog.find("assets:/broken.material")->dependencies.empty());
    CHECK_FALSE(catalog.find("assets:/broken.gltf")->error.empty());
    CHECK(catalog.find("assets:/broken.gltf")->contentHash == hashOf("["));
}

TEST_CASE("Merged catalog changes describe both updates")
{
    AssetCatalogChanges changes;
    changes.added = {"assets:/a", "assets:/b"};
    changes.modified = {"assets:/c"};
    changes.removed = {"assets:/d"};
    AssetCatalogChanges next;
    next.added = {"assets:/d", "assets:/e"};
    next.modified = {"assets:/a", "assets:/f"};
    next.removed = {"assets:/b", "assets:/c"};
    next.renamed = {{"assets:/x", "assets:/y"}};
    changes.merge(next);
    CHECK(changes.added == std::vector<std::string>{"assets:/a", "assets:/e"});
    CHECK(changes.modified == std::vector<std::string>{"assets:/d", "assets:/f"});
    CHECK(changes.removed == std::vector<std::string>{"assets:/c"});
    CHECK(changes.renamed == std::vector<AssetRename>{{"assets:/x", "assets:/y"}});
    CHECK_FALSE(changes.empty());
    changes.merge({});
    CHECK(changes.added.size() == 2);
}

TEST_CASE("Without an assets mount the catalog is empty")
{
    VirtualFileSystem files;
    AssetCatalog catalog;
    CHECK(catalog.refresh(files).empty());
    CHECK(catalog.scanned());
    CHECK(catalog.size() == 0);
}

TEST_CASE("The editor posts asset.changed for changes made outside it")
{
    editor::EditorContextOptions options;
    options.assetWatching = FileWatchBackend::Polling;
    options.assetPollInterval = 0.0;
    EditorFixture fixture(1, options);
    EditorContext& editor = *fixture.editor;
    CHECK(editor.assetWatchBackend() == FileWatchBackend::Polling);
    std::vector<std::string> payloads;
    editor.events().subscribe("asset.changed",
                              [&payloads](const EditorEvent& event) { payloads.push_back(event.payload); });
    REQUIRE(editor.assets().scanned());
    editor.endFrame(0.016);
    CHECK(payloads.empty());

    const std::filesystem::path assets = fixture.directory.path / "project" / "assets";
    std::ofstream(assets / "notes.txt") << "hello";
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == R"({"added":["assets:/notes.txt"],"modified":[],"removed":[],"renamed":[]})");
    CHECK(editor.assets().find("assets:/notes.txt") != nullptr);

    std::filesystem::create_directories(assets / "docs");
    std::filesystem::rename(assets / "notes.txt", assets / "docs" / "notes.txt");
    editor.endFrame(0.016);
    REQUIRE(payloads.size() == 2);
    CHECK(payloads[1] ==
          R"({"added":["assets:/docs"],"modified":[],"removed":[],"renamed":[{"from":"assets:/notes.txt",)"
          R"("to":"assets:/docs/notes.txt"}]})");
}

TEST_CASE("asset.find_missing lists references to assets that do not exist")
{
    EditorFixture fixture;
    fixture.buildScene();
    fixture.addAssets();
    EditorContext& editor = *fixture.editor;

    // The open scene refers to models/ship.glb, which the fixture never creates.
    JsonDocument result = ok(editor, "asset.find_missing");
    CHECK(result.root().find("assets").size() == 1);
    CHECK(result.root().pointer("/assets/0").asString() == "assets:/models/ship.glb");
    REQUIRE(result.root().find("references").size() == 1);
    CHECK(result.root().pointer("/references/0/asset").asString() == "assets:/models/ship.glb#mesh/0");
    CHECK(result.root().pointer("/references/0/owner").asString() == "test.Look");

    ok(editor, "project.file_write", R"({"path":"assets:/models/ship.glb","text":"glb"})");
    ok(editor, "asset.delete", R"({"path":"assets:/textures/red.png","force":true})");
    result = ok(editor, "asset.find_missing");
    CHECK(result.root().find("assets").size() == 1);
    CHECK(result.root().pointer("/assets/0").asString() == "assets:/textures/red.png");
    CHECK(result.root().pointer("/references/0/source").asString() == "assets:/materials/red.material");
    CHECK(result.root().pointer("/references/0/field").asString() == "/textures/baseColor");
}

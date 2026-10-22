#include "TestSupport.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/VirtualPath.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace illusion;

namespace illusion::test
{

struct AssetHolder
{
    AssetRef model;
    std::vector<AssetRef> textures;
    std::optional<AssetRef> sound;
};

} // namespace illusion::test

namespace
{

const StructReflection& assetHolderReflection()
{
    reflectStruct<test::AssetHolder>("test.AssetHolder")
        .field("model", &test::AssetHolder::model, {.assetKind = AssetKind::Model})
        .field("textures", &test::AssetHolder::textures, {.assetKind = AssetKind::Texture})
        .field("sound", &test::AssetHolder::sound, {.assetKind = AssetKind::AudioClip});
    return *ComponentRegistry::instance().structs().reflectionOf(typeHash<test::AssetHolder>());
}

} // namespace

TEST_CASE("virtual paths are validated and normalized in core")
{
    CHECK(isValidMountName("game"));
    CHECK(isValidMountName("my-mod_2"));
    CHECK_FALSE(isValidMountName("Game"));
    CHECK_FALSE(isValidMountName(""));
    CHECK(isNormalizedVirtualPath("game:/a/b.png"));
    CHECK(isNormalizedVirtualPath("game:/"));
    CHECK_FALSE(isNormalizedVirtualPath("game:/a/./b.png"));
    CHECK_FALSE(isNormalizedVirtualPath("game:/a//b.png"));
    CHECK_FALSE(isNormalizedVirtualPath("game:/nul.txt"));
    CHECK(normalizeVirtualPath("game:/a/./x/../b.png") == "game:/a/b.png");
    CHECK_FALSE(normalizeVirtualPath("game:/../b.png").has_value());
    // VirtualFileSystem uses the same rules.
    CHECK(VirtualFileSystem::normalizePath("game:/a//b.png") == normalizeVirtualPath("game:/a//b.png"));
}

TEST_CASE("asset paths normalize their file part and fragment")
{
    CHECK(normalizeAssetPath("game:/models/./ship.glb#mesh/007") == "game:/models/ship.glb#mesh/7");
    CHECK(normalizeAssetPath("game:/ship.glb#material/0") == "game:/ship.glb#material/0");
    CHECK(normalizeAssetPath("game:/ship.glb#node/Hull Left") == "game:/ship.glb#node/Hull Left");
    CHECK(normalizeAssetPath("game:/a/../ship.glb") == "game:/ship.glb");
    CHECK_FALSE(normalizeAssetPath("game:/ship.glb#mesh/").has_value());
    CHECK_FALSE(normalizeAssetPath("game:/ship.glb#mesh/-1").has_value());
    CHECK_FALSE(normalizeAssetPath("game:/ship.glb#mesh/4294967296").has_value());
    CHECK_FALSE(normalizeAssetPath("game:/ship.glb#lod/1").has_value());
    CHECK_FALSE(normalizeAssetPath("game:/ship.glb#node/").has_value());
    CHECK_FALSE(normalizeAssetPath("not a path").has_value());

    CHECK(isNormalizedAssetPath("game:/ship.glb#mesh/7"));
    CHECK_FALSE(isNormalizedAssetPath("game:/ship.glb#mesh/07"));

    const std::optional<AssetPathParts> mesh = splitAssetPath("game:/ship.glb#mesh/3");
    REQUIRE(mesh.has_value());
    CHECK(mesh->file == "game:/ship.glb");
    CHECK(mesh->fragment == AssetFragment::Mesh);
    CHECK(mesh->index == 3);
    const std::optional<AssetPathParts> node = splitAssetPath("game:/ship.glb#node/Hull");
    REQUIRE(node.has_value());
    CHECK(node->fragment == AssetFragment::Node);
    CHECK(node->node == "Hull");
    const std::optional<AssetPathParts> file = splitAssetPath("game:/ship.glb");
    REQUIRE(file.has_value());
    CHECK(file->fragment == AssetFragment::None);
}

TEST_CASE("asset ids hash the normalized path")
{
    CHECK(AssetId::fromPath("game:/models/../a.glb#mesh/01") == AssetId::fromPath("game:/a.glb#mesh/1"));
    CHECK(AssetId::fromPath("game:/a.glb").value == fnv1a64("game:/a.glb"));
    CHECK(AssetId::fromPath("game:/a.glb#mesh/0") != AssetId::fromPath("game:/a.glb#mesh/1"));
    CHECK_FALSE(static_cast<bool>(AssetId::fromPath("game:/a.glb#bad")));
}

TEST_CASE("built-in assets are listed and their mount is reserved")
{
    const std::span<const std::string_view> builtins = builtinAssetPaths();
    CHECK(builtins.size() == 7);
    CHECK(std::is_sorted(builtins.begin(), builtins.end()));
    for (const std::string_view path : builtins)
    {
        CHECK(isNormalizedAssetPath(path));
        CHECK(isOnBuiltinMount(path));
        CHECK(isBuiltinAsset(path));
    }
    CHECK(isBuiltinAsset("builtin:/mesh/capsule"));
    CHECK_FALSE(isBuiltinAsset("builtin:/mesh/torus"));
    CHECK(isOnBuiltinMount("builtin:/mesh/torus"));
    CHECK_FALSE(isOnBuiltinMount("builtins:/mesh/cube"));

    test::TempDirectory directory;
    VirtualFileSystem files;
    CHECK(files.mount("builtin", directory.path) == FileError::InvalidPath);
}

TEST_CASE("the asset path table interns paths and maps ids back")
{
    AssetPathTable table;
    CHECK(table.size() == 0);
    const AssetId id = table.intern("game:/textures/./rock.png");
    CHECK(id == AssetId::fromPath("game:/textures/rock.png"));
    CHECK(table.path(id) == "game:/textures/rock.png");
    CHECK(table.intern("game:/textures/rock.png") == id);
    CHECK(table.size() == 1);
    CHECK_FALSE(static_cast<bool>(table.intern("bad path")));
    CHECK(table.path(AssetId{}).empty());
    CHECK(table.path(AssetId{12345}).empty());
    CHECK(assetKindName(AssetKind::AudioClip) == "AudioClip");

    // Interning a recorded, normalized path does not allocate.
    const std::string_view recorded = table.path(id);
    AssetId again;
    usize allocationCount = 0;
    {
        const test::ScopedAllocationCounter allocations;
        again = table.intern(recorded);
        allocationCount = allocations.count();
    }
    CHECK(again == id);
    CHECK(allocationCount == 0);
}

TEST_CASE("the asset path table is safe to use from several threads")
{
    AssetPathTable table;
    constexpr int kThreads = 4;
    constexpr int kPaths = 200;
    std::atomic<int> mismatches{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&table, &mismatches] {
            for (int i = 0; i < kPaths; ++i)
            {
                const std::string path = std::format("game:/assets/{}.png", i);
                const AssetId id = table.intern(path);
                mismatches += table.path(id) == path ? 0 : 1;
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    CHECK(mismatches.load() == 0);
    CHECK(table.size() == kPaths);
}

TEST_CASE("asset references are written as paths and read from paths, objects and null")
{
    const StructReflection& reflection = assetHolderReflection();
    CHECK(reflection.field("model")->type == FieldType::Asset);
    CHECK(reflection.field("model")->meta.assetKind == AssetKind::Model);
    CHECK(fieldTypeName(FieldType::Asset) == "Asset");

    test::AssetHolder holder;
    holder.model = AssetRef("game:/models/../ship.glb#mesh/0");
    holder.textures = {AssetRef("game:/a.png"), AssetRef()};
    CHECK(holder.model.path() == "game:/ship.glb#mesh/0");

    JsonMutDocument doc;
    const auto written = writeValue(doc, reflection, &holder);
    REQUIRE(written.hasValue());
    CHECK(written->dump() ==
          R"({"model":"game:/ship.glb#mesh/0","textures":["game:/a.png",""],"sound":null})");

    const auto json = JsonDocument::parse(
        R"({"model": {"path": "game:/x/../other.glb"}, "textures": [null, "game:/b.png"], "sound": "game:/s.ogg"})");
    REQUIRE(json.hasValue());
    test::AssetHolder read;
    std::vector<AssetId> referenced;
    const ValueReadResult result =
        readValue(reflection, &read, json->root(), {.referencedAssets = &referenced});
    REQUIRE(result.ok());
    CHECK(read.model.path() == "game:/other.glb");
    REQUIRE(read.textures.size() == 2);
    CHECK(read.textures[0].empty());
    CHECK(read.textures[1].path() == "game:/b.png");
    REQUIRE(read.sound.has_value());
    CHECK(read.sound->path() == "game:/s.ogg");
    CHECK(referenced.size() == 3);

    const auto invalid = JsonDocument::parse(R"({"model": "no mount", "textures": [3]})");
    REQUIRE(invalid.hasValue());
    const ValueReadResult failed = readValue(reflection, &read, invalid->root());
    REQUIRE(failed.errors.size() == 2);
    CHECK(failed.errors[0].pointer == "/model");
    CHECK(failed.errors[1].pointer == "/textures/0");
    CHECK(read.model.path() == "game:/other.glb"); // atomic: unchanged
}

TEST_CASE("asset references use the table given in the options")
{
    const StructReflection& reflection = assetHolderReflection();
    AssetPathTable table;
    const auto json = JsonDocument::parse(R"({"model": "local:/only/here.glb"})");
    REQUIRE(json.hasValue());
    test::AssetHolder holder;
    REQUIRE(readValue(reflection, &holder, json->root(), {.assetPaths = &table}).ok());
    CHECK(table.path(holder.model.id) == "local:/only/here.glb");
    CHECK(AssetPathTable::instance().path(holder.model.id).empty());

    JsonMutDocument doc;
    CHECK_FALSE(writeValue(doc, reflection, &holder).hasValue()); // unknown to the process-wide table
    const auto written = writeValue(doc, reflection, &holder, {.assetPaths = &table});
    REQUIRE(written.hasValue());
    CHECK(written->find("model").asString() == "local:/only/here.glb");
}

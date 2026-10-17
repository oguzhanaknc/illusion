#include "AssetTestProject.hpp"

#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <filesystem>

using namespace illusion;

using Project = test::AssetProject;

TEST_CASE("the asset database imports once and then loads from the cache")
{
    Project project;
    AssetDatabase database(project.files, "cache");
    std::string error;

    const std::optional<ModelData> imported = database.loadModel("game:/scene/two_nodes.gltf", &error);
    INFO(error);
    REQUIRE(imported.has_value());
    CHECK(database.statistics().imports == 1);
    CHECK(database.statistics().cacheWrites == 1);
    CHECK(project.files.exists(database.cachePath("game:/scene/two_nodes.gltf", "gltf:1")));

    const std::optional<ModelData> cached = database.loadModel("game:/scene/../scene/two_nodes.gltf", &error);
    REQUIRE(cached.has_value());
    CHECK(database.statistics().cacheHits == 1);
    CHECK(database.statistics().imports == 1);
    CHECK(serializeModel(*cached) == serializeModel(*imported));

    // A second database (a new run) uses the same cache.
    AssetDatabase later(project.files, "cache");
    REQUIRE(later.loadModel("game:/scene/two_nodes.gltf").has_value());
    CHECK(later.statistics().cacheHits == 1);
    CHECK(later.statistics().imports == 0);
}

TEST_CASE("changing any source file of a model invalidates its cache entry")
{
    Project project;
    AssetDatabase database(project.files, "cache");
    REQUIRE(database.loadModel("game:/scene/two_nodes.gltf").has_value());

    // The texture is a dependency of the model: replace it with a different (valid) image.
    std::vector<std::byte> other;
    REQUIRE(project.files.readFile("game:/textures/checker.png", other) == FileError::None);
    const std::vector<std::byte> png =
        encodePng(makeSolidImage(2, 2, Vec4(1.0f, 0.0f, 0.0f, 1.0f), ImageFormat::RGBA8));
    REQUIRE(project.files.writeFile("game:/textures/checker.png", png) == FileError::None);

    const std::optional<ModelData> reloaded = database.loadModel("game:/scene/two_nodes.gltf");
    REQUIRE(reloaded.has_value());
    CHECK(database.statistics().imports == 2);
    CHECK(database.statistics().cacheHits == 0);
    const auto pixel = reloaded->images[0].mip(0);
    CHECK(static_cast<u8>(pixel[0]) == 255);
    CHECK(static_cast<u8>(pixel[1]) == 0);

    REQUIRE(database.loadModel("game:/scene/two_nodes.gltf").has_value());
    CHECK(database.statistics().cacheHits == 1);
}

TEST_CASE("image imports are cached per settings")
{
    Project project;
    AssetDatabase database(project.files, "cache");
    const std::optional<Image> colour = database.loadImage("game:/textures/checker.png");
    REQUIRE(colour.has_value());
    CHECK(colour->format == ImageFormat::RGBA8Srgb);
    CHECK(colour->mips.size() == 2);

    ImageImportSettings data;
    data.srgb = false;
    data.generateMips = false;
    const std::optional<Image> linear = database.loadImage("game:/textures/checker.png", data);
    REQUIRE(linear.has_value());
    CHECK(linear->format == ImageFormat::RGBA8);
    CHECK(linear->mips.size() == 1);
    CHECK(database.statistics().imports == 2);

    REQUIRE(database.loadImage("game:/textures/checker.png").has_value());
    REQUIRE(database.loadImage("game:/textures/checker.png", data).has_value());
    CHECK(database.statistics().cacheHits == 2);
}

TEST_CASE("the asset database recovers from bad caches and reports bad sources")
{
    Project project;
    AssetDatabase database(project.files, "cache");
    REQUIRE(database.loadImage("game:/textures/checker.png").has_value());

    // A damaged cache file is ignored and rewritten.
    const std::string cached = database.cachePath("game:/textures/checker.png", "image:1|srgb=1|mips=1");
    const std::string garbage = "definitely not an asset";
    REQUIRE(project.files.writeText(cached, garbage) == FileError::None);
    REQUIRE(database.loadImage("game:/textures/checker.png").has_value());
    CHECK(database.statistics().imports == 2);
    REQUIRE(database.loadImage("game:/textures/checker.png").has_value());
    CHECK(database.statistics().cacheHits == 1);

    std::string error;
    CHECK_FALSE(database.loadImage("game:/textures/missing.png", {}, &error).has_value());
    CHECK(error.find("missing.png") != std::string::npos);
    CHECK_FALSE(database.loadModel("game:/unsupported.gltf", &error).has_value());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(database.loadImage("game:/scene/two_nodes.bin", {}, &error).has_value());

    // Without a cache mount every load imports.
    AssetDatabase uncached(project.files, "");
    REQUIRE(uncached.loadImage("game:/textures/checker.png").has_value());
    REQUIRE(uncached.loadImage("game:/textures/checker.png").has_value());
    CHECK(uncached.statistics().imports == 2);
    CHECK(uncached.statistics().cacheWrites == 0);
}

#include "AssetTestProject.hpp"

#include <illusion/assets/AssetCooker.hpp>
#include <illusion/assets/AssetDatabase.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/platform/Archive.hpp>

#include <doctest/doctest.h>

using namespace illusion;

TEST_CASE("cooked assets load from an archive without their sources")
{
    test::AssetProject project;
    const CookResult result = cookAssets(project.files, "game:/", "cache");
    CHECK(result.models == 3);        // two_nodes.gltf, binary.glb, embedded.gltf
    CHECK(result.images == 1);        // textures/checker.png
    CHECK(result.errors.size() == 2); // unsupported.gltf and scene/bad_image_uri.gltf are broken on purpose

    const std::filesystem::path archive = project.directory.path / "cooked.illpak";
    REQUIRE(packDirectory(project.directory.path / "cache", archive) == FileError::None);

    // A "shipped game": only the cooked archive, mounted as the cache; no "game" mount at all.
    VirtualFileSystem shipped;
    REQUIRE(shipped.mountArchive("cache", archive) == FileError::None);
    AssetDatabase database(shipped, "cache", false);
    std::string error;
    const std::optional<ModelData> model = database.loadModel("game:/scene/two_nodes.gltf", &error);
    INFO(error);
    REQUIRE(model.has_value());
    CHECK(model->images.size() == 1);
    CHECK(database.loadImage("game:/textures/checker.png").has_value());
    CHECK(database.statistics().cacheHits == 2);
    CHECK(database.statistics().imports == 0);
    // Assets that were not cooked fail cleanly, since there is no source to import.
    CHECK_FALSE(database.loadImage("game:/textures/checker.png", ImageImportSettings{false, false}, &error)
                    .has_value());

    // With source verification on, the missing sources make the entries unusable.
    AssetDatabase verifying(shipped, "cache", true);
    CHECK_FALSE(verifying.loadModel("game:/scene/two_nodes.gltf").has_value());

    // The streamer reads the archive the same way.
    JobSystem jobs(JobSystemDesc{2, 256});
    AssetStreamerDesc desc;
    desc.verifySources = false;
    AssetStreamer streamer(shipped, jobs, desc);
    const AssetHandle<ModelData> streamed = streamer.loadModel("game:/binary.glb");
    const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
    streamer.waitIdle();
    REQUIRE(streamed.ready());
    REQUIRE(image.ready());
    CHECK(streamed.value().meshes.size() == 1);
    CHECK(streamer.statistics().cacheHits == 2);
}

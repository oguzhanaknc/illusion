#include "AssetTestProject.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>

#include <doctest/doctest.h>

#include <format>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

JobSystemDesc smallPool()
{
    return JobSystemDesc{2, 256};
}

} // namespace

TEST_CASE("the asset streamer loads in the background and publishes in update")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);

    const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
    const AssetHandle<ModelData> model = streamer.loadModel("game:/scene/two_nodes.gltf");
    // States change only in update(), so nothing is ready before it runs.
    CHECK(image.state() == AssetState::Loading);
    CHECK(image.get() == nullptr);
    CHECK(streamer.pending() == 2);
    CHECK(image.id() == AssetId::fromPath("game:/textures/checker.png"));

    streamer.waitIdle();
    CHECK(streamer.pending() == 0);
    REQUIRE(image.ready());
    CHECK(image.value().width() == 2);
    CHECK(image.value().format == ImageFormat::RGBA8Srgb);
    REQUIRE(model.ready());
    CHECK(model.value().meshes.size() == 1);
    CHECK(model.value().images.size() == 1);
    CHECK(streamer.statistics().imports == 2);
    CHECK(image.error().empty());
}

TEST_CASE("identical requests share a handle; different settings do not")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);

    const AssetHandle<Image> a = streamer.loadImage("game:/textures/checker.png");
    const AssetHandle<Image> b = streamer.loadImage("game:/scene/../textures/checker.png");
    ImageImportSettings linear;
    linear.srgb = false;
    const AssetHandle<Image> c = streamer.loadImage("game:/textures/checker.png", linear);
    CHECK(a == b);
    CHECK_FALSE(a == c);
    CHECK(streamer.pending() == 2);
    streamer.waitIdle();
    CHECK(a.get() == b.get());
    CHECK(c.value().format == ImageFormat::RGBA8);
}

TEST_CASE("streamed loads use and refresh the import cache")
{
    test::AssetProject project;
    {
        JobSystem jobs(smallPool());
        AssetStreamer streamer(project.files, jobs);
        const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
        const AssetHandle<ModelData> model = streamer.loadModel("game:/scene/two_nodes.gltf");
        streamer.waitIdle();
    } // shutting down writes pending cache entries

    {
        JobSystem jobs(smallPool());
        AssetStreamer streamer(project.files, jobs);
        const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
        const AssetHandle<ModelData> model = streamer.loadModel("game:/scene/two_nodes.gltf");
        streamer.waitIdle();
        REQUIRE(image.ready());
        REQUIRE(model.ready());
        CHECK(streamer.statistics().cacheHits == 2);
        CHECK(streamer.statistics().imports == 0);
    }

    // Changing the texture invalidates both the image and the model that uses it.
    const std::vector<std::byte> red =
        encodePng(makeSolidImage(2, 2, Vec4(1.0f, 0.0f, 0.0f, 1.0f), ImageFormat::RGBA8));
    REQUIRE(project.files.writeFile("game:/textures/checker.png", red) == FileError::None);
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
    const AssetHandle<ModelData> model = streamer.loadModel("game:/scene/two_nodes.gltf");
    streamer.waitIdle();
    REQUIRE(image.ready());
    REQUIRE(model.ready());
    CHECK(streamer.statistics().imports == 2);
    CHECK(static_cast<u8>(image.value().mip(0)[0]) == 255);
    CHECK(static_cast<u8>(image.value().mip(0)[1]) == 0);
    CHECK(static_cast<u8>(model.value().images[0].mip(0)[1]) == 0);
}

TEST_CASE("failed streamed loads report why")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<Image> missing = streamer.loadImage("game:/textures/missing.png");
    const AssetHandle<Image> notImage = streamer.loadImage("game:/scene/two_nodes.bin");
    const AssetHandle<ModelData> unsupported = streamer.loadModel("game:/unsupported.gltf");
    const AssetHandle<Image> badPath = streamer.loadImage("no mount here");
    CHECK(badPath.state() == AssetState::Failed); // rejected immediately
    CHECK_FALSE(badPath.error().empty());
    streamer.waitIdle();
    CHECK(missing.state() == AssetState::Failed);
    CHECK(missing.error().find("missing.png") != std::string::npos);
    CHECK(notImage.state() == AssetState::Failed);
    CHECK(unsupported.state() == AssetState::Failed);
    CHECK(unsupported.error().find("KHR_draco") != std::string::npos);
    CHECK(AssetHandle<Image>().state() == AssetState::Failed);
}

TEST_CASE("many concurrent loads all complete with the right data")
{
    test::AssetProject project;
    constexpr int kCount = 24;
    for (int i = 0; i < kCount; ++i)
    {
        const auto shade = static_cast<f32>(i) / static_cast<f32>(kCount);
        const std::vector<std::byte> png =
            encodePng(makeSolidImage(4, 4, Vec4(shade, 0.5f, 0.0f, 1.0f), ImageFormat::RGBA8));
        REQUIRE(project.files.writeFile(std::format("game:/generated/{}.png", i), png) == FileError::None);
    }
    JobSystem jobs(JobSystemDesc{4, 256});
    AssetStreamer streamer(project.files, jobs);
    ImageImportSettings linear;
    linear.srgb = false;
    std::vector<AssetHandle<Image>> handles;
    for (int i = 0; i < kCount; ++i)
    {
        handles.push_back(streamer.loadImage(std::format("game:/generated/{}.png", i), linear));
    }
    streamer.waitIdle();
    for (int i = 0; i < kCount; ++i)
    {
        REQUIRE(handles[static_cast<usize>(i)].ready());
        const u8 red = static_cast<u8>(handles[static_cast<usize>(i)].value().mip(0)[0]);
        CHECK(std::abs(int(red) - int(255.0f * static_cast<f32>(i) / kCount + 0.5f)) <= 1);
    }
}

TEST_CASE("the asset streamer shuts down cleanly with loads in flight")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    std::vector<AssetHandle<Image>> handles;
    {
        AssetStreamer streamer(project.files, jobs);
        for (int i = 0; i < 8; ++i)
        {
            ImageImportSettings settings;
            settings.generateMips = i % 2 == 0;
            settings.srgb = i % 4 < 2;
            handles.push_back(streamer.loadImage("game:/textures/checker.png", settings));
        }
        // Destroyed without update(): handles stay valid objects, still in the Loading state.
    }
    for (const AssetHandle<Image>& handle : handles)
    {
        CHECK(handle.state() == AssetState::Loading);
    }
}

TEST_CASE("released assets are forgotten and can be loaded again")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const Image* first = nullptr;
    {
        const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
        streamer.waitIdle();
        first = image.get();
        REQUIRE(first != nullptr);
    }
    const AssetHandle<Image> again = streamer.loadImage("game:/textures/checker.png");
    CHECK(again.state() == AssetState::Loading); // a new request, not the released one
    streamer.waitIdle();
    CHECK(again.ready());
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("reading the value of an asset that is not loaded asserts")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<Image> loading = streamer.loadImage("game:/textures/checker.png");
    {
        test::ScopedAssertCatcher catcher;
        CHECK_THROWS_AS(static_cast<void>(loading.value()), test::AssertionFailure);
        CHECK_THROWS_AS(static_cast<void>(AssetHandle<Image>().value()), test::AssertionFailure);
    }
    streamer.waitIdle();
    CHECK(loading.value().width() == 2);
}
#endif

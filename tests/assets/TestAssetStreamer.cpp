#include "AssetTestProject.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <format>
#include <optional>
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

TEST_CASE("loadFile reads a file on the I/O thread and decodes it on the Job System")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    REQUIRE(
        project.files.writeFile("game:/notes.txt", std::vector<std::byte>{std::byte{'h'}, std::byte{'i'}}) ==
        FileError::None);
    REQUIRE(project.files.writeFile("game:/empty.txt", std::vector<std::byte>{}) == FileError::None);

    std::atomic<int> decodes{0};
    const AssetFileDecoder<std::string> asText =
        [&decodes](std::vector<std::byte>&& bytes, std::string& error) -> std::optional<std::string> {
        decodes.fetch_add(1, std::memory_order_relaxed);
        if (bytes.size() > 8)
        {
            error = "too long";
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const AssetHandle<std::string> notes = streamer.loadFile("game:/notes.txt", "test.text", asText);
    const AssetHandle<std::string> same = streamer.loadFile("game:/./notes.txt", "test.text", asText);
    const AssetHandle<std::string> missing = streamer.loadFile("game:/missing.txt", "test.text", asText);
    const AssetHandle<std::string> empty = streamer.loadFile("game:/empty.txt", "test.text", asText);
    const AssetHandle<std::string> tooLong =
        streamer.loadFile("game:/scene/two_nodes.gltf", "test.text", asText);
    const AssetHandle<std::string> invalid = streamer.loadFile("../outside.txt", "test.text", asText);
    CHECK(notes == same);
    CHECK(notes.state() == AssetState::Loading);
    CHECK(invalid.state() == AssetState::Failed);

    streamer.waitIdle();
    REQUIRE(notes.ready());
    CHECK(notes.value() == "hi");
    CHECK(notes.id() == AssetId::fromPath("game:/notes.txt"));
    CHECK(missing.state() == AssetState::Failed);
    CHECK(missing.error().find("game:/missing.txt") != std::string::npos);
    CHECK(empty.state() == AssetState::Failed);
    CHECK(empty.error().find("empty") != std::string::npos);
    CHECK(tooLong.state() == AssetState::Failed);
    CHECK(tooLong.error() == "too long");
    CHECK(decodes.load() == 2); // notes once, and the long file; failed reads never reach the decoder
    CHECK(streamer.statistics().imports == 0);
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

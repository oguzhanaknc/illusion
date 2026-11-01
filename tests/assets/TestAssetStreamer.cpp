#include "AssetTestProject.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/assets/ImportSettings.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/JobSystem.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

TEST_CASE("invalidate forgets the requests of one file so the next request reads it again")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetFileDecoder<std::string> asText = [](std::vector<std::byte>&& bytes,
                                                    std::string&) -> std::optional<std::string> {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    REQUIRE(project.files.writeFile("game:/a.txt", std::vector<std::byte>{std::byte{'1'}}) ==
            FileError::None);
    REQUIRE(project.files.writeFile("game:/sub/a.txt", std::vector<std::byte>{std::byte{'s'}}) ==
            FileError::None);
    const AssetHandle<std::string> first = streamer.loadFile("game:/a.txt", "test.text", asText);
    const AssetHandle<std::string> upper = streamer.loadFile("game:/a.txt", "test.upper", asText);
    const AssetHandle<std::string> other = streamer.loadFile("game:/sub/a.txt", "test.text", asText);
    streamer.waitIdle();
    REQUIRE(first.ready());

    REQUIRE(project.files.writeFile("game:/a.txt", std::vector<std::byte>{std::byte{'2'}}) ==
            FileError::None);
    CHECK(streamer.loadFile("game:/a.txt", "test.text", asText) == first); // still the old request
    CHECK(streamer.invalidate("game:/./a.txt") == 2);                      // both decoders, not sub/a.txt
    CHECK(streamer.invalidate("game:/a.txt") == 0);
    CHECK(streamer.invalidate("../outside.txt") == 0);
    const AssetHandle<std::string> second = streamer.loadFile("game:/a.txt", "test.text", asText);
    CHECK_FALSE(second == first);
    CHECK(streamer.loadFile("game:/sub/a.txt", "test.text", asText) == other);
    streamer.waitIdle();
    CHECK(second.value() == "2");
    CHECK(first.value() == "1"); // handles given out before keep their data
    CHECK(upper.ready());
}

TEST_CASE("reload invalidates a file and publishes AssetReloadedEvent")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<Image> image = streamer.loadImage("game:/textures/checker.png");
    streamer.waitIdle();

    std::vector<AssetReloadedEvent> events;
    const Subscription subscription = streamer.events().subscribe<AssetReloadedEvent>(
        [&events](const AssetReloadedEvent& event) { events.push_back(event); });
    CHECK(streamer.reload("game:/textures/../textures/checker.png"));
    CHECK_FALSE(streamer.reload("../outside.png"));
    REQUIRE(events.size() == 1);
    CHECK(events[0].path == "game:/textures/checker.png");
    CHECK(events[0].asset == AssetId::fromPath("game:/textures/checker.png"));
    const AssetHandle<Image> again = streamer.loadImage("game:/textures/checker.png");
    CHECK_FALSE(again == image);
    streamer.waitIdle();
    CHECK(again.ready());
}

TEST_CASE("reload reports its mode in AssetReloadedEvent")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    std::vector<AssetReloadMode> modes;
    const Subscription subscription = streamer.events().subscribe<AssetReloadedEvent>(
        [&modes](const AssetReloadedEvent& event) { modes.push_back(event.mode); });
    CHECK(streamer.reload("game:/textures/checker.png"));
    CHECK(streamer.reload("game:/textures/checker.png", AssetReloadMode::Reimport));
    CHECK(modes == std::vector<AssetReloadMode>{AssetReloadMode::IfChanged, AssetReloadMode::Reimport});
}

namespace
{

constexpr std::string_view kCookedKey = "test-cooked:1";

AssetHandle<CookedAsset> lookUp(AssetStreamer& streamer, std::string_view path,
                                AssetReloadMode mode = AssetReloadMode::IfChanged)
{
    AssetHandle<CookedAsset> handle =
        streamer.loadCooked(path, kCookedKey, AssetType::Environment, kEnvironmentFormatVersion, mode);
    streamer.waitIdle();
    return handle;
}

AssetFile cookedFile(std::vector<AssetDependency> sources, u8 marker)
{
    AssetFile file;
    file.type = AssetType::Environment;
    file.version = kEnvironmentFormatVersion;
    file.dependencies = std::move(sources);
    file.payload = {std::byte{marker}};
    return file;
}

} // namespace

TEST_CASE("loadCooked misses with the current sources, then hits what storeCooked wrote after a restart")
{
    test::AssetProject project;
    {
        JobSystem jobs(smallPool());
        AssetStreamer streamer(project.files, jobs);
        const AssetHandle<CookedAsset> miss = lookUp(streamer, "game:/textures/../textures/checker.png");
        REQUIRE(miss.ready());
        CHECK_FALSE(miss.value().file.has_value());
        REQUIRE(miss.value().sources.size() == 2);
        CHECK(miss.value().sources[0].path == "game:/textures/checker.png");
        CHECK(miss.value().sources[1].path == importSettingsPath("game:/textures/checker.png"));
        CHECK(miss.value().sources[1].contentHash == xxhash64(std::span<const std::byte>{}));
        streamer.storeCooked("game:/textures/checker.png", kCookedKey, cookedFile(miss.value().sources, 7));
    } // shutting down writes the entry

    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<CookedAsset> hit = lookUp(streamer, "game:/textures/checker.png");
    REQUIRE(hit.ready());
    REQUIRE(hit.value().file.has_value());
    CHECK(hit.value().file->payload == std::vector<std::byte>{std::byte{7}});
    CHECK(hit.value().sources.empty());
    CHECK(streamer.statistics().cacheHits == 1);
    CHECK(streamer.statistics().cacheWrites == 0);
    // Same path and key share a handle; another key or type does not find the entry.
    CHECK(streamer.loadCooked("game:/textures/checker.png", kCookedKey, AssetType::Environment,
                              kEnvironmentFormatVersion) == hit);
    const AssetHandle<CookedAsset> otherType = streamer.loadCooked(
        "game:/textures/checker.png", "test-other:1", AssetType::Environment, kEnvironmentFormatVersion);
    streamer.waitIdle();
    CHECK_FALSE(otherType.value().file.has_value());
}

TEST_CASE("loadCooked misses once the file or its import settings sidecar changes, and with Reimport")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const std::string path = "game:/textures/checker.png";
    {
        const AssetHandle<CookedAsset> miss = lookUp(streamer, path);
        streamer.storeCooked(path, kCookedKey, cookedFile(miss.value().sources, 1));
    }
    streamer.waitIdle();
    // Writes are queued before later loads, so the entry is there for the next lookup.
    CHECK(lookUp(streamer, path).value().file.has_value());
    CHECK_FALSE(lookUp(streamer, path, AssetReloadMode::Reimport).value().file.has_value());

    SUBCASE("the file")
    {
        const std::vector<std::byte> red =
            encodePng(makeSolidImage(2, 2, Vec4(1.0f, 0.0f, 0.0f, 1.0f), ImageFormat::RGBA8));
        REQUIRE(project.files.writeFile(path, red) == FileError::None);
        streamer.invalidate(path);
        const AssetHandle<CookedAsset> stale = lookUp(streamer, path);
        CHECK_FALSE(stale.value().file.has_value());
        REQUIRE(stale.value().sources.size() == 2);
        CHECK(stale.value().sources[0].contentHash == xxhash64(red));
    }
    SUBCASE("the sidecar")
    {
        const std::string_view sidecar =
            R"({"format": "illusion.import", "version": 1, "texture": {"generateMips": false}})";
        REQUIRE(project.files.writeFile(importSettingsPath(path), std::as_bytes(std::span(sidecar))) ==
                FileError::None);
        streamer.invalidate(path);
        const AssetHandle<CookedAsset> stale = lookUp(streamer, path);
        CHECK_FALSE(stale.value().file.has_value());
        REQUIRE(stale.value().sources.size() == 2);
        CHECK(stale.value().sources[1].contentHash == xxhash64(std::as_bytes(std::span(sidecar))));
    }
}

TEST_CASE("loadCooked of a missing file misses without sources, and an invalid path fails")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs);
    const AssetHandle<CookedAsset> missing = lookUp(streamer, "game:/textures/none.png");
    REQUIRE(missing.ready());
    CHECK_FALSE(missing.value().file.has_value());
    CHECK(missing.value().sources.empty());
    const AssetHandle<CookedAsset> invalid = lookUp(streamer, "../outside.png");
    CHECK(invalid.state() == AssetState::Failed);
    // Storing without sources or for an invalid path writes nothing.
    streamer.storeCooked("game:/textures/none.png", kCookedKey, cookedFile({}, 1));
    streamer.storeCooked("../outside.png", kCookedKey, cookedFile({{"game:/x", 1}}, 1));
    streamer.waitIdle();
    CHECK(streamer.statistics().cacheWrites == 0);
}

TEST_CASE("loadCooked without a cache mount always misses at once")
{
    test::AssetProject project;
    JobSystem jobs(smallPool());
    AssetStreamer streamer(project.files, jobs, AssetStreamerDesc{.cacheMount = ""});
    const AssetHandle<CookedAsset> miss = streamer.loadCooked(
        "game:/textures/checker.png", kCookedKey, AssetType::Environment, kEnvironmentFormatVersion);
    CHECK(miss.ready());
    CHECK(streamer.pending() == 0);
    CHECK_FALSE(miss.value().file.has_value());
    CHECK(miss.value().sources.empty());
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

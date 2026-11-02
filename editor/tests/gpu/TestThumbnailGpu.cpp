#include "core/EditorCoreTestSupport.hpp"
#include "gpu/GoldenSupport.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/editor/core/ThumbnailCache.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/viewport/ThumbnailRenderer.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>

using namespace illusion;
using namespace illusion::editor;

// GPU tests of the thumbnail renderer behind asset.thumbnail: golden images of each kind, the frame budget
// (one thumbnail per frame, a bounded CPU cost) and live reload of a changed material.

namespace
{

// CPU time one thumbnail step may take in a frame. The plan's 2 ms holds for optimized builds on a GPU;
// unoptimized builds, the validation layer and software rasterizers record several times slower.
#if defined(NDEBUG)
constexpr f64 kStepBudgetMs = 2.0;
#else
constexpr f64 kStepBudgetMs = 20.0;
#endif
constexpr f64 kSoftwareSlack = 10.0;

struct GpuThumbnails
{
    std::unique_ptr<RenderDevice> device;
    test::EditorFixture fixture;
    ui::UiContext context;
    std::unique_ptr<ui::OffscreenUi> offscreen;
    std::unique_ptr<viewport::ViewportService> service;
    u32 maxSteps = 0;      // most thumbnail steps in one frame
    f64 maxStepMs = 0.0;   // slowest frame with a step, after the first render
    bool warmedUp = false; // the first render (pipeline creation) is not timed

    GpuThumbnails()
        : device(RenderDevice::create(test::headlessDeviceDesc()))
        , fixture(1, pollingOptions())
    {
        REQUIRE(device != nullptr);
        reflectRenderComponents();
        offscreen = ui::OffscreenUi::create(*device, context, UVec2(64, 64));
        REQUIRE(offscreen != nullptr);
        service = viewport::ViewportService::create(*device, *fixture.editor, offscreen->renderer());
        REQUIRE(service != nullptr);
        CHECK(fixture.editor->thumbnailHost() == &service->thumbnails());
    }

    ~GpuThumbnails()
    {
        service.reset();
        offscreen.reset();
        if (device)
        {
            device->waitIdle();
            CHECK(device->validationErrorCount() == 0);
        }
    }

    GpuThumbnails(const GpuThumbnails&) = delete;
    GpuThumbnails& operator=(const GpuThumbnails&) = delete;

    static EditorContextOptions pollingOptions()
    {
        EditorContextOptions options;
        options.assetWatching = FileWatchBackend::Polling;
        options.assetPollInterval = 0.0;
        return options;
    }

    EditorContext& editor() { return *fixture.editor; }

    // One editor frame: the viewports (and the thumbnail renderer) render, then the editor posts events.
    void frame()
    {
        const u32 renderedBefore = service->thumbnails().stats().rendered;
        service->render(false);
        const viewport::ThumbnailRendererStats& stats = service->thumbnails().stats();
        maxSteps = std::max(maxSteps, stats.steps);
        CHECK(stats.rendered - renderedBefore <= 1);
        if (stats.steps != 0)
        {
            if (warmedUp)
            {
                maxStepMs = std::max(maxStepMs, stats.updateMilliseconds);
            }
            warmedUp = warmedUp || stats.rendered != 0;
        }
        device->waitIdle();
        editor().endFrame(1.0 / 60.0);
    }

    // Runs asset.thumbnail for @p path to its end, one frame per update.
    Expected<std::string, CommandError> thumbnail(std::string_view path, bool refresh = false)
    {
        const JsonDocument doc =
            test::parseJson(std::format(R"({{"path":"{}","refresh":{}}})", path, refresh ? "true" : "false"));
        Expected<std::unique_ptr<RunningCommand>, CommandError> started =
            editor().startJson("asset.thumbnail", doc.root());
        if (!started)
        {
            return unexpected(std::move(started).error());
        }
        for (int i = 0; i < 20000; ++i)
        {
            Expected<std::optional<std::string>, CommandError> updated = started.value()->update();
            if (!updated)
            {
                return unexpected(std::move(updated).error());
            }
            if (updated->has_value())
            {
                return std::move(**updated);
            }
            frame();
        }
        FAIL("asset.thumbnail did not finish");
        return std::string();
    }

    // The thumbnail image of @p path (which must succeed).
    Image image(std::string_view path, bool refresh = false)
    {
        const Expected<std::string, CommandError> result = thumbnail(path, refresh);
        REQUIRE_MESSAGE(result.hasValue(), (result ? std::string() : result.error().message));
        const JsonDocument doc = test::parseJson(*result);
        const std::filesystem::path file = utf8Path(doc.root().find("file").asString().value_or(""));
        std::optional<Image> decoded = decodeImage(test::readBinaryFile(file), true);
        REQUIRE(decoded.has_value());
        REQUIRE(decoded->width() == kThumbnailSize);
        REQUIRE(decoded->height() == kThumbnailSize);
        return std::move(decoded).value();
    }

    void write(std::string_view path, std::span<const std::byte> bytes)
    {
        REQUIRE(editor().files().writeFile(path, bytes) == FileError::None);
        frame(); // the catalog sees it
    }
};

// A 64 x 32 sRGB texture: red to blue from left to right, opaque at the top and transparent at the bottom.
std::vector<std::byte> gradientPng()
{
    Image image;
    image.format = ImageFormat::RGBA8Srgb;
    image.mips.push_back(ImageMip{64, 32, 0, 64 * 32 * 4});
    image.data.resize(image.mips[0].size);
    for (u32 y = 0; y < 32; ++y)
    {
        for (u32 x = 0; x < 64; ++x)
        {
            std::byte* pixel = image.data.data() + (y * 64 + x) * 4;
            pixel[0] = static_cast<std::byte>(255 - x * 4);
            pixel[1] = static_cast<std::byte>(40);
            pixel[2] = static_cast<std::byte>(x * 4);
            pixel[3] = static_cast<std::byte>(255 - y * 8);
        }
    }
    return encodePng(image);
}

// A 16 x 8 Radiance HDR environment: a bright blue sky over a brown ground.
std::vector<std::byte> skyHdr()
{
    std::string text = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 8 +X 16\n";
    for (u32 y = 0; y < 8; ++y)
    {
        for (u32 x = 0; x < 16; ++x)
        {
            const Vec3 color = y < 4 ? Vec3(0.3f, 0.55f, 1.6f) * (1.0f + static_cast<f32>(3 - y))
                                     : Vec3(0.35f, 0.22f, 0.12f);
            const f32 largest = std::max(color.x, std::max(color.y, color.z));
            int exponent = 0;
            const f32 scale = std::frexp(largest, &exponent) * 256.0f / largest;
            text.push_back(static_cast<char>(static_cast<u8>(color.x * scale)));
            text.push_back(static_cast<char>(static_cast<u8>(color.y * scale)));
            text.push_back(static_cast<char>(static_cast<u8>(color.z * scale)));
            text.push_back(static_cast<char>(static_cast<u8>(exponent + 128)));
        }
    }
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::vector<std::byte> testModel()
{
    const std::vector<std::byte> bytes =
        test::readBinaryFile(utf8Path(ILLUSION_ENGINE_TEST_DATA) / "gltf" / "binary.glb");
    REQUIRE_FALSE(bytes.empty());
    return bytes;
}

// The mean colour of the 16 x 16 pixels at the centre of @p image.
Vec3 centreColor(const Image& image)
{
    const std::span<const std::byte> pixels = image.mip(0);
    Vec3 sum(0.0f);
    for (u32 y = kThumbnailSize / 2 - 8; y < kThumbnailSize / 2 + 8; ++y)
    {
        for (u32 x = kThumbnailSize / 2 - 8; x < kThumbnailSize / 2 + 8; ++x)
        {
            const usize i = (static_cast<usize>(y) * kThumbnailSize + x) * 4;
            sum += Vec3(static_cast<f32>(std::to_integer<int>(pixels[i])),
                        static_cast<f32>(std::to_integer<int>(pixels[i + 1])),
                        static_cast<f32>(std::to_integer<int>(pixels[i + 2])));
        }
    }
    return sum / 256.0f;
}

} // namespace

TEST_CASE("material, model, texture and environment thumbnails match their golden images")
{
    GpuThumbnails gpu;
    EditorContext& editor = gpu.editor();
    test::ok(
        editor, "material.create",
        R"({"path":"assets:/materials/red.material","value":{"material":{"baseColorFactor":[0.9,0.1,0.08,1],)"
        R"("roughnessFactor":0.35}}})");
    gpu.write("assets:/models/quad.glb", testModel());
    gpu.write("assets:/textures/gradient.png", gradientPng());
    gpu.write("assets:/sky.hdr", skyHdr());

    const Image material = gpu.image("assets:/materials/red.material");
    const Vec3 red = centreColor(material);
    CHECK(red.r > 2.0f * red.g);
    test::checkGolden("thumbnail-material", material);
    test::checkGolden("thumbnail-model", gpu.image("assets:/models/quad.glb"));
    test::checkGolden("thumbnail-texture", gpu.image("assets:/textures/gradient.png"));
    const Image environment = gpu.image("assets:/sky.hdr");
    // The sky is blue above the sphere (top left pixel).
    CHECK(std::to_integer<int>(environment.mip(0)[2]) > std::to_integer<int>(environment.mip(0)[0]));
    test::checkGolden("thumbnail-environment", environment);
    CHECK(gpu.service->thumbnails().stats().rendered == 3); // textures are scaled on the CPU
}

TEST_CASE("thumbnails render one per frame within their frame budget and allocate nothing when idle")
{
    GpuThumbnails gpu;
    EditorContext& editor = gpu.editor();
    for (int i = 0; i < 4; ++i)
    {
        test::ok(editor, "material.create",
                 std::format(R"({{"path":"assets:/materials/m{}.material","value":{{"material":)"
                             R"({{"baseColorFactor":[{},0.5,0.2,1]}}}}}})",
                             i, 0.2 * i));
    }
    gpu.frame();
    for (int i = 0; i < 4; ++i)
    {
        const Expected<std::string, CommandError> result =
            gpu.thumbnail(std::format("assets:/materials/m{}.material", i));
        REQUIRE_MESSAGE(result.hasValue(), (result ? std::string() : result.error().message));
    }
    // Three requests at once still render at most one per frame.
    viewport::ThumbnailRenderer& renderer = gpu.service->thumbnails();
    std::vector<u64> tickets;
    for (int i = 0; i < 3; ++i)
    {
        Expected<u64, CommandError> ticket = renderer.requestThumbnail(
            ThumbnailRequest{std::format("assets:/materials/m{}.material", i), ThumbnailKind::Material});
        REQUIRE(ticket.hasValue());
        tickets.push_back(*ticket);
    }
    for (int frame = 0; frame < 20000 && !tickets.empty(); ++frame)
    {
        gpu.frame();
        std::erase_if(tickets, [&](u64 ticket) {
            std::optional<Expected<ThumbnailImage, CommandError>> done = renderer.thumbnail(ticket);
            CHECK((!done || done->hasValue()));
            return done.has_value();
        });
    }
    CHECK(tickets.empty());
    CHECK(gpu.maxSteps == 1);
    const f64 budget = kStepBudgetMs * (gpu.service->gpuInfo().software ? kSoftwareSlack : 1.0);
    INFO("slowest thumbnail step " << gpu.maxStepMs << " ms, budget " << budget << " ms");
    CHECK(gpu.maxStepMs < budget);

    CHECK(renderer.stats().pending == 0);
    {
        test::ScopedAllocationCounter allocations;
        renderer.update();
        CHECK(allocations.count() == 0);
    }
}

TEST_CASE("an edited material gets a new thumbnail with its new colour after asset.changed")
{
    GpuThumbnails gpu;
    EditorContext& editor = gpu.editor();
    test::ok(
        editor, "material.create",
        R"({"path":"assets:/materials/tint.material","value":{"material":{"baseColorFactor":[0.9,0.1,0.1,1]}}})");
    gpu.frame();
    const Vec3 before = centreColor(gpu.image("assets:/materials/tint.material"));
    CHECK(before.r > before.b);
    test::ok(
        editor, "material.set",
        R"({"path":"assets:/materials/tint.material","value":{"material":{"baseColorFactor":[0.1,0.1,0.9,1]}}})");
    gpu.frame();
    const Expected<std::string, CommandError> result = gpu.thumbnail("assets:/materials/tint.material");
    REQUIRE(result.hasValue());
    CHECK(test::parseJson(*result).root().find("cached").asBool() == false);
    const Vec3 after = centreColor(gpu.image("assets:/materials/tint.material"));
    CHECK(after.b > after.r);
}

TEST_CASE("files that do not load fail their thumbnails without stopping the queue")
{
    GpuThumbnails gpu;
    const std::string_view junk = "not an image or a model";
    gpu.write("assets:/textures/broken.png", std::as_bytes(std::span<const char>(junk.data(), junk.size())));
    gpu.write("assets:/models/broken.glb", std::as_bytes(std::span<const char>(junk.data(), junk.size())));
    gpu.write("assets:/textures/gradient.png", gradientPng());
    const Expected<std::string, CommandError> texture = gpu.thumbnail("assets:/textures/broken.png");
    REQUIRE_FALSE(texture.hasValue());
    CHECK(texture.error().kind == ErrorKind::IoError);
    const Expected<std::string, CommandError> model = gpu.thumbnail("assets:/models/broken.glb");
    REQUIRE_FALSE(model.hasValue());
    CHECK(model.error().kind == ErrorKind::IoError);
    CHECK(gpu.thumbnail("assets:/textures/gradient.png").hasValue());
}

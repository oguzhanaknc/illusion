#include "EditorTestSupport.hpp"
#include "OffscreenUi.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/editor/ui/UiRenderer.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <imgui.h>
#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;

// GPU tests of the editor UI. Gallery captures are compared with the golden images in
// editor/tests/data/golden; every capture is also written to ILLUSION_UI_CAPTURE_DIR (the build tree by
// default) so that CI can publish them for design review. Run with ILLUSION_UPDATE_GOLDENS=1 to rewrite the
// goldens from the current captures after an intended visual change, and review the diff before committing.

namespace
{

constexpr UVec2 kGoldenSize{1280, 800};

RenderDeviceDesc headlessDesc()
{
    RenderDeviceDesc desc;
    desc.applicationName = "illusion editor GPU tests";
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

std::filesystem::path captureDirectory()
{
    if (const char* directory = std::getenv("ILLUSION_UI_CAPTURE_DIR");
        directory != nullptr && directory[0] != '\0')
    {
        return utf8Path(directory);
    }
    return utf8Path(ILLUSION_UI_CAPTURE_DIR);
}

bool updateGoldens()
{
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    return update != nullptr && std::string_view(update) == "1";
}

std::vector<std::byte> readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<std::byte> result(bytes.size());
    std::memcpy(result.data(), bytes.data(), bytes.size());
    return result;
}

void writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(file.good());
}

struct Difference
{
    f64 meanAbsolute = 0.0;      // mean absolute channel difference (0-255)
    f64 differentFraction = 0.0; // fraction of pixels with a channel off by more than 24
};

Difference compare(const Image& a, const Image& b)
{
    Difference difference;
    const std::span<const std::byte> pa = a.mip(0);
    const std::span<const std::byte> pb = b.mip(0);
    u64 total = 0;
    u64 different = 0;
    for (usize i = 0; i < pa.size(); i += 4)
    {
        int worst = 0;
        for (usize c = 0; c < 4; ++c)
        {
            const int d = std::abs(static_cast<int>(pa[i + c]) - static_cast<int>(pb[i + c]));
            total += static_cast<u64>(d);
            worst = std::max(worst, d);
        }
        if (worst > 24)
        {
            ++different;
        }
    }
    difference.meanAbsolute = static_cast<f64>(total) / static_cast<f64>(pa.size());
    difference.differentFraction = static_cast<f64>(different) / static_cast<f64>(pa.size() / 4);
    return difference;
}

// The gallery rendered by a UiRenderer into an offscreen target.
struct GpuGallery
{
    std::unique_ptr<RenderDevice> device;
    test::GalleryFixture fixture;
    std::unique_ptr<gallery::OffscreenUi> offscreen;
    UVec2 size;

    GpuGallery(std::string_view theme, f32 scale)
        : device(RenderDevice::create(headlessDesc()))
        , fixture(theme, scale, test::FixtureRenderer::External)
        , size(static_cast<u32>(static_cast<f32>(kGoldenSize.x) * scale),
               static_cast<u32>(static_cast<f32>(kGoldenSize.y) * scale))
    {
        REQUIRE(device != nullptr);
        offscreen = gallery::OffscreenUi::create(*device, fixture.context, size);
        REQUIRE(offscreen != nullptr);
    }

    ~GpuGallery()
    {
        offscreen.reset();
        if (device)
        {
            device->waitIdle();
        }
    }

    GpuGallery(const GpuGallery&) = delete;
    GpuGallery& operator=(const GpuGallery&) = delete;

    // Builds a frame of the gallery (and @p extra UI) without rendering it.
    template <typename F>
    void build(F&& extra)
    {
        fixture.context.makeCurrent();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<f32>(size.x), static_cast<f32>(size.y));
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        fixture.galleryPage->draw(fixture.time);
        extra();
        ImGui::Render();
        fixture.time += 1.0 / 60.0;
    }

    void build()
    {
        build([] {});
    }

    // Runs @p frames frames and returns the last capture.
    Image capture(int frames)
    {
        std::optional<Image> image;
        for (int i = 0; i < frames; ++i)
        {
            build();
            image = offscreen->render(ImGui::GetDrawData(), Vec4(0.0f, 0.0f, 0.0f, 1.0f));
            REQUIRE(image.has_value());
        }
        return std::move(*image);
    }
};

struct GoldenCase
{
    const char* name;
    const char* theme;
    f32 scale;
    const char* locale;
    gallery::GalleryPage page;
};

constexpr std::array<GoldenCase, 9> kGoldens = {{
    {"widgets-dark-100", "dark", 1.0f, "en", gallery::GalleryPage::Widgets},
    {"widgets-dark-150", "dark", 1.5f, "en", gallery::GalleryPage::Widgets},
    {"widgets-light-100", "light", 1.0f, "en", gallery::GalleryPage::Widgets},
    {"widgets-light-150", "light", 1.5f, "en", gallery::GalleryPage::Widgets},
    {"widgets-high-contrast-100", "high-contrast", 1.0f, "en", gallery::GalleryPage::Widgets},
    {"widgets-high-contrast-150", "high-contrast", 1.5f, "en", gallery::GalleryPage::Widgets},
    {"widgets-dark-100-en-XA", "dark", 1.0f, "en-XA", gallery::GalleryPage::Widgets},
    {"typography-dark-100", "dark", 1.0f, "en", gallery::GalleryPage::Typography},
    {"editor-dark-100", "dark", 1.0f, "en", gallery::GalleryPage::Editor},
}};

} // namespace

TEST_CASE("gallery captures match the golden images")
{
    for (const GoldenCase& golden : kGoldens)
    {
        CAPTURE(golden.name);
        GpuGallery gpu(golden.theme, golden.scale);
        gpu.fixture.setLocale(golden.locale);
        gpu.fixture.galleryPage->setPage(golden.page);
        gpu.fixture.galleryPage->showToast(0.0);
        const Image actual = gpu.capture(12);
        const std::vector<std::byte> png = encodePng(actual);
        writeFile(captureDirectory() / std::format("{}.png", golden.name), png);

        const std::filesystem::path goldenPath =
            utf8Path(ILLUSION_GOLDEN_DIR) / std::format("{}.png", golden.name);
        if (updateGoldens())
        {
            writeFile(goldenPath, png);
            continue;
        }
        REQUIRE_MESSAGE(std::filesystem::exists(goldenPath),
                        "missing golden " << goldenPath.string() << " (run with ILLUSION_UPDATE_GOLDENS=1)");
        const std::optional<Image> expected = decodeImage(readFile(goldenPath), true);
        REQUIRE(expected.has_value());
        REQUIRE(expected->width() == actual.width());
        REQUIRE(expected->height() == actual.height());
        const Difference difference = compare(actual, *expected);
        INFO("mean difference " << difference.meanAbsolute << ", pixels off "
                                << difference.differentFraction * 100.0 << "% (capture written to "
                                << captureDirectory().string() << ")");
        CHECK(difference.meanAbsolute < 1.0);
        CHECK(difference.differentFraction < 0.003);
        CHECK(gpu.device->validationErrorCount() == 0);
    }
}

TEST_CASE("the sRGB and UNORM present paths show the same colours")
{
    GpuGallery gpu("light", 1.0f);
    gpu.fixture.galleryPage->setPage(gallery::GalleryPage::Widgets);
    (void)gpu.capture(4);
    gpu.build();
    const std::optional<Image> direct =
        gpu.offscreen->render(ImGui::GetDrawData(), Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    gpu.build();
    const std::optional<Image> unorm = gpu.offscreen->renderPresented(
        ImGui::GetDrawData(), Vec4(0.0f, 0.0f, 0.0f, 1.0f), nvrhi::Format::RGBA8_UNORM);
    gpu.build();
    const std::optional<Image> srgb = gpu.offscreen->renderPresented(
        ImGui::GetDrawData(), Vec4(0.0f, 0.0f, 0.0f, 1.0f), nvrhi::Format::SRGBA8_UNORM);
    REQUIRE(direct.has_value());
    REQUIRE(unorm.has_value());
    REQUIRE(srgb.has_value());
    CHECK(direct->data == unorm->data);
    int worst = 0;
    for (usize i = 0; i < unorm->data.size(); ++i)
    {
        worst = std::max(worst, std::abs(static_cast<int>(unorm->data[i]) - static_cast<int>(srgb->data[i])));
    }
    CHECK(worst <= 1);
    CHECK(gpu.device->validationErrorCount() == 0);
}

TEST_CASE("the font atlas texture is created, replaced and destroyed across frames")
{
    GpuGallery gpu("dark", 1.0f);
    CHECK(gpu.offscreen->renderer().textureCount() == 0);
    (void)gpu.capture(2);
    const u32 created = gpu.offscreen->renderer().textureCount();
    CHECK(created >= 1);

    // A larger scale rasterises new glyphs into the atlas (updates, possibly a larger texture).
    gpu.fixture.fonts->setScale(2.0f, 1.0f);
    (void)gpu.capture(3);
    CHECK(gpu.offscreen->renderer().textureCount() >= 1);

    // Compacting throws the atlas away: a new texture replaces it and the old one is destroyed.
    ImGui::GetIO().Fonts->CompactCache();
    (void)gpu.capture(6);
    CHECK(gpu.offscreen->renderer().textureCount() == created);
    CHECK(gpu.device->validationErrorCount() == 0);
}

TEST_CASE("registered textures are drawn by ImGui::Image and can be unregistered")
{
    GpuGallery gpu("dark", 1.0f);
    nvrhi::IDevice* nvrhiDevice = gpu.device->device();
    nvrhi::TextureDesc desc;
    desc.width = 4;
    desc.height = 4;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.debugName = "Registered test texture";
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    nvrhi::TextureHandle texture = nvrhiDevice->createTexture(desc);
    REQUIRE(texture);
    std::array<u32, 16> red{};
    red.fill(0xFF0000FFu); // RGBA bytes FF 00 00 FF
    nvrhi::CommandListHandle commands = nvrhiDevice->createCommandList();
    commands->open();
    commands->writeTexture(texture, 0, 0, red.data(), 4 * 4);
    commands->close();
    nvrhiDevice->executeCommandList(commands);

    (void)gpu.capture(2);
    const u32 before = gpu.offscreen->renderer().textureCount();
    const u64 id = gpu.offscreen->renderer().registerTexture(texture);
    CHECK(gpu.offscreen->renderer().textureCount() == before + 1);

    gpu.build([&] {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(200.0f, 200.0f));
        ImGui::Begin("###test.image", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetCursorScreenPos(ImVec2(50.0f, 50.0f));
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(id)), ImVec2(100.0f, 100.0f));
        ImGui::End();
    });
    const std::optional<Image> image =
        gpu.offscreen->render(ImGui::GetDrawData(), Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    REQUIRE(image.has_value());
    const usize centre = (static_cast<usize>(100) * gpu.size.x + 100) * 4;
    CHECK(static_cast<int>(image->data[centre + 0]) == 255);
    CHECK(static_cast<int>(image->data[centre + 1]) == 0);
    CHECK(static_cast<int>(image->data[centre + 2]) == 0);

    gpu.offscreen->renderer().unregisterTexture(id);
    CHECK(gpu.offscreen->renderer().textureCount() == before);
    CHECK(gpu.device->validationErrorCount() == 0);
}

#include "EditorTestSupport.hpp"
#include "gpu/GoldenSupport.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
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

// GPU tests of the editor UI: gallery goldens (see GoldenSupport.hpp) and renderer behaviour.

namespace
{

constexpr UVec2 kGoldenSize{1280, 800};

// The gallery rendered by a UiRenderer into an offscreen target.
struct GpuGallery
{
    std::unique_ptr<RenderDevice> device;
    test::GalleryFixture fixture;
    std::unique_ptr<ui::OffscreenUi> offscreen;
    UVec2 size;

    GpuGallery(std::string_view theme, f32 scale)
        : device(RenderDevice::create(test::headlessDeviceDesc()))
        , fixture(theme, scale, test::FixtureRenderer::External)
        , size(static_cast<u32>(static_cast<f32>(kGoldenSize.x) * scale),
               static_cast<u32>(static_cast<f32>(kGoldenSize.y) * scale))
    {
        REQUIRE(device != nullptr);
        offscreen = ui::OffscreenUi::create(*device, fixture.context, size);
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
        test::checkGolden(golden.name, actual);
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

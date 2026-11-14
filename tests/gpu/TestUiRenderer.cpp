// The UI pass (ADR 0057): golden images of text, nine-slice panels and clipping at two target sizes with the
// same canvas, texture registration, capacities and the allocation-free steady frame.

#include "FontTestSupport.hpp"
#include "TestSupport.hpp"

#include <illusion/assets/Image.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/UiRenderer.hpp>
#include <illusion/ui/UiDrawList.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace illusion;

namespace
{

constexpr nvrhi::Format kFormat = nvrhi::Format::SRGBA8_UNORM;
constexpr UVec2 kSizes[] = {{1280, 720}, {1920, 1080}};
constexpr int kClear = 124; // sRGB of the linear 0.2 that draw() clears to

UiRendererDesc srgbDesc()
{
    UiRendererDesc desc;
    desc.outputFormat = kFormat;
    return desc;
}

RenderDeviceDesc headless()
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Enabled;
    return desc;
}

nvrhi::TextureHandle makeTarget(RenderDevice& device, UVec2 size, nvrhi::Format format = kFormat)
{
    nvrhi::TextureDesc desc;
    desc.width = size.x;
    desc.height = size.y;
    desc.format = format;
    desc.isRenderTarget = true;
    desc.debugName = "UI test target";
    desc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
    return device.device()->createTexture(desc);
}

// A 16x16 sRGB panel: a 4-pixel opaque blue border with a white corner dot, a half-transparent light centre.
Image panelImage()
{
    constexpr u32 kSide = 16;
    Image image;
    image.format = ImageFormat::RGBA8Srgb;
    image.mips.push_back(ImageMip{kSide, kSide, 0, usize{kSide} * kSide * 4});
    image.data.resize(image.mips[0].size);
    for (u32 y = 0; y < kSide; ++y)
    {
        for (u32 x = 0; x < kSide; ++x)
        {
            const bool border = x < 4 || y < 4 || x >= kSide - 4 || y >= kSide - 4;
            const bool dot = x < 2 && y < 2;
            const u8 rgba[4] = {static_cast<u8>(dot      ? 255
                                                : border ? 40
                                                         : 230),
                                static_cast<u8>(dot      ? 255
                                                : border ? 80
                                                         : 230),
                                static_cast<u8>(dot      ? 255
                                                : border ? 200
                                                         : 240),
                                static_cast<u8>(border ? 255 : 128)};
            for (usize c = 0; c < 4; ++c)
            {
                image.data[(static_cast<usize>(y) * kSide + x) * 4 + c] = static_cast<std::byte>(rgba[c]);
            }
        }
    }
    return image;
}

// Clears @p target to @p clear (linear), draws @p list's data and reads the target back as RGBA8.
std::vector<std::byte> draw(RenderDevice& device, ReadbackRing& ring, UiRenderer& ui, nvrhi::ITexture* target,
                            const UiDrawData& data, bool* rendered = nullptr,
                            Vec4 clear = Vec4(0.2f, 0.2f, 0.2f, 1.0f))
{
    nvrhi::IDevice* gpu = device.device();
    nvrhi::CommandListHandle commands = gpu->createCommandList();
    commands->open();
    commands->clearTextureFloat(target, nvrhi::AllSubresources,
                                nvrhi::Color(clear.r, clear.g, clear.b, clear.a));
    const bool ok = ui.render(commands, target, data);
    commands->close();
    gpu->executeCommandList(commands);
    if (rendered != nullptr)
    {
        *rendered = ok;
    }
    std::vector<std::byte> pixels(static_cast<usize>(target->getDesc().width) * target->getDesc().height * 4);
    const auto ticket = ring.request(target);
    REQUIRE(ticket.has_value());
    REQUIRE(ring.read(*ticket, pixels, true) == ReadbackStatus::Ready);
    return pixels;
}

int channel(const std::vector<std::byte>& pixels, UVec2 size, u32 x, u32 y, u32 c)
{
    return std::to_integer<int>(pixels[(static_cast<usize>(y) * size.x + x) * 4 + c]);
}

// Compares with tests/data/render/<name>.png as the editor's goldens do: mean channel difference under 1 and
// under 0.3% of the pixels off by more than 24 (font bakes and rasterizers differ slightly between compilers
// and drivers). ILLUSION_UPDATE_GOLDENS=1 rewrites the golden; captures go to render-captures/.
void golden(std::string_view name, UVec2 size, const std::vector<std::byte>& pixels)
{
    Image image;
    image.mips.push_back(ImageMip{size.x, size.y, 0, pixels.size()});
    image.data = pixels;
    const std::vector<std::byte> encoded = encodePng(image);
    const auto write = [&](const std::filesystem::path& path) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        REQUIRE(stream.good());
    };
    const std::string filename = std::format("{}-{}x{}.png", name, size.x, size.y);
    write(std::filesystem::path("render-captures") / filename);
    const std::filesystem::path reference = utf8Path(ILLUSION_TEST_DATA_DIR) / "render" / filename;
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    if (update != nullptr && std::string_view(update) == "1")
    {
        write(reference);
    }
    std::ifstream stream(reference, std::ios::binary);
    REQUIRE_MESSAGE(stream.good(), reference.string());
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    const auto expected = decodeImage(std::as_bytes(std::span(bytes)), false);
    REQUIRE(expected.has_value());
    REQUIRE(expected->data.size() == pixels.size());
    u64 total = 0;
    usize different = 0;
    for (usize i = 0; i < pixels.size(); i += 4)
    {
        int worst = 0;
        for (usize c = 0; c < 4; ++c)
        {
            const int d =
                std::abs(std::to_integer<int>(pixels[i + c]) - std::to_integer<int>(expected->data[i + c]));
            total += static_cast<u64>(d);
            worst = std::max(worst, d);
        }
        different += worst > 24 ? 1u : 0u;
    }
    const f64 mean = static_cast<f64>(total) / static_cast<f64>(pixels.size());
    const f64 fraction = static_cast<f64>(different) / static_cast<f64>(pixels.size() / 4);
    INFO(filename << ": mean difference " << mean << ", pixels off " << fraction * 100.0 << "%");
    CHECK(mean < 1.0);
    CHECK(fraction < 0.003);
}

struct Scene
{
    UiFontFace face;
    UiTextureId panel;
    Vec2 panelSize{16.0f};
};

void textScene(UiDrawList& list, const Scene& scene)
{
    list.rect({{60.0f, 60.0f}, {1860.0f, 1020.0f}}, Vec4(0.214f, 0.214f, 0.214f, 1.0f));
    UiTextStyle style;
    style.size = 12.0f;
    CHECK(list.text(scene.face, {100.0f, 100.0f}, "Small text at 12 units: The quick brown fox.", style));
    style.size = 24.0f;
    CHECK(list.text(scene.face, {100.0f, 140.0f}, "Body text at 24 units: jumps over the lazy dog.", style));
    style.size = 96.0f;
    CHECK(list.text(scene.face, {100.0f, 200.0f}, "Title 96 AVWg", style));
    style.size = 32.0f;
    style.align = UiTextAlign::Center;
    style.wrapWidth = 800.0f;
    style.color = Vec4(1.0f, 0.8f, 0.2f, 1.0f);
    CHECK(list.text(
        scene.face, {560.0f, 420.0f},
        "Wrapped and centred text breaks at spaces inside an 800 unit wide column, line after line.", style));
}

void panelScene(UiDrawList& list, const Scene& scene)
{
    list.rect({{100.0f, 100.0f}, {900.0f, 700.0f}}, Vec4(1.0f));
    list.rect({{300.0f, 300.0f}, {1300.0f, 900.0f}}, Vec4(0.0f, 0.0f, 0.0f, 0.5f));
    CHECK(list.nineSlice({{1000.0f, 150.0f}, {1800.0f, 550.0f}}, scene.panel, scene.panelSize,
                         UiEdges{4.0f, 4.0f, 4.0f, 4.0f}, Vec4(1.0f), 8.0f));
    CHECK(list.nineSlice({{1400.0f, 650.0f}, {1600.0f, 1000.0f}}, scene.panel, scene.panelSize,
                         UiEdges{4.0f, 4.0f, 4.0f, 4.0f}, Vec4(1.0f, 0.5f, 0.5f, 1.0f), 4.0f));
}

void clipScene(UiDrawList& list, const Scene& scene)
{
    list.rect({{200.0f, 200.0f}, {1000.0f, 600.0f}}, Vec4(0.05f, 0.05f, 0.1f, 1.0f));
    REQUIRE(list.pushClip({{200.0f, 200.0f}, {1000.0f, 600.0f}}));
    UiTextStyle style;
    style.size = 120.0f;
    CHECK(list.text(scene.face, {120.0f, 520.0f}, "Clipped text", style));
    CHECK(list.image({{800.0f, 120.0f}, {1200.0f, 420.0f}}, scene.panel));
    list.popClip();
    CHECK(list.image({{1300.0f, 300.0f}, {1500.0f, 500.0f}}, scene.panel));
}

} // namespace

TEST_CASE("the UI pass draws text, nine-slice panels and clipped content at two target sizes")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto ring = ReadbackRing::create(*device);
        REQUIRE(ring != nullptr);
        auto ui = UiRenderer::create(*device, resources, srgbDesc());
        REQUIRE(ui != nullptr);
        Scene scene;
        scene.face = UiFontFace{&test::testFont(), ui->addFont(test::testFont())};
        REQUIRE(scene.face.atlas.value != 0);
        const TextureHandle panel = resources.createTexture(panelImage(), "UI panel");
        scene.panel = ui->addImage(panel);
        REQUIRE(scene.panel.value != 0);
        CHECK(scene.panel != scene.face.atlas);

        UiDrawList list;
        for (const UVec2 size : kSizes)
        {
            CAPTURE(size.x);
            nvrhi::TextureHandle target = makeTarget(*device, size);
            const UiCanvasMetrics canvas = canvasMetrics({}, size);
            const auto run = [&](const char* name, void (*build)(UiDrawList&, const Scene&)) {
                list.begin(canvas);
                build(list, scene);
                CHECK(list.droppedPrimitives() == 0);
                CHECK(list.missingGlyphs() == 0);
                bool rendered = false;
                const std::vector<std::byte> pixels =
                    draw(*device, *ring, *ui, target, list.data(), &rendered);
                CHECK(rendered);
                golden(name, size, pixels);
                return pixels;
            };

            const std::vector<std::byte> text = run("ui-text", textScene);
            int brightest = 0;
            for (usize i = 0; i < text.size(); i += 4)
            {
                brightest = std::max(brightest, std::to_integer<int>(text[i]));
            }
            CHECK(brightest >= 250); // white text stays white over grey

            const std::vector<std::byte> panels = run("ui-panel", panelScene);
            // Linear 1.0 under 50% black is linear 0.5, sRGB 188: blended in linear light, not darker.
            const f32 scale = canvas.pixelsPerUnit;
            const auto at = [&](f32 x, f32 y, u32 c) {
                return channel(panels, size, static_cast<u32>(x * scale), static_cast<u32>(y * scale), c);
            };
            CHECK(std::abs(at(500.0f, 500.0f, 0) - 188) <= 3);
            CHECK(at(200.0f, 200.0f, 0) >= 254);
            CHECK(std::abs(at(1100.0f, 800.0f, 0) - 89) <= 3); // linear 0.2 (the clear colour) halved

            const std::vector<std::byte> clipped = run("ui-clip", clipScene);
            const auto clippedAt = [&](f32 x, f32 y) {
                return channel(clipped, size, static_cast<u32>(x * scale), static_cast<u32>(y * scale), 2);
            };
            CHECK(clippedAt(1100.0f, 300.0f) == channel(clipped, size, 5, 5, 2)); // the image's cut-off part
            CHECK(clippedAt(1310.0f, 400.0f) > 190);                              // the unclipped image
        }
        ui->remove(scene.panel);
        ui->remove(scene.face.atlas);
        resources.releaseTexture(panel);
        device->waitIdle();
    }
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("the UI pass skips unknown textures, reuses removed ids and rejects what it cannot draw")
{
    auto device = RenderDevice::create(headless());
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto ring = ReadbackRing::create(*device);
        REQUIRE(ring != nullptr);
        UiRendererDesc desc;
        desc.maxVertices = 64;
        desc.maxIndices = 96;
        desc.maxTextures = 2;
        desc.outputFormat = kFormat;
        auto ui = UiRenderer::create(*device, resources, desc);
        REQUIRE(ui != nullptr);
        const UVec2 size(64, 64);
        nvrhi::TextureHandle target = makeTarget(*device, size);
        const UiCanvasMetrics canvas = canvasMetrics({UiScaleMode::Pixels}, size);
        UiDrawList list;

        SUBCASE("commands with unknown or removed textures draw nothing")
        {
            const UiTextureId font = ui->addFont(test::testFont());
            ui->remove(font);
            list.begin(canvas);
            CHECK(list.image({{0.0f, 0.0f}, {64.0f, 64.0f}}, UiTextureId{77}));
            CHECK(list.image({{0.0f, 0.0f}, {64.0f, 64.0f}}, font));
            bool rendered = false;
            const std::vector<std::byte> pixels = draw(*device, *ring, *ui, target, list.data(), &rendered);
            CHECK(rendered);
            CHECK(channel(pixels, size, 32, 32, 0) == kClear); // the clear colour
        }
        SUBCASE("removed ids are reused and the table has a fixed size")
        {
            const UiTextureId font = ui->addFont(test::testFont());
            const TextureHandle white =
                resources.createTexture(makeSolidImage(4, 4, Vec4(1.0f), ImageFormat::RGBA8));
            const UiTextureId image = ui->addImage(white);
            CHECK(font.value != 0);
            CHECK(image.value != 0);
            CHECK(ui->addFont(test::testFont()).value == 0); // full
            ui->remove(font);
            CHECK(ui->addImage(TextureHandle{}).value == 0); // not a texture
            const UiTextureId again = ui->addImage(white);
            CHECK(again == font);
            CHECK(ui->addFont(Font{}).value == 0); // empty
            list.begin(canvas);
            CHECK(list.image({{0.0f, 0.0f}, {64.0f, 64.0f}}, again, Vec4(1.0f, 0.0f, 0.0f, 1.0f)));
            bool rendered = false;
            const std::vector<std::byte> pixels = draw(*device, *ring, *ui, target, list.data(), &rendered);
            CHECK(rendered);
            CHECK(channel(pixels, size, 32, 32, 0) == 255);
            CHECK(channel(pixels, size, 32, 32, 1) == 0);

            // A released image is skipped even while it is still registered.
            resources.releaseTexture(white);
            const std::vector<std::byte> released = draw(*device, *ring, *ui, target, list.data(), &rendered);
            CHECK(rendered);
            CHECK(channel(released, size, 32, 32, 0) == kClear);
            ui->remove(again);
            ui->remove(image);
        }
        SUBCASE("data beyond the capacities and incompatible targets are not drawn")
        {
            list.begin(canvas);
            for (int i = 0; i < 17; ++i)
            {
                CHECK(list.rect({{0.0f, 0.0f}, {64.0f, 64.0f}}, Vec4(1.0f)));
            }
            bool rendered = true;
            std::vector<std::byte> pixels = draw(*device, *ring, *ui, target, list.data(), &rendered);
            CHECK_FALSE(rendered);
            CHECK(channel(pixels, size, 32, 32, 0) == kClear);

            list.begin(canvas);
            CHECK(list.rect({{0.0f, 0.0f}, {64.0f, 64.0f}}, Vec4(1.0f)));
            nvrhi::TextureHandle unorm = makeTarget(*device, size, nvrhi::Format::RGBA8_UNORM);
            pixels = draw(*device, *ring, *ui, unorm, list.data(), &rendered);
            CHECK_FALSE(rendered);
            nvrhi::CommandListHandle commands = device->device()->createCommandList();
            commands->open();
            CHECK_FALSE(ui->render(commands, nullptr, list.data()));
            CHECK_FALSE(ui->render(nullptr, target, list.data()));
            CHECK(ui->render(commands, target,
                             UiDrawData{.vertices = {}, .indices = {}, .commands = {}, .canvas = canvas}));
            commands->close();
            device->device()->executeCommandList(commands);
        }
        SUBCASE("a UNORM target gets the same colours as an sRGB one")
        {
            UiRendererDesc unormDesc;
            unormDesc.outputFormat = nvrhi::Format::RGBA8_UNORM;
            auto unormUi = UiRenderer::create(*device, resources, unormDesc);
            REQUIRE(unormUi != nullptr);
            nvrhi::TextureHandle unorm = makeTarget(*device, size, nvrhi::Format::RGBA8_UNORM);
            list.begin(canvas);
            CHECK(list.rect({{0.0f, 0.0f}, {64.0f, 64.0f}}, Vec4(0.5f, 0.2f, 1.0f, 1.0f)));
            bool rendered = false;
            const std::vector<std::byte> srgb = draw(*device, *ring, *ui, target, list.data(), &rendered);
            CHECK(rendered);
            const std::vector<std::byte> encoded =
                draw(*device, *ring, *unormUi, unorm, list.data(), &rendered, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
            CHECK(rendered);
            for (u32 c = 0; c < 3; ++c)
            {
                CHECK(std::abs(channel(srgb, size, 32, 32, c) - channel(encoded, size, 32, 32, c)) <= 1);
            }
        }
        device->waitIdle();
    }
    CHECK(device->validationErrorCount() == 0);
}

TEST_CASE("a steady frame of the UI pass allocates only what NVRHI allocates for its barriers")
{
    // NVRHI allocates the state tracking of each resource a command list touches first, its validation layer
    // allocates per call, and every barrier commit (the vertex and index uploads need two) builds a local
    // vector. So the pass is measured without the validation layer on its second render() into a command
    // list: what remains is the same small number in every frame.
    RenderDeviceDesc desc = headless();
    desc.validation = ValidationMode::Disabled;
    auto device = RenderDevice::create(desc);
    REQUIRE(device != nullptr);
    {
        RenderResources resources(*device);
        auto ui = UiRenderer::create(*device, resources, srgbDesc());
        REQUIRE(ui != nullptr);
        Scene scene;
        scene.face = UiFontFace{&test::testFont(), ui->addFont(test::testFont())};
        scene.panel = ui->addImage(resources.createTexture(panelImage(), "UI panel"));
        const UVec2 size(1280, 720);
        nvrhi::TextureHandle target = makeTarget(*device, size);
        UiDrawList full;
        full.begin(canvasMetrics({}, size));
        textScene(full, scene);
        panelScene(full, scene);
        clipScene(full, scene);
        REQUIRE(full.data().commands.size() >= 8);

        nvrhi::IDevice* gpu = device->device();
        nvrhi::CommandListHandle commands = gpu->createCommandList();
        const auto steady = [&](const UiDrawData& data) {
            std::vector<usize> counts;
            counts.reserve(8);
            for (int frame = 0; frame < 8; ++frame)
            {
                commands->open();
                CHECK(ui->render(commands, target, data));
                usize allocations = 0;
                {
                    const test::ScopedAllocationCounter counter;
                    CHECK(ui->render(commands, target, data));
                    allocations = counter.count();
                }
                counts.push_back(allocations);
                commands->close();
                gpu->executeCommandList(commands);
                device->waitIdle();
                gpu->runGarbageCollection();
            }
            CHECK(counts[5] == counts[6]);
            CHECK(counts[6] == counts[7]);
            return counts.back();
        };
        MESSAGE("allocations of a steady render(), all NVRHI's: " << steady(full.data()));
    }
}

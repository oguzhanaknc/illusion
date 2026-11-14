// Benchmark of the UI pass: one frame of a HUD-sized draw list recorded, submitted and finished.

#include "Budget.hpp"

#include <illusion/assets/Font.hpp>
#include <illusion/assets/Image.hpp>
#include <illusion/core/Assert.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/RenderResources.hpp>
#include <illusion/render/UiRenderer.hpp>
#include <illusion/ui/UiDrawList.hpp>

#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace illusion;

// Budgets are roughly 5-10x a desktop result (see Budget.hpp).

ILL_BENCHMARK("ui/render-hud", 500'000.0)
{
    RenderDeviceDesc desc;
    desc.headless = true;
    desc.validation = ValidationMode::Disabled;
    const std::unique_ptr<RenderDevice> device = RenderDevice::create(desc);
    ILL_CHECK(device != nullptr, "the GPU benchmarks need a Vulkan 1.3 device");
    {
        std::ifstream file(utf8Path(ILLUSION_TEST_FONT), std::ios::binary);
        const std::vector<char> chars((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
        const Font font = std::move(Font::bake(std::as_bytes(std::span(chars)))).value();

        RenderResources resources(*device);
        auto ui = UiRenderer::create(*device, resources);
        ILL_CHECK(ui != nullptr, "cannot create the UI pass");
        const UiFontFace face{&font, ui->addFont(font)};
        const UiTextureId icons = ui->addImage(
            resources.createTexture(makeSolidImage(64, 64, Vec4(1.0f), ImageFormat::RGBA8Srgb), "Icons"));
        const UiTextureId panel = ui->addImage(resources.createTexture(
            makeSolidImage(16, 16, Vec4(0.2f, 0.3f, 0.6f, 0.8f), ImageFormat::RGBA8Srgb), "Panel"));

        nvrhi::TextureDesc targetDesc;
        targetDesc.width = 1920;
        targetDesc.height = 1080;
        targetDesc.format = nvrhi::Format::SBGRA8_UNORM;
        targetDesc.isRenderTarget = true;
        targetDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::RenderTarget);
        nvrhi::TextureHandle target = device->device()->createTexture(targetDesc);

        // About 200 quads: 4 panels (36), 50 icons, a bar and roughly 110 glyphs.
        UiDrawList list;
        list.begin(canvasMetrics({}, {1920, 1080}));
        UiTextStyle style;
        style.size = 28.0f;
        for (u32 i = 0; i < 4; ++i)
        {
            const Vec2 origin(40.0f + static_cast<f32>(i) * 460.0f, 40.0f);
            (void)list.nineSlice({origin, origin + Vec2(420.0f, 120.0f)}, panel, Vec2(16.0f),
                                 UiEdges{4.0f, 4.0f, 4.0f, 4.0f});
            (void)list.text(face, origin + Vec2(20.0f, 16.0f), std::format("Player {} 12,480", i + 1), style);
            (void)list.text(face, origin + Vec2(20.0f, 64.0f), "HP 100  AMMO 30/90", style);
        }
        for (u32 i = 0; i < 50; ++i)
        {
            const Vec2 origin(40.0f + static_cast<f32>(i % 25) * 76.0f,
                              900.0f + static_cast<f32>(i / 25) * 76.0f);
            (void)list.image({origin, origin + Vec2(64.0f)}, icons);
        }
        (void)list.rect({{660.0f, 840.0f}, {1260.0f, 870.0f}}, Vec4(0.8f, 0.1f, 0.1f, 0.9f));
        const UiDrawData data = list.data();

        nvrhi::IDevice* gpu = device->device();
        nvrhi::CommandListHandle commands = gpu->createCommandList();
        bench.batch(1).unit("frame").minEpochIterations(200).run(
            std::format("render() of {} quads with 3 textures at 1920x1080", data.vertices.size() / 4), [&] {
                commands->open();
                (void)ui->render(commands, target, data);
                commands->close();
                gpu->executeCommandList(commands);
                gpu->waitForIdle();
                gpu->runGarbageCollection();
            });
    }
    device->waitIdle();
}

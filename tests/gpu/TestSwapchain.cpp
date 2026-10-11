#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/DebugTriangle.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/Swapchain.hpp>

#include <doctest/doctest.h>

using namespace illusion;

// Needs a display: a desktop session locally, Xvfb in CI.

TEST_CASE("a swapchain presents frames and follows window resizes and vsync changes")
{
    auto platform = Platform::create();
    REQUIRE(platform != nullptr);
    WindowDesc windowDesc;
    windowDesc.title = "illusion swapchain test";
    windowDesc.width = 320;
    windowDesc.height = 240;
    auto window = Window::create(*platform, windowDesc);
    REQUIRE(window != nullptr);

    RenderDeviceDesc deviceDesc;
    deviceDesc.applicationName = "illusion GPU tests";
    deviceDesc.validation = ValidationMode::Enabled;
    auto device = RenderDevice::create(deviceDesc);
    REQUIRE(device != nullptr);
    CHECK(device->canPresent());
    {
        auto swapchain = Swapchain::create(*device, *window);
        REQUIRE(swapchain != nullptr);
        CHECK(swapchain->imageCount() >= 2);
        CHECK(swapchain->format() != nvrhi::Format::UNKNOWN);

        auto triangle =
            DebugTriangle::create(*device, nvrhi::FramebufferInfo().addColorFormat(swapchain->format()));
        REQUIRE(triangle != nullptr);
        nvrhi::CommandListHandle commands = device->device()->createCommandList();

        u32 presented = 0;
        for (u32 frame = 0; frame < 90; ++frame)
        {
            if (frame == 20)
            {
                window->setSize(UVec2(400, 300));
            }
            if (frame == 40)
            {
                window->setSize(UVec2(200, 150));
            }
            if (frame == 60)
            {
                swapchain->setVsync(false);
            }
            platform->pollEvents();
            if (!swapchain->beginFrame())
            {
                continue;
            }
            commands->open();
            commands->clearTextureFloat(swapchain->currentBackBuffer(), nvrhi::AllSubresources,
                                        nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
            triangle->draw(commands, swapchain->currentFramebuffer());
            commands->close();
            device->device()->executeCommandList(commands);
            swapchain->present();
            ++presented;
        }
        CHECK(presented > 80);
        CHECK(swapchain->size() == window->framebufferSize());
        device->waitIdle();
    }
    device->waitIdle();
    CHECK(device->validationErrorCount() == 0);
}

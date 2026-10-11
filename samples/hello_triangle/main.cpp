// hello_triangle: opens a window and renders the diagnostic triangle every frame.
//
// Usage: illusion_hello_triangle [--frames N] [--no-vsync]
//   --frames N   exit after N frames (for automated smoke runs); exit code 2 on validation errors
//   --no-vsync   present without waiting for vertical blank
// Press Escape or close the window to quit.

#include <illusion/core/EventBus.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/DebugTriangle.hpp>
#include <illusion/render/RenderDevice.hpp>
#include <illusion/render/Swapchain.hpp>

#include <charconv>
#include <cstring>
#include <format>
#include <string_view>

using namespace illusion;

int main(int argc, char** argv)
{
    u64 frameLimit = 0;
    bool vsync = true;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        if (arg == "--frames" && i + 1 < argc)
        {
            const std::string_view value = argv[++i];
            std::from_chars(value.data(), value.data() + value.size(), frameLimit);
        }
        else if (arg == "--no-vsync")
        {
            vsync = false;
        }
    }

    EventBus events;
    auto platform = Platform::create();
    if (!platform)
    {
        return 1;
    }
    WindowDesc windowDesc;
    windowDesc.title = "illusion - hello triangle";
    auto window = Window::create(*platform, windowDesc, &events);
    if (!window)
    {
        return 1;
    }

    auto device = RenderDevice::create();
    if (!device)
    {
        return 1;
    }
    SwapchainDesc swapchainDesc;
    swapchainDesc.vsync = vsync;
    auto swapchain = Swapchain::create(*device, *window, swapchainDesc);
    if (!swapchain)
    {
        return 1;
    }
    auto triangle =
        DebugTriangle::create(*device, nvrhi::FramebufferInfo().addColorFormat(swapchain->format()));
    if (!triangle)
    {
        return 1;
    }
    nvrhi::CommandListHandle commands = device->device()->createCommandList();

    FrameClock clock;
    Stopwatch titleTimer;
    u64 frames = 0;
    u64 framesSinceTitle = 0;
    while (!window->shouldClose() && (frameLimit == 0 || frames < frameLimit))
    {
        platform->pollEvents();
        events.dispatchQueued();
        clock.tick(monotonicSeconds());
        if (window->input().wasPressed(Key::Escape))
        {
            window->setShouldClose(true);
        }

        if (swapchain->beginFrame())
        {
            commands->open();
            commands->clearTextureFloat(swapchain->currentBackBuffer(), nvrhi::AllSubresources,
                                        nvrhi::Color(0.02f, 0.02f, 0.03f, 1.0f));
            triangle->draw(commands, swapchain->currentFramebuffer());
            commands->close();
            device->device()->executeCommandList(commands);
            swapchain->present();
        }
        ++frames;
        ++framesSinceTitle;
        ILL_PROFILE_FRAME();

        if (titleTimer.elapsedSeconds() >= 0.5)
        {
            const f64 fps = static_cast<f64>(framesSinceTitle) / titleTimer.restart();
            framesSinceTitle = 0;
            window->setTitle(
                std::format("illusion - hello triangle - {} - {:.0f} FPS", device->gpu().name, fps));
        }
    }

    device->waitIdle();
    const u32 errors = device->validationErrorCount();
    ILL_LOG_INFO("Sample", "rendered {} frames, {} validation error(s)", frames, errors);
    return errors == 0 ? 0 : 2;
}

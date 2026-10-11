#pragma once

/// @file
/// Presents rendered images to a Window.
///
/// ```cpp
/// auto swapchain = Swapchain::create(*device, *window);
/// while (running)
/// {
///     platform->pollEvents();
///     if (swapchain->beginFrame())   // false while minimized
///     {
///         render(swapchain->currentFramebuffer());
///         swapchain->present();
///     }
/// }
/// ```
///
/// The swapchain follows the window's framebuffer size automatically: when the window is resized,
/// beginFrame() recreates the swapchain images.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>

namespace illusion
{

class RenderDevice;
class Window;

/// Creation parameters for Swapchain.
struct SwapchainDesc
{
    bool vsync = true;      ///< Wait for vertical blank (FIFO). Without vsync, mailbox or immediate mode.
    u32 framesInFlight = 2; ///< Frames the CPU may record ahead of the GPU (1-4).
};

/// Swapchain for one window. Use on the thread that renders (normally the main thread).
class Swapchain : NonCopyable
{
public:
    /// Creates a swapchain for @p window. Returns nullptr (and logs why) on failure. The device must not
    /// be headless, and both @p device and @p window must outlive the swapchain.
    [[nodiscard]] static std::unique_ptr<Swapchain> create(RenderDevice& device, Window& window,
                                                           const SwapchainDesc& desc = {});

    /// Waits for the GPU and destroys the swapchain.
    ~Swapchain();

    /// Waits until a frame slot is free, then acquires the next back buffer. Returns false if nothing
    /// should be rendered this frame (the window is minimized). Recreates the swapchain when the window
    /// size changed.
    [[nodiscard]] bool beginFrame();

    /// Queues the current back buffer for presentation. Call once after a successful beginFrame().
    void present();

    /// Back buffer acquired by beginFrame().
    [[nodiscard]] nvrhi::ITexture* currentBackBuffer() const noexcept;

    /// Framebuffer wrapping currentBackBuffer().
    [[nodiscard]] nvrhi::IFramebuffer* currentFramebuffer() const noexcept;

    /// Format of the back buffers (an sRGB format when the display supports one).
    [[nodiscard]] nvrhi::Format format() const noexcept;

    /// Size of the back buffers in pixels.
    [[nodiscard]] UVec2 size() const noexcept;

    /// Number of back buffers.
    [[nodiscard]] u32 imageCount() const noexcept;

    /// Changes vsync; takes effect at the next beginFrame().
    void setVsync(bool vsync) noexcept;

private:
    Swapchain();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

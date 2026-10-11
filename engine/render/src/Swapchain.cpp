#include "RenderDeviceImpl.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/platform/Window.hpp>
#include <illusion/render/Swapchain.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <deque>
#include <limits>
#include <vector>

namespace illusion
{
namespace
{

struct SurfaceFormatChoice
{
    VkSurfaceFormatKHR format;
    nvrhi::Format nvrhiFormat;
};

// Preferred back buffer formats, best first. sRGB formats let shaders write linear colour.
constexpr SurfaceFormatChoice kFormatPreference[] = {
    {{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, nvrhi::Format::SBGRA8_UNORM},
    {{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, nvrhi::Format::SRGBA8_UNORM},
    {{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, nvrhi::Format::BGRA8_UNORM},
    {{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, nvrhi::Format::RGBA8_UNORM},
};

} // namespace

struct Swapchain::Impl
{
    RenderDevice::Impl* device = nullptr;
    Window* window = nullptr;
    SwapchainDesc desc;
    bool vsyncRequested = true;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR surfaceFormat{};
    nvrhi::Format format = nvrhi::Format::UNKNOWN;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    UVec2 extent{0, 0};
    // What the current swapchain was created for. Compared each frame instead of extent/presentMode,
    // which the driver may legitimately choose differently (no recreation loop).
    UVec2 createdForSize{0, 0};
    bool createdWithVsync = true;

    std::vector<nvrhi::TextureHandle> backBuffers;
    std::vector<nvrhi::FramebufferHandle> framebuffers;
    // One "image available" semaphore per acquire in flight, and one "render finished" semaphore per
    // swapchain image (a present semaphore may only be reused once its image is acquired again).
    std::vector<VkSemaphore> acquireSemaphores;
    std::vector<VkSemaphore> presentSemaphores;
    u32 acquireIndex = 0;
    u32 imageIndex = 0;
    bool frameActive = false;
    bool needsRecreate = false;

    nvrhi::CommandListHandle signalList; // empty submission that carries the present semaphore signal
    std::deque<nvrhi::EventQueryHandle> framesInFlight;
    std::vector<nvrhi::EventQueryHandle> queryPool;

    ~Impl();
    bool createSwapchain();
    void destroySwapchain();
    void destroySemaphores();
    bool recreate();
};

Swapchain::Impl::~Impl()
{
    auto& d = detail::vk();
    if (device != nullptr && device->device != VK_NULL_HANDLE)
    {
        device->validatedDevice->waitForIdle();
    }
    framesInFlight.clear();
    queryPool.clear();
    signalList = nullptr;
    destroySwapchain();
    if (surface != VK_NULL_HANDLE)
    {
        d.vkDestroySurfaceKHR(device->instance, surface, nullptr);
    }
}

void Swapchain::Impl::destroySemaphores()
{
    auto& d = detail::vk();
    for (VkSemaphore semaphore : acquireSemaphores)
    {
        d.vkDestroySemaphore(device->device, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : presentSemaphores)
    {
        d.vkDestroySemaphore(device->device, semaphore, nullptr);
    }
    acquireSemaphores.clear();
    presentSemaphores.clear();
}

void Swapchain::Impl::destroySwapchain()
{
    framebuffers.clear();
    backBuffers.clear();
    if (device != nullptr)
    {
        device->validatedDevice->runGarbageCollection();
        destroySemaphores();
        if (swapchain != VK_NULL_HANDLE)
        {
            detail::vk().vkDestroySwapchainKHR(device->device, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }
    }
}

bool Swapchain::Impl::createSwapchain()
{
    ILL_PROFILE_SCOPE();
    // Stays set unless creation succeeds, so a failed attempt (minimized, surface lost) is retried by the
    // next beginFrame() instead of acquiring from a swapchain without images or semaphores.
    needsRecreate = true;
    auto& d = detail::vk();
    VkPhysicalDevice physical = device->physicalDevice;

    VkSurfaceCapabilitiesKHR capabilities{};
    d.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);

    u32 formatCount = 0;
    d.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    d.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());
    format = nvrhi::Format::UNKNOWN;
    for (const SurfaceFormatChoice& choice : kFormatPreference)
    {
        const bool supported = std::any_of(formats.begin(), formats.end(), [&](const VkSurfaceFormatKHR& f) {
            return f.format == choice.format.format && f.colorSpace == choice.format.colorSpace;
        });
        if (supported)
        {
            surfaceFormat = choice.format;
            format = choice.nvrhiFormat;
            break;
        }
    }
    if (format == nvrhi::Format::UNKNOWN)
    {
        ILL_LOG_ERROR("Render", "the window surface supports no 8-bit RGBA/BGRA format");
        return false;
    }

    u32 modeCount = 0;
    d.vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    d.vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &modeCount, modes.data());
    auto hasMode = [&](VkPresentModeKHR mode) {
        return std::find(modes.begin(), modes.end(), mode) != modes.end();
    };
    presentMode = VK_PRESENT_MODE_FIFO_KHR; // always supported
    if (!vsyncRequested)
    {
        if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR))
        {
            presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        }
        else if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR))
        {
            presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }

    const UVec2 framebufferSize = window->framebufferSize();
    createdForSize = framebufferSize;
    createdWithVsync = vsyncRequested;
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max())
    {
        extent = UVec2(capabilities.currentExtent.width, capabilities.currentExtent.height);
    }
    else
    {
        extent = glm::clamp(framebufferSize,
                            UVec2(capabilities.minImageExtent.width, capabilities.minImageExtent.height),
                            UVec2(capabilities.maxImageExtent.width, capabilities.maxImageExtent.height));
    }
    if (extent.x == 0 || extent.y == 0)
    {
        return false; // minimized
    }

    u32 imageCount = std::max(capabilities.minImageCount + 1, desc.framesInFlight + 1);
    if (capabilities.maxImageCount != 0)
    {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info =
        detail::vkStruct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = VkExtent2D{extent.x, extent.y};
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) == 0)
    {
        info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = swapchain;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    const VkResult result = d.vkCreateSwapchainKHR(device->device, &info, nullptr, &created);
    if (swapchain != VK_NULL_HANDLE)
    {
        d.vkDestroySwapchainKHR(device->device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    if (result != VK_SUCCESS)
    {
        ILL_LOG_ERROR("Render", "vkCreateSwapchainKHR failed: {}", detail::resultName(result));
        return false;
    }
    swapchain = created;

    u32 count = 0;
    d.vkGetSwapchainImagesKHR(device->device, swapchain, &count, nullptr);
    std::vector<VkImage> images(count);
    d.vkGetSwapchainImagesKHR(device->device, swapchain, &count, images.data());

    nvrhi::IDevice* nvrhiDevice = device->validatedDevice.Get();
    for (VkImage image : images)
    {
        nvrhi::TextureDesc textureDesc;
        textureDesc.width = extent.x;
        textureDesc.height = extent.y;
        textureDesc.format = format;
        textureDesc.debugName = "Swapchain back buffer";
        textureDesc.isRenderTarget = true;
        textureDesc.enableAutomaticStateTracking(nvrhi::ResourceStates::Present);
        nvrhi::TextureHandle texture = device->nvrhiDevice->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), textureDesc);
        backBuffers.push_back(texture);
        framebuffers.push_back(
            nvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
    }

    VkSemaphoreCreateInfo semaphoreInfo =
        detail::vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    acquireSemaphores.resize(count + 1);
    presentSemaphores.resize(count);
    for (VkSemaphore& semaphore : acquireSemaphores)
    {
        d.vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore);
    }
    for (VkSemaphore& semaphore : presentSemaphores)
    {
        d.vkCreateSemaphore(device->device, &semaphoreInfo, nullptr, &semaphore);
    }
    acquireIndex = 0;
    needsRecreate = false;
    ILL_LOG_DEBUG("Render", "swapchain {}x{}, {} images, present mode {}", extent.x, extent.y, count,
                  static_cast<int>(presentMode));
    return true;
}

bool Swapchain::Impl::recreate()
{
    device->validatedDevice->waitForIdle();
    framebuffers.clear();
    backBuffers.clear();
    device->validatedDevice->runGarbageCollection();
    destroySemaphores();
    // createSwapchain() passes the old swapchain as oldSwapchain and destroys it afterwards.
    return createSwapchain();
}

Swapchain::Swapchain()
    : m_impl(std::make_unique<Impl>())
{
}

std::unique_ptr<Swapchain> Swapchain::create(RenderDevice& renderDevice, Window& window,
                                             const SwapchainDesc& desc)
{
    ILL_PROFILE_SCOPE();
    if (!renderDevice.canPresent())
    {
        ILL_LOG_ERROR("Render", "cannot create a swapchain on a headless RenderDevice");
        return nullptr;
    }
    ILL_ASSERT(desc.framesInFlight >= 1 && desc.framesInFlight <= 4, "framesInFlight must be 1-4");

    std::unique_ptr<Swapchain> swapchain(new Swapchain());
    Impl& impl = *swapchain->m_impl;
    impl.device = renderDevice.m_impl.get();
    impl.window = &window;
    impl.desc = desc;
    impl.desc.framesInFlight = std::clamp(desc.framesInFlight, 1u, 4u);
    impl.vsyncRequested = desc.vsync;

    const VkResult result = glfwCreateWindowSurface(
        impl.device->instance, static_cast<GLFWwindow*>(window.nativeHandle()), nullptr, &impl.surface);
    if (result != VK_SUCCESS)
    {
        ILL_LOG_ERROR("Render", "glfwCreateWindowSurface failed: {}", detail::resultName(result));
        return nullptr;
    }
    VkBool32 presentSupported = VK_FALSE;
    detail::vk().vkGetPhysicalDeviceSurfaceSupportKHR(
        impl.device->physicalDevice, impl.device->graphicsFamily, impl.surface, &presentSupported);
    if (presentSupported != VK_TRUE)
    {
        ILL_LOG_ERROR("Render", "the selected GPU cannot present to this window");
        return nullptr;
    }

    impl.signalList = impl.device->nvrhiDevice->createCommandList();
    // A zero-sized (minimized) window is fine: the swapchain is created by the first beginFrame() that
    // sees a non-zero size.
    if (window.framebufferSize().x > 0 && window.framebufferSize().y > 0 && !impl.createSwapchain())
    {
        return nullptr;
    }
    return swapchain;
}

Swapchain::~Swapchain() = default;

bool Swapchain::beginFrame()
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    ILL_ASSERT(!impl.frameActive, "beginFrame() called twice without present()");
    auto& d = detail::vk();
    nvrhi::IDevice* nvrhiDevice = impl.device->validatedDevice.Get();

    // Frame pacing: wait until the GPU finished the frame that used this slot.
    while (impl.framesInFlight.size() >= impl.desc.framesInFlight)
    {
        nvrhi::EventQueryHandle query = impl.framesInFlight.front();
        impl.framesInFlight.pop_front();
        nvrhiDevice->waitEventQuery(query);
        impl.queryPool.push_back(query);
    }

    const UVec2 framebufferSize = impl.window->framebufferSize();
    if (framebufferSize.x == 0 || framebufferSize.y == 0)
    {
        return false; // minimized: render nothing, keep the old swapchain
    }
    if (impl.swapchain == VK_NULL_HANDLE || impl.needsRecreate || framebufferSize != impl.createdForSize ||
        impl.vsyncRequested != impl.createdWithVsync)
    {
        if (!impl.recreate())
        {
            return false;
        }
    }

    if (impl.acquireSemaphores.empty())
    {
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        VkSemaphore semaphore = impl.acquireSemaphores[impl.acquireIndex];
        const VkResult result =
            d.vkAcquireNextImageKHR(impl.device->device, impl.swapchain, std::numeric_limits<u64>::max(),
                                    semaphore, VK_NULL_HANDLE, &impl.imageIndex);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)
        {
            impl.acquireIndex = (impl.acquireIndex + 1) % static_cast<u32>(impl.acquireSemaphores.size());
            impl.needsRecreate = result == VK_SUBOPTIMAL_KHR;
            impl.device->nvrhiDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
            impl.frameActive = true;
            return true;
        }
        if (result != VK_ERROR_OUT_OF_DATE_KHR || !impl.recreate())
        {
            if (result != VK_ERROR_OUT_OF_DATE_KHR)
            {
                ILL_LOG_ERROR("Render", "vkAcquireNextImageKHR failed: {}", detail::resultName(result));
            }
            return false;
        }
    }
    return false;
}

void Swapchain::present()
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    ILL_ASSERT(impl.frameActive, "present() without a successful beginFrame()");
    impl.frameActive = false;
    nvrhi::IDevice* nvrhiDevice = impl.device->validatedDevice.Get();

    VkSemaphore renderFinished = impl.presentSemaphores[impl.imageIndex];
    impl.device->nvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, renderFinished, 0);
    // Submit an empty command list so the pending semaphore wait and signal reach the queue.
    impl.signalList->open();
    impl.signalList->close();
    impl.device->nvrhiDevice->executeCommandList(impl.signalList);

    VkPresentInfoKHR info = detail::vkStruct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &renderFinished;
    info.swapchainCount = 1;
    info.pSwapchains = &impl.swapchain;
    info.pImageIndices = &impl.imageIndex;
    const VkResult result = detail::vk().vkQueuePresentKHR(impl.device->graphicsQueue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        impl.needsRecreate = true;
    }
    else if (result != VK_SUCCESS)
    {
        ILL_LOG_ERROR("Render", "vkQueuePresentKHR failed: {}", detail::resultName(result));
    }

    nvrhi::EventQueryHandle query;
    if (!impl.queryPool.empty())
    {
        query = impl.queryPool.back();
        impl.queryPool.pop_back();
        nvrhiDevice->resetEventQuery(query);
    }
    else
    {
        query = nvrhiDevice->createEventQuery();
    }
    nvrhiDevice->setEventQuery(query, nvrhi::CommandQueue::Graphics);
    impl.framesInFlight.push_back(query);
    nvrhiDevice->runGarbageCollection();
}

nvrhi::ITexture* Swapchain::currentBackBuffer() const noexcept
{
    ILL_ASSERT(m_impl->frameActive, "no frame in progress");
    return m_impl->backBuffers[m_impl->imageIndex].Get();
}

nvrhi::IFramebuffer* Swapchain::currentFramebuffer() const noexcept
{
    ILL_ASSERT(m_impl->frameActive, "no frame in progress");
    return m_impl->framebuffers[m_impl->imageIndex].Get();
}

nvrhi::Format Swapchain::format() const noexcept
{
    return m_impl->format;
}

UVec2 Swapchain::size() const noexcept
{
    return m_impl->extent;
}

u32 Swapchain::imageCount() const noexcept
{
    return static_cast<u32>(m_impl->backBuffers.size());
}

void Swapchain::setVsync(bool vsync) noexcept
{
    m_impl->vsyncRequested = vsync;
}

} // namespace illusion

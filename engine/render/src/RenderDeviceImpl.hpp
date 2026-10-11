#pragma once

// Internal state of RenderDevice, shared with Swapchain.

#include "Vulkan.hpp"

#include <illusion/render/RenderDevice.hpp>

#include <nvrhi/vulkan.h>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace illusion
{

struct RenderDevice::Impl
{
    /// Forwards NVRHI messages to the log and counts errors.
    struct NvrhiMessages final : nvrhi::IMessageCallback
    {
        std::atomic<u32>* errors = nullptr;
        void message(nvrhi::MessageSeverity severity, const char* messageText) override;
    };

    Impl() { messages.errors = &validationErrors; }
    ~Impl();
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    bool createInstance(const RenderDeviceDesc& desc);
    bool selectPhysicalDevice(const RenderDeviceDesc& desc);
    bool createLogicalDevice();
    bool createNvrhiDevice();

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    u32 graphicsFamily = 0;
    bool headless = false;
    bool validation = false;          // Vulkan validation layer active
    bool nvrhiValidationOnly = false; // validation requested, but only NVRHI's layer is available
    bool bufferDeviceAddress = false;

    std::vector<std::string> instanceExtensions;
    std::vector<std::string> deviceExtensions;
    std::vector<const char*> instanceExtensionPointers;
    std::vector<const char*> deviceExtensionPointers;

    GpuInfo gpu;
    std::atomic<u32> validationErrors{0};
    NvrhiMessages messages;
    nvrhi::vulkan::DeviceHandle nvrhiDevice; // the Vulkan backend device
    nvrhi::DeviceHandle validatedDevice;     // nvrhiDevice, or NVRHI's validation layer wrapping it
};

} // namespace illusion

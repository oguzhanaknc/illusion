#include "RenderDeviceImpl.hpp"

#include <illusion/core/Assert.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <nvrhi/validation.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string_view>

namespace illusion
{
namespace
{

std::atomic<bool> g_deviceAlive{false};

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool contains(const std::vector<std::string>& names, std::string_view name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

std::vector<std::string> availableInstanceExtensions(const char* layer)
{
    auto& d = detail::vk();
    u32 count = 0;
    d.vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    d.vkEnumerateInstanceExtensionProperties(layer, &count, properties.data());
    std::vector<std::string> names;
    names.reserve(count);
    for (const VkExtensionProperties& p : properties)
    {
        names.emplace_back(p.extensionName);
    }
    return names;
}

std::vector<std::string> availableLayers()
{
    auto& d = detail::vk();
    u32 count = 0;
    d.vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> properties(count);
    d.vkEnumerateInstanceLayerProperties(&count, properties.data());
    std::vector<std::string> names;
    for (const VkLayerProperties& p : properties)
    {
        names.emplace_back(p.layerName);
    }
    return names;
}

std::vector<std::string> availableDeviceExtensions(VkPhysicalDevice physicalDevice)
{
    auto& d = detail::vk();
    u32 count = 0;
    d.vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> properties(count);
    d.vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, properties.data());
    std::vector<std::string> names;
    names.reserve(count);
    for (const VkExtensionProperties& p : properties)
    {
        names.emplace_back(p.extensionName);
    }
    return names;
}

/// Feature structures of one physical device, chained for vkGetPhysicalDeviceFeatures2.
struct DeviceFeatures
{
    VkPhysicalDeviceVulkan13Features v13 = detail::vkStruct<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    VkPhysicalDeviceVulkan12Features v12 = detail::vkStruct<VkPhysicalDeviceVulkan12Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    VkPhysicalDeviceVulkan11Features v11 = detail::vkStruct<VkPhysicalDeviceVulkan11Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    VkPhysicalDeviceFeatures2 core =
        detail::vkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);

    DeviceFeatures() noexcept
    {
        core.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = &v13;
    }
    DeviceFeatures(const DeviceFeatures&) = delete;
    DeviceFeatures& operator=(const DeviceFeatures&) = delete;
};

struct Candidate
{
    VkPhysicalDevice device = VK_NULL_HANDLE;
    u32 graphicsFamily = 0;
    int score = -1;
    GpuInfo info;
};

int scoreDevice(VkPhysicalDeviceType type, GpuPreference preference) noexcept
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return preference == GpuPreference::HighPerformance ? 100 : 50;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return preference == GpuPreference::HighPerformance ? 50 : 100;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return 20;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return 10;
    default:
        return 1;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                   void* user)
{
    auto* errors = static_cast<std::atomic<u32>*>(user);
    const char* text = data != nullptr && data->pMessage != nullptr ? data->pMessage : "";
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
    {
        errors->fetch_add(1, std::memory_order_relaxed);
        ILL_LOG_ERROR("Vulkan", "{}", text);
    }
    else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
    {
        ILL_LOG_WARN("Vulkan", "{}", text);
    }
    return VK_FALSE;
}

} // namespace

void RenderDevice::Impl::NvrhiMessages::message(nvrhi::MessageSeverity severity, const char* messageText)
{
    switch (severity)
    {
    case nvrhi::MessageSeverity::Info:
        ILL_LOG_DEBUG("NVRHI", "{}", messageText);
        break;
    case nvrhi::MessageSeverity::Warning:
        ILL_LOG_WARN("NVRHI", "{}", messageText);
        break;
    case nvrhi::MessageSeverity::Error:
    case nvrhi::MessageSeverity::Fatal:
        errors->fetch_add(1, std::memory_order_relaxed);
        ILL_LOG_ERROR("NVRHI", "{}", messageText);
        break;
    }
}

RenderDevice::Impl::~Impl()
{
    auto& d = detail::vk();
    if (device != VK_NULL_HANDLE)
    {
        d.vkDeviceWaitIdle(device);
    }
    validatedDevice = nullptr;
    nvrhiDevice = nullptr;
    if (device != VK_NULL_HANDLE)
    {
        d.vkDestroyDevice(device, nullptr);
    }
    if (messenger != VK_NULL_HANDLE)
    {
        d.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    }
    if (instance != VK_NULL_HANDLE)
    {
        d.vkDestroyInstance(instance, nullptr);
    }
}

bool RenderDevice::Impl::createInstance(const RenderDeviceDesc& desc)
{
    const PFN_vkGetInstanceProcAddr getInstanceProcAddr = detail::loadVulkanLoader();
    if (getInstanceProcAddr == nullptr)
    {
        ILL_LOG_ERROR("Render", "no Vulkan loader found; install a GPU driver with Vulkan support");
        return false;
    }
    auto& d = detail::vk();
    d.init(getInstanceProcAddr);

    u32 loaderVersion = VK_API_VERSION_1_0;
    if (d.vkEnumerateInstanceVersion != nullptr)
    {
        d.vkEnumerateInstanceVersion(&loaderVersion);
    }
    if (loaderVersion < VK_API_VERSION_1_3)
    {
        ILL_LOG_ERROR("Render", "Vulkan loader {}.{} is too old; 1.3 is required",
                      VK_API_VERSION_MAJOR(loaderVersion), VK_API_VERSION_MINOR(loaderVersion));
        return false;
    }

    std::vector<std::string> available = availableInstanceExtensions(nullptr);
    std::vector<const char*> layers;
    const bool wantValidation = desc.validation == ValidationMode::Enabled ||
                                (desc.validation == ValidationMode::Default && ILL_ASSERTS_ENABLED);
    if (wantValidation)
    {
        if (contains(availableLayers(), kValidationLayer))
        {
            layers.push_back(kValidationLayer);
            for (std::string& name : availableInstanceExtensions(kValidationLayer))
            {
                available.push_back(std::move(name));
            }
        }
        else
        {
            ILL_LOG_WARN("Render", "Vulkan validation layer not installed; only NVRHI validation is active");
            nvrhiValidationOnly = true;
        }
    }

    if (!headless)
    {
        if (glfwVulkanSupported() != GLFW_TRUE)
        {
            ILL_LOG_ERROR("Render",
                          "the windowing system cannot create Vulkan surfaces (is a native Platform alive?)");
            return false;
        }
        u32 count = 0;
        const char** required = glfwGetRequiredInstanceExtensions(&count);
        for (u32 i = 0; i < count; ++i)
        {
            if (!contains(available, required[i]))
            {
                ILL_LOG_ERROR("Render", "required instance extension {} is not available", required[i]);
                return false;
            }
            instanceExtensions.emplace_back(required[i]);
        }
    }
    const bool debugUtils = !layers.empty() && contains(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils)
    {
        instanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkInstanceCreateFlags flags = 0;
    if (contains(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        instanceExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    std::vector<const char*> extensionNames;
    for (const std::string& name : instanceExtensions)
    {
        extensionNames.push_back(name.c_str());
    }

    VkApplicationInfo application = detail::vkStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    application.pApplicationName = desc.applicationName.c_str();
    application.pEngineName = "illusion";
    application.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    application.apiVersion = VK_API_VERSION_1_3;

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = detail::vkStruct<VkDebugUtilsMessengerCreateInfoEXT>(
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
    messengerInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = &onValidationMessage;
    messengerInfo.pUserData = &validationErrors;

    VkInstanceCreateInfo info =
        detail::vkStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    info.pNext = debugUtils ? &messengerInfo : nullptr; // also reports instance creation problems
    info.flags = flags;
    info.pApplicationInfo = &application;
    info.enabledLayerCount = static_cast<u32>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    info.enabledExtensionCount = static_cast<u32>(extensionNames.size());
    info.ppEnabledExtensionNames = extensionNames.data();

    const VkResult result = d.vkCreateInstance(&info, nullptr, &instance);
    if (result != VK_SUCCESS)
    {
        ILL_LOG_ERROR("Render", "vkCreateInstance failed: {}", detail::resultName(result));
        return false;
    }
    d.init(vk::Instance(instance));
    validation = !layers.empty();

    if (debugUtils)
    {
        d.vkCreateDebugUtilsMessengerEXT(instance, &messengerInfo, nullptr, &messenger);
    }
    return true;
}

bool RenderDevice::Impl::selectPhysicalDevice(const RenderDeviceDesc& desc)
{
    auto& d = detail::vk();
    u32 count = 0;
    d.vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    d.vkEnumeratePhysicalDevices(instance, &count, devices.data());
    if (count == 0)
    {
        ILL_LOG_ERROR("Render", "no Vulkan devices found");
        return false;
    }

    Candidate best;
    for (VkPhysicalDevice candidate : devices)
    {
        VkPhysicalDeviceVulkan12Properties properties12 =
            detail::vkStruct<VkPhysicalDeviceVulkan12Properties>(
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
        VkPhysicalDeviceProperties2 properties =
            detail::vkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        properties.pNext = &properties12;
        d.vkGetPhysicalDeviceProperties2(candidate, &properties);
        const VkPhysicalDeviceProperties& p = properties.properties;

        GpuInfo info;
        info.name = p.deviceName;
        info.driverName = properties12.driverName;
        info.vendorId = p.vendorID;
        info.deviceId = p.deviceID;
        info.apiVersion = p.apiVersion;
        info.discrete = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        info.software = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;

        auto reject = [&info](std::string_view reason) {
            ILL_LOG_INFO("Render", "GPU '{}' not suitable: {}", info.name, reason);
        };

        if (p.apiVersion < VK_API_VERSION_1_3)
        {
            reject("Vulkan 1.3 not supported");
            continue;
        }
        DeviceFeatures features;
        d.vkGetPhysicalDeviceFeatures2(candidate, &features.core);
        if (!features.v13.dynamicRendering || !features.v13.synchronization2 ||
            !features.v12.timelineSemaphore || !features.v11.shaderDrawParameters)
        {
            reject("required Vulkan 1.3 features missing");
            continue;
        }
        const std::vector<std::string> extensions = availableDeviceExtensions(candidate);
        if (!headless && !contains(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            reject("no swapchain support");
            continue;
        }

        u32 familyCount = 0;
        d.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        d.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        std::optional<u32> queueFamily;
        for (u32 i = 0; i < familyCount; ++i)
        {
            const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[i].queueFlags & needed) != needed)
            {
                continue;
            }
            if (!headless && glfwGetPhysicalDevicePresentationSupport(instance, candidate, i) != GLFW_TRUE)
            {
                continue;
            }
            queueFamily = i;
            break;
        }
        if (!queueFamily)
        {
            reject("no graphics queue that can present");
            continue;
        }

        const int score = scoreDevice(p.deviceType, desc.gpuPreference);
        if (score > best.score)
        {
            best = Candidate{candidate, *queueFamily, score, std::move(info)};
        }
    }

    if (best.device == VK_NULL_HANDLE)
    {
        ILL_LOG_ERROR("Render", "no GPU supports the required Vulkan 1.3 features");
        return false;
    }
    physicalDevice = best.device;
    graphicsFamily = best.graphicsFamily;
    gpu = std::move(best.info);
    return true;
}

bool RenderDevice::Impl::createLogicalDevice()
{
    auto& d = detail::vk();
    DeviceFeatures supported;
    d.vkGetPhysicalDeviceFeatures2(physicalDevice, &supported.core);

    // Enable what the engine needs plus widely useful optional features the device supports.
    DeviceFeatures enabled;
    enabled.v13.dynamicRendering = VK_TRUE;
    enabled.v13.synchronization2 = VK_TRUE;
    enabled.v13.maintenance4 = supported.v13.maintenance4;
    enabled.v12.timelineSemaphore = VK_TRUE;
    enabled.v12.bufferDeviceAddress = supported.v12.bufferDeviceAddress;
    enabled.v12.descriptorIndexing = supported.v12.descriptorIndexing;
    enabled.v12.runtimeDescriptorArray = supported.v12.runtimeDescriptorArray;
    enabled.v12.descriptorBindingPartiallyBound = supported.v12.descriptorBindingPartiallyBound;
    enabled.v12.descriptorBindingVariableDescriptorCount =
        supported.v12.descriptorBindingVariableDescriptorCount;
    enabled.v12.shaderSampledImageArrayNonUniformIndexing =
        supported.v12.shaderSampledImageArrayNonUniformIndexing;
    enabled.v12.scalarBlockLayout = supported.v12.scalarBlockLayout;
    // NVRHI's bindless descriptor tables are written while command buffers that bind them are pending.
    enabled.v12.descriptorBindingSampledImageUpdateAfterBind =
        supported.v12.descriptorBindingSampledImageUpdateAfterBind;
    enabled.v12.descriptorBindingUpdateUnusedWhilePending =
        supported.v12.descriptorBindingUpdateUnusedWhilePending;
    enabled.v11.shaderDrawParameters = VK_TRUE;
    enabled.core.features.samplerAnisotropy = supported.core.features.samplerAnisotropy;
    enabled.core.features.fillModeNonSolid = supported.core.features.fillModeNonSolid;
    enabled.core.features.independentBlend = supported.core.features.independentBlend;
    enabled.core.features.textureCompressionBC = supported.core.features.textureCompressionBC;
    enabled.core.features.shaderInt16 = supported.core.features.shaderInt16;
    enabled.core.features.multiDrawIndirect = supported.core.features.multiDrawIndirect;
    enabled.core.features.drawIndirectFirstInstance = supported.core.features.drawIndirectFirstInstance;

    VkPhysicalDeviceVulkan12Properties properties12 = detail::vkStruct<VkPhysicalDeviceVulkan12Properties>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES);
    VkPhysicalDeviceProperties2 properties =
        detail::vkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    properties.pNext = &properties12;
    d.vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
    gpu.bindless =
        supported.v12.descriptorIndexing && supported.v12.runtimeDescriptorArray &&
        supported.v12.descriptorBindingPartiallyBound &&
        supported.v12.shaderSampledImageArrayNonUniformIndexing &&
        supported.v12.descriptorBindingSampledImageUpdateAfterBind &&
        supported.v12.descriptorBindingUpdateUnusedWhilePending &&
        properties12.maxPerStageDescriptorUpdateAfterBindSampledImages >= kBindlessTextureCapacity &&
        properties12.maxDescriptorSetUpdateAfterBindSampledImages >= kBindlessTextureCapacity;
    gpu.multiDrawIndirect =
        supported.core.features.multiDrawIndirect && supported.core.features.drawIndirectFirstInstance;

    const std::vector<std::string> available = availableDeviceExtensions(physicalDevice);
    if (!headless)
    {
        deviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // Required by the spec whenever a portability implementation (MoltenVK) exposes it.
    if (contains(available, "VK_KHR_portability_subset"))
    {
        deviceExtensions.emplace_back("VK_KHR_portability_subset");
    }
    std::vector<const char*> extensionNames;
    for (const std::string& name : deviceExtensions)
    {
        extensionNames.push_back(name.c_str());
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo =
        detail::vkStruct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    queueInfo.queueFamilyIndex = graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo info = detail::vkStruct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    info.pNext = &enabled.core;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = static_cast<u32>(extensionNames.size());
    info.ppEnabledExtensionNames = extensionNames.data();

    const VkResult result = d.vkCreateDevice(physicalDevice, &info, nullptr, &device);
    if (result != VK_SUCCESS)
    {
        ILL_LOG_ERROR("Render", "vkCreateDevice failed: {}", detail::resultName(result));
        return false;
    }
    d.init(vk::Device(device));
    d.vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    bufferDeviceAddress = enabled.v12.bufferDeviceAddress == VK_TRUE;
    return true;
}

bool RenderDevice::Impl::createNvrhiDevice()
{
    instanceExtensionPointers.clear();
    for (const std::string& name : instanceExtensions)
    {
        instanceExtensionPointers.push_back(name.c_str());
    }
    deviceExtensionPointers.clear();
    for (const std::string& name : deviceExtensions)
    {
        deviceExtensionPointers.push_back(name.c_str());
    }

    nvrhi::vulkan::DeviceDesc desc;
    desc.errorCB = &messages;
    desc.instance = instance;
    desc.physicalDevice = physicalDevice;
    desc.device = device;
    desc.graphicsQueue = graphicsQueue;
    desc.graphicsQueueIndex = static_cast<int>(graphicsFamily);
    desc.instanceExtensions = instanceExtensionPointers.data();
    desc.numInstanceExtensions = instanceExtensionPointers.size();
    desc.deviceExtensions = deviceExtensionPointers.data();
    desc.numDeviceExtensions = deviceExtensionPointers.size();
    desc.bufferDeviceAddressSupported = bufferDeviceAddress;

    nvrhiDevice = nvrhi::vulkan::createDevice(desc);
    if (!nvrhiDevice)
    {
        ILL_LOG_ERROR("Render", "NVRHI device creation failed");
        return false;
    }
    validatedDevice = validation || nvrhiValidationOnly
                          ? nvrhi::validation::createValidationLayer(nvrhiDevice)
                          : nvrhiDevice;
    return true;
}

RenderDevice::RenderDevice()
    : m_impl(std::make_unique<Impl>())
{
}

std::unique_ptr<RenderDevice> RenderDevice::create(const RenderDeviceDesc& desc)
{
    ILL_PROFILE_SCOPE();
    const bool alreadyAlive = g_deviceAlive.exchange(true);
    ILL_ASSERT(!alreadyAlive, "only one RenderDevice may exist at a time");
    if (alreadyAlive)
    {
        return nullptr;
    }

    std::unique_ptr<RenderDevice> renderDevice(new RenderDevice());
    Impl& impl = *renderDevice->m_impl;
    impl.headless = desc.headless;
    if (!impl.createInstance(desc) || !impl.selectPhysicalDevice(desc) || !impl.createLogicalDevice() ||
        !impl.createNvrhiDevice())
    {
        return nullptr; // the destructor releases what was created and clears g_deviceAlive
    }
    ILL_LOG_INFO("Render", "GPU: {} ({}, Vulkan {}.{}){}", impl.gpu.name, impl.gpu.driverName,
                 VK_API_VERSION_MAJOR(impl.gpu.apiVersion), VK_API_VERSION_MINOR(impl.gpu.apiVersion),
                 impl.validation ? ", validation on" : "");
    return renderDevice;
}

RenderDevice::~RenderDevice()
{
    m_impl.reset();
    g_deviceAlive.store(false);
}

nvrhi::IDevice* RenderDevice::device() const noexcept
{
    return m_impl->validatedDevice.Get();
}

const GpuInfo& RenderDevice::gpu() const noexcept
{
    return m_impl->gpu;
}

bool RenderDevice::canPresent() const noexcept
{
    return !m_impl->headless;
}

bool RenderDevice::isValidationEnabled() const noexcept
{
    return m_impl->validation || m_impl->nvrhiValidationOnly;
}

u32 RenderDevice::validationErrorCount() const noexcept
{
    return m_impl->validationErrors.load(std::memory_order_relaxed);
}

void RenderDevice::waitIdle()
{
    ILL_PROFILE_SCOPE();
    m_impl->validatedDevice->waitForIdle();
}

void RenderDevice::collectGarbage()
{
    m_impl->validatedDevice->runGarbageCollection();
}

} // namespace illusion

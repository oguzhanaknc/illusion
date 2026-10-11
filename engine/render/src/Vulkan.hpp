#pragma once

// Internal Vulkan access for the render module.
//
// NVRHI is compiled with vulkan.hpp's dynamic dispatcher, and a static NVRHI build expects the
// application to provide the dispatcher storage (see VulkanDispatch.cpp) and initialize it. We use the
// same configuration so there is one dispatcher in the process, but we call the plain C function
// pointers it holds (vk().vkCreateInstance(...)) instead of vulkan.hpp's wrappers, which report errors
// with exceptions.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <string_view>
#include <vulkan/vulkan.hpp>

namespace illusion::detail
{

/// The process-wide Vulkan dispatcher shared with NVRHI.
inline auto& vk() noexcept
{
    return VULKAN_HPP_DEFAULT_DISPATCHER;
}

/// Opens the Vulkan loader library and returns vkGetInstanceProcAddr, or nullptr if no loader is
/// installed. The library stays loaded until the process exits.
PFN_vkGetInstanceProcAddr loadVulkanLoader() noexcept;

/// Returns a zero-initialized Vulkan structure with its sType set.
template <class T>
[[nodiscard]] T vkStruct(VkStructureType type) noexcept
{
    T value{};
    value.sType = type;
    return value;
}

/// Name of a VkResult for log messages.
std::string_view resultName(VkResult result) noexcept;

} // namespace illusion::detail

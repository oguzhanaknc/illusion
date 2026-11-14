#include "Vulkan.hpp"

#include <illusion/core/Base.hpp>

#include <nvrhi/vulkan.h>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <array>

// Storage for the dispatcher declared by vulkan.hpp; must exist exactly once in the program.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace illusion::detail
{
namespace
{

PFN_vkGetInstanceProcAddr openLoader() noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    HMODULE library = LoadLibraryW(L"vulkan-1.dll");
    if (library == nullptr)
    {
        return nullptr;
    }
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        reinterpret_cast<void*>(GetProcAddress(library, "vkGetInstanceProcAddr")));
#else
#if defined(ILL_PLATFORM_MACOS)
    // The loader that an exported game's bundle carries (cmake/IllusionGame.cmake; it finds MoltenVK through
    // Contents/Resources/vulkan/icd.d), then the Vulkan SDK's; MoltenVK can also be used directly.
    constexpr std::array kCandidates = {"@executable_path/../Frameworks/libvulkan.1.dylib",
                                        "libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib"};
#else
    constexpr std::array kCandidates = {"libvulkan.so.1", "libvulkan.so"};
#endif
    for (const char* name : kCandidates)
    {
        if (void* library = dlopen(name, RTLD_NOW | RTLD_LOCAL))
        {
            if (void* symbol = dlsym(library, "vkGetInstanceProcAddr"))
            {
                return reinterpret_cast<PFN_vkGetInstanceProcAddr>(symbol);
            }
            dlclose(library);
        }
    }
    return nullptr;
#endif
}

} // namespace

PFN_vkGetInstanceProcAddr loadVulkanLoader() noexcept
{
    static const PFN_vkGetInstanceProcAddr loader = openLoader();
    return loader;
}

std::string_view resultName(VkResult result) noexcept
{
    return nvrhi::vulkan::resultToString(result);
}

} // namespace illusion::detail

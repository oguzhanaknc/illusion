#pragma once

/// @file
/// GPU device: Vulkan instance, physical device selection and the NVRHI device used for all rendering.
///
/// The Vulkan loader is opened at run time, so applications do not link against the Vulkan SDK. A
/// device needs Vulkan 1.3 (dynamic rendering, synchronization2, timeline semaphores). Only one
/// RenderDevice may exist at a time.

#include <illusion/core/Base.hpp>

#include <nvrhi/nvrhi.h>

#include <memory>
#include <string>

namespace illusion
{

/// Which GPU to prefer when several are available.
enum class GpuPreference : u8
{
    HighPerformance, ///< Prefer discrete GPUs.
    LowPower,        ///< Prefer integrated GPUs.
};

/// Whether GPU validation is enabled.
enum class ValidationMode : u8
{
    Default,  ///< On when the engine was built with assertions (Debug builds and CI), off otherwise.
    Enabled,  ///< Always on.
    Disabled, ///< Always off.
};

/// Creation parameters for RenderDevice.
struct RenderDeviceDesc
{
    /// Application name reported to the driver.
    std::string applicationName = "illusion";

    /// Controls the Vulkan validation layer (when installed) and NVRHI's validation layer. Validation
    /// messages are logged; errors are counted (see RenderDevice::validationErrorCount()).
    ValidationMode validation = ValidationMode::Default;

    /// Creates a device without presentation support, for tests, tools and servers. A non-headless
    /// device requires a native Platform to exist, because the windowing system decides which surface
    /// extensions are needed.
    bool headless = false;

    /// GPU preference when more than one suitable GPU is present.
    GpuPreference gpuPreference = GpuPreference::HighPerformance;
};

/// Description of the selected GPU.
struct GpuInfo
{
    std::string name;       ///< Device name, e.g. "NVIDIA GeForce RTX 4070".
    std::string driverName; ///< Driver name, e.g. "NVIDIA" or "llvmpipe".
    u32 vendorId = 0;       ///< PCI vendor id.
    u32 deviceId = 0;       ///< PCI device id.
    u32 apiVersion = 0;     ///< Supported Vulkan version (VK_MAKE_API_VERSION encoding).
    bool discrete = false;  ///< True for discrete GPUs.
    bool software = false;  ///< True for CPU implementations such as lavapipe.
    /// Descriptor indexing with update-after-bind sampled images (bindless textures) is available.
    bool bindless = false;
    bool wireframe = false;  ///< Non-solid polygon fill is available.
    bool timestamps = false; ///< Graphics-queue timestamp queries are available.
    /// Multi-draw indirect with a first-instance offset per draw is available.
    bool multiDrawIndirect = false;
};

/// Number of textures a bindless texture table can hold; GpuInfo::bindless requires the device to allow it.
inline constexpr u32 kBindlessTextureCapacity = 16384;

/// Owns the Vulkan instance and device and the NVRHI device. See the file documentation.
class RenderDevice : NonCopyable
{
public:
    /// Creates the device. Returns nullptr (and logs why) if Vulkan is unavailable or no GPU qualifies.
    [[nodiscard]] static std::unique_ptr<RenderDevice> create(const RenderDeviceDesc& desc = {});

    /// Waits for the GPU to finish and destroys the device.
    ~RenderDevice();

    /// The NVRHI device. Wrapped in NVRHI's validation layer when validation is enabled.
    [[nodiscard]] nvrhi::IDevice* device() const noexcept;

    /// The selected GPU.
    [[nodiscard]] const GpuInfo& gpu() const noexcept;

    /// Returns true if the device can present to windows.
    [[nodiscard]] bool canPresent() const noexcept;

    /// Returns true if validation is active.
    [[nodiscard]] bool isValidationEnabled() const noexcept;

    /// Number of validation errors reported so far. Tests assert that this stays zero.
    [[nodiscard]] u32 validationErrorCount() const noexcept;

    /// Blocks until the GPU is idle.
    void waitIdle();

    /// Releases resources whose GPU work has completed. Swapchain::present() does this every frame;
    /// call it yourself once per frame only when rendering without a swapchain.
    void collectGarbage();

private:
    friend class Swapchain;
    RenderDevice();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

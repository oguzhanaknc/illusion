# 0009. Renderer foundation: NVRHI on Vulkan 1.3, run-time loader, Direct3D-style clip space

- Status: Accepted
- Date: 2026-10-11

## Context

The brief fixes NVRHI on Vulkan (MoltenVK on macOS). Remaining choices: how the Vulkan loader is found,
which Vulkan version and features to require, how device-level dispatch is shared with NVRHI, how
errors are reported, and which clip-space convention the engine exposes.

## Decision

- **Vulkan 1.3 minimum** with dynamic rendering, synchronization2 and timeline semaphores (all used by
  NVRHI) plus `shaderDrawParameters` (used by Slang's `SV_VertexID`). Optional features (buffer device
  address, descriptor indexing, anisotropy, BC compression, ...) are enabled when supported.
- **Run-time loader.** The engine opens `vulkan-1.dll` / `libvulkan.so.1` / `libvulkan.1.dylib` (falling
  back to `libMoltenVK.dylib`) itself. Builds do not need the Vulkan SDK; headers come from the pinned
  Vulkan-Headers package.
- **One dispatcher.** NVRHI's static build uses vulkan.hpp's default dynamic dispatcher; the engine
  provides its storage and initializes it. Engine code calls the C function pointers stored in it and
  never vulkan.hpp's exception-throwing wrappers. Consequently only one `RenderDevice` exists at a time.
- **Errors.** Creation functions return `nullptr` and log the reason. Validation (Vulkan layer when
  installed, plus NVRHI's validation layer) is on by default in Debug; errors are counted and tests
  require zero.
- **Swapchain.** One acquire semaphore per in-flight acquire and one render-finished semaphore per
  swapchain image; frame pacing with NVRHI event queries; recreation is decided by comparing the window
  size and vsync setting the swapchain was created for (never the driver-chosen extent, which can
  differ and would cause a recreation loop).
- **Clip space is y-up with depth [0, 1]** (Direct3D convention). NVRHI flips the Vulkan viewport; the
  engine documents and keeps this convention everywhere (shaders, projection matrices).

## Consequences

- Machines without Vulkan 1.3 drivers cannot run the engine. This excludes very old GPUs and macOS
  versions without a recent MoltenVK; acceptable for a new commercial engine.
- macOS builds must bundle MoltenVK (and optionally the loader); handled by the release milestone.
- Multi-GPU and multiple simultaneous devices are not supported until the dispatcher strategy changes.
- GPU tests run on Mesa lavapipe in CI, so correctness is checked without GPU runners; performance on
  real hardware is measured locally.

# Rendering

The render module creates the GPU device and presents images to windows. It is built on
[NVRHI](https://github.com/NVIDIA-RTX/NVRHI) over Vulkan 1.3 (MoltenVK on macOS); game code records
GPU work with NVRHI command lists.

Headers: `#include <illusion/render/RenderDevice.hpp>`, `<illusion/render/Swapchain.hpp>`

!!! note "Early stage"
    This milestone provides the device, swapchains, the shader build pipeline and a diagnostic triangle.
    Meshes, materials, PBR lighting and shadows follow in the renderer milestone (see the roadmap).

## Requirements

- A GPU driver with Vulkan 1.3 (dynamic rendering, synchronization2, timeline semaphores). All
  current NVIDIA, AMD and Intel drivers qualify, as does Mesa (including the lavapipe software
  renderer used in CI).
- The Vulkan SDK is **not** required to build or run. Install it to get the validation layers, which
  the engine enables automatically in Debug builds when they are present.

## Creating the device

```cpp
illusion::RenderDeviceDesc desc;
desc.applicationName = "My Game";
auto device = illusion::RenderDevice::create(desc);
if (!device)
{
    return 1; // no suitable GPU; the reason is in the log
}
ILL_LOG_INFO("Game", "rendering on {}", device->gpu().name);
nvrhi::IDevice* gpu = device->device();
```

- The Vulkan loader is opened at run time; nothing links against the Vulkan SDK.
- When several GPUs qualify, `gpuPreference` chooses discrete (default) or integrated.
- `headless = true` creates a device without presentation support for tests, tools and servers.
  Otherwise a native `Platform` must exist, because the windowing system decides which surface
  extensions are needed.
- Only one `RenderDevice` may exist at a time. Destroy every NVRHI object and swapchain before it.

### Validation

`validation` (`ValidationMode::Default`: on when the engine is built with assertions, as in Debug and
CI builds) turns on the Vulkan validation layer, when installed, and NVRHI's own validation layer. Messages go to the log; `validationErrorCount()` counts errors. The
GPU tests and the sample fail if it is not zero.

## Presenting to a window

```cpp
auto swapchain = illusion::Swapchain::create(*device, *window);
nvrhi::CommandListHandle commands = device->device()->createCommandList();

while (!window->shouldClose())
{
    platform->pollEvents();
    if (swapchain->beginFrame()) // false while the window is minimized
    {
        commands->open();
        commands->clearTextureFloat(swapchain->currentBackBuffer(), nvrhi::AllSubresources,
                                    nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
        // ... draw into swapchain->currentFramebuffer() ...
        commands->close();
        device->device()->executeCommandList(commands);
        swapchain->present();
    }
}
device->waitIdle();
```

- `beginFrame()` waits until one of the `framesInFlight` (default 2) frame slots is free, then
  acquires a back buffer. When the window size changed, it recreates the swapchain first.
- Back buffers use an sRGB format when the display supports one, so shaders write linear colour.
- `vsync` (default on) selects FIFO presentation; without vsync, mailbox or immediate mode is used.
  `setVsync()` changes it at the next frame.

## Coordinate conventions

The renderer follows Direct3D-style clip space: **y points up** and depth goes from 0 to 1. NVRHI flips
the Vulkan viewport to achieve this, so projection matrices from `glm::perspective()` work without a y
flip. Texture coordinates have their origin at the top-left.

## Shaders

Shaders are written in [Slang](https://shader-slang.org/) (an HLSL-compatible language) and compiled to
SPIR-V at build time. The compiled bytecode is embedded in the program, so there are no shader files to
ship or load.

```cmake
illusion_add_shaders(my_game
    NAMESPACE my_game::shaders
    HEADER my_game/Shaders.hpp
    SHADERS
        meshVertex shaders/Mesh.slang vertexMain vertex
        meshFragment shaders/Mesh.slang fragmentMain fragment)
```

Each line names a C++ function, the source file, the entry point and the stage. The generated header
declares `std::span<const std::byte> meshVertex() noexcept;` and so on; pass the bytes to
`nvrhi::IDevice::createShader()`. Shader warnings are errors, and the build tracks `#include`d files.

The Slang compiler is downloaded automatically (pinned version). Set `ILLUSION_SLANGC_EXECUTABLE` to use
another `slangc`.

## Diagnostics

`DebugTriangle` draws a coloured triangle without any buffers. The `illusion_hello_triangle` sample uses it
to verify the whole path from shader compilation to presentation:

```bash
./build/linux-clang/samples/Debug/illusion_hello_triangle --frames 300
```

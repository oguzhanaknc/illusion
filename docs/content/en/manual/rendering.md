# Rendering

The render module creates the GPU device, presents images to windows and draws ECS scenes with
physically based materials. It is built on [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) over Vulkan 1.3
(MoltenVK on macOS); game code records GPU work with NVRHI command lists.

Headers: `#include <illusion/render/RenderDevice.hpp>`, `<illusion/render/Swapchain.hpp>`

!!! note "In progress"
    The scene renderer draws Meshes with PBR Materials, direct lights and image-based lighting. Shadows,
    ambient occlusion and post-processing follow in later steps of the renderer milestone (see the
    roadmap).

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

## Drawing a scene

`SceneRenderer` draws every Entity that has a `Transform` and a `MeshRenderer`, lit by
`DirectionalLight` and `PointLight` Entities. `RenderResources` owns the GPU Meshes, Textures and
Materials and hands out small Handles that Components store.

Headers: `<illusion/render/RenderResources.hpp>`, `<illusion/render/SceneRenderer.hpp>`,
`<illusion/render/Components.hpp>`, `<illusion/ecs/Transform.hpp>`

```cpp
illusion::RenderResources resources(*device);
illusion::SceneRendererDesc rendererDesc;
rendererDesc.colorFormat = swapchain->format();
auto renderer = illusion::SceneRenderer::create(*device, resources, rendererDesc);

illusion::World world;
const illusion::MeshHandle sphere = resources.createMesh(illusion::makeSphere(0.5f), "Sphere");
illusion::MaterialDesc gold;
gold.parameters.baseColorFactor = illusion::Vec4(1.0f, 0.77f, 0.34f, 1.0f);
gold.parameters.metallicFactor = 1.0f;
gold.parameters.roughnessFactor = 0.3f;
const illusion::MaterialHandle material = resources.createMaterial(gold);

world.create(illusion::Transform{}, illusion::MeshRenderer{sphere, 0, material});

illusion::Transform sun;
sun.rotation = glm::quatLookAt(glm::normalize(illusion::Vec3(-0.4f, -1.0f, -0.6f)), illusion::Vec3(0.0f, 1.0f, 0.0f));
world.create(sun, illusion::DirectionalLight{illusion::Vec3(1.0f), 3.0f});
```

Every frame, build a `RenderView` from the camera and render into a colour target:

```cpp
illusion::Transform camera;
camera.position = illusion::Vec3(0.0f, 1.0f, 4.0f);
const illusion::UVec2 size = swapchain->size();
const auto view = illusion::RenderView::fromCamera(camera, illusion::Camera{},
                                                   static_cast<float>(size.x) / static_cast<float>(size.y));
commands->open();
renderer->render(commands, swapchain->currentBackBuffer(), world, view, &jobs);
commands->close();
device->device()->executeCommandList(commands);
ILL_LOG_DEBUG("Game", "{} draw calls", renderer->stats().drawCalls);
```

- `MeshRenderer` names a Mesh, one of its submeshes and a Material (an unset Material uses the
  default white, rough Material).
- Lights shine along their Transform's -z axis (`DirectionalLight`) or from its position
  (`PointLight`, fading to zero at `range`). The first 4 directional and 16 point lights are used.
- `render()` clears the target, culls Entities against the view frustum (on the Job System when you
  pass one), sorts them by Pipeline, Material and Mesh, and issues one instanced draw call for each run
  of identical (Mesh, submesh, Material). `stats()` reports candidates, visible Entities, draw calls
  and triangles.
- The renderer owns a depth buffer that follows the size of the target. Create a `SceneRenderer` per
  colour format you render to.
- Colour targets with an sRGB format receive linear colour; UNORM targets receive sRGB-encoded colour,
  so both look the same.

### Materials and shading

Materials follow the glTF 2.0 metallic-roughness model: base colour, metallic, roughness, normal,
occlusion and emissive, each a factor with an optional Texture. Alpha modes are opaque, mask (with a
cutoff) and blend; double-sided Materials disable back-face culling. Lighting uses the GGX
distribution, height-correlated Smith visibility and Schlick Fresnel, followed by exposure and ACES
tonemapping. Indirect light comes from a `SkyLight` (see below).

To draw a glTF model, load it with `loadGltf()` (see [Assets and Models](assets.md)), upload it with
`RenderResources::uploadModel()` and create its Entities with `spawnModel()`.

### Image-based lighting

A `SkyLight` lights the scene with an environment: an equirectangular image of everything around it,
usually an HDR photo. Free, high-quality HDRIs (CC0) are available from
[Poly Haven](https://polyhaven.com/hdris); download them as `.hdr` files.

```cpp
std::vector<std::byte> bytes;
files.readFile("game:/skies/meadow_2k.hdr", bytes);
std::optional<illusion::Image> sky = illusion::decodeImage(bytes, /*srgb=*/false);
if (!sky)
{
    sky = illusion::makeSkyImage(1024); // procedural fallback
}
const illusion::EnvironmentHandle environment = resources.createEnvironment(*sky);

illusion::Transform orientation;
orientation.rotation = glm::angleAxis(glm::radians(90.0f), illusion::Vec3(0.0f, 1.0f, 0.0f));
world.create(orientation, illusion::SkyLight{environment, 1.0f, true});
```

- `createEnvironment()` bakes the image on the GPU once: a background cube map, a specular cube map
  prefiltered for increasing roughness, and the diffuse irradiance as spherical harmonics (on the
  CPU). Bake environments at load time: a 2K HDRI takes under 100 ms on a desktop machine.
  `EnvironmentDesc` sets the resolutions and sample count.
- The renderer uses the first `SkyLight`. Its Transform rotates the environment; `intensity` scales
  it, and `background = false` keeps the clear colour behind the scene.
- Without a `SkyLight`, `SceneRendererDesc::ambient` adds uniform ambient light instead.
- Metals reflect their surroundings, so they look right only with an environment.
- The shading is energy-conserving: in a uniform white environment, every white Material looks like the
  background, whatever its roughness or metalness (the "white furnace" test in the GPU tests).

### The glTF viewer

`illusion_gltf_viewer` shows a model with an orbit camera (left mouse drag or right stick to orbit,
mouse wheel or triggers to zoom). Without a model it shows a grid of spheres with increasing metallic
(left to right) and decreasing roughness (top to bottom). `--environment` lights the scene with an
HDR file instead of the built-in procedural sky. `--screenshot` saves the final view as a 1280x720
PNG.

```bash
./build/linux-clang/samples/Debug/illusion_gltf_viewer path/to/model.glb
./build/linux-clang/samples/Debug/illusion_gltf_viewer path/to/model.glb --environment meadow_2k.hdr
./build/linux-clang/samples/Debug/illusion_gltf_viewer --frames 60 --screenshot scene.png
```

## Coordinate conventions

The renderer follows Direct3D-style clip space: **y points up** and depth goes from 0 to 1. NVRHI flips
the Vulkan viewport to achieve this, so projection matrices need no y flip. Texture coordinates have
their origin at the top-left.

World space is right-handed with y up; cameras look along their -z axis, and front faces are
counter-clockwise. The scene renderer uses **reversed-Z** with an infinite far plane: the near plane
maps to depth 1 and infinity to 0, which keeps depth precision nearly uniform over distance. Build
projection matrices with `perspectiveReversedZ()` (or `RenderView::fromCamera()`), not
`glm::perspective()`.

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

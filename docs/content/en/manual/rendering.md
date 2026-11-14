# Rendering

The render module creates the GPU device, presents images to windows and draws ECS scenes with
physically based materials. It is built on [NVRHI](https://github.com/NVIDIA-RTX/NVRHI) over Vulkan 1.3
(MoltenVK on macOS); game code records GPU work with NVRHI command lists.

Headers: `#include <illusion/render/RenderDevice.hpp>`, `<illusion/render/Swapchain.hpp>`

!!! note "In progress"
    The scene renderer draws Meshes with PBR Materials, direct and image-based lighting, soft shadows,
    ambient occlusion, post-processing, GPU occlusion culling and bindless multi-draw rendering. The
    asset pipeline with streaming comes next (see the roadmap).

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

`SceneRenderer` draws every Entity that has a `Transform` and a `RenderMesh`, lit by
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

world.create(illusion::Transform{}, illusion::RenderMesh{sphere, 0, material});

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

- `RenderMesh` names a GPU Mesh, one of its submeshes and a Material (an unset Material uses the
  default white, rough Material). Scenes use `MeshRenderer` with asset references instead (see below).
- Lights shine along their Transform's -z axis (`DirectionalLight`) or from its position
  (`PointLight`, fading to zero at `range`). The first 4 directional and 16 point lights are used.
- `render()` culls Entities (see [Culling](#culling)), sorts them by Pipeline, Material and Mesh, and
  draws each run of identical (Mesh, submesh, Material) with one instanced draw per pass. `stats()`
  reports candidates, Entities inside the frustum, draw calls and triangles.
- The renderer owns a depth buffer that follows the size of the target. Create a `SceneRenderer` per
  view, with the colour format of its target.
- Colour targets with an sRGB format receive linear colour; UNORM targets receive sRGB-encoded colour,
  so both look the same.

### MeshRenderer and RenderMesh

Scenes describe what to draw with the saved Component `MeshRenderer`, which refers to assets by path
([asset references](assets.md#asset-references)); the renderer draws the runtime Component `RenderMesh`,
which holds GPU Handles. A `RenderAssetResolver` per World connects the two, through a `RenderAssetCache`
shared by every World drawn with the same `RenderResources`:

Header: `<illusion/render/RenderAssets.hpp>`

```cpp
illusion::RenderAssetCache cache(resources, streamer);
illusion::RenderAssetResolver resolver(cache);

world.create(illusion::Transform{}, illusion::MeshRenderer{.mesh = illusion::AssetRef("game:/models/ship.glb#mesh/0")});
world.create(illusion::Transform{}, illusion::MeshRenderer{.mesh = illusion::AssetRef("builtin:/mesh/cube"),
                                                           .material = illusion::AssetRef("game:/materials/paint.material")});
resolver.waitIdle(world, &jobs); // optional: finish loading before the first frame

// Every frame, before rendering:
streamer.update();
cache.update();
resolver.update(world, &jobs);
```

- `MeshRenderer` holds `mesh`, `submesh`, `material` (empty: the model's material for that submesh),
  `layers`, `castShadows`, `visible` and `lodBias` (see [levels of detail](#levels-of-detail)). Every `update()` compares each one with the key its `RenderMesh`
  was resolved from (in parallel, without allocating) and resolves only those that changed.
- Assets load in the background: a renderer keeps drawing what it drew before until its new assets are
  on the GPU (new renderers draw nothing until then). Streamed models are uploaded within
  `RenderAssetCacheDesc::uploadBytesPerUpdate` (levels added by
  [mip streaming](#texture-compression-and-mip-streaming) have a budget of their own).
- A mesh that cannot be loaded draws nothing; a material that cannot be loaded draws with the default
  material. The cache logs why, `RenderAssetCache::error()` returns it and
  `RenderAssetResolver::statistics().failures` counts the renderers affected.
- Removing a `MeshRenderer` removes its `RenderMesh`. Code that builds geometry at run time writes
  `RenderMesh` directly (with `sourceKey` 0); the resolver leaves it alone. `RenderMesh` is not saved.
- The cache shares every asset between renderers and Worlds (100 renderers of one mesh upload it once)
  and counts the resolvers that use each asset (`references()`).
- **Live reload.** `AssetStreamer::reload(path)` (see [Streaming](assets.md#reloading-changed-files)) makes
  the cache load every asset made from that file again. The previous version keeps drawing until the new
  one is on the GPU, and stays if the new file cannot be loaded (`error()` says why). A `.material` file
  changes in place; meshes, models and environments get new Handles and every resolver resolves its
  renderers again on its next `update()`. An asset that failed loads again when its file is reloaded.
- **Eviction.** `RenderAssetResolver::releaseUnused(world)` stops using the assets no renderer of the World
  refers to any more, and `RenderAssetCache::evictUnused()` frees the GPU resources of every asset no
  resolver uses (and of the models and Textures only they used). Both walk everything: call them when
  assets may have gone, such as after loading another scene, not every frame.

### Resource lifetime

`RenderResources` frees what it is told to release. Handles carry the generation of their slot, so a
released Handle is stale at once: renderers skip it, `isValid()` returns false, and releasing or updating
it again asserts. The memory is reused only after the GPU finished the frames submitted before the
release: `collect()` (called by the create functions and by `RenderAssetCache::update()`) frees what the
GPU no longer uses. Meshes live in shared vertex and index pools; freed ranges are reused, so reimporting
a model again and again keeps the pools at the same size.

```cpp
const illusion::MeshHandle mesh = resources.createMesh(illusion::makeCube());
resources.releaseMesh(mesh);           // stale at once: draws nothing, cannot be released again
ILL_ASSERT(!resources.isValid(mesh));
resources.collect();                   // once per frame: frees what the GPU no longer uses

illusion::MaterialDesc paint;
paint.parameters.baseColorFactor = illusion::Vec4(0.9f, 0.2f, 0.1f, 1.0f);
const illusion::MaterialHandle material = resources.createMaterial(paint);
paint.parameters.roughnessFactor = 0.2f;
resources.updateMaterial(material, paint); // same handle, no new binding set
```

- `releaseMesh()`, `releaseTexture()`, `releaseMaterial()`, `releaseEnvironment()` and `releaseModel()`
  ignore empty Handles. The default Material and Textures cannot be released.
- `updateMaterial()` rewrites a Material's factors and Textures under the same Handle. It makes a new
  binding set only when a Texture or the wrapping changed, so editing factors every frame is cheap.
  Update or release the Materials that use a Texture before releasing it.
- `statistics()` reports the use and capacity of the pools, their free ranges and how many released
  resources still wait for the GPU. It also reports Texture and environment memory (see
  [Texture compression and mip streaming](#texture-compression-and-mip-streaming)).

### Materials and shading

Materials follow the glTF 2.0 metallic-roughness model: base colour, metallic, roughness, normal,
occlusion and emissive, each a factor with an optional Texture. Alpha modes are opaque, mask (with a
cutoff) and blend; double-sided Materials disable back-face culling. Lighting uses the GGX
distribution, height-correlated Smith visibility and Schlick Fresnel, followed by exposure and ACES
tonemapping. Indirect light comes from a `SkyLight` (see below).

To draw a glTF model, load it with `loadGltf()` (see [Assets and Models](assets.md)), upload it with
`RenderResources::uploadModel()` and create its Entities with `spawnModel()`.

### Texture compression and mip streaming

Images are compressed to BC formats on import (see [texture compression](assets.md#texture-compression) for
the formats and the settings that choose them). A compressed `Image` reaches the GPU as it is, so a Texture
takes a quarter of the memory of RGBA8 (BC7, BC5) or an eighth (BC4), and an environment an eighth of
RGBA16F (BC6H).

- `RenderResources::createTexture()` uploads the levels of a block-compressed image unchanged when the
  device samples BC formats (`GpuInfo::textureCompressionBC`). `RenderDeviceDesc::compressedTextures` (on by
  default) can turn that off. Without BC support the images are decoded on the CPU and uploaded as RGBA8,
  and the renderer logs one warning that they take 4 to 8 times the memory. No supported platform needs
  this today.
- Every normal map is sampled the same way: the shader reads x and y and rebuilds z, so a BC5 map and an
  RGB map look alike.
- `RenderResources::statistics()` reports `textureBytes` and `environmentBytes` (what is on the GPU),
  `textureBytesUncompressed` and `environmentBytesUncompressed` (what the same Textures would take as RGBA8
  or RGBA32F, and the environments as RGBA16F) and `streamingSavedBytes` (levels left out by mip streaming).

```cpp
const illusion::RenderResourceStatistics stats = resources.statistics();
ILL_LOG_DEBUG("Game", "textures {} bytes of {} uncompressed, {} saved by streaming", stats.textureBytes,
              stats.textureBytesUncompressed, stats.streamingSavedBytes);
```

**Mip streaming.** A Texture can keep only some of its levels in memory. Its *resident range* is
`[textureFirstMip(), textureMipCount())`: level 0 is the largest, so a first level of 2 leaves out the two
largest levels and the Texture takes about a sixteenth of the memory. `createTexture(image, firstMip)` uploads
a Texture with only the levels from `firstMip` on, and `setTextureMips()` changes the range later:

```cpp
// image is the illusion::Image the Texture is made from.
const illusion::TextureHandle texture = resources.createTexture(image, 2u, "Rock"); // levels 2 and up
const bool raised = resources.setTextureMips(texture, 0u, &image);                  // all levels again
resources.setTextureMips(texture, 3u);                                              // drop to levels 3 and up
```

- A change builds a new Texture for the range, copies the levels both ranges share on the GPU and uploads
  only the added levels from the `Image`. The Handle stays valid and `textureFirstMip()` and `statistics()`
  report the new range at once. Materials start using the new Texture in a later `collect()`, once the GPU
  has finished writing it, so nothing half-written is drawn; the old Texture is freed after that. Raising a
  Texture needs the `Image` it was made from; dropping levels does not.
- `setTextureMips()` returns false and changes nothing for an empty, stale or default Texture, for a raise
  without an `Image`, or for a different `Image`.

`RenderAssetCache` streams the Textures of `.material` files on its own. Set
`RenderAssetCacheDesc::textureBudgetBytes` to the GPU bytes those Textures may take and pass the views the
scene is seen from when you update the resolver:

```cpp
illusion::RenderAssetCacheDesc desc;
desc.textureBudgetBytes = 256ull << 20; // 0 (the default) keeps every level resident
illusion::RenderAssetCache cache(resources, streamer, desc);
illusion::RenderAssetResolver resolver(cache);

// Every frame, for a perspective camera with a vertical field of view of fovY radians:
const illusion::UVec2 size = swapchain->size();
const float pixelsPerUnit = static_cast<float>(size.y) / (2.0f * std::tan(fovY * 0.5f));
const illusion::TextureStreamingView views[] = {{.position = camera.position, .pixelsPerUnit = pixelsPerUnit}};
streamer.update();
cache.update();
resolver.update(world, &jobs, views);
```

- `update(world, jobs, views)` does what `update(world, jobs)` does and then reports, for every material, the
  largest size in pixels at which a visible renderer that uses it appears (the bounds of its submesh, the
  world scale and the distance to the nearest view). It does not allocate. For an orthographic view, put
  `position` far behind the camera and multiply `pixelsPerUnit` by that distance.
- From the demand, the cache picks the first level each Texture needs, so that its first resident level has
  about as many pixels on its longer side as the Texture covers on screen. While the wanted levels together exceed the budget, it
  drops the largest levels of every Texture one step at a time until they fit. A material that no renderer
  reports falls to its smallest levels.
- Adding levels loads the image again through the `AssetStreamer` and counts against
  `uploadBytesPerUpdate`, like model uploads. Dropping levels waits for `textureStreamingCooldown` updates of
  lower demand (120 by default), so a Texture that goes out of view for a moment does not flicker between
  sizes. Levels up to `textureStreamingMinSize` pixels on their longer side (64 by default) are always resident.
- `RenderAssetCache::statistics()` counts `streamedTextures` (Textures with levels left out now),
  `textureRaises` and `textureDrops`.
- Not streamed: the sprite Textures of particle emitters and the images inside glTF models; they stay
  fully resident. With a budget of 0 nothing is streamed. See
  [ADR 0054](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0054-texture-compression.md).

### Wind

A `Wind` Component (one per scene) makes vegetation move and particles drift. It has a direction, an
average `speed` in m/s, and gusts: `gustStrength` (0 steady, 1 from calm to double speed) and
`gustFrequency` (gusts per second at a point). Gust fronts travel downwind at the wind's speed, so a field of
grass bends in waves.

Materials choose how they react, so trunks, leaves and rocks of one model can differ:

- `windSway` bends the whole object away from the wind: metres of offset per m/s at 1 m above its origin,
  growing with the height squared, so the base stays put and the top leans. The top sinks a little as it
  leans, keeping the stem's length.
- `windFlutter` adds a fast jitter of every vertex with its own phase (leaves, grass blades): metres per m/s,
  times the wind's `turbulence`.

The offsets are computed in the vertex shader of every pass: shading, the depth prepass, entity IDs,
shadow maps and motion vectors (with the previous frame's wind), so TAA and shadows stay stable. Casters
with wind are drawn with their own Material in shadow maps instead of batching by Mesh alone.
`ParticleEmitter::windFactor` makes particles take on the wind's velocity (see [Particles](particles.md)).

```cpp
world.create(illusion::Name{"Wind"}, illusion::Wind{.direction = {1.0f, 0.0f, 0.4f}, .speed = 4.0f});

illusion::MaterialData leaves = illusion::MaterialData{};
leaves.windSway = 0.004f;    // a 6 m tree leans about 0.6 m in a 4 m/s wind
leaves.windFlutter = 0.006f; // leaves shake by about 2 cm
emitter.windFactor = 1.5f;   // smoke drifts downwind

illusion::advanceWind(world, deltaSeconds); // GameSession and the editor call this for you
```

The wind's clock is the runtime `WindState`; `advanceWind()` adds and advances it. `windVelocity()` gives
the air's velocity at a point for gameplay, matching what the renderer shows.

### Translucency

Thin surfaces such as leaves, grass blades, paper and cloth let light through. `diffuseTransmission` (0 to
1) is the fraction of a Material's diffuse light that passes to the other side: light reaching the back
face leaves the front as soft diffuse light, tinted by the base colour times `diffuseTransmissionColor`,
and the light reflected on the lit side drops by the same fraction, so the surface creates no energy.
Under an even sky a white translucent surface therefore looks the same as an opaque one; against the sun
it glows. Metals do not transmit. This is the glTF `KHR_materials_diffuse_transmission` extension: models
that use it import their factors (its two textures are not read).

Shadows still apply: a leaf behind another leaf, or behind a trunk, stays dark. The shadow test of a
translucent surface uses the side the light reaches, so a single leaf does not shade itself.

```cpp
illusion::MaterialData leaf = illusion::MaterialData{};
leaf.alphaMode = illusion::AlphaMode::Mask;
leaf.doubleSided = true;
leaf.diffuseTransmission = 0.4f;                    // 40% of the diffuse light passes through
leaf.diffuseTransmissionColor = {0.9f, 1.0f, 0.5f}; // the light inside a leaf turns yellow-green
```

### Culling

Entities with the `Inactive` tag (they or an ancestor are [Disabled](ecs.md#disabled-and-inactive)) are not
drawn, and their lights and sky lights do not shine. Each frame the renderer culls in two steps:

1. **Frustum culling** on the CPU, in parallel on the Job System: Entities outside the view (and outside
   every shadow cascade) are skipped.
2. **Occlusion culling** on the GPU for opaque and alpha-masked Meshes: objects hidden behind others
   are not drawn. It works in two phases. First, objects are tested against the depth of the previous
   frame and the survivors are drawn into the depth prepass. Then the remaining objects are tested
   against a depth pyramid of that new depth and drawn if they are visible after all. An object that
   comes into view is therefore drawn in the same frame; nothing pops in late.

```cpp
renderer->setOcclusionCulling(true); // the default
renderer->render(commands, target, world, view, &jobs);
// ... after the GPU finished the frame (for example in a statistics overlay one frame later):
ILL_LOG_INFO("Game", "{} of {} opaque instances drawn", renderer->readGpuDrawnInstances(),
             renderer->stats().visible);
```

- The GPU writes the instance counts of indirect draws, so the CPU does not wait for culling results.
  `readGpuDrawnInstances()` reads them back for tools and statistics; call it only after the GPU has
  finished the frame.
- Occlusion culling pays off with many objects and real occluders (buildings, terrain). For scenes of
  a few large objects, `setOcclusionCulling(false)` saves the depth-pyramid passes.
- Blended Meshes and shadow casters are culled against the frustum only.

### Levels of detail

A submesh can carry up to three coarser **levels of detail** (`Submesh::lods`): simplified versions that
use fewer triangles. Imported glTF models get them automatically (see
[import settings](assets.md#import-settings)); for your own Meshes call `generateLods()`. For every instance
the culling Jobs pick the coarsest level whose geometric error, projected onto the screen, stays within
`RenderView::lodPixelError` pixels (1 by default), so objects keep their silhouette while far ones cost a
fraction of their triangles.

```cpp
illusion::MeshData tree = illusion::makeSphere(1.0f, 64, 32);
illusion::generateLods(tree); // up to three coarser levels, each about half the triangles

illusion::RenderView view = illusion::RenderView::fromCamera(cameraTransform, camera, aspect);
view.lodPixelError = 1.0f;   // the default; 0 always draws full detail
view.lodFadeSeconds = 0.25f; // the default; 0 switches levels at once
view.deltaSeconds = frameSeconds;
renderer->render(commands, target, world, view, &jobs);
ILL_LOG_INFO("Game", "{} instances at full detail, {} at the coarsest level, {} cross-fading",
             renderer->stats().lodInstances[0], renderer->stats().lodInstances[illusion::kMaxSubmeshLods],
             renderer->stats().lodFades);
```

- The level is chosen from the camera's distance and the instance's scale, once per frame, and the same
  level is drawn into the shadow maps, so shadows match the surfaces that cast them.
- Instances of one Mesh at different levels form separate batches; the GPU culls and draws each with one
  indirect draw, as before.
- When an instance's level changes, it cross-fades to the new one over `lodFadeSeconds`: both levels are
  drawn, dithered per pixel with complementary patterns, so every pixel shows one of them and nothing pops.
  The fade covers the depth prepass, the shading, Entity IDs and the shadow maps; going back to the level it
  is leaving reverses the fade. An instance that comes into view starts at its level without a fade.
- The renderer keeps each Entity's level and fade between frames, so give `deltaSeconds` the real frame
  time. `RenderStats::lodFades` counts the instances fading (`lodInstances` counts them at both levels).
- `setDebugView(RenderDebugView::LevelOfDetail)` colours every surface by the level it draws: green for
  full detail, then yellow, orange and red.
- `MeshRenderer::lodBias` (and `RenderMesh::lodBias` for code) multiplies the allowance for one renderer: 2
  switches to each coarser level at half the distance, 0.5 at twice the distance, and 0 always draws full
  detail. Give it to objects the player only sees from afar, or to foliage that may thin out sooner.
- Foliage made of cards (leaves, needle sprigs: many small parts that share no vertices) cannot be simplified
  like a closed surface. Its levels keep an evenly spread half of the previous level's cards instead, each
  enlarged about its centre to make up for the leaf area of the others; the error of such a level is how much
  the gaps between cards grow. These enlarged cards are extra vertices of the Mesh.

### Bindless rendering

On GPUs that support it, the renderer draws all opaque Meshes with a handful of calls:

- All Meshes share one vertex pool and one index pool, so switching Meshes needs no new buffers.
- All Materials are records in one table and all Textures sit in one bindless descriptor array, so a
  draw can mix Materials: each instance names its own.
- Batches with the same Pipeline (opaque or alpha-masked, single- or double-sided) are drawn by one
  multi-draw indirect call per pass.

```cpp
const illusion::GpuInfo& gpu = device->gpu();
ILL_LOG_INFO("Game", "bindless: {}, multi-draw indirect: {}", gpu.bindless, gpu.multiDrawIndirect);

illusion::SceneRendererDesc desc;
desc.bindless = true; // the default; set false to force the per-material path
auto renderer = illusion::SceneRenderer::create(*device, resources, desc);
// After the first render():
ILL_LOG_INFO("Game", "rendering bindless: {}", renderer->usesBindless());
```

- It needs descriptor indexing and multi-draw indirect (`GpuInfo::bindless`, `GpuInfo::multiDrawIndirect`),
  available on current NVIDIA, AMD and Intel drivers and in Mesa. Elsewhere, or with
  `SceneRendererDesc::bindless = false`, each batch is drawn with its Material's bindings; the image is
  the same.
- Up to `kBindlessTextureCapacity` (16384) Textures can be used.
- Blended Meshes and shadow casters are drawn per batch in both modes.

### Shadows

The first `DirectionalLight` with `castShadows` (on by default) casts soft shadows from every opaque or
alpha-masked Mesh. Blended Materials do not cast shadows.

```cpp
illusion::Transform sun;
sun.rotation = glm::quatLookAt(glm::normalize(illusion::Vec3(-0.4f, -1.0f, -0.6f)), illusion::Vec3(0.0f, 1.0f, 0.0f));
illusion::DirectionalLight light{illusion::Vec3(1.0f, 0.95f, 0.9f), 3.0f};
light.angularDiameter = 0.02f; // a little softer than the real sun
world.create(sun, light);

illusion::ShadowSettings shadows = renderer->shadowSettings();
shadows.maxDistance = 120.0f;
shadows.resolution = 4096;
renderer->setShadowSettings(shadows);
```

- Shadows use up to four cascades that cover the view up to `maxDistance`, each with its own
  `resolution`² shadow map. Nearer cascades are smaller, so nearby shadows are sharper. Cascades are
  fitted so their edges do not shimmer when the camera moves or turns.
- `angularDiameter` is the apparent size of the light in radians (the sun is about 0.0093). Shadows
  are sharp where the caster touches the receiver and grow softer with distance, like real shadows
  (percentage-closer soft shadows). Penumbrae are limited to 5% of a cascade's width.
- Objects outside the view still cast shadows into it, up to `casterDistance` towards the light.
- If you see acne (stripes on lit surfaces) raise `depthBias` or `normalBias`; if shadows detach from
  their casters, lower them.
- `stats()` reports `shadowCasters` and `shadowDrawCalls`; `shadowCascades()` returns the cascades of
  the last frame.

### Point light shadows

A `PointLight` with `castShadows` (off by default) throws shadows in every direction: a campfire shades
the ground behind the trees and the people around it, and the shadows move when the light flickers or the
casters move in the wind.

```cpp
illusion::Transform fire;
fire.position = illusion::Vec3(0.0f, 0.6f, 0.0f);
illusion::PointLight light{illusion::Vec3(1.0f, 0.55f, 0.22f), 40.0f, 14.0f};
light.castShadows = true;
light.sourceRadius = 0.25f;    // a wide flame: soft shadow edges
light.shadowNearPlane = 0.3f;  // the logs right under the flame do not darken the whole camp
world.create(fire, light);

illusion::ShadowSettings shadows = renderer->shadowSettings();
shadows.pointLightCount = 2;   // shadow maps for the two lights nearest the camera
shadows.pointResolution = 1024;
renderer->setShadowSettings(shadows);
```

- Each shadowed light renders a cube shadow map (six faces of `pointResolution`² texels) from the
  opaque and alpha-masked Meshes within its `range`. Up to `ShadowSettings::pointLightCount` lights (at
  most `kMaxShadowedPointLights`, 4) get one per frame: those whose range reaches into the view, nearest
  to the camera first. The others still light the scene, without shadows.
- `sourceRadius` is the size of the glowing body in metres. As with the sun, shadows are sharp where
  the caster touches the receiver and soften with distance; penumbrae are limited to 1/32 of a cube face.
- Casters closer to the light than `shadowNearPlane` are ignored, so a lantern's own glass or housing
  does not black out its light. If surfaces shadow themselves, raise `shadowBias` (metres) or
  `ShadowSettings::normalBias`.
- The [volumetric fog](#volumetric-fog) reads the same shadow maps, so a shadowed light's glow stops at
  walls and casters cut shafts out of it.
- `stats()` reports `shadowedPointLights`, `pointShadowCasters` and `pointShadowDrawCalls`. Turning
  `ShadowSettings::enabled` off disables point light shadows too. See
  [ADR 0051](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0051-point-light-shadows.md).

### Image-based lighting

A `SkyLight` lights the scene with an environment: an equirectangular image of everything around it,
usually an HDR photo. Free, high-quality HDRIs (CC0) are available from
[Poly Haven](https://polyhaven.com/hdris); download them as `.hdr` files.

```cpp
// From a file: a RenderAssetResolver loads and bakes it.
illusion::Transform orientation;
orientation.rotation = glm::angleAxis(glm::radians(90.0f), illusion::Vec3(0.0f, 1.0f, 0.0f));
world.create(orientation, illusion::SkyLight{.environment = illusion::AssetRef("game:/skies/meadow_2k.hdr")});

// The built-in procedural sky, shaped by a ProceduralSky on the same Entity.
world.create(illusion::Transform{}, illusion::SkyLight{.environment = illusion::AssetRef("builtin:/sky/procedural")},
             illusion::ProceduralSky{.resolution = 1024});

// Baked by code: add the RenderSky yourself.
const illusion::EnvironmentHandle environment = resources.createEnvironment(illusion::makeSkyImage(1024));
world.create(illusion::Transform{}, illusion::SkyLight{}, illusion::RenderSky{environment});
```

- `createEnvironment()` bakes the image on the GPU once: a background cube map, a specular cube map
  prefiltered for increasing roughness, and the diffuse irradiance as spherical harmonics (on the
  CPU). Bake environments at load time: a 2K HDRI takes under 100 ms on a desktop machine.
  `EnvironmentDesc` sets the resolutions and sample count.
- A `RenderAssetResolver` gives a `SkyLight` with an `environment` asset its `RenderSky`; the file
  is baked when it has loaded. `builtin:/sky/procedural` bakes `makeSkyImage()` with the Entity's
  `ProceduralSky`, again whenever its fields change.
- **Bake cache.** The `RenderAssetCache` keeps the bakes of image files in the streamer's import cache
  (`AssetType::Environment`, through [`loadCooked()`](assets.md#assets-you-make-yourself)), keyed by the
  image and its import settings sidecar and by the `EnvironmentDesc`. A later run (or another cache with
  the same `EnvironmentDesc`) uploads the stored cube maps instead of loading the image and baking it.
  A bake is read back from the GPU a few frames after it is made and then written; `waitIdle()` finishes
  the readbacks still pending. A changed image bakes again; a reload in `AssetReloadMode::Reimport` bakes
  again although nothing changed. `statistics()` counts `bakes` and `bakeCacheHits`. Procedural skies are
  not cached (they have no file).
- **Compressed bakes.** With `EnvironmentDesc::compress` on (the default), the cache compresses a bake to
  BC6H on the Job System after reading it back and stores that. The session that bakes keeps drawing the
  half-float bake; later sessions upload BC6H at about an eighth of the memory (see
  [texture compression](#texture-compression-and-mip-streaming)). Procedural and atmosphere skies stay
  half-float.
- `requestEnvironmentReadback()` and `readEnvironment()` copy an environment's cube maps back to the CPU
  as a `BakedEnvironment` without waiting (pass `block = true` in tools and tests), and
  `createEnvironment(const BakedEnvironment&)` uploads one without baking (a compressed `BakedEnvironment`
  is uploaded as BC6H, or decoded on the CPU like any compressed Texture). An environment created from a
  compressed bake cannot be read back: `requestEnvironmentReadback()` returns 0 for it.

```cpp
// Bake once, read the result back (here waiting for it), and upload it later without baking.
const illusion::EnvironmentHandle sky = resources.createEnvironment(image);
illusion::BakedEnvironment baked;
if (resources.readEnvironment(resources.requestEnvironmentReadback(sky), baked, true) ==
    illusion::ReadbackStatus::Ready)
{
    const illusion::EnvironmentHandle copy = resources.createEnvironment(baked, "Meadow");
}
```

- The renderer uses the first `SkyLight` whose environment is ready. Its Transform rotates the
  environment; `intensity` scales it, and `background = false` keeps the clear colour behind the scene.
- Without a `SkyLight`, `SceneRendererDesc::ambient` adds uniform ambient light instead.
- Metals reflect their surroundings, so they look right only with an environment.
- The shading is energy-conserving: in a uniform white environment, every white Material looks like the
  background, whatever its roughness or metalness (the "white furnace" test in the GPU tests).

### Day and night

A `SkyAtmosphere` Component draws a sky for any time of day and lights the scene with it. Change
`timeOfDay` (hours, 0 to 24) every frame and the sun crosses the sky; nothing is baked. It replaces any
SkyLight.

- **Sky.** Air (Rayleigh) and haze (Mie) scatter the sunlight along an air mass that grows towards the
  horizon, with ozone keeping the twilight blue: a deep blue noon, a bright horizon, an orange sunset. More
  `turbidity` gives a hazier, whiter sky. The sun is a bright disc that blooms.
- **Night.** The moon stands `moonOffset` hours behind the point opposite the sun (0: full moon, 12: new
  moon) and shows its phase; stars twinkle and a faint Milky Way crosses the sky, both fading in after
  sunset (`starIntensity`).
- **Light.** The sun, coloured by the air, is the scene's first directional light and casts the shadows
  (`castShadows`); after sunset the light fades out and the bluish moonlight fades in, so it never jumps.
  `sunIntensity` and `moonIntensity` are illuminances like `DirectionalLight::intensity`.
- **Image-based lighting.** Each renderer redraws a small cube map of the sky
  (`lightingResolution`, 64 by default) and its reflections only when the sky changed, and projects the sky
  onto spherical harmonics on the CPU for diffuse light. `stats().atmosphereUpdates` reports the redraws.
- `sunAzimuth` turns the sun's path (by default it rises at +x and sets at -x) and `sunTilt` sets how high
  the noon sun stands; `pathDirection()`, `sunDirection()` and `moonDirection()` give the directions.

```cpp
const illusion::Entity sky = world.create(illusion::SkyAtmosphere{.timeOfDay = 17.5f, .turbidity = 3.0f});

// in a game module: a full day in four minutes
world.get<illusion::SkyAtmosphere>(sky).timeOfDay = std::fmod(hours + deltaSeconds / 10.0f, 24.0f);

const illusion::SkyAtmosphereState state = illusion::evaluateSkyAtmosphere(world.get<illusion::SkyAtmosphere>(sky));
const bool lampsOn = state.starVisibility > 0.3f;
```

`evaluateSkyAtmosphere()` (`illusion/render/SkyAtmosphere.hpp`) returns what the renderer uses: the light,
how visible the stars are and the moon's lit fraction. `atmosphereRadiance()` and `atmosphereIrradiance()`
(`illusion/assets/Atmosphere.hpp`) evaluate the sky model itself on the CPU (ADR 0049).

### Exposure and colour grading

A `PostEffects` Component (one per scene) sets how the camera turns light into the image. Without it the
renderer uses `Camera::exposure` and ACES with no grading.

- **Automatic exposure** meters the image every frame (a centre-weighted average of log luminance) and moves
  the exposure towards the one that puts it at middle grey, in stops and at `adaptationSpeed` per second,
  like an eye getting used to the dark. `minExposure` and `maxExposure` (stops) limit it, so night stays
  night; `exposureCompensation` shifts it. `RenderView::deltaSeconds` is the time it adapts by.
- **Tonemapper**: `AgX` keeps the hue of bright saturated light (neon, fire, sunsets) and fades it to white;
  `Aces` gives punchier contrast but pushes bright colours towards yellow.
- **Grading**: `temperature` and `tint` (white balance), `contrast` around middle grey and `saturation`.
- **Lens**: `vignette`, animated film `grain` and `chromaticAberration` (pixels of colour fringe at the
  corners).

```cpp
world.create(illusion::PostEffects{.autoExposure = true,
                                   .minExposure = -1.0f, // a sunny day is not darkened below half
                                   .maxExposure = 2.5f,  // a night is lifted by at most 2.5 stops
                                   .tonemapper = illusion::Tonemapper::AgX,
                                   .contrast = 1.1f,
                                   .vignette = 0.25f,
                                   .grain = 0.15f});
view.deltaSeconds = frameSeconds; // adaptation follows real time
```

Diagnostic views (`setDebugView()`) skip all of it.

### Volumetric fog

A `VolumetricFog` Component (one per scene) fills the air with fog that scatters light. Where the sun or the
moon shines through it the fog glows, and where trees or walls shade it, it stays dark, so light shafts appear;
point lights glow in it too, so a campfire or a neon sign lights the air around it.

- **Medium**: `density` is the extinction per metre at `baseHeight`; above it the fog thins by
  `heightFalloff` per metre (0 fills the air evenly). `albedo` is the colour of the light it scatters.
- **Lighting**: `anisotropy` (Henyey-Greenstein g) makes the fog glow towards the lights (positive) or away
  from them (negative). `lightIntensity` scales the sun, moon and point lights in the fog, `ambientIntensity`
  the sky light.
- **Shape**: the fog reaches `maxDistance` metres from the camera. `noiseAmount` and `noiseSize` break it
  into patches that the `Wind` carries (`windFactor`).

```cpp
world.create(illusion::VolumetricFog{.density = 0.03f,      // half the light is gone after 23 m
                                     .heightFalloff = 0.15f, // and the fog thins with height
                                     .baseHeight = 0.0f,
                                     .anisotropy = 0.6f, // glows towards the sun and the lamps
                                     .maxDistance = 90.0f,
                                     .noiseAmount = 0.6f, // patchy wisps, carried by the Wind
                                     .noiseSize = 10.0f});
```

The renderer fills a froxel grid (cells of about 8x8 pixels, 64 slices spaced exponentially in depth) with
the fog and its light, reads the shadowed directional light's shadow cascades for the shafts, blends each frame
with the previous one so the shafts resolve smoothly, and integrates it front to back. The fog covers opaque
surfaces, the sky, blended surfaces and particles. Because of the blending a sudden change (a light switched
off) fades out over a few frames; call `SceneRenderer::resetTemporalHistory()` on a camera cut.
`RenderStats::fogFroxels` counts the froxels of the frame. Point lights with
[shadows](#point-light-shadows) cast them into the fog too; diagnostic views show the scene without fog. See
[ADR 0050](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0050-volumetric-fog.md).

### HDR and post-processing

The renderer shades into an internal HDR image and finishes each frame with post-processing:

1. A depth prepass for opaque and alpha-masked Meshes, which also removes overdraw from shading.
2. **Ambient occlusion** (GTAO) from the depth buffer darkens indirect light in creases and contacts.
3. Shading, the sky, blended Meshes, then [particles](particles.md#drawing).
4. **Temporal anti-aliasing** (optional) smooths edges and the noise of soft shadows by jittering the
   camera by a fraction of a pixel every frame and accumulating frames.
5. **Bloom** spreads light from bright pixels.
6. Exposure (`Camera::exposure`), ACES tonemapping and, for UNORM targets, sRGB encoding into the
   target passed to `render()`.

```cpp
illusion::PostProcessSettings post = renderer->postProcessSettings();
post.ambientOcclusion.radius = 1.0f;
post.bloom.intensity = 0.06f;
post.temporalAntiAliasing.enabled = true;
renderer->setPostProcessSettings(post);

// After a camera cut, start the TAA history again so the old view does not smear into the new one.
renderer->resetTemporalHistory();
```

- `SceneRendererDesc::clearColor` is radiance: it goes through exposure and tonemapping like
  everything else.
- Bloom preserves energy: it replaces `bloom.intensity` of the image with its blurred copy instead of
  adding light.
- TAA follows moving objects with per-pixel motion vectors from the depth prepass. Each renderer keeps
  its own previous transforms, checked against Entity generation. Rendering does not modify the World.
  Use one renderer per view and call `resetTemporalHistory()` after teleporting objects or replacing
  a World. TAA is off by default.
- `motionVectors()` returns the motion-vector texture for debug views.
- `stats().drawCalls` counts every draw: with occlusion culling, an opaque batch has one indirect draw
  per culling phase in both the depth prepass and shading.

### Cameras and view filters

Camera schema v2 adds `projection`, `orthoHeight`, `farPlane` and `priority`. Old scenes retain
perspective defaults. Perspective uses `fovY` and an infinite far plane; orthographic uses a positive
`orthoHeight` and `farPlane > nearPlane`. `findMainCamera(world)` returns the active Camera with a
Transform and the highest priority (lowest Entity index breaks ties), or a null Entity.

```cpp
illusion::Camera camera;
camera.projection = illusion::CameraProjection::Orthographic;
camera.orthoHeight = 12.0f;
camera.farPlane = 200.0f;
camera.priority = 10;
auto view = illusion::RenderView::fromCamera(illusion::Transform{}, camera, 16.0f / 9.0f);
view.layerMask = 1u << 2;
```

`RenderMesh::kVisible`, its `layers & view.layerMask`, and `view.hiddenEntities` filter every pass.
The hidden bitset is borrowed until `render()` returns: bit `index % 64` in word `index / 64` hides an
Entity; missing words mean visible. Refresh it when Entity indices are reused. `kCastShadows` controls
shadow casting independently. Cascades support both projections and stop at the orthographic far plane.

### Entity IDs, readback and overlays

Set `SceneRendererDesc::entityIds = true` to allocate an RG32_UINT attachment containing Entity index
and generation. Background has both words `~0u`. Masked pixels obey alpha cutoff; blended pixels with
nonzero alpha select the nearest surface without changing the scene's opaque depth.

`viewTargets()` exposes borrowed `depth`, `ids`, `hdr`, `motion`, `renderSize`, `outputSize`,
`viewProjection` (jittered) and `unjitteredViewProjection`. They last until resize or renderer destruction.
Record overlays after `render()`, sample depth as an SRV and map output pixels to render pixels.

Header: `<illusion/render/ReadbackRing.hpp>`. Create the ring once and submit rendering before requesting
a copy. Poll on later frames; `Pending` leaves the destination unchanged. A full ring rejects requests.
Readbacks validate regions and destination sizes; a short destination does not consume its ticket.

```cpp
auto readback = illusion::ReadbackRing::create(*device, 3);
const auto ticket = readback->request(renderer->viewTargets().ids,
                                     illusion::UVec2(20, 30), illusion::UVec2(1));
if (ticket)
{
    illusion::UVec2 id;
    const auto result = readback->read(*ticket, std::as_writable_bytes(std::span(&id, 1)), true);
    // true waits for this copy only; interactive tools poll with false on later frames.
    if (result == illusion::ReadbackStatus::Ready)
    {
        // Validate index and generation against the current World before using the Entity.
    }
}
```

Each ticket occupies a slot until successfully read. Slots reuse staging storage for matching dimensions
and format. Both polling and blocking modes use event queries and never call `waitForIdle()`.

### Debug drawing and diagnostics

`DebugDraw` (`<illusion/render/DebugDraw.hpp>`) reserves line storage at construction. `line`, `box` and
`sphere` return false without appending on invalid input or insufficient space. Call `clear()` each frame.
Recording does not allocate; the `render/debugdraw-record` Benchmark budgets 20 ns per line for 100k lines.

```cpp
illusion::DebugDraw draw(16384);
draw.line(illusion::Vec3(0), illusion::Vec3(1, 0, 0), illusion::Vec4(1, 0, 0, 1),
          3.0f, illusion::DebugDepth::OnTop);
auto overlay = illusion::DebugDrawRenderer::create(*device, rendererDesc.colorFormat);
// While commands is open, after renderer->render(...):
overlay->render(commands, swapchain->currentBackBuffer(), renderer->viewTargets(), draw);
```

Create the overlay renderer once. Widths are output pixels, implemented with quads rather than wide-line
GPU support. `DebugDepth::Tested` samples reversed-Z scene depth; `OnTop` remains visible through geometry.
Separate render/output sizes keep occlusion correct when output resolution differs.

`setDebugView()` selects Lit, Unlit, Albedo, Normals, Roughness, Metallic, AmbientOcclusion,
ShadowCascades, MotionVectors or LevelOfDetail. Diagnostic modes bypass temporal accumulation, bloom, exposure and ACES,
while preserving output colour encoding. `setWireframeOverlay(true)` overlays opaque triangle edges and
returns false on GPUs without non-solid fill. `setClearColor()` changes background radiance.

Opt in with `SceneRendererDesc::gpuTimers`. `gpuTimings()` polls completed pass durations in milliseconds:
`available` reports timestamp support and `valid` reports a completed measurement. A busy query ring skips
measurement instead of waiting. `stats().lightsDropped` counts active lights exceeding the 4 directional
and 16 point light limits. GPU regressions compare DebugDraw, orthographic shadows and normals against
`tests/data/render/` PNGs; `ILLUSION_UPDATE_GOLDENS=1` updates them, and `render-captures/` retains captures
in the test working directory for review.

### The glTF viewer

`illusion_gltf_viewer` shows a model with an orbit camera (left mouse drag or right stick to orbit,
mouse wheel or triggers to zoom). Without a model it shows a grid of spheres with increasing metallic
(left to right) and decreasing roughness (top to bottom). `--environment` lights the scene with an
HDR file instead of the built-in procedural sky, and `--taa` turns on temporal anti-aliasing.
`--screenshot` saves the final view as a 1280x720 PNG.

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
counter-clockwise. Perspective rendering uses **reversed-Z** with an infinite far plane: the near plane
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

# Assets and Models

The assets module turns files into CPU-side data that the renderer, tools and tests can use: images,
Meshes and whole glTF models. It does not touch the GPU; `RenderResources` uploads the data (see
[Rendering](rendering.md#drawing-a-scene)).

Headers: `#include <illusion/assets/Model.hpp>`, `<illusion/assets/Mesh.hpp>`,
`<illusion/assets/Image.hpp>`, `<illusion/assets/Environment.hpp>`

## Loading a glTF model

[glTF 2.0](https://www.khronos.org/gltf/) is the engine's model format. `loadGltf()` reads `.gltf` files
(with external or embedded buffers) and binary `.glb` files through the [virtual file system](filesystem.md):

```cpp
illusion::VirtualFileSystem files;
files.mount("game", illusion::executableDirectory() / "data");

std::string error;
const std::optional<illusion::ModelData> model = illusion::loadGltf(files, "game:/models/helmet.glb", &error);
if (!model)
{
    ILL_LOG_ERROR("Game", "cannot load helmet: {}", error);
    return;
}
const illusion::ModelHandles handles = resources.uploadModel(*model);
const std::vector<illusion::Entity> entities = illusion::spawnModel(world, *model, handles);
```

- Meshes keep their submeshes (glTF primitives), each with its own Material and bounding box. Missing
  normals are computed; tangents are read when present and derived in the shader otherwise.
- Materials use the metallic-roughness model with base colour, metallic-roughness, normal, occlusion
  and emissive Textures, alpha modes and `KHR_materials_emissive_strength`.
- Images (PNG, JPEG) are decoded and get full mip chains. Colour Textures (base colour, emissive) are
  sRGB; the others are linear.
- Nodes keep the hierarchy of the default scene; `ModelData::worldMatrices()` resolves it.
  `spawnModel()` creates the same [hierarchy](ecs.md#transform-hierarchy) of Entities, with an optional
  root matrix: one Entity per node with its `Name`, `Hierarchy`, `LocalTransform` and `Transform`. A node
  whose mesh has one submesh gets its `MeshRenderer`; a mesh with several submeshes gets one child per
  submesh, named `<node> (<submesh>)`. It returns every created Entity, parents first.
- Required extensions the engine does not support (Draco, meshopt, KTX2 Textures) make loading fail
  with a clear error instead of rendering garbage. `KHR_mesh_quantization` is supported.

Relative references inside a model (`../textures/a.png`) resolve within its mount; a model can never
read files outside it.

## Images

`Image` stores RGBA pixels with a mip chain: `RGBA8` (linear data such as normal maps), `RGBA8Srgb`
(colours) or `RGBA32F` (HDR environments). `decodeImage()` reads PNG, JPEG, TGA, BMP and Radiance HDR;
`encodePng()` writes 8-bit images, for example for screenshots. `readPixel()` returns any pixel as
linear RGBA, whatever the format.

```cpp
std::vector<std::byte> bytes;
files.readFile("game:/textures/rock.png", bytes);
std::optional<illusion::Image> image = illusion::decodeImage(bytes, /*srgb=*/true);
if (image)
{
    illusion::generateMips(*image);
    const illusion::TextureHandle texture = resources.createTexture(*image, "Rock");
}
```

`generateMips()` builds the chain with a box filter; sRGB images are filtered in linear space so
that mips do not darken.

## Meshes

`MeshData` is an indexed triangle list with 48-byte vertices (position, normal, tangent, UV),
submeshes and bounds. `makeCube()`, `makeSphere()` and `makePlane()` create procedural Meshes for
prototypes and tests; `computeNormals()` fills in smooth normals.

```cpp
illusion::MeshData ground = illusion::makePlane(20.0f);
const illusion::MeshHandle groundMesh = resources.createMesh(ground, "Ground");
```

All Meshes use counter-clockwise front faces in a right-handed, y-up space.

## Environments

Header: `<illusion/assets/Environment.hpp>`

Environment images use the equirectangular layout of Poly Haven and most HDRI libraries: the image
centre faces -z (where an unrotated camera looks), u grows towards +x and the top row is straight up.
`decodeImage()` reads Radiance `.hdr` files into `RGBA32F` images.

```cpp
const illusion::Vec3 front = illusion::equirectDirection(illusion::Vec2(0.5f, 0.5f)); // (0, 0, -1)
const illusion::ShIrradiance irradiance = illusion::computeShIrradiance(*sky);
const illusion::Vec3 fromAbove = irradiance.evaluate(illusion::Vec3(0.0f, 1.0f, 0.0f));
```

- `computeShIrradiance()` projects an environment onto 9 spherical-harmonics coefficients with the
  cosine convolution applied, so `evaluate()` returns the irradiance on a surface facing a direction.
  `RenderResources::createEnvironment()` calls it for you.
- `makeSkyImage()` creates a procedural sky (zenith-to-horizon gradient, ground and an optional sun
  disc) for prototypes, tests and as a fallback.

## Asset database

Decoding PNGs, building mip chains and converting glTF files takes time. The `AssetDatabase` does it once
per source file and keeps the result in a cache; later loads read the cooked file instead.

Header: `<illusion/assets/AssetDatabase.hpp>`

```cpp
illusion::VirtualFileSystem files;
files.mount("game", illusion::executableDirectory() / "data");
files.mount("cache", illusion::userDataDirectory("MyStudio", "MyGame") / "cache",
            illusion::MountAccess::ReadWrite);

illusion::AssetDatabase assets(files, "cache");
std::optional<illusion::ModelData> helmet = assets.loadModel("game:/models/helmet.glb");

illusion::ImageImportSettings normalMap;
normalMap.srgb = false;
std::optional<illusion::Image> bumps = assets.loadImage("game:/textures/rock_normal.png", normalMap);
```

- The cache mount must be writable. With an empty cache mount name, every load imports from source.
- A cache entry remembers the content hash of every file the asset came from (for glTF: the model, its
  buffers and its images). When any of them changes, the next load imports again and refreshes the
  entry; nothing has to be cleared by hand.
- Each import setting gets its own entry, so the same image can be loaded as sRGB colour and as linear
  data.
- Loading a cached 512x512 texture with mips is about ten times faster than decoding the PNG.
- `statistics()` counts cache hits, imports and cache writes.

Assets are identified by `AssetId`, a stable hash of the normalized virtual path:

```cpp
const illusion::AssetId id = illusion::AssetId::fromPath("game:/models/helmet.glb");
```

Cooked files (`AssetFile`, `serializeImage()`, `serializeModel()` in `<illusion/assets/AssetFormat.hpp>`)
are versioned, checksummed and fully validated when read, so a damaged cache file is ignored rather than
trusted.

## Streaming

`AssetStreamer` loads in the background, so the game keeps running while assets arrive. It uses the
same import cache as the `AssetDatabase`.

Headers: `<illusion/assets/AssetStreamer.hpp>`, `<illusion/render/UploadQueue.hpp>`

```cpp
illusion::AssetStreamer streamer(files, jobs); // the import cache defaults to the "cache" mount
illusion::UploadQueue uploads(resources);

const illusion::AssetHandle<illusion::ModelData> helmet = streamer.loadModel("game:/models/helmet.glb");
const std::shared_ptr<const illusion::ModelUpload> upload = uploads.upload(helmet);
bool spawned = false;

while (running)
{
    streamer.update();          // publishes finished loads
    uploads.process(16 << 20);  // at most ~16 MiB of GPU uploads this frame
    if (upload->uploaded && !spawned)
    {
        illusion::spawnModel(world, helmet.value(), upload->handles);
        spawned = true;
    }
    if (helmet.state() == illusion::AssetState::Failed)
    {
        ILL_LOG_ERROR("Game", "{}", helmet.error());
    }
    // ... render ...
}
```

- `loadImage()` and `loadModel()` return a handle immediately. A handle is `Loading` until `update()`
  publishes its result as `Ready` (then `value()` returns the data) or `Failed` (then `error()` says why).
  States change only in `update()`, so a handle never changes in the middle of a frame.
- Requests for the same file (and the same import settings) share one handle. The data is freed when
  the last handle is gone.
- A dedicated I/O thread reads the files; the Job System checks the cache, decodes and deserializes.
  Importing a glTF file for the first time happens on the I/O thread.
- `UploadQueue` spreads GPU uploads over frames: `process()` uploads loaded models in request order until
  its byte budget is spent, but always at least one per call.
- `waitIdle()` blocks until everything requested is published: useful for loading screens and tools.
- Destroying the streamer skips loads that have not started, finishes the running ones and writes their
  cache entries.

### Other file types

`loadFile()` streams files that other modules decode. The I/O thread reads the file and your decoder
runs on the Job System, so it must be safe to call from any thread:

```cpp
// Any file, turned into any type on the Job System; the audio module loads its clips this way.
const illusion::AssetHandle<std::string> credits = streamer.loadFile<std::string>(
    "game:/text/credits.txt", "game.text",
    [](std::vector<std::byte>&& bytes, std::string& error) -> std::optional<std::string> {
        if (bytes.empty())
        {
            error = "no credits";
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    });
```

Requests with the same path and decoder name share a handle, so the name must identify the decoder, its
settings and its result type. The import cache is not used. A file that cannot be read, is empty or that
the decoder rejects becomes `Failed` with the reason in `error()`.

## Shipping cooked assets

A shipped game does not need its source images and models: cook them ahead of time and ship the import
cache as an archive.

```bash
illusion_pack --cook game data/ build/cooked.illpak
```

`--cook` imports every image and glTF model under `data/`, mounted as `game` (use the mount name your
game uses), and packs the resulting cache. In the game, mount the archive as the cache and turn source
verification off:

```cpp
illusion::VirtualFileSystem files;
files.mountArchive("cache", illusion::executableDirectory() / "cooked.illpak");

illusion::AssetStreamerDesc desc;
desc.verifySources = false; // trust the cooked archive; the sources are not shipped
illusion::AssetStreamer streamer(files, jobs, desc);
auto helmet = streamer.loadModel("game:/models/helmet.glb"); // served from the cooked archive
```

- Requests use the same virtual paths as during development; the `game` mount itself does not have to
  exist.
- Standalone images are cooked with the default import settings; images loaded with other settings need
  their source (or a cooking step that knows those settings).
- `cookAssets()` does the same from code and reports what it cooked and which files failed.

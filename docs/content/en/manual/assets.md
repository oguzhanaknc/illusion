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
const std::vector<illusion::Entity> entities = illusion::spawnModel(world, *model, "game:/models/helmet.glb");
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
  whose mesh has one submesh gets a `MeshRenderer` that refers to `<model>#mesh/N` (see
  [asset references](#asset-references)); a mesh with several submeshes gets one child per submesh, named
  `<node> (<submesh>)`. It returns every created Entity, parents first. A `RenderAssetResolver` loads the
  model when the World is drawn; `spawnModel()` with `ModelHandles` instead writes `RenderMesh` Components
  for a model uploaded by code (they are drawn at once but not saved).
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
submeshes and bounds. `makeCube()`, `makeSphere()`, `makePlane()`, `makeCapsule()` and `makeCylinder()`
create procedural Meshes for prototypes and tests (`makeBuiltinMesh()` makes the `builtin:/mesh/...`
assets); `computeNormals()` fills in smooth normals.

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

## Asset references

Components refer to assets with `AssetRef`, so scenes save what they draw, play and collide with
([ADR 0035](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0035-asset-references.md)). An
`AssetRef` holds the `AssetId` of a path; the process-wide `AssetPathTable` remembers the path, and scene
files store the path as text.

Header: `<illusion/core/AssetRef.hpp>`

```cpp
illusion::MeshRenderer ship;
ship.mesh = illusion::AssetRef("game:/models/ship.glb#mesh/0");
ship.material = illusion::AssetRef("game:/materials/hull.material");
world.create(illusion::Transform{}, ship);

ILL_LOG_INFO("Game", "{}", ship.mesh.path()); // game:/models/ship.glb#mesh/0
```

- Paths are normalized when they are recorded (`game:/a/../b.png` becomes `game:/b.png`); a malformed path
  gives an empty reference.
- In JSON an `AssetRef` is its path, `""` for none; reading also accepts `null` and `{"path": "..."}`.
- Reflect a reference like any other field; `FieldMeta::assetKind` tells editors what kind of asset fits.
- Resolvers load the assets: `RenderAssetResolver` for `MeshRenderer` and `SkyLight`
  ([Rendering](rendering.md#meshrenderer-and-rendermesh)), `PhysicsAssetResolver` for collision meshes
  ([Physics](physics.md#mesh-and-convex-hull-colliders)); `AudioWorld` loads `AudioSource::clip`.
- A scene that refers to a missing file still loads and keeps the reference;
  `SceneLoadResult::missingAssets` lists it ([Scenes](scenes.md#saving-and-loading-scenes)).
- Shared [game modules](modules.md) use the host's table, so their references resolve in the host.

### Sub-assets and built-in assets

A fragment after `#` selects part of a model:

| Path | Asset |
| --- | --- |
| `game:/ship.glb` | the whole model (as a mesh: its first mesh) |
| `game:/ship.glb#mesh/2` | mesh 2 |
| `game:/ship.glb#material/0` | material 0 |
| `game:/ship.glb#node/Hull` | the mesh of the node named `Hull` |

The reserved mount `builtin:` provides assets that need no files; `VirtualFileSystem` refuses to mount it:

| Path | Asset |
| --- | --- |
| `builtin:/mesh/cube`, `sphere`, `plane`, `capsule`, `cylinder` | `makeCube()` to `makeCylinder()` with default sizes |
| `builtin:/material/default` | the default Material (white, rough, non-metallic) |
| `builtin:/sky/procedural` | a `makeSkyImage()` sky shaped by the Entity's `ProceduralSky` |

### Material assets

A `.material` file describes a Material without a model, so several Meshes can share it and a
`MeshRenderer` can override a model's Material:

```json
{
  "format": "illusion.material",
  "version": 1,
  "material": {"baseColorFactor": [0.8, 0.1, 0.1, 1], "roughnessFactor": 0.4, "metallicFactor": 0},
  "textures": {"baseColor": "game:/textures/paint.png", "normal": "game:/textures/paint_n.png"}
}
```

- `material` holds the fields of `MaterialData` by name; missing fields keep their defaults. Its
  `...Texture` members only set wrap modes.
- `textures` names the images: `baseColor` and `emissive` load as sRGB, `metallicRoughness`, `normal` and
  `occlusion` as linear data.
- `parseMaterialAsset()` and `writeMaterialAsset()` read and write the files (call
  `reflectAssetStructs()` once first); `materialAssetDecoder()` streams them with
  `AssetStreamer::loadFile()`.
- `cookAssets()` checks material files and cooks their Textures with these settings. The `.material`
  files themselves are read directly, so ship them with the game's files.

Header: `<illusion/assets/MaterialAsset.hpp>`

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

Assets are identified by `AssetId` (`<illusion/core/AssetId.hpp>`), a stable hash of the normalized
virtual path:

```cpp
const illusion::AssetId id = illusion::AssetId::fromPath("game:/models/helmet.glb");
```

Cooked files (`AssetFile`, `serializeImage()`, `serializeModel()` in `<illusion/assets/AssetFormat.hpp>`)
are versioned, checksummed and fully validated when read, so a damaged cache file is ignored rather than
trusted.

## Import settings

An image or glTF model can carry an import settings sidecar: a `<file>.import.json` file next to it that
changes how it is imported. The sidecar exists only while a setting differs from its default, and holds
only those settings:

```json
{
  "format": "illusion.import",
  "version": 1,
  "texture": {"colorSpace": "linear", "maxSize": 1024}
}
```

| Kind | Setting | Default | Meaning |
| --- | --- | --- | --- |
| Texture (`.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.hdr`) | `colorSpace` | `"auto"` | `"srgb"` or `"linear"` overrides the colour space the image's user asks for (a material asks for sRGB base colour and linear normal maps). |
| | `generateMips` | `true` | `false` imports only the top level, even when a user asks for mips. |
| | `maxSize` | `0` | Halve the image until no side exceeds this (1 to 16384; 0 is no limit). |
| Model (`.gltf`, `.glb`) | `scale` | `1` | Uniform scale of positions and node translations (greater than 0, at most 1000). |

- `AssetDatabase`, `AssetStreamer` and `cookAssets()` read the sidecar of every image and model they
  import, so the editor, the game and the cooked build all see the same result.
- The sidecar is one of the sources of the cache entry: editing, adding or removing it makes the entry
  stale and the next load imports again. Cache entries stay keyed by the settings the caller asked for,
  so a game that ships only the cooked cache finds them without the sidecars.
- Sidecars are versioned (`"version"`) and validated: an unknown member, a section of the wrong kind
  (`"model"` next to a texture), a wrong type or an out-of-range value fails the import with an error
  that names the sidecar and the JSON pointer of the value.
- `parseImportSettings()`, `mergeImportSettings()` (JSON merge patch: `null` resets a setting or a
  section) and `writeImportSettings()` read and write sidecars from code:

```cpp
#include <illusion/assets/ImportSettings.hpp>

illusion::ImportSettings settings;
settings.texture.maxSize = 512;
const std::string text = illusion::writeImportSettings(settings, illusion::ImportSettingsKind::Texture);
// text is empty when every setting is a default: delete the sidecar instead of writing it.
files.writeText(illusion::importSettingsPath("game:/textures/ui.png"), text);
```

In the editor, `asset.set_import_settings` changes the settings (with undo) and `asset.reimport` imports
an asset again; see [the editor manual](editor/index.md#import-settings).

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

### Reloading changed files

Requests for a file share a handle until the last handle is gone, so a file that changed on disk is not
read again by itself. `invalidate(path)` forgets the requests for a file (with any import settings or
decoder): the next request reads it again, and reimports it when the import cache entry is stale, while
handles given out before keep their data. `reload(path)` does the same and publishes an
`AssetReloadedEvent` on `events()`, which the [renderer's asset cache](rendering.md#meshrenderer-and-rendermesh)
uses to load its assets again:

```cpp
// A file changed on disk (the editor does this for every file in its asset.changed event):
streamer.reload("game:/materials/paint.material");

illusion::Subscription onReload = streamer.events().subscribe<illusion::AssetReloadedEvent>(
    [](const illusion::AssetReloadedEvent& event) { ILL_LOG_INFO("Game", "reloaded {}", event.path); });
```

- `reload()` returns false for an invalid path; the event carries the normalized path and its `AssetId`.
- Reload a file after its [import settings](#import-settings) sidecar changed: the stale cache entry
  imports again. To import from source although nothing changed, pass `AssetReloadMode::Reimport`, which
  deletes the cache entries of the file's current requests first:

```cpp
// Import again from source although neither the file nor its sidecar changed:
streamer.reload("game:/textures/ui.png", illusion::AssetReloadMode::Reimport);
```
- A glTF model depends on its buffers and images: reload the model file when one of them changes (the
  editor finds such files in its asset catalog).

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
- Cooking honours [import settings](#import-settings) sidecars, and the cooked entries do not need them at
  run time, so they are not shipped. A sidecar whose asset is missing, or next to a file that has no
  import settings, is reported as an error.
- `cookAssets()` does the same from code and reports what it cooked and which files failed.

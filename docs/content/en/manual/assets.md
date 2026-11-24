# Assets and Models

The assets module turns files into CPU-side data that the renderer, tools and tests can use: images,
Meshes, whole glTF models and fonts. It does not touch the GPU; `RenderResources` uploads the data (see
[Rendering](rendering.md#drawing-a-scene)).

Headers: `#include <illusion/assets/Model.hpp>`, `<illusion/assets/Mesh.hpp>`,
`<illusion/assets/Image.hpp>`, `<illusion/assets/Environment.hpp>`, `<illusion/assets/Font.hpp>`

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
  normals are computed; tangents are read when present and derived in the shader otherwise. Vertex
  colours (`COLOR_0`) are kept in `MeshData::colors` for [material shaders](#material-shaders).
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
- Skins become `ModelData::skins` (a `Skeleton` per skin: joints ordered parents first, rest pose, inverse
  bind matrices) and a skinned mesh gets `MeshData::skin`, four joints and weights per vertex (the four
  largest when a vertex has more). Animations whose channels move joints become `ModelData::animations`
  (`AnimationClip`: Step, Linear and CubicSpline keyframes per joint name); morph target weights are
  skipped. `engine/animation` samples and poses them (ADR 0059).
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

`generateLods()` adds coarser [levels of detail](rendering.md#levels-of-detail) to every submesh with
[meshoptimizer](https://github.com/zeux/meshoptimizer): each level keeps the vertices, appends its own
indices and records its error in object-space units. It keeps the borders between materials closed, stops
when a level would remove less than a tenth of the triangles or exceed `MeshLodSettings::maxError`, and
skips submeshes below `MeshLodSettings::minTriangles`. Model imports call it with the sidecar's `lodCount`
and `lodReduction`; `clearLods()` removes the levels again.

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

## Fonts

Header: `<illusion/assets/Font.hpp>`

`Font::bake()` turns a TrueType or OpenType file (`.ttf`, `.otf`) into one 8-bit signed distance field atlas
that draws crisp text at every size, plus the glyphs, the vertical metrics and the pair kerning. All lengths
are in ems: multiply them by the text size. The same bytes and settings give the same glyphs, metrics and
atlas layout on every machine.

```cpp
std::vector<std::byte> bytes;
files.readFile("game:/fonts/ui.ttf", bytes);
auto font = illusion::Font::bake(bytes);
if (font)
{
    const illusion::FontGlyph& glyph = font->glyphOrFallback(U'A');
    const illusion::f32 advancePixels = glyph.advance * 32.0f; // at a size of 32
}

const illusion::AssetHandle<illusion::Font> handle = streamer.loadFont("game:/fonts/ui.ttf");
```

- `FontBakeSettings` sets `pixelSize` (pixels per em in the atlas, 16 to 128), `spread` (2 to 16) and the
  code point `ranges`. The default ranges cover Basic Latin, Latin-1 and Latin Extended-A, so Turkish and
  most European languages work, plus general punctuation and the euro and trade mark signs.
  Code points the font lacks are left out; `glyphOrFallback()` draws U+FFFD, else `?`.
- `AssetDatabase::loadFont()` and `AssetStreamer::loadFont()` bake with the settings of the font's
  [import settings](#import-settings) sidecar and keep the result in the import cache. `cookAssets()` cooks
  fonts too.
- Limits: there is no text shaping (glyphs are placed one by one with kerning), no right-to-left text and
  no CJK-sized ranges yet. The engine ships no font; a game brings its own.

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
| `game:/hero.glb#skin/0` | skeleton 0 (`ModelData::skins`) |
| `game:/hero.glb#animation/Walk` | the animation clip named `Walk` |

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

### Material shaders

A `.shader` file is the Slang source of a material's surface (ADR 0063). It declares the material's
parameters and a `surface()` function; the engine's lighting and passes do the rest. The shader compiler
(`slangc`, pinned by the engine) places the file between the surface API (`illusion/Surface.slang`) and the
engine's passes and cooks it into an Asset with SPIR-V for every pass. The player has no compiler and loads
the cooked result.

```slang
[ShadingModel("Unlit")]
struct Parameters
{
    [Color(1, 0.5, 0.25, 1)] float4 tint;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(-3)] int steps;
    [Default(1)] bool pulse;
    [Default2(0.5, 0.25)] float2 scroll;
    [Texture("black", "mask")] Texture2DSlot mask;
    [Texture("game:/textures/detail_n.png", "normal")] Texture2DSlot detail;
    float3 direction;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    SurfaceOutput output = defaultSurface();
    const float2 uv = input.uv + p.scroll * input.time;
    output.baseColor = p.tint.rgb * sampleTexture(input, p.mask, uv).r;
    output.normal = unpackNormal(sampleTexture(input, p.detail, uv), 1.0);
    output.emission = p.tint.rgb * p.intensity * (p.pulse ? 0.5 + 0.5 * sin(input.time) : 1.0);
    output.opacity = p.tint.a * valueNoise(input.worldPosition * float(p.steps));
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    return float3(0.0, 0.1 * sin(input.time + input.worldPosition.x), 0.0);
}
```

- `struct Parameters` is required and may be empty. It holds at most 256 bytes and at most eight textures.
  Fields are `float`, `float2`, `float3`, `float4`, `int`, `bool` or `Texture2DSlot` (with `[Texture]`).
- `surface()` is required. Read the parameters with `parameters(input)`, sample a texture with
  `sampleTexture()` and start the result from `defaultSurface()`.
- `worldOffset()` is optional and moves vertices in world space; it must have exactly the signature above.
- `[ShadingModel("...")]` on `Parameters` names the shading model: `Default`, `Unlit`, `Subsurface`,
  `TwoSidedFoliage` or `ClearCoat`. Without it the shader uses `Default`. What each model reads from
  `SurfaceOutput` is in [Shading models](rendering.md#shading-models) of the rendering manual.

| Attribute | On | Meaning |
| --- | --- | --- |
| `[Default(v)]` | `float`, `int`, `bool` | Default value (an `int` takes a whole number, a `bool` 0 or 1). |
| `[Default2(x, y)]`, `[Default3(...)]`, `[Default4(...)]` | `float2`, `float3`, `float4` | Default of a vector. |
| `[Color(r, g, b, a)]` | `float3`, `float4` | Default of a colour. |
| `[Range(min, max)]` | `float`, `int` | Lowest and highest value the Editor offers. |
| `[Texture(fallback, usage)]` | `Texture2DSlot` | `fallback` is `"white"`, `"black"`, `"normal"` (a flat normal map) or an Asset path; `usage` is `"color"`, `"linear"`, `"normal"` or `"mask"` and picks how images are imported. |

A parameter without an attribute defaults to zero. A field has at most one default.

#### Variants and cooking

The compiler builds one SPIR-V module per pass and alpha mode: the passes `forward`, `forwardBindless`,
`depthPrepass`, `shadowCaster`, `motionVectors` and `entityIds`, each for `opaque` and `mask` materials, and
for `blend` materials only `forward`, `forwardBindless` and `entityIds`. That makes 15 variants for every
shader. Cooking (`cookAssets()` with a `ShaderCompiler`, or [`illusion_pack`](#shipping-cooked-assets)) writes
the cooked shader to the import cache. The cache entry (`shader:1`) is valid while the hash of the source, the
hash of the engine's shader library and the version of `slangc` stored in the file match; any change compiles
the shader again. A shader that does not compile is not cached.

#### Errors

Diagnostics keep the file, line and column of the `.shader` file and are written one per line like GCC
messages:

```text
game:/materials/glow.shader:8:24: error: undefined identifier 'undefinedThing'. [E30015]
```

Checks of the engine (a missing `surface()` or `Parameters`, a parameter of an unsupported type, a default that
does not fit its type, too many bytes or textures) name the field they are about in the same format.

#### Limits

- `#include` in a `.shader` file is not supported.
- `SurfaceInput.color` is the mesh's vertex colour (glTF `COLOR_0`), white without one;
  `SurfaceInput.customData` is zero for now.
- `.material` files do not name shaders yet (MAT-4); code gives shaders to Materials through
  `RenderResources` (see [Material shaders](rendering.md#material-shaders) in the rendering manual). The
  reference of the surface API comes with MAT-4; until then the header comment of `illusion/Surface.slang`
  describes it.

Headers: `<illusion/assets/ShaderAsset.hpp>`, `<illusion/assets/ShaderCompiler.hpp>`

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

An image, glTF model or font can carry an import settings sidecar: a `<file>.import.json` file next to it that
changes how it is imported. The sidecar exists only while a setting differs from its default, and holds
only those settings:

```json
{
  "format": "illusion.import",
  "version": 2,
  "texture": {"colorSpace": "linear", "maxSize": 1024}
}
```

| Kind | Setting | Default | Meaning |
| --- | --- | --- | --- |
| Texture (`.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.hdr`) | `colorSpace` | `"auto"` | `"srgb"` or `"linear"` overrides the colour space the image's user asks for (a material asks for sRGB base colour and linear normal maps). |
| | `generateMips` | `true` | `false` imports only the top level, even when a user asks for mips. |
| | `maxSize` | `0` | Halve the image until no side exceeds this (1 to 16384; 0 is no limit). |
| | `compression` | `"auto"` | `"none"`, `"bc7"`, `"bc5"`, `"bc4"` or `"bc6h"` overrides the [compression](#texture-compression) the image's user asks for; `"auto"` keeps it. |
| | `quality` | `"normal"` | Encoder effort when the image is compressed: `"fast"`, `"normal"` or `"high"`. |
| Model (`.gltf`, `.glb`) | `scale` | `1` | Uniform scale of positions and node translations (greater than 0, at most 1000). |
| | `lodCount` | `3` | Coarser [levels of detail](rendering.md#levels-of-detail) generated per submesh (0 to 3; 0 turns them off). |
| | `lodReduction` | `0.5` | Fraction of the previous level's triangles each level aims for (0.1 to 0.9). |
| | `compressTextures` | `true` | Compress the model's images by their use in its materials. |
| | `textureQuality` | `"normal"` | Encoder effort for the model's images: `"fast"`, `"normal"` or `"high"`. |
| Font (`.ttf`, `.otf`) | `pixelSize` | `48` | Pixels per em in the atlas (16 to 128). |
| | `spread` | `6` | Distance in atlas pixels that the field covers on each side of an edge (2 to 16). |
| | `ranges` | Latin | Code points to bake as `[first, last]` pairs, 1 to 64 ranges. |

A font sidecar:

```json
{
  "format": "illusion.import",
  "version": 2,
  "font": {"pixelSize": 64, "spread": 8, "ranges": [[32, 126], [160, 383], [1024, 1279]]}
}
```

- `AssetDatabase`, `AssetStreamer` and `cookAssets()` read the sidecar of every image, model and font they
  import, so the editor, the game and the cooked build all see the same result.
- The sidecar is one of the sources of the cache entry: editing, adding or removing it makes the entry
  stale and the next load imports again. Cache entries stay keyed by the settings the caller asked for,
  so a game that ships only the cooked cache finds them without the sidecars.
- Sidecars are versioned (`"version"`, currently 2; version 1 files still load) and validated: an unknown member, a section of the wrong kind
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

## Texture compression

Images are compressed to GPU block formats on import. An image keeps its pixel size; each level is stored
as 4x4 blocks of 16 bytes (8 for BC4).

| Format | Channels | Used for |
| --- | --- | --- |
| BC7 | RGBA | Base colour, emissive, metallic-roughness: any colour data (`BC7Srgb` for sRGB images). |
| BC5 | R, G | Tangent-space normals; shaders rebuild z from x and y. |
| BC4 | R | Single-channel masks such as occlusion. |
| BC6H | RGB, unsigned half float | HDR colour. A float image always becomes BC6H and an 8-bit image never does. |

Who decides the format, strongest first:

1. The sidecar: `"compression"` of a texture, `"auto"` unless set.
2. The user of the image. A material asks for BC7 for base colour, emissive and metallic-roughness, BC5 for
   normals and BC4 for occlusion (BC7 when it is the same file as metallic-roughness, so one texture is
   loaded); see `materialTextureUses()`.
3. Code that reads pixels (thumbnails, environment bakes, screenshots) asks for no compression and gets
   uncompressed pixels, whatever the sidecar says.

Images inside a glTF model are compressed by their use in its materials: normal maps as BC5, images used
only for occlusion as BC4, everything else as BC7. `"compressTextures": false` in the model sidecar turns
this off. A texture sidecar and a model sidecar:

```json
{
  "format": "illusion.import",
  "version": 2,
  "texture": {"compression": "bc7", "quality": "fast"}
}
```

```json
{
  "format": "illusion.import",
  "version": 2,
  "model": {"compressTextures": true, "textureQuality": "high"}
}
```

`TextureQuality` sets the encoder effort: `Fast` for iteration, `Normal` (the default) and `High` for final
builds. Each step is several times slower than the one before. The import cache keeps the result, so the
cost is paid on the first import, and `cookAssets()` compresses the textures of material files (and the
images of models) at export time.

`compressImage()` compresses every level of an image, in parallel when given a Job System, and produces the
same bytes with any worker count. `decompressImage()` decodes it back on the CPU:

```cpp
#include <illusion/assets/TextureCompression.hpp>

// image is an RGBA8 illusion::Image; jobs may be nullptr.
const auto bc7 = illusion::compressImage(image, illusion::TextureCompression::BC7,
                                         illusion::TextureQuality::High, &jobs);
const auto pixels = illusion::decompressImage(*bc7); // RGBA8 again, for tools and tests
```

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
- Besides the renderer, the [`PhysicsAssetResolver`](physics.md#mesh-and-convex-hull-colliders) cooks the
  collision geometry of a reloaded model again and the [`AudioWorld`](audio.md#loading-in-the-background)
  loads a reloaded clip again. The event's `mode` tells listeners that the reload was a `Reimport`, so
  they also rebuild what they keep in the cache themselves (baked environments).

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

### Assets you make yourself

Some assets are made from a file by code the streamer cannot run, for example the renderer's
environments, which are baked on the GPU. `loadCooked()` and `storeCooked()` keep them in the import
cache with the same checks as imports: an entry records the content hashes of the file and its import
settings sidecar and is used only while both are unchanged.

```cpp
// Look up an asset you make yourself (here: something computed from an image on the GPU).
const illusion::AssetHandle<illusion::CookedAsset> lookup = streamer.loadCooked(
    "game:/skies/meadow.hdr", "game.bake:1", illusion::AssetType::Environment,
    illusion::kEnvironmentFormatVersion);
// ... once lookup.ready():
if (lookup.value().file)
{
    use(lookup.value().file->payload); // a hit: the sources are unchanged
}
else
{
    illusion::AssetFile file{illusion::AssetType::Environment, illusion::kEnvironmentFormatVersion,
                             lookup.value().sources, make()}; // a miss: make it and store it
    streamer.storeCooked("game:/skies/meadow.hdr", "game.bake:1", file);
}
```

- The import key names the maker, its version and its settings (bump the version when the output changes);
  it must not contain `|`. The entry is stored under the cache mount like any import.
- A miss is `Ready` without a file and with `sources`, the current hashes to record in the entry. A file
  that cannot be read misses with no sources (`storeCooked()` ignores entries without dependencies, so
  nothing is stored and the maker's own load reports the error). Only an invalid path fails.
- `AssetReloadMode::Reimport` ignores the entry, for a forced rebuild. Without a cache mount every lookup
  misses at once.
- The renderer's [bake cache](rendering.md#image-based-lighting) is built on this.

## Shipping cooked assets

A shipped game does not need its source images, models and fonts: cook them ahead of time and ship the import
cache as an archive.

```bash
illusion_pack --cook game data/ build/cooked.illpak
```

`--cook` imports every image, glTF model and font under `data/`, mounted as `game` (use the mount name your
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
- Cooking also compiles the [material shaders](#material-shaders) (`.shader`) under `data/` with `slangc`.
  Name the compiler and the engine's shader library before `--cook`:

```bash
illusion_pack --slangc slang/bin/slangc --shaders share/illusion/shaders --cook game data/ build/cooked.illpak
```

- `--slangc` and `--shaders` go together. Without them, `illusion_pack` of an installed engine uses `slang/bin`
  and `share/illusion/shaders` two directories above it; with no compiler a `.shader` file is a cook error.
- The compiler's diagnostics are printed as `file:line:column: error: message` lines, and a failed shader fails
  the cook.

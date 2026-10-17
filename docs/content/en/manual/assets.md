# Assets and Models

The assets module turns files into CPU-side data that the renderer, tools and tests can use: images,
Meshes and whole glTF models. It does not touch the GPU; `RenderResources` uploads the data (see
[Rendering](rendering.md#drawing-a-scene)).

Headers: `#include <illusion/assets/Model.hpp>`, `<illusion/assets/Mesh.hpp>`,
`<illusion/assets/Image.hpp>`

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
- Nodes keep the hierarchy of the default scene; `ModelData::worldMatrices()` resolves it, and
  `spawnModel()` creates one Entity per mesh node and submesh, with an optional root matrix.
- Required extensions the engine does not support (Draco, meshopt, KTX2 Textures) make loading fail
  with a clear error instead of rendering garbage. `KHR_mesh_quantization` is supported.

Relative references inside a model (`../textures/a.png`) resolve within its mount; a model can never
read files outside it.

## Images

`Image` stores RGBA pixels with a mip chain: `RGBA8` (linear data such as normal maps), `RGBA8Srgb`
(colours) or `RGBA32F` (HDR environments). `decodeImage()` reads PNG, JPEG, TGA, BMP and Radiance HDR;
`encodePng()` writes 8-bit images, for example for screenshots.

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

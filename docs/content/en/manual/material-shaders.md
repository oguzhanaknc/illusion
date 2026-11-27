# Material shaders

A material shader is a small Slang file that decides what one surface looks like: its colour, roughness,
emission, opacity and, if you want, how its vertices move. The engine's own PBR material is written the same
way (`builtin:/shader/standard`), so a custom shader has the same tools, passes and lighting as every other
material ([ADR 0063](../architecture/decisions.md)). You write the surface; illusion draws it in the depth
prepass, the shadow cascades, the lit pass (bound and GPU-driven), the motion vectors and the Entity id
pass, in every alpha mode.

A shader is a text file, so an agent can write it, compile it and read the errors: see
[From the editor and agents](#from-the-editor-and-agents).

## A first shader

Create `assets/shaders/pulse.shader`:

```slang
// A pulsing emissive surface: stripes of light scroll over a dark body and a hanging cloth sways in the wind.
[ShadingModel("Default")]
struct Parameters
{
    [Color(1, 0.4, 0.1, 1)] float3 glow;
    [Default(2.5)] [Range(0, 10)] float intensity;
    [Default(3)] int bands;
    [Default(1)] bool pulse;
    [Default2(0.5, 0.25)] float2 scroll;
    [Texture("white", "mask")] Texture2DSlot mask;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float2 uv = input.uv + p.scroll * input.time;
    const float band = step(0.5, frac(uv.y * float(p.bands)));
    const float beat = p.pulse ? 0.5 + 0.5 * sin(input.time * 3.0) : 1.0;
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.08, 0.08, 0.1);
    output.emission = p.glow * p.intensity * band * beat * sampleTexture(input, p.mask, uv).r;
    return output;
}

float3 worldOffset(WorldOffsetInput input)
{
    return windOffset(input, 0.05, 0.002);
}
```

A `.shader` file has three parts:

- `struct Parameters`: the values a material can set, with their defaults. It is required and may be empty.
- `SurfaceOutput surface(SurfaceInput input)`: required. Read `parameters(input)`, the UVs, the world position and
  the time, and fill a `SurfaceOutput` that starts from `defaultSurface()`.
- `float3 worldOffset(WorldOffsetInput input)`: optional. Moves the vertex in world space (wind, waves, a
  pulse). It runs in every pass, with that frame's time, so shadows and motion vectors agree with the picture.

The file does not import anything: the compiler places it between the engine's surface API
(`illusion/Surface.slang`) and its passes.

Use the shader from a material file (see [Materials that name a shader](#materials-that-name-a-shader)) and give
the material to a `MeshRenderer`.

## The surface API

### SurfaceInput

| Field | Meaning |
| --- | --- |
| `worldPosition` | Position of the pixel in world space. |
| `normal`, `tangent`, `bitangent` | The world-space frame, normalized; `normal` faces the viewer. |
| `uv` | Texture coordinates of the mesh. |
| `color` | Vertex colour (white for meshes without colours). |
| `viewDirection` | From the surface to the camera, normalized. |
| `time` | Seconds, the same in every pass of a frame. |
| `customData` | The Entity's custom data (zero until `MeshRenderer` carries it). |
| `frontFace` | True when the front of the triangle faces the camera. |

### SurfaceOutput

Start from `defaultSurface()` (white, dielectric, roughness 0.5, opaque) and change what you need.

| Field | Meaning |
| --- | --- |
| `baseColor` | Linear albedo (metal colour for metals). |
| `normal` | Tangent-space normal, `(0, 0, 1)` for the geometric one. |
| `metallic`, `roughness`, `occlusion` | As in glTF. |
| `emission` | Linear radiance added to the lit colour. |
| `opacity`, `opacityMask` | Blended materials: coverage. Masked materials: a pixel is cut away where `opacity` is below `opacityMask`. |
| `subsurfaceColor`, `thickness` | Subsurface and TwoSidedFoliage models. |
| `clearCoat`, `clearCoatRoughness` | ClearCoat model. |

### WorldOffsetInput

`worldPosition`, `objectOrigin` (the world position of the object's origin), `normal`, `uv`, `time`,
`customData`. `worldOffset()` must depend on nothing that changes over time except `time`, so that the
previous frame's offset (for motion vectors) is the same function with the previous frame's time.

### Helpers

| Function | What it does |
| --- | --- |
| `parameters(input)` | The `Parameters` of the material being drawn. |
| `sampleTexture(input, slot, uv)` | Samples a texture parameter (in `surface()` only). |
| `sampleTextureLevel(material, slot, uv, level)` | Samples a given mip level (also in `worldOffset()`). |
| `unpackNormal(texel, scale)` | A tangent-space normal from a normal map texel. |
| `triplanar(input, slot, scale, sharpness)` | A texture projected along the three world axes. |
| `valueNoise(p)`, `fractalNoise(p, octaves)` | Smooth noise in [0, 1], for `float2` or `float3` points. |
| `hash12(p)`, `hash13(p)` | A cell hashed to [0, 1). |
| `windOffset(input, sway, flutter)` | The scene's Wind at the vertex: bend by height and flutter, in metres per m/s. |

## Parameters

Every field of `Parameters` is a parameter of the material. Attributes set what the editor and the files know
about it:

| Declaration | Type in the `.material` file |
| --- | --- |
| `float x;` with `[Default(2)]` and `[Range(0, 10)]` | a number; the Inspector shows a slider |
| `float2`, `float3`, `float4` with `[Default2(..)]`, `[Default3(..)]`, `[Default4(..)]` | an array of that many numbers |
| `float3` or `float4` with `[Color(r, g, b, a)]` | a colour: an array, or `"#rrggbb"` / `"#rrggbbaa"` (sRGB) |
| `int` with `[Default(3)]` | an integer |
| `bool` with `[Default(1)]` | `true` or `false` |
| `Texture2DSlot` with `[Texture("white", "mask")]` | an asset path (`""`: no image) |

A field without an attribute defaults to zero. A texture's first argument is its default: `"white"`, `"black"`,
`"normal"` (a flat normal map) or the path of an image; the second is its usage, which decides how the image is
imported: `"color"` (sRGB, BC7), `"linear"` (BC7), `"normal"` (BC5) or `"mask"` (one channel, BC4). `Parameters`
is at most 256 bytes and a shader has at most eight textures.

## Shading models

`[ShadingModel("...")]` on `Parameters` picks how the lighting code uses the output. The models differ in what
the lighting does, not in the passes, so every model works with every alpha mode.

| Model | Lighting |
| --- | --- |
| `Default` | Metallic-roughness PBR. |
| `Unlit` | Base colour plus emission, no lighting. |
| `Subsurface` | Diffuse light wraps past the terminator by `1 - thickness`, tinted by `subsurfaceColor` (skin, wax). |
| `TwoSidedFoliage` | Leaf translucency: light passes through thin surfaces, tinted by `subsurfaceColor`. |
| `ClearCoat` | A second specular lobe along the geometric normal with `clearCoat` and `clearCoatRoughness` (car paint). |

## Materials that name a shader

A `.material` file of version 2 has a `shader` (an asset path) and `parameters`, the values by name:

```json
{
  "format": "illusion.material",
  "version": 2,
  "shader": "assets:/shaders/pulse.shader",
  "material": {"name": "Pulse", "alphaMode": "Opaque"},
  "parameters": {
    "glow": [0.2, 0.8, 1],
    "intensity": 4,
    "bands": 5,
    "pulse": false,
    "scroll": [0.1, 0.4],
    "mask": "assets:/textures/smoke.png"
  }
}
```

- Parameters the file leaves out take the shader's defaults, so a material is as short as its changes.
- A name the shader does not declare, or a value of the wrong type or size, is an error with the JSON Pointer
  of the value, for example `/parameters/glow: the shader 'assets:/shaders/pulse.shader' declares no parameter
  'glow' (it declares: ...)`. A material that fails to load draws with the default material.
- `material` still sets the alpha mode, double-sidedness and the wrap modes of every shader.
- Version 1 files, which have neither member, read as they are: materials of the standard shader. Writing a
  file always writes version 2.
- `builtin:/shader/standard` and `builtin:/shader/foliage` name the engine's own shaders. A material without a
  shader and with `diffuseTransmission` above zero still draws with the foliage shader.

## In the editor

- **Asset Browser.** `.shader` files are the asset kind **Shader**. Select a `.material` file: the right panel
  shows its shader (drop a `.shader` file on it, or press Clear) and one typed widget per declared parameter:
  a slider or drag field for floats, a colour picker for colours, drag fields for vectors, a checkbox for bools,
  and a drop target for textures. Every change is a `material.set`, so it is undoable and the Scene view
  shows it at once.
- **Hot reload.** When a `.shader` file changes the editor compiles it on a background thread, never inside a
  frame, and swaps it in with `RenderResources::updateShader()` when it is done. Materials keep their values
  by name. A shader that stops compiling keeps drawing with its last working version, and the errors go to the
  Console as `file:line:column: error: message [code]`. A material whose shader never compiled draws with a
  magenta checker, the error shader.
- **Compile errors.** Errors name the `.shader` file with line and column. Errors in the engine's own shader
  library, which your shader includes, name that file instead; they usually mean the shader is missing
  `Parameters` or `surface()`, or declares a field the compiler cannot lay out.

## From the editor and agents

| Command | What it does |
| --- | --- |
| `material.compile` | Compiles a `.shader` file on the background thread and answers when done, with `status` (`ok` or `failed`), the diagnostics (file, line, column, severity, code, message) and the declared parameters. |
| `material.validate` | Checks a `.material` or `.shader` file: a shader is valid when its current source built, a material when its `parameters` fit the shader it names. Errors have a JSON Pointer in `errorPath`. |
| `material.get` | Returns a material and, for a custom shader, its status and declared parameters (name, type, default, range, usage). |
| `material.set` | Merges fields into a material; `parameters` merges by name and `null` removes one. |

An agent writes the shader and the material, calls `material.compile`, reads the diagnostics until
`status` is `ok`, then calls `material.validate` on the material:

```json
{"call": "material.compile", "params": {"path": "assets:/shaders/pulse.shader"}}
{"call": "material.validate", "params": {"path": "assets:/materials/pulse.material"}}
```

## Export and the player

Exporting cooks every `.shader` file with the `slangc` of the installed engine into SPIR-V for every pass and
alpha mode; the cooked shaders are in the game's data. The player has no compiler. A shader that does not
compile fails the export with the compiler's message and its file, line and column.

## From C++

`ShaderCompiler` (`illusion/assets/ShaderCompiler.hpp`) compiles a `.shader` source into a `ShaderAsset`, or
diagnostics. `ShaderLoader` (`illusion/assets/ShaderLoader.hpp`) builds shaders on a thread of its own, and a
`RenderAssetCache` given one in `RenderAssetCacheDesc::shaders` loads the shaders that material files name:

```cpp
illusion::ShaderLoader shaders(files, illusion::ShaderLoaderDesc{.cacheMount = "cache", .compiler = {},
                                                                 .verifySources = false});
illusion::RenderAssetCacheDesc desc;
desc.shaders = &shaders;
illusion::RenderAssetCache cache(resources, streamer, desc);
```

Without a compiler the loader reads the cooked shaders of the import cache, as the player does. The editor's
loader has the compiler of its module SDK.

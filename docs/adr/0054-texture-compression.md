# 0054. Compress textures to BC on import with bc7enc and an own BC6H encoder; stream mips by resident range

- Status: Proposed
- Date: 2026-11-12

## Context

Every texture reaches the GPU as RGBA8 (4 bytes per pixel) and every baked environment as RGBA16F (8 bytes
per pixel): 4 to 8 times the memory a block-compressed texture needs, on every platform. The whole mip chain
of every texture is resident from the first frame it is used.

Constraints: one format family on Windows, Linux and macOS; a permissively licensed encoder that builds with
every supported compiler without a second toolchain; imports run on the Job System and are cached in the
import cache (ADR 0045), keyed by what the user of an image asks for; code that reads pixels on the CPU
(thumbnails, environment bakes, screenshots) must keep getting pixels.

Format family:

- **BC (BC4/5/6H/7)**: mandatory on desktop Direct3D-class hardware, exposed by Vulkan as
  `textureCompressionBC` on Windows and Linux drivers and by MoltenVK on Apple Silicon and Intel Macs.
- **ASTC**: native on Apple Silicon, but absent on desktop GPUs of the other two platforms; it would need a
  second cooked variant per texture.
- **A supercompressed intermediate** (Basis Universal, transcoded at load): one file for every GPU, but
  lower quality per bit than BC7, a transcode step on every load and a large dependency. Worth revisiting
  if a mobile or web target is added.

Encoder:

| Option | Licence | Formats | Why not |
| --- | --- | --- | --- |
| bc7enc_rdo (`bc7enc.cpp`, `rgbcx`) | MIT or public domain | BC7, BC1/3/4/5 | no BC6H |
| Intel ISPC Texture Compressor, `bc7e.ispc` | MIT, Apache-2.0 | BC6H, BC7 | needs the ISPC compiler on every build machine |
| Compressonator (`CMP_Core`) | MIT | all | large, slow at comparable quality, x86 intrinsics in the core |
| DirectXTex | MIT | all | needs DirectXMath and DirectX-Headers off Windows; very slow BC7 |
| bcdec | MIT or Unlicense | decoder for all | decoder only |

Mip streaming: a Vulkan texture owns memory for every level it was created with, so levels can only be left
out by creating a smaller texture. Sparse textures would avoid the re-creation but are optional in Vulkan
and not available through MoltenVK.

## Decision

**Formats.** `ImageFormat` gains `BC7`, `BC7Srgb`, `BC5` (two channels, unsigned), `BC4` (one channel,
unsigned) and `BC6H` (unsigned half-float RGB). A compressed `Image` keeps its pixel dimensions per level;
a level of `w x h` pixels holds `ceil(w/4) * ceil(h/4)` blocks in row order (16 bytes each, 8 for BC4).

**Encoders** (`illusion/assets/TextureCompression.hpp`, all third-party code private to `illusion::assets`):

- BC7 with `bc7enc.cpp` and BC4/BC5 with `rgbcx` from bc7enc_rdo, pinned to a commit. `bc7e.ispc`, the
  RDO post-processor and the tools of that repository are not compiled.
- BC6H with an encoder of our own in `engine/assets/src/Bc6hEncoder.cpp`: unsigned, single-region mode 11
  (10-bit endpoints, 4-bit indices), endpoints fitted along the principal axis and refined by least squares.
  It is deterministic, has no dependency and is enough for the smooth content of environments.
- Decoding on the CPU with bcdec (`decompressImage()`), for tests, tools and the device fallback below.
- `compressImage()` encodes the rows of blocks of each level with `parallelFor` when given a Job System and
  produces identical bytes with any worker count. Levels smaller than a block are padded by repeating edge
  pixels.
- `TextureQuality` maps to encoder effort: `Fast` (bc7enc uber level 0, 16 partitions; BC6H without
  refinement), `Normal` (uber 1, 64 partitions; one refinement pass), `High` (uber 4, 64 partitions; three
  passes). BC7 uses perceptual weights for sRGB images and linear weights otherwise.

**Who decides the format.** The user of an image asks for a compression in `ImageImportSettings::compression`
(default `None`, so CPU readers are unaffected); it is part of the import cache key. The renderer and
`cookAssets()` ask through `materialTextureUses()`: BC7 for base colour, emissive and metallic-roughness,
BC5 for normals, BC4 for occlusion (BC7 when it is the same file as metallic-roughness, so one texture is
loaded). The sidecar overrides it per texture (`"compression"`: `auto`, `none`, `bc7`, `bc5`, `bc4`, `bc6h`)
and sets `"quality"` (`fast`, `normal`, `high`); the sidecar version becomes 2 and version 1 files still
load. A float image is always encoded as BC6H and an 8-bit image never is (`resolveCompression()`). Images
inside a glTF model are compressed by their use in its materials (normal: BC5, occlusion only: BC4, anything
else: BC7), controlled by `compressTextures` and `textureQuality` in the model sidecar. Shaders rebuild the
normal's z from x and y for every normal map.

**Cooked data.** The cooked image payload (version 2) stores the format, the levels and their bytes as
before; the model payload (version 5) the same for its images. Baked environments (version 2) store their
two cube maps either as half-float texels or as BC6H blocks (`BakedEnvironment::compressed`); the render
asset cache compresses a bake on the Job System after reading it back and before storing it, so a session
that bakes draws the half-float bake and later sessions upload BC6H. Atmosphere environments stay
half-float because they are redrawn every frame.

**GPU.** `RenderDevice` requires nothing new: `RenderResources::createTexture()` uploads compressed levels
as they are when the device has `textureCompressionBC`, and otherwise decompresses on the CPU and logs one
warning. `RenderResourceStatistics` reports texture and environment bytes on the GPU, the bytes the same
textures would take uncompressed, and the bytes streaming leaves out.

**Mip streaming.** A texture has a resident range of levels `[firstMip, mipCount)`.
`RenderResources::setTextureMips()` changes it in place: it creates a texture for the new range, copies the
levels both ranges share on the GPU, uploads the others from an `Image`, rebinds the Handle's bindless slot
and the materials that use it, and retires the old texture. Handles stay valid. `RenderAssetCache` drives it
for the textures of material files when `RenderAssetCacheDesc::textureBudgetBytes` is not zero: resolvers
report, per material, the largest projected size of the renderers that use it
(`TextureStreamingView`), the cache turns that into a wanted first level per texture, adds a global bias
while the wanted total is over the budget, loads images through the `AssetStreamer` when levels have to be
added (uploads count against `uploadBytesPerUpdate`) and drops levels only after
`textureStreamingCooldown` updates of lower demand. Levels up to `textureStreamingMinSize` pixels are always
resident. With a budget of zero every level is resident, as before.

## Consequences

- Texture memory drops to a quarter (BC7, BC5) or an eighth (BC4, BC6H against RGBA16F); the statistics
  and a GPU test hold the ratio.
- Imports get slower: BC7 at `Normal` is budgeted in `benchmarks/BenchTextureCompression.cpp`. The import
  cache hides the cost after the first import, and `cookAssets()` pays it at export time.
- Two small dependencies, both compiled from a few files with our own targets. bc7enc_rdo has no releases,
  so it is pinned to a commit archive like stb.
- Our BC6H encoder uses one of fourteen modes. Sharp, saturated HDR edges show more error than a full
  encoder would give; the error bound test documents what it reaches. Revisit (two-region modes or another
  encoder) if HDR textures other than environments need it.
- bc7enc searches a subset of the BC7 modes, so it is a little behind the best encoders in quality. Revisit
  with `bc7e` if a build machine with ISPC becomes acceptable.
- A raised texture needs its whole cooked file read again, because the asset container checksums one
  payload. Reading only the missing levels, and streaming the images inside models, need a container with
  separately addressed levels (ROADMAP, TEX-7).
- A device without BC support (none of the supported platforms today) works but uses the old amount of
  memory and pays a decode on load.
- ASTC or a transcoded format would be a new `ImageFormat` family beside this one, chosen at cook time.

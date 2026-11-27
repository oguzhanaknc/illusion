# 0066. Decals: box-projected decal shaders on the material system, written into a decal buffer that the forward pass reads

- Status: Proposed
- Date: 2026-11-27

## Context

Games need marks on surfaces that the surfaces' own materials do not have: bullet holes, blood, scorch marks,
footprints and tyre tracks (SHOOTER, PLAY's surface types), and authored graffiti, puddles, cracks and signs
placed by level designers. A decal changes the receiving surface's material where it lands (its colour, its
normal, how rough or metallic it is) and may glow (embers, a hot impact); it fades with age, layers over other
decals, and must never land on what should not receive it (the player's weapon, glass, a character's face).

What exists and shapes the design:

- **A forward renderer** (ADR 0013) with a depth prepass in two occlusion-culling phases (ADR 0017) that writes
  reversed-Z depth, RG16F motion vectors and, in the editor, RG32_UINT Entity ids; GTAO reads that depth. There
  is no G-buffer and no normal target: lighting happens once, in the forward pass, from the `SurfaceOutput` of
  the material's surface function.
- **Material shaders** (ADR 0063): a `.shader` is a Slang `surface()` (and optional `worldOffset()`), compiled
  by `ShaderCompiler` into one SPIR-V module per pass and alpha mode, with typed parameters and up to eight
  textures; `RenderResources` builds pipelines per (shader, pass, state) lazily. ROADMAP orders MAT before
  decals so that decal materials are written once, on the same API.
- **Bindless** (ADR 0018) is optional: devices without it draw per batch with per-material binding sets, and
  both paths must give the same image.
- **Pooled runtime data** outside saved Components: `ParticleWorld` (ADR 0047) and `AnimationWorld` keep
  runtime state the renderer reads; the renderer reads `AnimationWorld` through `RenderView::animation`.
- **Scripting API services** (ADR 0041): polled interfaces in header-only `*_api` targets, an adapter in the
  host, `Expected` results, a module API version raised per change (`kModuleApiVersion` is 13 on main; ANIM-5c
  takes 14).

### Options for the technique

1. **Clustered decals in the forward pass.** Decals are binned into the view's froxels like lights; every
   forward fragment walks the decals of its cluster and blends them into `SurfaceOutput` before lighting.
   No extra targets and correct lighting, but the decal's appearance must be evaluated *inside the receiving
   material's shader*: a decal can only be data (textures and factors in a bindless table or an atlas), not a
   Slang surface function, since one forward shader cannot call every decal shader. That contradicts the
   ordering decision (decals on MAT) and makes every forward pixel pay for the loop, also on devices without
   bindless.
2. **Deferred-style projected boxes into a decal buffer (DBuffer).** After the depth prepass, each decal draws
   its box; its fragment reconstructs the receiving surface from depth, evaluates the decal's own surface
   function and blends the result into a few screen-sized targets (base colour, normal, occlusion/roughness/
   metallic, each with a remaining-weight alpha). The forward pass then reads the DBuffer at its pixel and
   blends it into `SurfaceOutput` before lighting. Decals are real material shaders (a pass variant of MAT),
   cost scales with the pixels they cover, layering is hardware blending in draw order, and the forward
   pass's lighting stays the only lighting. Costs: three RGBA8 targets, a receiver-mask target written by the
   prepass, normals reconstructed from depth, and a decal pass that knows nothing of the receiver's own
   tangent frame. This is the approach shipped forward renderers use for material-changing decals.
3. **Mesh decals.** Geometry clipped from the receiver at spawn time, drawn as blended meshes over it. Exact
   on curved and skinned surfaces and cheap per pixel, but spawning needs CPU copies of the receiver's
   triangles (the renderer keeps none; physics meshes are not render meshes), each decal allocates its own
   vertices, and a blended mesh cannot change the lighting inputs of the surface below (its normal and
   roughness), only paint over the lit result.

Option 2 is the only one that keeps decals on the material system, works on the per-batch path without
bindless, and changes the receiver's lighting inputs. Mesh decals remain a later addition for skinned
receivers (Consequences).

## Decision

### Decal shaders (MAT)

- A `.shader` becomes a decal shader with the attribute `[Decal("<channels>")]` on its `Parameters` struct; the
  string lists, separated by spaces, the channels it writes: `BaseColor`, `Normal`, `ORM` (occlusion,
  roughness and metallic together) and `Emission`, at least one. `[Decal]` and `[ShadingModel]` together, an
  unknown channel or an empty list are compiler errors with file, line and column. `worldOffset()` in a decal
  shader is an error.
- Decal shaders implement the same `SurfaceOutput surface(SurfaceInput)`. For a decal, `SurfaceInput` holds
  the reconstructed receiver point (`worldPosition`), the receiver normal reconstructed from depth (`normal`),
  a tangent frame made of the decal's local x and y axes projected onto the receiver plane (`tangent`,
  `bitangent`), the decal's UV (`uv`: local x and y mapped to [0, 1], v down), the decal's colour (`color`),
  `customData` = (age in seconds, life fraction from 0 to 1, the fade weight of the Decal or the pool, the
  variant as a float), the view direction, the frame time and the decal's material. The fields of
  `SurfaceOutput` used are `baseColor`, `normal` (tangent space in that frame), `occlusion`, `roughness`,
  `metallic`, `emission` and `opacity` (the coverage that weights every channel); the others are ignored.
  `illusion/Decal.slang`, included for decal shaders, adds `decalAge()`, `decalLife()`, `decalVariant()` and
  `decalAtlasUv(uv, frame, columns, rows)` for atlases of variants (bullet hole 1 to 4 in one texture).
- Texture sampling in decal passes uses `SampleGrad` with UV gradients from the intersection of the
  neighbouring pixels' rays with the receiver's tangent plane, not screen-space derivatives of the
  reconstructed position, so mip selection does not break at depth edges.
- New cooked data (listed, not yet in headers): `enum class DecalChannels : u8` flags (`BaseColor = 1`,
  `Normal = 2`, `ORM = 4`, `Emission = 8`) with `decalChannelsName()` and `parseDecalChannels()` in
  `ShaderAsset.hpp`; `ShaderAsset::decalChannels` (0 for surface shaders); `ShaderPass::Decal` (writes the
  DBuffer) and `ShaderPass::DecalEmission` (adds emission to the HDR colour), `kShaderPassCount` 8; decal
  shaders compile only these two passes (the second only with `Emission`, the first only with another
  channel), always with `AlphaMode::Blend` as their key; the cooked shader format version is raised and the
  engine shader library hash changes, so every shader recooks. The passes live in `illusion/DecalPass.slang`;
  `ShaderCompiler` picks it for decal shaders.
- `builtin:/shader/decal` is the standard decal: base colour texture and factor (opacity from its alpha),
  normal map with strength, ORM texture and factors, emissive texture and factor, an atlas grid with the
  frame from the variant, and all four channels. It is compiled at build time like `builtin:/shader/standard`.
- A decal shader used by a `MeshRenderer` draws the error material; a `Decal` whose material's shader is not
  a decal shader draws nothing and counts as a resolver failure with that reason.

### The passes

- **Receiver mask.** Every prepass variant writes an R8_UINT target with the instance's decal layers: bits 24 to
  31 of `InstanceData::extra.w` (free today), from `RenderMesh::decalLayers` when the material receives decals
  and 0 otherwise. Blended surfaces, particles and UI never receive.
- **Decal pass**, after GTAO and before the forward pass, only in frames with a visible decal that writes
  `BaseColor`, `Normal` or `ORM`. Targets: DBufferA RGBA8_SRGB (premultiplied base colour, remaining weight in
  alpha), DBufferB RGBA8_UNORM (premultiplied world normal encoded as n / 2 + 1/2, remaining weight), DBufferC
  RGBA8_UNORM (premultiplied occlusion, roughness, metallic, remaining weight), cleared to (0, 0, 0, 1) and
  blended with colour = src x srcAlpha + dst x (1 - srcAlpha), alpha = dst x (1 - srcAlpha). Channels a shader
  does not write are masked off in its pipeline, so their target keeps the decals below.
- **Drawing a decal** is a unit box from `SV_VertexID` (no vertex buffer), front faces culled, no depth test or
  write, so a camera inside the box still sees it. The fragment loads depth, reconstructs the world point with
  the jittered inverse view-projection the forward pass uses, moves it into the box (discard outside
  [-1/2, 1/2] on each axis), discards when its receiver mask and the decal's `receiverLayers` share no bit,
  reconstructs the normal from the depth of the four neighbours (on each axis the side with the smaller depth
  difference, so silhouettes do not bend it), and weights the output by `opacity`, the record's opacity (`Decal::opacity`), its fade weight, the angle fade (1 up to
  `angleFadeStart` between the receiver normal and the box's +z axis, 0 from `angleFadeEnd`), the depth fade
  (to 0 over the last `depthFade` of the half depth towards the ±z faces) and the distance fade (over the last
  10 % of `drawDistance`).
- **Forward pass.** Every forward variant (bound and bindless, every shading model) reads DBufferA, B and C at
  its pixel after `surface()` and before lighting, behind a view constant that is off in frames without a
  decal pass: base colour = base x A.a + A.rgb; world normal = normalize(lerp(N, decoded B, 1 - B.a)) where
  the decoded normal is (B.rgb / (1 - B.a)) x 2 - 1; occlusion, roughness, metallic = own x C.a + C.rgb. The
  forward pass's depth test (equal to the prepass surface) guarantees the DBuffer pixel belongs to the surface
  being shaded. The shadow-receiver normal stays the geometric one.
- **Emission pass**, after the sky and before the fog is applied (so fog covers it): decals with `Emission` draw
  their boxes again with the `DecalEmission` variant, blending emission x weight additively (One, One) into
  the HDR colour. Emission is not lit, so it needs no DBuffer.
- **Motion vectors and TAA.** Decals write no motion vectors: a decal parented to its receiver (Hierarchy) or
  attached in the pool moves with it and the receiver's vectors are right; a decal sliding over its receiver
  is reprojected as the receiver, an accepted, documented ghost. Fading is a colour change only.
- **Shadows and depth** are never affected: decals are in no shadow, depth, id or Hi-Z pass.
- **Layout:** passes in order: shadows, depth prepass (+ receiver mask), GTAO, decals, fog volume, opaque
  shading, sky, decal emission, fog, blended, particles, post-processing. `RenderGpuTimings::decals` measures the
  decal and emission passes together.

### Sorting, culling and limits

- Decals are frustum-culled on the CPU by the bounding sphere of their box (parallel on the Job System above
  256 decals), skipped when `visible` is false, the Entity is `Inactive` or hidden by the view, the material
  is not resolved, or the distance fade is zero. More than `kMaxVisibleDecals` (4096) visible per view keeps
  the nearest (distance, then the order below) and counts the others in `RenderStats::decalsDropped`.
- Draw order is ascending `sortOrder`, then scene decals before pooled decals, then the material handle, then
  the Entity index (scene) or the spawn sequence (pool), so the order and the image are deterministic. Runs of
  one material within one `sortOrder` are one instanced draw with the material's binding set (the bound path
  on every device; decals never need the bindless table). Two overlapping decals with the same `sortOrder` and
  different materials therefore compose in material order: layer them with `sortOrder`.
- Per visible decal the renderer uploads one 160-byte record (decal-to-world and world-to-decal 3x4
  matrices, colour, the packed fades and angles, layers, material, variant, age) into a per-frame buffer of
  `kMaxVisibleDecals` records, reserved at creation; nothing is allocated per frame.
- Memory: the DBuffer is 12 bytes per pixel of the render resolution (24.9 MB at 1920x1080), created on the
  first frame that needs it and resized with the other targets; the receiver mask is 1 byte per pixel.
  `SceneRendererDesc::decals = false` creates neither and compiles the prepass without the mask, for games
  without decals.

### Data

- **Saved `Decal`** (`illusion/render/DecalComponents.hpp`, reflected as "illusion.Decal" with metres on
  `size`, radians on the angles, a colour hint and an icon): material, box size, colour, opacity, sort order,
  receiver layers, angle and depth fades, draw distance, atlas variant, visible. The box is the Entity's world
  `Transform` times `size`; it projects along its local -z (like a Camera looks), local x is u and local y is
  up in the image. `decalRotation(normal, twist)` gives the rotation that projects onto a surface with that
  normal.
- **Runtime `RenderDecal`** (new in `Components.hpp`, transient): `MaterialHandle material`, `u64 sourceKey`.
  `RenderAssetResolver::update()` resolves `Decal::material` like `MeshRenderer::material`, with the same
  pending, failure and reload rules.
- **New fields of existing structs** (added by the packages, not by this ADR, so CI's missing-initializer
  checks stay green): `MeshRenderer::decalLayers` (u32, default `kDefaultDecalLayers`, bits 0 to 7),
  `RenderMesh::decalLayers`; `MaterialAsset::receiveDecals` (bool, default true; `.material` key
  `receiveDecals`) and `MaterialDesc::receiveDecals`; `SceneRendererDesc::decals` (bool, default true);
  `RenderView::decals` (`const DecalPool*`, default null); `RenderStats::decals`, `decalDrawCalls`,
  `decalsDropped`; `RenderGpuTimings::decals`; `ProjectSettings` gains a `decals` section with `capacity`
  (default 1024) and `materialCapacity` (default 64); new functions `RenderAssetResolver::resolveDecals(DecalPool&)`
  and `GameSession::decals()`.
- **Skinned and moving receivers.** Rigid moving receivers keep their decals when the Decal is their child or
  the pooled decal is attached to them. Skinned receivers receive like any surface (`decalLayers` applies), but
  a box projection does not deform with the skin, so marks slide while the mesh bends: games keep characters
  on a layer their impact decals do not project onto, and use particles or a `BoneAttachment` child for rigid
  parts. Mesh decals for skinned receivers are open.

### Pooled decals and the scripting API

- **`DecalPool`** (`illusion/render/DecalPool.hpp`, CPU-only code in `illusion::render`; its header names
  `MaterialHandle`, so modules reach it only through `DecalService`) holds the decals game
  code spawns at run time, outside the ECS: a fixed array of `capacity` slots reserved at construction, an
  intrusive list in spawn order, and a table of up to `materialCapacity` decal materials interned by
  `AssetRef` (the only allocation, on a material's first use). `spawn()` takes a free slot, or evicts the
  oldest live decal when none is free (O(1), counted in `evictedCount()`); handles carry the slot's generation,
  so a stale handle fails with `NotFound`. `update(deltaSeconds)` ages decals, applies fade-in, fade-out at the
  end of the lifetime and explicit `fadeOut()`, retires finished ones, moves attached decals with their
  Entity (decals of a destroyed or `Inactive` Entity retire or hide), and computes world matrices. No
  allocation per spawn or update; results depend only on the calls and the steps, so fixed steps repeat
  exactly. Pooled decals are not saved in saved games (documented).
- **`GameSession`** owns one `DecalPool` per session, updates it in a new Decals step after Cameras (it reads
  this update's Transforms), resolves its materials in the Resolvers step through
  `RenderAssetResolver::resolveDecals()`, and the player and Play mode pass it as `RenderView::decals`.
- **`DecalService`** (`USES render`, `illusion::render_api`, module API version raised by one: the next free at
  landing, 15 if ANIM-5c lands first) with the adapter `DecalPoolService`: `material()`, `spawn()`, `fadeOut()`,
  `remove()`, `clear()`, `isAlive()`, `aliveCount()`, `capacity()`; errors are `DecalError`.

### Editor

- The Scene view draws each `Decal`'s box and its projection direction (`VisualiserColors::decal`, brighter
  when selected); the Inspector shows the reflected fields; Play mode's pool is listed, not drawn as boxes.
- Commands: `decal.list` (scene decals with their material and state; in Play the pool's counts),
  `decal.place` (creates a `Decal` Entity on the surface a ray from the viewport or given points hits, rotation
  from `decalRotation()`, undoable through `WorldEditor`), `decal.pool` (Play: alive, capacity, evicted,
  materials; `clear`).

### Budgets

- GPU benchmark `render/decals-1000` (local, Release, `gpu;benchmark`): 1,000 visible decals of four materials
  in two sort orders, 0.5 m boxes over a ground and walls at 1920x1080. Target on the reference desktop GPU
  (RTX 4070 SUPER): decal and emission passes together at most 0.6 ms, and the DBuffer reads in the forward
  pass at most 0.1 ms over the same frame without decals; the budget is set from the measurement (5 to 10x).
- `render/10k-instances` and `render/material-shaders` stay within 2 % with `SceneRendererDesc::decals` on and
  no decal in the World (the receiver mask is the only cost).
- CPU benchmarks: `render/decal-pool-update-4096` (target under 40 us) and `render/decal-spawn` (target under
  100 ns per spawn), budgets from the measurement.
- On lavapipe CI the GPU tests render 1,000 decals at 1920x1080 with zero validation errors and the expected
  counters (draw calls equal to the material runs, none dropped); time is not budgeted on a software
  rasterizer.

## Consequences

- DECAL-2 starts after MAT-4 (`.material` naming a `shader` with `parameters`) has landed; it does not carry
  that work itself.
- GPU tests on lavapipe check images, counters and zero validation errors, never time; time budgets are
  measured on a desktop GPU only, like every other render benchmark.
- Box projection slides on skinned receivers, so characters do not receive decals by default (their
  receiver layer is off); mesh decals for skinned surfaces are a later addendum after SHOOTER.
- Writing the receiver mask costs at most 2 % of `render/10k-instances`; `SceneRendererDesc::decals = false`
  removes it for renderers that never draw decals.

- Decals are ordinary material shaders that agents write and fix from compiler errors; the same `surface()`
  API serves surfaces and decals, and new decal looks need no engine code.
- Every forward variant of every shader gains three texture loads behind a uniform branch, and every shader
  recooks once for the new library hash and passes.
- The DBuffer costs 24.9 MB at 1080p while decals are in use, and the receiver mask 2 MB always unless
  `SceneRendererDesc::decals` is false.
- Decals change base colour, normal, occlusion, roughness and metallic of opaque and masked surfaces only;
  blended surfaces, water and particles do not receive them. Shading-model fields (subsurface colour, clear
  coat) are not decal channels.
- Normals reconstructed from depth are flat per pixel quad on very thin geometry and noisy on grazing angles;
  the angle fade hides most of it. A prepass normal target would fix it at a per-frame cost for every game.
- Box projections slide on skinned receivers; mesh decals (clipped geometry with skin weights) are the later
  answer if SHOOTER needs marks on characters. Decals projected per surface type (PLAY) build on `variant` and
  `material()`.

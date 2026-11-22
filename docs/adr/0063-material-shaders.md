# 0063. Material shaders: Slang surface functions in `.shader` assets, compiled by slangc at cook time

- Status: Proposed
- Date: 2026-11-22

## Context

Games can only set the parameters of the built-in PBR material (`.material` files, ADR 0035). A commercial
engine needs custom surfaces: animated emission, dissolve effects, triplanar rock, foliage wind, water-like
panning normals, unlit UI-in-world, and shading models beyond the default (subsurface skin, two-sided
leaves, clear-coated paint).

What exists and shapes the design:

- **Shaders** are Slang (`engine/render/shaders`), compiled at build time by a pinned prebuilt `slangc`
  (`cmake/Dependencies.cmake`) and embedded as SPIR-V (`illusion_add_shaders`). Nothing of Slang is linked
  into the engine; the player has no shader compiler.
- **Passes** that draw meshes: depth prepass, cascaded shadows, forward (bound and bindless/GPU-driven
  variants), motion vectors, entity ids and outline in the editor; skinned meshes reach them through the
  skinning pass's vertex buffers (ADR 0059), so vertex input is the same 48-byte `Vertex` everywhere.
- **Assets** are cooked into packs for export (ADR 0055); the editor watches files and hot-reloads assets
  (ADR 0045); `Process` runs child processes (the editor already builds game modules with CMake).
- **Agents** edit text: a shader must be a file an agent can write, compile and read errors from.

Options considered for **who compiles game shaders**:

- **Link the Slang library into the editor** (in-process, rich reflection API, fastest hot reload): a large
  binary dependency in the editor, and still none in the player, so cooking is needed anyway.
- **Run `slangc` as a child process** at cook time and on hot reload, reading its JSON reflection: one
  compiler for engine and game shaders, the player only loads SPIR-V, the editor ships the same pinned
  `slangc` it is built with. Slower per compile (hundreds of milliseconds), acceptable for edits.
- **A node graph that generates shaders**: friendly, but it needs the text path underneath anyway; it is a
  later layer on top of this one.

## Decision

- **Surface API.** The engine provides `illusion/Surface.slang`: a `SurfaceInput` (world position, normal,
  tangent frame, UVs, vertex colour when present, view direction, time, the Entity's custom data) and a
  `SurfaceOutput` (base colour, normal in tangent space, metallic, roughness, occlusion, emission, opacity,
  opacity mask threshold, and the shading model's extra fields), plus helpers (texture sampling through the
  material's slots, noise, triplanar). A material shader is a `.shader` file that declares its parameters and
  textures and implements `SurfaceOutput surface(SurfaceInput)` and optionally `float3 worldOffset(...)`.
  The built-in PBR material becomes the template `builtin:/shader/standard`, written against the same API.
- **Parameters.** Declared with attributes in the `.shader` file (float, vec2-4, colour, int, bool, texture
  2D with a default and import usage); `slangc` reflection gives their layout. A `.material` file gains
  `shader` (an `AssetRef`) and a `parameters` object by name; unknown names are errors, missing ones take the
  declared default. Parameter blocks are at most 256 bytes and eight textures per material.
- **Compilation.** `ShaderCompiler` (editor and cooker side, `editor/` and the asset cooker) runs the pinned
  `slangc` for each variant: pass (forward, forward bindless, depth prepass, shadow caster, motion vectors,
  entity ids), alpha mode (opaque, masked, blended) and whether `worldOffset` exists; it links the surface
  into the engine's pass shaders, writes SPIR-V per variant and the parameter layout into a cooked
  `AssetType::Shader` (cache keyed by the source hash, the engine's shader version and the compiler version).
  Errors keep the file, line and column and reach the Console, the Inspector and agents.
- **Runtime.** `RenderResources` loads cooked shaders, builds pipelines per (shader, variant, render state)
  lazily and caches them; a shader that failed or is still compiling draws with the standard template
  (editor: a checker error material). The bindless path keeps one parameter buffer per material instance and
  indexes textures through the existing bindless table, so custom materials stay GPU-driven.
- **Hot reload.** The editor recompiles a changed `.shader` on a background thread and swaps its pipelines at
  the next frame; materials keep their parameter values by name.
- **Shading models.** A `.shader` names one: `Default` (current PBR), `Unlit`, `Subsurface` (wrap lighting
  with a scatter colour and thickness), `TwoSidedFoliage` (the GFX-2d leaf translucency, generalised) and
  `ClearCoat` (a second specular lobe). The lighting code branches per model on a value in the G-data the
  forward pass already computes; no deferred buffer is added.
- **Shipping.** The editor's CPack component and the SDK carry the pinned `slangc`; the player carries only
  cooked SPIR-V. Exports fail with the compiler's message when a shader does not compile.

## Consequences

- Game shaders are text files agents can write and fix from compiler errors; the same compiler and version
  builds engine and game shaders, so behaviour matches across machines and CI.
- Variants multiply compile time and cache size (six passes x three alpha modes x offset), bounded because
  only used combinations are compiled and cached by hash.
- Custom shaders cannot change vertex input or add passes; post-process materials, decals (DECAL) and terrain
  layers (TERRAIN) build on the same surface API later. A node editor is open.
- `worldOffset` must be deterministic in time so shadows and motion vectors agree; it receives the frame's
  time and the previous frame's time.

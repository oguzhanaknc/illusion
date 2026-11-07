# 0050. Volumetric fog in a froxel grid: injected with shadowed light, temporally reprojected, integrated front to back

- Status: Accepted
- Date: 2026-11-07

## Context

The tech demo and games need air that light travels through: shafts of sunlight between trees, moonlight
in mist, the glow of a campfire or a neon sign. Distance fog alone (a colour blended by depth) cannot show
shafts, because it does not know which parts of the air are lit. The renderer already has cascaded shadow
maps for the main directional light, up to 16 point lights in the view constants, a sky that lights the
scene through spherical harmonics, and a `Wind` that moves vegetation and particles.

Options considered:

- **Analytic height fog.** A closed-form integral of exponential height fog per pixel. Cheap and smooth,
  but no shadows: no shafts, no glow that stops at a wall.
- **Ray marching per pixel** through the shadow map at half resolution, then upsampling. Sharp shafts, but
  cost grows with resolution and steps, blended surfaces and particles need their own march, and the noise
  needs a heavy blur.
- **A froxel grid** (Wronski 2014, Hillaire 2015): a low-resolution volume aligned with the view frustum,
  filled once per frame with the fog and its light, integrated front to back, then sampled by any surface
  with one filtered lookup. Cost depends on the grid, not the screen; every pass that draws (opaque, sky,
  blended, particles) uses the same result.

## Decision

- `VolumetricFog` (`illusion.VolumetricFog`, a saved, GPU-free singleton render Component) holds the
  medium (density, height falloff, base height, albedo), the lighting (Henyey-Greenstein anisotropy, light
  and ambient intensity), the reach (`maxDistance`) and wind-carried noise. The first active one counts.
- The grid has about 8x8 pixels per cell (at most 240 columns) and 64 slices spaced exponentially in view
  depth between `maxDistance * 0.005` (0.05 to 1 m) and `maxDistance`. Its volumes (RGBA16F) are made the
  first time a scene has fog and kept for the render size; binding sets are cached.
- `VolumetricFog.slang` has three compute passes, run by an internal `VolumetricFogRenderer`:
  - **inject**: per froxel, the density, the sky light (SH irradiance averaged over both hemispheres), every
    directional light (the shadowed one through one filtered tap of its cascade) and every point light with
    the forward shader's falloff. The sample moves along its slice every frame (Halton) and blends with the
    previous frame's value at the reprojected position (12% new), so shafts resolve without banding.
  - **integrate**: per column, energy-conserving front-to-back integration (in-scattered light and
    transmittance to the far side of each slice).
  - **apply**: fogs the HDR image by the depth buffer after the opaque surfaces and the sky; the sky counts as
    beyond the fog.
- Blended surfaces (`ForwardPbr.slang`, flag bit 1) and particles (`ParticlesCommon.slang`) sample the same
  volume through `FogSample.slang`; additive particles are dimmed but get no in-scattered light of their own.
- The view constants gain `fog` (slice mapping) and `fogScreen`; the scene HDR texture becomes a UAV. The fog
  passes run inside the shading timer.

## Consequences

- The cost is fixed by the grid: about 0.1 ms at 1280x720 on a desktop GPU with 16 point lights, plus three
  volumes while a scene has fog (about 22 MB at 1280x720, 50 MB at 1920x1080 and above). Benchmark
  `render/volumetric-fog` guards it.
- Shafts are as sharp as the grid: thin shadows (a branch) blur into the froxels around them, and a light
  change fades in over a few frames. `SceneRenderer::resetTemporalHistory()` drops the history on cuts.
- Point lights do not cast shadows into the fog until they have shadow maps (GFX-1c); the inject pass is
  where they will be read.
- Diagnostic views skip the fog, like post effects.

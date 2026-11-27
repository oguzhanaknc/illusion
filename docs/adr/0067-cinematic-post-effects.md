# 0067. Cinematic post effects: gather depth of field from a physical lens, tile-based motion blur and lens flares after TAA, with quality levels

- Status: Proposed
- Date: 2026-11-27

## Context

SHOOTER's cutscenes, aiming and menus need the look of a film camera: the background softens behind a
character in focus (depth of field), fast objects and quick turns smear (motion blur), and bright lights flare
on the lens and light up dirt on it. ROADMAP's POST milestone asks for these, driven by `PostEffects` and by
virtual cameras, with quality levels in `RenderSettings` and tests on lavapipe.

What exists and shapes the design:

- **The post chain** (ADR 0016, GFX-1a): the forward renderer writes an RGBA16F HDR target of the render size,
  reversed-Z depth with an infinite far plane (ADR 0013) and RG16F motion vectors in UV units, current minus
  previous position, from unjittered positions of every opaque object, skinned meshes included (ADR 0017).
  `PostProcessor::resolve()` then runs TAA (opt-in; Halton jitter, variance clipping), bloom (half-resolution
  mip chain, energy-preserving `lerp`), automatic exposure (metered on the GPU into a 1x1 texture kept between
  frames) and a full-screen tonemap pass that also grades and applies vignette, grain and chromatic aberration
  into the caller's target; `UiRenderer` draws the game UI afterwards. Blended surfaces and particles write no
  depth and no motion.
- **`PostEffects`** (saved singleton, "illusion.PostEffects" version 1): exposure, tonemapper, grading and the
  lens effects vignette, grain and chromatic aberration. `VolumetricFog` (ADR 0050) is a separate singleton.
- **`Camera`** ("illusion.Camera" version 2) has a field of view in radians, a near plane and a linear exposure;
  no lens. **Virtual cameras** (ADR 0064) produce a `CameraPose` (position, rotation, `fovY`, `nearPlane`,
  `dutch`), blend poses between shots, and `CameraWorld` writes the brain's `Camera` lens through reflection,
  since `engine/camera` must not link `engine/render`.
- **`RenderSettings`** (`settings:/render.json`) has `vsync`, `maxFramesPerSecond` and `renderScale`; no
  quality. The editor's Scene views take their quality from `ViewportQuality` (`postProcessSettingsOf()`).
- **Decals** (ADR 0066) come before POST and add their passes before shading; POST touches only the post chain.
- **Tests** run on Mesa lavapipe in CI: images and counters, never time. Desktop budgets come from the GPU
  benchmarks on the reference desktop GPU (RTX 4070 SUPER, as in ADR 0066).

### Options for depth of field

1. **Scatter (a sprite per pixel or per bright pixel).** The best bokeh, but the cost follows the blur, not the
   resolution; blending order of overlapping sprites is not fixed on every driver, so images are not
   repeatable on lavapipe; near-field occlusion still needs a separate solution.
2. **Separable blurs** (Gaussian, or the hexagonal three-pass blur). Cheap and fixed in cost, but the shape is
   fixed (a hexagon), there is no correct near field (in-focus pixels bleed into the foreground blur), and a
   blurred foreground cannot cover what is behind it.
3. **Scatter-as-gather at half resolution with separate near and far fields** (Jimenez 2014, the technique of
   shipped engines): the circle of confusion (CoC) of each pixel from the depth, tiles holding the largest
   near CoC dilated to their neighbours so a blurred foreground spreads over sharp pixels, a ring gather for
   each field where a sample counts when its own CoC reaches the pixel, polygon bokeh by mapping the ring
   samples onto the aperture's shape, a median post-filter, and a full-resolution composite. Fixed cost per
   pixel, deterministic, shape and aspect as parameters. Bright bokeh is softer than with scatter.
4. **Ray-traced or accumulated lens samples.** Offline quality; many frames or rays per image.

Option 3 is chosen. **The CoC is physical:** the thin-lens model from the focal length (derived from the field
of view and a sensor height), the f-number and the focus distance, so the same lens values give the same blur
at any resolution and a cinematographer's numbers (a 50 mm at f/1.8 on full frame) mean what they say. The
alternative, an artist's "focus range" in metres, is not physical and does not blend with the field of view.

### Options for the place in the chain

- **Before TAA:** TAA would smooth the gather's noise, but its neighbourhood clipping fights large blurs, the
  CoC edges jitter with the projection, and motion blur before TAA is reprojected and smeared again.
- **After TAA (chosen):** depth of field and motion blur read a stable, unjittered, anti-aliased image; TAA's
  history never holds blurred pixels, so the two features do not interact. The gather's noise is handled by
  enough samples and the median, at a fixed cost per quality level. Motion blur follows depth of field (both
  happen together in a camera; this order lets bokeh smear and keeps blur edges from refocusing). Bloom and
  flares follow both, so a blurred highlight blooms as its bokeh. Exposure meters the image before both, so
  focus and motion never change the exposure.

### Options for motion blur

1. **Camera-only blur from depth reprojection.** Already possible, but objects moving with the camera blur
   wrongly and moving objects do not blur.
2. **Per-object blur from the motion vectors with tile max and neighbour max** (McGuire 2012, with Jimenez
   2014's directional sampling): tiles store the dominant motion, the 3x3 neighbourhood of tiles bounds the
   search so blur spills past an object's silhouette, and a gather along the motion with soft depth tests
   keeps sharp objects in front of blurred backgrounds. Uses the existing RG16F vectors; skinned meshes blur
   because their previous positions exist.
3. **Accumulation of sub-frames.** Exact, costs a frame per sample.

Option 2 is chosen. **Shutter angle** (the film convention) sets the length: a pixel's blur is its motion
over one frame times the open fraction, so a higher frame rate shortens the blur as a real camera at the
same angle does; a fixed exposure time would keep the blur length at any frame rate but needs the frame time
on the GPU and blurs more at low frame rates. The **camera's share** of the motion is split off by
reprojecting the depth with the previous view-projection (as TAA does for the sky) and scaled separately: games
expose "camera motion blur" as a separate option.

### Options for lens effects

- **Lens dirt:** a texture multiplying the bloom and the flares, added on top of the energy-preserving bloom.
  Cheap and the expected look; the only choice is where the texture comes from (an `AssetRef`).
- **Flares:** screen-space ghosts and a halo from a thresholded, already blurred bloom level, mirrored through
  the image centre with a chromatic shift (cheap, every light flares, no per-light setup), versus sprite flares
  placed at each light with a visibility test (art-directed per light, needs occlusion queries and an asset
  format for flare elements), versus a simulated lens system (physically exact, expensive). Screen-space ghosts
  are chosen now; sprite flares for the sun are an addition later (Consequences).
- **Chromatic aberration** exists in the tonemap pass and stays there.

### Options for the saved data

- **Everything on `PostEffects`:** one place, but the struct grows to forty fields, and "is depth of field on"
  becomes a bool that every scene file saves.
- **New singletons per effect (chosen for depth of field and motion blur):** an active `DepthOfField` or
  `MotionBlur` turns the effect on, as `VolumetricFog` does; scenes without them save nothing and the renderer
  creates no textures and records no passes. Lens dirt and flares stay on `PostEffects`, whose purpose is
  already "lens effects" and which owns bloom-related look settings.
- **Per shot:** a lens is a property of the camera, not of the scene. The focus distance, the f-number and the
  shutter angle go on `Camera` (and the sensor height, which with the field of view gives the focal length), so
  `CameraWorld` writes them per shot exactly as it writes `fovY`, and blends them. The alternative, a blended
  override of any `PostEffects` field per virtual camera (post-process volumes), is more general but blends
  reflected structs field by field; it is left for a later decision with spatial volumes.

## Decision

### Saved data

- **`Camera`** becomes "illusion.Camera" version 3 (version 2 files load unchanged; new fields take their
  defaults): `focusDistance` (f32, 10 m, `Unit::Meters`), `fStop` (f32, 5.6, 0.7 to 64, log slider),
  `sensorHeight` (f32, 0.024 m shown in millimetres: a new `Unit::Millimeters` that converts with `Meters`).
  Version 4 adds `shutterAngle` (f32, pi radians shown in degrees, 0 to 2 pi) with motion blur. The f-number
  does not change the exposure: `Camera::exposure` and `PostEffects` keep exposure separate from the lens, as
  games expect.
- **`DepthOfField`** ("illusion.DepthOfField", singleton, `CinematicEffects.hpp`): focus mode (`Manual`:
  `Camera::focusDistance`; `ScreenPoint`: the depth at `focusPoint`, metered on the GPU), `focusSpeed`,
  `maxBlur` (largest CoC radius as a fraction of the image height, so the look is the same at any resolution
  and render scale), `nearScale`, `farScale`, `bladeCount` (0 circle, 3 to 9 polygon), `bladeRotation`,
  `bokehAspect`.
- **`MotionBlur`** ("illusion.MotionBlur", singleton): `intensity`, `cameraBlur`, `maxBlur`.
- **`PostEffects`** becomes version 2 (version 1 loads unchanged), new fields in a "Lens" category: `lensDirt`
  (`AssetRef` to a texture, empty for none), `lensDirtIntensity` (0), `flareIntensity` (0: no flares),
  `flareThreshold` (1.0, brightness after exposure above which light flares), `flareGhosts` (u32, 4, 0 to 8),
  `flareGhostSpacing` (0.3, fraction of the distance to the centre), `flareHalo` (0.6, halo radius as a fraction
  of the half diagonal; 0 for none), `flareChromaticShift` (0.01, fraction of the image width).
- **`VirtualCamera`** gains `focusDistance`, `fStop`, `shutterAngle` (defaults as on `Camera`), `focusTarget`
  (Entity; null for none), `focusOffset` (metres added to the target's distance) and `focusDamping` (smooth
  time in seconds). With a focus target, `CameraWorld` sets the shot's focus distance each update to
  `focusDistanceTo()` the target's world position plus the offset, damped with `smoothDamp()` (CAM-1);
  `VirtualCameraState` gains `focusVelocity`. `CameraPose` gains `CameraLens lens` (`CameraLens.hpp`):
  `blendCameraPoses()` blends it with `blendCameraLenses()` (focus in dioptres, aperture in stops, shutter
  linearly), and `CameraWorld` writes `focusDistance`, `fStop` and `shutterAngle` to the brain's `Camera`
  through reflection beside `fovY`. The sensor height stays on `Camera` only (a property of the body, not of
  the shot).
- **`CameraBrainState`** gains `cuts` (u64): incremented when the live camera changes with a `Cut` blend or a
  zero-length one. Hosts (the player, Play mode) set `RenderView::cameraCut` when it changed since their last
  frame.

### Settings and quality

- **`RenderSettings`** gains `quality` (`RenderQuality`, `High`), `depthOfField` (bool, true) and
  `motionBlur` (bool, true): the player's options switch the effects off without editing scenes (motion blur
  is an accessibility option). The host turns `quality` into `PostProcessSettings::quality` with
  `postQualityOf()` and skips an effect whose switch is off.
- **`PostProcessSettings`** gains `quality` (`PostQualitySettings`). The table:

  | | Low | Medium | High | Ultra |
  | --- | --- | --- | --- | --- |
  | Depth of field rings (taps) | 2 (25) | 3 (49) | 4 (81) | 5 (121) |
  | Median post-filter | no | yes | yes | yes |
  | Motion blur taps | 6 | 8 | 12 | 16 |
  | Lens flares | no | yes | yes | yes |

  The depth of field always gathers at half resolution and composites at full resolution; quality changes the
  taps, not the resolution, so the shape and size of the blur are the same at every level. Existing passes
  (GTAO, bloom, TAA, shadows) keep their own settings; mapping them to `RenderQuality` is a later step.
- **The editor:** `ViewportQuality` gains `postQuality` (`RenderQuality`, High), `depthOfField` (true) and
  `motionBlur` (false: a flying Scene view camera would smear), applied by `postProcessSettingsOf()`; the Game
  view uses the project's `RenderSettings`.

### Rendering

- **Chain:** TAA (when on), then focus metering (ScreenPoint), exposure metering of that image, depth of
  field, motion blur, bloom of the result, flares from the bloom chain, tonemap (bloom, flares, lens dirt,
  grading, vignette, grain, chromatic aberration), then the UI. Each effect reads the previous image and writes
  its own full-resolution RGBA16F target, created on the first frame it runs and kept until a resize;
  `RenderViewTargets::hdr` stays the scene colour before post. Debug views (`RenderDebugView` other than `Lit`),
  orthographic cameras (depth of field only) and an absent or Inactive singleton skip a pass; a frame without
  the singletons and lens fields at their defaults is byte-identical to before.
- **`RenderView`** gains `lens` (`ThinLens`, filled by `fromCamera()` with `thinLensOf()`), `shutterAngle` and
  `cameraCut` (bool: history is invalid this frame, as after `resetTemporalHistory()`: TAA restarts, motion blur
  is skipped, the ScreenPoint focus snaps).
- **Depth of field:** (1) CoC per full-resolution pixel from the linear view depth with `circleOfConfusion()`,
  scaled by `nearScale`/`farScale`, clamped to `maxBlur`, stored signed in R16F; (2) a half-resolution prefilter
  of colour and CoC (four taps weighted by CoC similarity, no Karis average, so bokeh keeps its brightness);
  (3) 16x16-pixel tiles of the largest near CoC and the largest far CoC, the near value dilated over the 3x3
  neighbour tiles; (4) a gather per field over `depthOfFieldRings` rings of 8 times the ring index samples,
  placed by Shirley's concentric map onto the unit disc and then onto the `bladeCount` polygon turned by
  `bladeRotation` and stretched by `bokehAspect`; a far sample counts when its CoC reaches the pixel and it is
  not in front, near samples accumulate with a coverage alpha; tiles whose CoC is under half a pixel take an
  early exit; (5) an optional 3x3 median; (6) a full-resolution composite: the far blur blended by the pixel's
  CoC, the near blur over it by its alpha. A pixel whose CoC is under half a pixel and that no near blur covers
  is copied unchanged. The ScreenPoint focus takes the median of 5x5 depth samples over a window of 2 % of the
  image height at `focusPoint`, adapts with `1 - exp(-deltaSeconds * focusSpeed)` in dioptres into a 1x1 R32F
  texture kept between frames (as the exposure), and the CoC pass reads it on the GPU (no readback).
- **Motion blur:** the camera's motion per pixel from depth and `previousViewProjection`; the pixel's motion is
  `cameraBlur * camera + (vector - camera)`, times `shutterFraction(shutterAngle) * intensity`, clamped to
  `maxBlur`; tile max in two passes (8x8, then to `motionBlurTileSize()`), neighbour max over 3x3 tiles, then
  `motionBlurSamples` taps along the neighbour-max and the pixel's own direction with soft depth and velocity
  tests (McGuire 2012) and interleaved-gradient-noise offsets fixed per pixel (not per frame). Tiles with less
  than half a pixel of motion copy their pixels unchanged.
- **Flares:** a compute pass at the resolution of bloom level 2 reads that level, subtracts `flareThreshold`
  (after exposure), and sums `flareGhosts` copies mirrored through the centre at `flareGhostSpacing` steps and a
  halo ring at `flareHalo`, each with a red/blue shift of `flareChromaticShift`, faded towards the image edges;
  the tonemap adds `flareIntensity` times its bilinear upsample. Without bloom there are no flares.
- **Lens dirt:** the tonemap adds `(bloom + flares) * dirt * lensDirtIntensity`, the dirt texture stretched
  over the image; bloom stays energy-preserving without dirt.
- **Determinism:** fixed sample patterns and per-pixel noise that does not depend on the frame index, no float
  atomics, reductions in a fixed order, the ScreenPoint focus from `deltaSeconds` only; the same frames give
  the same bytes on one device.
- **Counters and timings:** `RenderStats::cinematicPasses` (dispatches of the three effects recorded this
  frame) and `RenderGpuTimings::depthOfField` and `motionBlur` (flares stay in `post`);
  `RenderDebugView::CircleOfConfusion` (near blue, far orange, sharp black).

### Editor and scripting

- Inspectors come from reflection. The Scene view draws the selected `Camera` or `VirtualCamera`'s focus plane
  and its sharp range (`focusRange()` at half a pixel at 1080p) inside its frustum
  (`VisualiserColors::focusPlane`); the viewport shading menu and command gain `CircleOfConfusion`; a command
  `camera.focus` (undoable) sets the focus distance of a camera or virtual camera from an Entity, a point or a
  distance; `camera.status` reports the lens.
- `CameraService` (module API version raised by one, the next free at the time) gains `setFocusDistance()`,
  `setFocusTarget()` and `setAperture()` with `CameraWorldService`; `DepthOfField` and `MotionBlur` are data a
  module writes like any Component, so they get no service.

### Targets on the reference desktop GPU (High; GPU timings, budgets from the measurement)

| | 1920x1080 | 3840x2160 |
| --- | --- | --- |
| Depth of field (Ultra) | 0.45 ms (0.7 ms) | 1.5 ms (2.4 ms) |
| Motion blur (Ultra) | 0.25 ms (0.35 ms) | 0.9 ms (1.2 ms) |
| Flares and dirt | 0.08 ms | 0.15 ms |

Scenes without the singletons keep `render/10k-instances` and `render/empty-frame` within 1 %.

## Consequences

- The focal length is derived from the vertical field of view and the sensor height, never stored, so a
  camera has one source of truth for its framing.
- Only the new effects follow `RenderQuality` for now; GTAO, bloom and shadows join it with PLAY's run-time
  settings work, which also lets modules change `RenderSettings` from an options menu.
- Modules mark a camera cut through `CameraService` in POST-5, so game-driven cuts skip motion blur too.
- Sun sprite flares and post-process volumes are later work.

- Three new full-resolution RGBA16F targets at most (about 200 MB at 4K with all effects; created only when used)
  and a few half-resolution and tile textures.
- Blended surfaces and particles write no depth or motion: they take the focus and the motion of what is
  behind them (glass in front of a blurred background blurs with it; a fast particle does not smear). A
  later pass could write depth for chosen blended materials.
- A camera teleported by game code without a brain is not a cut: its first frame blurs by the clamped maximum,
  as TAA ghosts today. Modules have no way to mark a cut yet.
- Gather bokeh is softer than scatter bokeh; sprite flares for the sun and post-process volumes that blend any
  look setting per area or shot are later decisions.
- `Camera` grows by four floats; `CameraPose` and `VirtualCamera` by their lens. Camera code that copies
  poses gets the lens for free.
- Revisit if upscaling (M9) runs before post: depth of field and motion blur would then run at output
  resolution after the upscaler, which is the order shipped upscalers expect.

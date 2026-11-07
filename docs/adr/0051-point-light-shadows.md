# 0051. Point light shadows: opt-in per light, a budget of cube maps in one array, reversed depth, PCSS

- Status: Accepted
- Date: 2026-11-07

## Context

Only the main directional light cast shadows (cascaded PCSS, ADR 0013 for depth). At night the tech
demo's campfire and lanterns light trees, rocks, benches and the player without shadows, so everything
glows from inside, and the volumetric fog (ADR 0050) glows behind walls. Point lights are cheap today
(up to 16 in the view constants, shaded in one loop); a shadow for each would multiply the caster draws.

Options considered:

- **Dual-paraboloid maps**: two draws per light instead of six, but strong distortion on low-polygon
  casters (straight edges bend between vertices) and a seam at the equator.
- **Hardware cube map arrays** (`TextureCubeArray`): seamless filtering across faces, but it needs the
  `imageCubeArray` device feature, which the renderer does not enable today, and comparison filtering with
  a cube array is a second sampling path next to the cascades' 2D arrays.
- **Cube faces as layers of a 2D array**, with the face chosen in the shader. Six draws per light, no
  new device feature, the same sampling code as the cascades. Filtering across a face edge needs care.
- **Ray-traced or screen-space shadows**: not available on every target GPU (and lavapipe in CI), and
  screen-space shadows miss off-screen casters.

## Decision

- `PointLight` (saved) gains `castShadows` (default false, so old scenes render unchanged), `sourceRadius`
  (metres, softness), `shadowBias` (metres) and `shadowNearPlane` (metres; casters closer than this, such as
  a lantern's housing, are ignored). All have defaults; the Component's version stays 1.
- `ShadowSettings` gains `pointLightCount` (budget, 0 to `kMaxShadowedPointLights` = 4) and
  `pointResolution` (face size, 512 by default). `enabled` turns off point shadows too.
- Each frame the renderer picks, among the first 16 active point lights, those with `castShadows` whose
  range sphere intersects the view frustum, sorted by the distance from the camera to that sphere (stable
  order on ties), and gives the first `pointLightCount` a slot. No allocation: fixed arrays and an
  insertion sort.
- One `D32` 2D texture array holds six layers per slot. Faces follow the Vulkan cube map face table
  (`pointShadowFaceMatrix()`, `pointShadowFace()` in `Shadows.hpp`, unit tested against it). Each face is a
  perspective projection with **reversed, infinite depth** `near / distance` (ADR 0013): float precision is
  spent where shadows are seen, and no far plane needs fitting. The faces' field of view is a little wider
  than 90 degrees (`kPointShadowFaceScale` = 16/15), so a border of 1/32 of the face holds what the filter
  reads past the edge; penumbrae are clamped to that border.
- Casters are drawn with the existing `ShadowCaster.slang` path (instanced batches by mesh, wind
  displacement, alpha masks), from the same culling job: a range test per shadowed light, then a frustum
  test per face. Shadow views are numbered after the cascades (`kMaxShadowCascades + 6 * slot + face`)
  in the push constant and the caster pass bits; the face matrices live in `ViewConstants::pointShadows`.
  The pipelines compare with `GreaterOrEqual`, clear to 0 and use a negative slope bias.
- `ShadowReceiver.slang` filters with PCSS: a 12-tap blocker search over the border's reach, then a
  16-tap rotated Vogel disc with a radius from `sourceRadius`, blocker and receiver distances. The
  comparison sampler compares with `LESS`, so visibility is `1 - comparison` on reversed depth.
- The volumetric fog's injection reads one filtered tap per froxel and shadowed light.
- `RenderStats` reports `shadowedPointLights`, `pointShadowCasters` and `pointShadowDrawCalls`; the
  directional counters keep their meaning.

## Consequences

- Shadows from flickering fires and moving casters, including through the fog, at a cost that the budget
  bounds: at most 24 face passes. `render/point-light-shadows` (2k cubes, four lights) sets the budget.
- Filtering does not cross cube faces: a penumbra wider than 1/32 of a face is clamped, and very large
  `sourceRadius` values give no softer result. Moving to hardware cube arrays (seamless) or a filter that
  walks into the neighbouring face would lift that; this is the place to revisit if large soft lights
  matter.
- Lights beyond the budget, or beyond the first 16 point lights, light without shadows; a light that
  enters or leaves the budget pops its shadow. A fade by priority could hide that later.
- Six draw passes per light favour few shadowed lights; many small shadowed lights would want an atlas
  with per-light resolution or a cache of static casters.

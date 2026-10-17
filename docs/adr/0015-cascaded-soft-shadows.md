# 0015. Cascaded shadow maps with PCSS for one directional light

- Status: Accepted
- Date: 2026-10-17

## Context

The brief asks for soft shadows with CSM + PCSS. Decisions: which lights cast shadows, how cascades are
fitted, how casters are culled and drawn, the depth convention of the shadow map, and the filtering.

## Decision

- **One shadowed light:** the first `DirectionalLight` with `castShadows`. Point-light shadows come later.
- **Up to 4 cascades** in one `D32` texture array (default 2048², shadows up to 80 m), split by the
  practical scheme (blend of uniform and logarithmic, `splitLambda`).
- **Stable fitting:** each cascade bounds its view slice with a sphere computed in view space (size
  independent of camera rotation), and its light-space centre snaps to whole texels. The box extends
  `casterDistance` towards the light so casters outside the view still cast. The math is a public,
  unit-tested function (`computeShadowCascades`).
- **Standard depth (0 nearest the light) for shadow maps**, unlike the reversed-Z camera: NVRHI's
  comparison samplers always compare with `LESS`, which then means "lit", and orthographic depth has
  uniform precision anyway.
- **Culling once:** the parallel culling pass tests every entity against the camera frustum and each
  cascade and records a pass mask; per-pass instance blocks share the instance buffer. Opaque casters
  batch by mesh only; alpha-masked casters keep their material for alpha testing; blended materials do
  not cast. Casters render both faces with slope-scaled bias.
- **PCSS filtering:** a 16-sample blocker search sized by the light's angular diameter, then a 24-sample
  Vogel-disc PCF with hardware 2x2 comparison filtering, both rotated per pixel by interleaved gradient
  noise. Receivers use normal offset and depth bias. Cascades are crossed with a dithered transition and
  shadows fade out over the last 10% of the distance.

## Consequences

- Penumbrae are capped at 5% of a cascade's width to bound the kernel; very large lights get softer
  only up to that limit.
- Four cascades redraw the casters four times; GPU-driven culling (M3e) will cut CPU cost.
- The noise-rotated kernels are meant to be resolved by TAA (M3d); without it, wide penumbrae show
  slight grain.

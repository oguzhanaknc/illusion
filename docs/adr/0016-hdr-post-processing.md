# 0016. HDR pipeline with depth prepass, GTAO, TAA, bloom and tonemapping

- Status: Accepted
- Date: 2026-10-17

## Context

Until now the forward pass tonemapped directly into the caller's target. The brief asks for an HDR
pipeline with GTAO, bloom, tonemapping and optional TAA. Choices: where ambient occlusion comes from and
how it reaches shading, the order of the passes, how bloom stays energy-preserving, and how TAA finds
history without per-object motion vectors.

## Decision

- **Internal HDR target** (RGBA16F) and depth, sized to the output. Shading writes linear radiance;
  exposure, ACES tonemapping and sRGB encoding happen in a final full-screen pass into the caller's
  target. The clear colour becomes radiance.
- **Depth prepass** for opaque and alpha-masked geometry with the same vertex shader as shading, so the
  shading pass tests `GreaterOrEqual` without writing depth and has no overdraw. Prepass and shading
  each issue one instanced draw per batch.
- **GTAO** (Jimenez 2016) computed from the prepass depth before shading: 2 slices × 4 steps per side,
  normals reconstructed from depth, horizons sampled with bilinear depth (exact on planes; snapping to
  pixel centres made grazing surfaces self-occlude), then a depth-aware 5×5 blur that also clamps the
  per-slice estimate. It multiplies indirect diffuse light and, through Lagarde's specular-occlusion
  fit, indirect specular light. Blended surfaces do not use it.
- **Pass order:** shadows, prepass, GTAO, shading (opaque, sky, blended), TAA, bloom, tonemapping.
- **Bloom:** 13-tap downsample with a Karis average on the first level and tent upsampling that
  accumulates all levels; the sum is divided by the level count and mixed in with `lerp`, so a uniform
  image is unchanged (the white-furnace test still holds).
- **TAA (opt-in):** Halton (2, 3) jitter over 8 frames applied to the shading projection only (culling
  and shadows use the stable one), reprojection of the closest depth in the 3×3 neighbourhood with the
  previous unjittered view-projection (homogeneous, so the sky reprojects too), variance clipping in
  YCoCg, blending after a reversible tonemap.

## Consequences

- One extra geometry pass (depth only) and a few full-screen compute passes per frame; textures are
  recreated only on resize or setting changes and all binding sets are cached.
- TAA reprojects with camera motion only: moving objects rely on neighbourhood clipping and can show
  slight trails until per-object motion vectors arrive. It is therefore off by default.
- Auto exposure is not part of this step; `Camera::exposure` stays manual.

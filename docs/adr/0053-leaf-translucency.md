# 0053. Leaf translucency: thin-surface diffuse transmission in the glTF model

- Status: Accepted
- Date: 2026-11-08

## Context

Foliage in the tech demo is made of alpha-masked, double-sided cards. Lit from behind, a card only
receives sky light, so trees against the low sun look like dark cut-outs, while real leaves glow
yellow-green. GFX-2d of the roadmap asks for thin-surface transmission in foliage Materials.

Options considered:

- **Subsurface scattering** (screen-space or with a diffusion profile): models light travelling inside
  thick media such as skin. Leaves are thinner than a pixel; a blur pass costs bandwidth for nothing.
- **A wrap-lighting hack** (`saturate((n·l + w) / (1 + w))`): brightens the back side, but it is not
  energy-conserving, has no physical parameter and does not match any authoring tool.
- **Thin-surface diffuse transmission**, as glTF's `KHR_materials_diffuse_transmission` defines it: a
  fraction of the diffuse lobe moves to the opposite hemisphere, tinted by a colour. Blender and other
  tools export it, it conserves energy, and it costs one extra term per light.

## Decision

- `MaterialData` gains `diffuseTransmission` (0 to 1, default 0) and `diffuseTransmissionColor` (linear,
  default white). They are saved in `.material` files (defaults keep old files valid) and in cooked models
  (`kModelFormatVersion` 4, so old caches are re-imported).
- The glTF importer reads the extension's two factors and accepts the extension when it is required. Its
  two textures are not read: foliage tints come from the base colour texture, and two more texture slots
  in every material record are not worth it yet.
- The GPU material constants gain a `float4` (rgb: factor × colour, w: factor), clamped to [0, 1] with NaN
  as 0 when the material is created. The record grows from 96 to 112 bytes.
- The forward shader splits the diffuse lobe: `(1 − t)` of it reflects as before, and light from the back
  hemisphere arrives as `t · colour · albedo / π · saturate(−n·l)`, scaled by `1 − F0` for the
  surface's dielectric layer. Image-based diffuse light does the same with the spherical-harmonics
  irradiance of `−n`, evaluated only when `t > 0`. Metals have no albedo, so they do not transmit.
- The shadow test of a translucent surface offsets the receiver towards the side the light reaches, not
  towards the viewer; otherwise a back-lit leaf would sit behind itself in the shadow map.

## Consequences

- Back-lit foliage glows, and a leaf behind a trunk or another leaf still darkens through the shadow map.
- Under a uniform sky a white translucent surface looks the same as an opaque one, which a test checks.
- Light passing through several leaves is not modelled (no thickness or volume); shadows decide whether
  light reaches a leaf at all.
- Transmission textures can be added later as new texture slots without changing the factors.

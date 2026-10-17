# 0014. Image-based lighting: GPU-baked cube maps, SH irradiance, multiple-scattering split sum

- Status: Accepted
- Date: 2026-10-17

## Context

The forward renderer (ADR 0013) only had direct lights and a constant ambient term, so metals looked
black and scenes flat. The brief asks for IBL from HDRIs (Poly Haven). Choices: where and when to
prefilter, how to represent diffuse irradiance, how to approximate the specular integral, and how game
code selects and orients an environment.

## Decision

- **Bake on the GPU at load time** with compute shaders, from an equirectangular image of any format:
  a background cube map (RGBA16F, full mip chain), a GGX-prefiltered specular cube map (RGBA16F,
  up to 6 mips mapping linearly to perceptual roughness, filtered importance sampling) and a 128x128
  split-sum BRDF table baked once per `RenderResources`. The source image is converted to half floats
  on the CPU because 32-bit float filtering is optional in Vulkan.
- **Diffuse irradiance as 9 spherical-harmonics coefficients** computed on the CPU (exact, testable,
  about 20 ns per texel, so roughly 40 ms for a 2K image) and passed in the view constants. No
  irradiance cube map.
- **Split sum with multiple-scattering compensation** (Fdez-Agüera 2019): rough metals keep their
  energy, verified by a white-furnace GPU test.
- **ECS-driven:** the first `SkyLight` (with a Transform for orientation) selects the environment;
  without one, the uniform `ambient` radiance is used through the same SH path.
- **Equirectangular convention:** image centre faces -z, u towards +x, top row up, matching Poly Haven
  images and the engine's camera forward axis.
- **No HDRIs in the repository.** Tests use generated environments; the sample defaults to a procedural
  sky and accepts any `.hdr` file.

## Consequences

- Environments cost about 3 MB (512 background + 256 specular faces) and tens of milliseconds to bake
  (mostly the CPU SH projection); swapping between baked environments is free, baking a new one at
  run time is not. Projecting from a downsampled image can make the CPU part cheaper if needed.
- SH irradiance cannot represent very sharp lighting (a small, bright sun): use a DirectionalLight for
  the sun, as the sample does.
- Local reflection probes, specular occlusion and time-of-day sky models are future work.

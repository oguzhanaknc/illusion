# 0018. Bindless materials, geometry pools and multi-draw indirect

- Status: Accepted
- Date: 2026-10-17

## Context

After GPU occlusion culling (ADR 0017) every opaque batch of (mesh, submesh, material) still needed its
own state change and indirect draw per phase and pass. The brief asks for bindless, GPU-driven
rendering where supported. Decisions: how geometry and materials are shared, which device features are
required, and what happens on devices without them.

## Decision

- **Geometry pools:** all meshes live in one vertex pool and one index pool. Meshes record offsets;
  draws pass the vertex offset and first index (indirect arguments carry them per draw). Pools grow by
  doubling with a GPU copy; meshes are never freed yet (an asset-streaming concern, M4).
- **Material table:** every material is an 80-byte record (constants, five texture indices, sampler
  index) in one structured buffer, indexed by the material handle. The nine sampler variants form a
  sampler array. Per-material binding sets remain for the fallback path, blended surfaces and shadow
  casters.
- **Bindless textures:** an NVRHI descriptor table (update-after-bind, partially bound) in descriptor
  set 1 holds every texture at its handle index, up to `kBindlessTextureCapacity` (16384). Shaders index
  it with `NonUniformResourceIndex`, because one draw mixes materials.
- **Batches and multi-draw:** in bindless mode instances carry their material index, so a batch is
  (pipeline, mesh, submesh) and different materials share it. Consecutive batches with the same pipeline
  (opaque or masked, single or double sided) are drawn by one `drawIndexedIndirect` with a draw count;
  each draw's `firstInstance` holds its draw-list offset (`SV_StartInstanceLocation` in the shader).
- **Requirements and fallback:** bindless mode needs descriptor indexing with non-uniform sampled-image
  indexing, partially bound and update-after-bind sampled images, and multi-draw indirect with
  `drawIndirectFirstInstance` (`GpuInfo::bindless`, `GpuInfo::multiDrawIndirect`). Otherwise, or with
  `SceneRendererDesc::bindless = false`, the per-batch path of ADR 0017 is used. The same shader source
  builds both variants (`ForwardPbrBindless.slang` defines `ILLUSION_BINDLESS`). A GPU test renders a
  mixed scene both ways and requires the same image.

## Consequences

- With many materials on few meshes, draw calls drop to a handful per pass (10k cubes in 50 materials:
  232 → 183 ns per instance on an RTX 4070 SUPER).
- Two shader variants and two draw paths must be kept in step; the comparison test guards them.
- Shadow casters and blended surfaces still draw per batch.

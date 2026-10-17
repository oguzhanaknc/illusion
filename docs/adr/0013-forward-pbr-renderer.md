# 0013. Forward PBR renderer with reversed-Z, CPU culling and instancing

- Status: Accepted
- Date: 2026-10-11

## Context

M3 starts the scene renderer. It needs a depth convention, a shading model, a way to get ECS entities
on screen with few draw calls, and an asset path from glTF to the GPU. Bindless and GPU-driven
submission are planned (M3e) but need a working baseline to compare against.

## Decision

- **Reversed-Z, infinite far plane.** `perspectiveReversedZ()` maps the near plane to depth 1 and
  infinity to 0; depth is cleared to 0 and tested with `GreaterOrEqual` on a `D32` target. This gives
  near-uniform float precision over distance and removes the far-plane parameter.
- **Forward shading, glTF metallic-roughness PBR.** GGX distribution, height-correlated Smith
  visibility, Schlick Fresnel; base colour, metallic-roughness, normal, occlusion and emissive textures;
  alpha mask and blend; up to 4 directional and 16 point lights per view. IBL, shadows and
  post-processing (M3b–M3d) build on this pass instead of replacing it.
- **ECS-driven, CPU-culled, instanced.** `MeshRenderer` + `Transform` entities are frustum-culled in
  parallel on the Job System (bounding spheres), sorted by pipeline, material and mesh, and drawn
  with one instanced draw per run of identical (mesh, submesh, material). Instance transforms go to a
  structured buffer.
- **Separate `illusion::assets` module** for CPU-side data (images, meshes, glTF via cgltf, images via
  stb_image) so tools and tests can load assets without a GPU. `RenderResources` uploads them and hands
  out small index handles.
- **Material binding set per material** (space 1) and one view binding set per frame (space 0); per-draw
  data is a push constant.

## Consequences

- Draw-call count scales with distinct (mesh, material) pairs, not entity count.
- Uploads are synchronous for now; async streaming is M4.
- Per-material binding sets cost descriptor switches; bindless (M3e) removes them.
- Code that builds projection matrices must use `perspectiveReversedZ()`; `glm::perspective()` no longer
  matches the depth test.

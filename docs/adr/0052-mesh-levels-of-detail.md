# 0052. Mesh levels of detail: index ranges per submesh, made on import with meshoptimizer, chosen by projected error

- Status: Accepted
- Date: 2026-11-08

## Context

Every instance is drawn at full detail, however small it is on screen. The tech demo's forest (`samples/techdemo`, about
14,700 entities) spends most of its triangles on far trees, rocks and plants
that cover a few pixels; its `make_scene.py` works around this with hand-made `_far` copies of some trees.
GFX-2 asks for levels of detail for every Mesh, generated on import, chosen per instance by screen size,
also for shadows, later with dithered cross-fades and impostors.

Options considered for the data:

- **Separate Meshes per level** (`<model>#mesh/N/lod/K`): each level is its own asset with its own vertex
  buffer. Simple to draw, but it multiplies vertex memory, needs new asset paths and a resolver that knows
  about levels, and every `RenderMesh` would have to name a level.
- **Index ranges in the same vertex buffer**: simplification only removes vertices, so a level is a new index
  list over the same vertices. Vertex memory stays the same; each level adds roughly half the indices of the
  previous one. `RenderMesh`, `MeshRenderer`, asset paths and scenes stay unchanged.
- **GPU-driven selection** (levels picked in the occlusion-culling compute shader): fewer CPU cycles, but the
  batches (one indirect draw per mesh, submesh and material) would have to become per level on the GPU,
  and the CPU-side shadow passes would need the same choice.

Options for choosing a level:

- **Screen-size thresholds** per level (the fraction of the screen a bounding sphere covers): common in
  content tools, but the thresholds are arbitrary per asset and do not say how wrong a level looks.
- **Projected geometric error**: the simplifier reports how far each level's surface deviates from full
  detail; projecting that distance to pixels gives a single, asset-independent quality setting.

## Decision

- `Submesh` gains `lodCount` and a fixed array of up to `kMaxSubmeshLods` = 3 `SubmeshLod`s (index offset,
  index count, object-space error). Levels are index ranges into `MeshData::indices` over the same
  vertices; the struct stays trivially copyable, so the cooked model format serializes it as before (model
  format version 3) and the deserializer validates every range.
- `generateLods()` uses meshoptimizer 1.3 (MIT; only its simplifier and vertex cache optimizer are
  compiled): `meshopt_simplifyWithAttributes` with normals, locked borders (submeshes of different
  materials stay closed against each other) and absolute errors, then `meshopt_optimizeVertexCache`.
  It stops when a level would remove less than a tenth of the triangles or exceed `maxError` (5 % of the
  submesh size), and skips submeshes under 64 triangles. It is deterministic.
- Model imports generate levels by default. The model import sidecar gains `lodCount` (0 to 3, default 3)
  and `lodReduction` (0.1 to 0.9, default 0.5); the sidecar is a source of the cooked file, so changing
  them reimports.
- The CPU culling Jobs choose the level of every instance from the camera: the coarsest level whose error,
  scaled by the instance's scale and projected at its distance to pixels of the colour target's height,
  stays within `RenderView::lodPixelError` (default 1 pixel; 0 turns levels off). The same level is drawn
  in the camera pass and in every shadow view, so shadows match their surfaces. Batches are keyed by
  mesh, submesh, level and material; the GPU culling and indirect draws are otherwise unchanged.
- `RenderStats::lodInstances` counts camera-visible instances per level.
- Everything that needs the real surface once (triangle-mesh colliders, picking) reads the full-detail
  triangles: `fullDetailIndices()` returns the indices before the first level, which `generateLods()`
  always appends after them.

## Consequences

- Far objects cost a fraction of their triangles with no change to scenes, Components or asset paths;
  every imported model benefits after its next import (the format version bump reimports cached models).
- Index memory grows by up to about 75 % of the full-detail indices with the default reduction; vertex
  memory does not change.
- Levels switch abruptly when the camera moves; at one pixel of error this is rarely visible, and dithered
  cross-fades (GFX-2b, see the addendum) hide it. Impostors for distant trees (GFX-2e) replace the coarsest levels where
  simplification cannot go further (leaf cards, which locked borders keep).
- Shadow casters outside the view use the camera's level, which can be coarser than a shadow seen up close
  needs; acceptable at one pixel, revisited with impostors.
- Selection stays on the CPU (one division and up to three comparisons per instance in the culling Jobs);
  moving it to the GPU remains possible because the data is already in the shared index pool.

## Addendum (2026-11-08, GFX-2b): dithered cross-fades

- The renderer keeps, per Entity, the level drawn last frame, the level it fades from and the fade progress
  (next to its motion-vector history). A changed level starts a fade of `RenderView::lodFadeSeconds`
  (default 0.25 s), advanced by `deltaSeconds`; turning back to the level being left reverses it. Instances
  that were not drawn last frame, or are not drawn now, take their level without a fade.
- A fading instance is drawn twice, once per level. `InstanceData::extra.w` carries the level and the fade
  threshold; the incoming copy keeps the pixels whose interleaved gradient noise is below the threshold and
  the outgoing copy the others, so each pixel is covered exactly once in the depth prepass, the shading pass,
  the Entity IDs and the shadow maps.
- Fading copies go through the alpha-tested pipelines (a separate batch key), which already discard;
  opaque draws keep their discard-free prepass and early depth testing. Opaque materials have an alpha
  cutoff of -1, so the alpha test never removes their pixels.
- Considered: alpha blending between levels (needs sorting and breaks the depth prepass) and morphing
  vertices between levels (needs vertex correspondence that simplification does not keep).

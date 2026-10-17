# 0017. Two-phase GPU occlusion culling, indirect draws and per-object motion vectors

- Status: Accepted
- Date: 2026-10-17

## Context

The brief asks for frustum and occlusion culling and GPU-driven submission where possible; TAA (ADR
0016) needed per-object motion. Decisions: where occlusion is tested, how hidden objects that become
visible avoid popping, how culling results reach draw calls without a CPU readback, and where an
object's previous transform lives.

## Decision

- **Frustum culling stays on the CPU** (parallel, ~7 ns per object, ADR 0013): it also produces the
  shadow-cascade masks and the sorted batches, and it keeps the GPU upload small.
- **Occlusion culling on the GPU, in two phases** (Haar and Aaltonen 2015). Phase 1 tests every opaque
  candidate against last frame's Hi-Z with last frame's camera and draws the survivors into the depth
  prepass. A Hi-Z of that depth is built; phase 2 tests only the phase-1 rejects against it with the
  current camera and draws those that are visible. Everything visible this frame is drawn in one of
  the phases, so there is no popping and no per-object visibility history. A second Hi-Z of the final
  depth serves the next frame. Tests project the bounding sphere's box and compare its nearest depth
  with the farthest depth of at most 2×2 Hi-Z texels at the matching level (reversed-Z: the minimum).
- **Indirect draws:** each opaque batch (mesh, submesh, material) has indirect arguments per phase whose
  instance count the culling shader increments, and a region of a draw list of instance indices; the
  vertex shader reads `instances[drawList[offset + instanceID]]`. Blended items use CPU-written draw
  list slots and direct draws. No `firstInstance` (an optional feature) is used: the offset is a push
  constant.
- **Motion vectors** are written by the depth prepass (RG16F) from the unjittered current and previous
  clip positions. The previous world matrix lives in a `MotionHistory` component that the renderer adds
  to MeshRenderer entities on first sight (applied after iteration) and updates while culling; this
  follows the ECS model instead of a renderer-side map keyed by entity. TAA uses the motion of the
  closest pixel in the 3×3 neighbourhood and Catmull-Rom history sampling; the sky keeps camera
  reprojection.
- **GPU benchmarks** (`illusion_gpu_benchmarks`, labels `gpu;benchmark`) measure whole frames on local
  hardware in Release; CI runs GPU tests on a software rasterizer and excludes them.

## Consequences

- Draw calls per opaque batch double (one per phase) even when phase 2 draws nothing; bindless
  multi-draw indirect (M3f) will collapse batches into a few calls.
- Each render() of a World updates `MotionHistory`, so rendering the same World twice per frame yields
  zero object motion for the second view.
- Shadow casters are not occlusion-culled.

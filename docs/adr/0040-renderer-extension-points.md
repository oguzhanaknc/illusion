# 0040. Expose renderer extension points for editors and tools

- Status: Accepted
- Date: 2026-10-25
- Supersedes the `MotionHistory` Component decision in [0017](0017-gpu-occlusion-culling-and-motion-vectors.md).

## Context

Scene and Game viewports render the same World independently. Updating an ECS `MotionHistory` after
each render erases motion for the second view and changes the World's structure on first use. Tools
also need picking, overlays and diagnostics without depending on the editor or stopping the GPU.

Keeping ECS history once per application frame would couple rendering to an external frame boundary.
A renderer-owned hash map avoids that coupling but adds indirection to every visible object. Attaching
scene depth to overlays would prevent render and output resolutions from diverging later.

## Decision

- Each `SceneRenderer` owns a dense history indexed by Entity index, checked against generation and
  the renderer's previous frame. Rendering never adds or changes World Components. Use one renderer
  per view; reset its temporal history after cuts or replacement of a World at the same address.
- Opt-in Entity IDs use RG32_UINT (index, generation), with both words `~0u` for background. Opaque and
  masked objects write IDs in the motion/depth prepass in either material path. A separate blended ID
  pass selects the nearest fragment with nonzero alpha using a private copy of opaque depth. It does
  not change scene depth. These are live handles, not persistent GUIDs; validate them against the World
  after asynchronous readback.
- `viewTargets()` borrows depth, IDs, HDR and motion textures plus both sizes and both view-projection
  matrices. Overlays run after rendering, sample depth, and map output pixels to render pixels.
- `ReadbackRing` owns bounded staging slots and graphics-queue event queries. Request never waits;
  polling leaves unfinished destinations untouched. Blocking tools wait on the copy's event, never
  device idle. Slots remain occupied until their result is consumed.
- `DebugDraw` reserves its CPU storage at construction and rejects overflow atomically. Its renderer
  expands lines to quads, clips before perspective division, and supports depth-tested/on-top lines.
  Wire boxes and spheres use the same bounded storage. GPU buffers grow only when capacity is exceeded.
- Camera schema version 2 adds projection, orthographic height, finite orthographic far plane and
  priority. Version 1 migrates with perspective defaults. Perspective keeps infinite reversed-Z.
  Cascades reconstruct frustum corners from the inverse projection for either mode.
- Views filter RenderMesh visibility, layers and a borrowed hidden-Entity bitset before all passes.
  Diagnostic shading bypasses TAA, bloom, exposure and ACES; wireframe is conditional on non-solid fill.
  Optional timestamp queries use a bounded ring and never stall when measurements are pending.

## Consequences

Two views keep independent motion and occlusion histories. Memory scales with the greatest Entity index
seen by each renderer; ID targets and timestamp queries cost nothing when disabled. Blended IDs require
an extra draw pass and depth copy. History for recycled indices or objects absent in the previous frame
is discarded. A caller must submit rendering before requesting a readback and consume completed tickets.

M7o builds viewport picking on this contract; M7p adds gizmos using the overlay targets. M9 can decouple
render/output sizes without changing depth sampling in tools. Revisit dense history if sparse high
indices become a measured memory issue, and revisit ID format only together with Entity handle changes.

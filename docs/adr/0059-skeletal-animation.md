# 0059. Skeletal animation: skins and clips in the cooked model, poses outside the ECS, skinning in a compute pass

- Status: Proposed
- Date: 2026-11-17

## Context

The brief asks for a commercial-quality engine; characters, creatures and machines that move with bones are
expected of one. Nothing of it exists: `loadGltf()` skips skins and animations, `Vertex` (48 bytes) has no
bone data, and the renderer draws every mesh rigidly from shared vertex buffers.

What exists and shapes the design:

- **Assets.** A glTF file is cooked into one `ModelData` (meshes, materials, images, nodes; model format 5).
  Saved Components refer to its parts by asset path fragments (`#mesh/N`, `#material/N`, `#node/<name>`,
  ADR 0035), and resolvers turn them into runtime Components (`MeshRenderer` → `RenderMesh`).
- **ECS.** Components are plain data in archetype chunks; runtime state that is large or variable-sized lives
  in a library-owned pool with a small runtime Component pointing at it (`ParticleWorld` and `ParticlePool`,
  ADR 0047). Systems run on the Job System and must not allocate per frame.
- **Renderer.** Forward, depth prepass, cascaded shadows and motion vectors (TAA) all read the 48-byte
  `Vertex`; culling uses world bounds of `RenderMesh`; there is a GPU-driven path for rigid instances.
- **Determinism.** Fixed-step sessions repeat exactly (ADR 0042); saved games capture saved Components
  (ADR 0058).

Options considered for **where joints live**:

- **One entity per joint.** Reuses `Transform`, `Hierarchy` and the Inspector, and sockets come for free; but a
  crowd of 300 characters with 60 joints is 18,000 entities whose hierarchy update, change tracking and
  chunk churn dominate the frame, and every joint would be saved into scenes.
- **A pose buffer owned by an animation library.** Joints are rows in contiguous arrays evaluated in one pass;
  entities only for what the game attaches (sockets, ANIM-3). Costs a debug view and explicit socket support.

Options considered for **skinning**: on the CPU (simple, but bandwidth- and core-bound for crowds), in the
vertex shader (no extra memory, but every pass that draws the mesh — prepass, shadow cascades, forward,
motion vectors — repeats the work and every shader gains a skinned variant), or **once per frame in a compute
pass** writing skinned vertices in the ordinary layout (one extra vertex buffer per visible instance, every
existing pass and shader unchanged).

## Decision

- **Assets.** Skins and animations are cooked into the `ModelData` of their glTF file (model format 6), next
  to the meshes they belong to:
  - `ModelData::skins`: a `Skeleton` per glTF skin — joints ordered parents before children, each with a name,
    a parent index, its rest pose (translation, rotation, scale) and its inverse bind matrix. At most
    `kMaxJoints` (1,024) joints.
  - `ModelData::animations`: an `AnimationClip` per glTF animation — a name, a duration and channels. A
    channel targets a joint **by name** (translation, rotation or scale; keyframe times and values; `Step`,
    `Linear` or `CubicSpline`), so one clip plays on every skeleton whose joints have those names. Morph
    targets are skipped (ANIM-2 or later).
  - A skinned mesh keeps its 48-byte `Vertex` array and gains a parallel stream `MeshData::skin` of
    `SkinVertex` (four `u16` joint indices and four `unorm16` weights, 16 bytes), empty for rigid meshes, and
    `MeshData::jointBounds`, the bind-space bounds of the vertices each joint influences. More than four
    influences keep the four largest, renormalized.
  - New asset path fragments `#skin/N` and `#animation/<name>`; new `AssetKind`s `Skeleton` and
    `AnimationClip`.
- **Library.** `engine/animation` (`illusion::animation`) is GPU-free, like `engine/particles`. Its pure
  functions (`bindClip()`, `sampleClip()`, `localToModel()`, `skinMatrices()`, `skinnedBounds()`) work on
  spans and are tested without a World. `engine/render` links it and reads the poses it publishes.
- **Components.** A saved `Animator` (`illusion.Animator`: `skeleton` `#skin/N`, `clip` `#animation/<name>`,
  `speed`, `time` in seconds, `loop`, `playing`) on the entity of a `MeshRenderer` whose mesh is skinned for
  that skeleton. The resolver adds the runtime `AnimationPose` (a slot in the `AnimationWorld` and the joint
  count). A skinned mesh without an `Animator`, or with one whose skeleton does not match the mesh, is drawn
  in its bind pose (logged once). Because `time` is saved, scenes and saved games keep where a clip was.
- **Evaluation.** `AnimationWorld::update(world, seconds)` runs in `GameSession::update()` after Saves and
  before Physics (root motion, ANIM-2, feeds the CharacterController). One `parallelFor` over the poses:
  advance `time` (in Play mode and the player only; the editor evaluates the saved `time`, so the Inspector
  scrubs), sample the clip through its cached binding (built once per skeleton and clip, at resolve time),
  compose model-space joint matrices, write skin matrices (3 x 4 floats, 48 bytes per joint) into the frame's
  contiguous buffer, and compute the entity's world bounds from `jointBounds`. The previous frame's skin
  matrices are kept for motion vectors. No allocation per frame. Pure function of clip and time, so fixed-step
  runs repeat.
- **Skinning.** A compute pass at the start of the scene frame skins every skinned instance whose bounds touch
  the camera or a shadow cascade: it reads the bind-pose `Vertex` buffer, the `SkinVertex` stream and the skin
  matrices, and writes a per-instance vertex buffer in the 48-byte `Vertex` layout, plus the previous frame's
  positions for motion vectors. Every existing pass draws a skinned instance like a rigid mesh from that buffer
  (one draw per instance; skinned instances are not batched with each other). Instance buffers are pooled and
  allocated when an instance first becomes visible.
- **Budgets.** 300 characters of 60 joints evaluate in under 1 ms on a desktop CPU with the Job System;
  skinning 300 instances of 10,000 vertices takes under 1.5 ms on a mid-range desktop GPU. The benchmarks
  `animation/evaluate-300x60` and the GPU test's timing set the budgets.

## Consequences

- Crowds scale: joints are rows, not entities, and skinning runs once per frame however many passes draw the
  mesh. The price is memory: one skinned vertex buffer (and previous positions) per visible instance.
- Attaching an entity to a bone needs an explicit socket (ANIM-3); joints do not show in the Hierarchy. The
  editor gets a skeleton debug drawing instead (ANIM-4).
- Clips are shared by joint name: a clip authored for one rig plays on another with the same names, without
  retargeting of proportions. Clips are stored uncompressed; keyframe compression is open.
- Model format 6 recooks every model once. Rigid meshes are unchanged (an empty `skin` stream).
- Skinned instances are not GPU-instanced; thousands of identical skinned characters need animation textures
  or instanced skinning, recorded as open.
- Revisit if games need more than four influences per vertex, morph-target-heavy faces, or GPU-evaluated
  poses.

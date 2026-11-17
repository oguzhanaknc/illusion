# Animation

Characters, creatures and machines move with bones: a skinned mesh follows the joints of a skeleton, and a
clip moves the joints over time. illusion imports skins and clips from glTF, poses them on the Job System
and skins the meshes on the GPU ([ADR 0059](../architecture/decisions.md)).

## Importing a rigged glTF model

`loadGltf()` reads the skins and animations of a glTF file into its `ModelData`, next to the meshes:

- `ModelData::skins` holds one `Skeleton` per glTF skin, of up to `kMaxJoints` (1,024) joints. Joints are
  sorted so that parents come before their children; each keeps its name, parent, rest pose and inverse
  bind matrix.
- `ModelData::animations` holds one `AnimationClip` per glTF animation. A channel moves the translation,
  rotation or scale of a joint **by name** with `Step`, `Linear` or `CubicSpline` keys, so a clip plays on
  every skeleton whose joints have those names. Clips without a name are called `animation<N>`; repeated
  names get `.2`, `.3`, ...
- A skinned mesh keeps its vertices and gets `MeshData::skin`, a parallel stream of `SkinVertex` (four
  joints and four weights per vertex, 16 bytes), plus the bounds each joint moves (`jointBounds`). Vertices
  with more than four influences keep the four largest, renormalized.

Morph targets, channels on nodes that are not joints and invalid skin data are skipped with a log line;
the mesh is then drawn rigid.

## Asset paths

Saved Components refer to the parts of a model by asset path fragments:

| Fragment | Refers to |
| --- | --- |
| `model.glb#mesh/N` | Mesh `N` (a `MeshRenderer`) |
| `model.glb#skin/N` | Skeleton `N` (`AssetKind::Skeleton`) |
| `model.glb#animation/<name>` | The clip called `<name>` (`AssetKind::AnimationClip`) |

## The Animator

A skinned mesh moves with the saved Component `Animator`, on the Entity of its `MeshRenderer`. An
`AnimationWorld` per World keeps the poses outside the ECS and adds the runtime Component `AnimationPose`
(the pose's slot, its joint count and its world bounds):

Headers: `<illusion/animation/AnimationComponents.hpp>`, `<illusion/animation/AnimationWorld.hpp>`

```cpp
illusion::AnimationWorld animation(world, streamer);
const illusion::Entity hero = world.create(
    illusion::Transform{}, illusion::MeshRenderer{.mesh = illusion::AssetRef("game:/hero.glb#mesh/0")},
    illusion::Animator{.skeleton = illusion::AssetRef("game:/hero.glb#skin/0"),
                       .clip = illusion::AssetRef("game:/hero.glb#animation/Walk")});

// Every frame, after game code and before physics:
animation.update(deltaSeconds, &jobs, true);
const illusion::AnimationPose& pose = world.get<illusion::AnimationPose>(hero);
const std::span<const illusion::SkinMatrix> skin = animation.skinMatrices(pose.slot);
```

| Field | Meaning |
| --- | --- |
| `skeleton` | The skeleton, `model.glb#skin/N`; it must be the skin of the mesh |
| `clip` | The clip, `model.glb#animation/<name>`; none shows the rest pose |
| `speed` | Playback rate; negative plays backwards |
| `time` | Seconds into the clip |
| `loop` | Wrap at the clip's end; otherwise hold the last pose |
| `playing` | Advance `time` while the game runs |

- Every field is saved, so scenes and [saved games](saved-games.md) keep where a clip was.
- With `advance` true (Play mode and the player; `GameSession` passes it while started), `update()` adds
  `speed` times the seconds to `time`, wrapped into the clip when `loop` is on; otherwise `time` stops at
  the clip's end (or its start when playing backwards) and `playing` turns false.
- Each model file loads once through the `AssetStreamer`; its skeletons, clips and the joint bounds of its
  meshes are shared by every Animator, and a clip is bound to a skeleton once. The poses are sampled and
  composed in parallel on the Job System into one contiguous buffer of skin matrices;
  `previousSkinMatrices()` holds the update before (equal to the current ones on a pose's first update),
  for motion vectors. `AnimationPose::bounds` is the skinned mesh's bounds in world space.
- The pose waits while its model loads (`jointCount` is 0). A skeleton that does not fit the mesh (another
  skin, another joint count, or a rigid mesh) is reported once and gets no pose; the mesh is drawn in its
  bind pose. Entities with the `Inactive` tag keep their pose and `time`.
- Reloading a model file (`AssetStreamer::reload()`) binds its Animators again; a reload that fails keeps
  the previous data. A steady update allocates nothing.

## Playback and scrubbing in the editor

Add an `Animator` to the Entity of a skinned `MeshRenderer` in the Inspector and set its `skeleton` and
`clip`. Outside Play mode the editor poses the edit World at the saved `time` without advancing it, so
dragging `time` in the Inspector scrubs the clip in the Scene view. In Play mode the session's
`AnimationWorld` advances `time` of every playing Animator; Stop returns to the saved values. The showcase
project's `Column` plays its clip `sway` this way ([Showcase project](editor/showcase.md)).

## Drawing skinned meshes

`SceneRenderer` skins every posed mesh on the GPU. Give it the poses in `RenderView::animation`; the
player, Play mode and the editor's viewports do this for you:

```cpp
streamer.update();
cache.update();
resolver.update(world, &jobs);
animation.update(deltaSeconds, &jobs, true);

illusion::RenderView view = illusion::RenderView::fromCamera(cameraTransform, camera, aspect);
view.animation = &animation; // null draws skinned meshes in their bind pose
renderer->render(commands, target, world, view, &jobs);
```

- At the start of the frame a compute pass skins each instance whose pose is fresh (written by the
  `AnimationWorld`'s last update) and whose `AnimationPose::bounds` touch the camera or a shadow cascade.
  It writes the posed vertices into a range of a skinned vertex buffer, and the positions under the
  previous pose next to them.
- The depth prepass, the shadow maps, shading, motion vectors, entity IDs and the wireframe overlay draw
  the instance from that range, so shadows show the posed shape, picking hits it and temporal
  anti-aliasing sees the joints move.
- Culling uses the pose's bounds. A skinned instance is drawn on its own (one draw per pass), off the
  GPU-driven instancing path and without occlusion culling. Levels of detail still apply.
- An instance keeps its range while its pose lasts, and gets one when it first becomes visible.
  `RenderStats::skinnedInstances` and `skinnedVertices` count the work of the last frame,
  `RenderGpuTimings::skinning` its GPU time.

## Not supported yet

- Blending between clips, layers, blend spaces, state machines, animation events and root motion
  (ANIM-2).
- Sockets (attaching an Entity to a joint), look-at and inverse kinematics, ragdolls (ANIM-3).
- Clip previews, a timeline, skeleton debug drawing and `animation.*` editor commands (ANIM-4).
- Morph targets, more than four influences per vertex, and keyframe compression.

## Performance

- Joints are rows in contiguous arrays, not Entities: posing 300 characters of 60 joints takes about
  1.5 ms on one desktop core (benchmark `animation/evaluate-300x60`), and spreads over the Job System.
- Skinning runs once per frame, however many passes draw the mesh: 300 instances of 10,240 vertices take
  0.66 ms on an NVIDIA GeForce RTX 4070 SUPER (the GPU test sets a budget of 3 ms).
- Each visible skinned instance costs 64 bytes of GPU memory per vertex (the posed vertex and its previous
  position), and one draw per pass. Thousands of identical characters need instanced skinning, which is
  not available yet.

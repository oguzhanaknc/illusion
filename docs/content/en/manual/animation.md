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
| `graph` | An [animation graph](#animation-graphs) (`.animgraph`) that drives the Animator instead of `clip` |
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

## Animation graphs

An Animator whose `graph` names a `.animgraph` file plays that graph instead of its clip (ADR 0060): layers
of state machines whose states play clips or 1D/2D blend spaces, transitions on parameters with cross-fades,
masked and additive layers, state events and root motion. The file is JSON (`"format": "illusion.animgraph"`,
read by `parseAnimationGraph()`); the editor's asset browser lists it as an `AnimationGraph`, and exports
ship it as it is.

- The graph loads once through the `AssetStreamer` and is bound once per skeleton; each Animator keeps its
  own state, which starts in the entry states, with `time` (seconds) as their time and the default
  parameters. A scene or saved game starts the graph again in the same way.
- While advancing and `playing`, the graph steps by `speed` times the seconds; outside Play mode the editor
  shows the entry states at `time`. `clip` and `loop` are not used.
- Game code drives it through [`AnimationService`](../reference/scripting-api.md#animationservice):
  parameters and triggers by name, the current state and its time, and the events of the last update.
  Animators without a graph change clip with `play()`, which cross-fades from the clip they were playing.
- A graph with `rootMotion` takes the horizontal motion and yaw of that joint out of the pose; the
  [game session](runtime.md#step-order) moves the Entity by it after the Animation step.
- Changing the file (`AssetStreamer::reload()`, which the editor does for changed files) restarts its
  Animators with the new graph; a file that fails to load or bind is reported and shows the rest pose.

```cpp
// In a game module: drive the graph of the Entity's Animator.
if (illusion::AnimationService* animation = context.service<illusion::AnimationService>())
{
    static_cast<void>(animation->setFloat(hero, "speed", 3.0f));
    static_cast<void>(animation->setTrigger(hero, "jump"));
    std::array<illusion::AnimationEvent, 16> events{};
    const illusion::u32 count = animation->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        playFootstep(events[i].entity);
    }
}
```

The full guide to the format, with a complete example, follows with ANIM-2c; the showcase project's
`Column` plays `assets/animations/column.animgraph`.

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

- Interrupting a running transition, sub-state machines and saving the live state of a graph.
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

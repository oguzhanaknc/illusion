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

The showcase project's `Column` plays `assets/animations/column.animgraph`.

### The file, key by key

A graph file has these keys; an unknown key is an error, so a typo does not go unnoticed.

| Key | Meaning |
| --- | --- |
| `format`, `version` | `"illusion.animgraph"` and `1` |
| `parameters` | The inputs of the graph (below); optional |
| `rootMotion` | The name of the joint whose horizontal motion and yaw drive the Entity; optional |
| `layers` | One or more layers, evaluated in order |

**Parameters** are `{"name", "type", "default"}`. The names are unique and not empty.

| `type` | Holds | `default` |
| --- | --- | --- |
| `Float` | A number (speed, direction) | A number, 0 if missing |
| `Int` | A whole number between -2^24 and 2^24 (weapon kind) | A whole number, 0 if missing |
| `Bool` | `true` or `false` (crouching) | `true` or `false`, `false` if missing |
| `Trigger` | Set once, stays set until a transition consumes it or the update ends | None |

**Layers** are `{"name", "weight", "blend", "mask", "entry", "states", "transitions"}`:

- `weight` in [0, 1] (default 1) scales the layer; `blend` is `Override` (default), which replaces what the
  layers below it posed, or `Additive`, which adds the difference of its pose from the first frame of each
  clip, so a breathing or leaning clip can ride on top of any base motion.
- `mask` lists `{"joint", "weight"}`: each entry sets the weight of that joint **and its descendants** (a
  later entry wins for its subtree). Joints no entry covers are not changed by the layer, and an empty or
  missing mask covers every joint. `"mask": [{"joint": "spine", "weight": 1}]` gives the upper body to the
  layer and leaves the legs to the layers below.
- `entry` names the state the layer starts in (default: the first state). `states` has at least one state.

**States** are `{"name", "clip" or "blend1d" or "blend2d", "speed", "speedParameter", "loop", "events"}`:

- A state has exactly one motion: `clip` (`"game:/hero.glb#animation/walk"`, the clip path of the
  [asset paths](#asset-paths)), `blend1d` or `blend2d`. The name `*` is reserved.
- `speed` (default 1, not negative) is the playback rate; `speedParameter` names a Float parameter that
  multiplies it, so one parameter can slow a whole state down. A negative speed holds the state.
- `loop` (default `true`) wraps at the end; otherwise the state holds its last pose and its normalized time
  goes on above 1.
- `events` are `{"name", "time"}` with `time` normalized in [0, 1], sorted by time.

**Blend spaces** play several clips at once, weighted by Float parameters:

- `"blend1d": {"parameter", "points": [{"value", "clip"}]}` has at least two points, sorted by `value`
  without repeats. Between two neighbours the weights are linear; below the first and above the last the end
  clip plays alone.
- `"blend2d": {"x", "y", "points": [{"x", "y", "clip"}]}` names two different Float parameters and has at
  least three points that are distinct and do not all lie on one line. The points are triangulated when the
  graph loads (Delaunay); inside a triangle the three clips get the barycentric weights of the parameter
  values, and outside the hull the nearest point of the hull's edges is used.
- The clips of a blend space play in sync: they share one normalized time, and the state's duration is the
  weighted average of the clips' durations. A walk and a run of different lengths therefore keep their steps
  together while the weights change, and the state's events fire at the same moment of every clip.

**Transitions** are `{"from", "to", "duration", "exitTime", "conditions"}`:

- `from` is a state name or `"*"` for any state (it never fires in the target state itself); `to` is another
  state of the layer.
- `conditions` must **all** hold. A condition is `{"parameter", "compare", "value"}`; a Trigger takes no
  `compare` and no `value` and holds while it is set.

  | Parameter | `compare` | `value` |
  | --- | --- | --- |
  | `Float` | `Greater`, `Less` | A number |
  | `Int` | `Greater`, `Less`, `Equal`, `NotEqual` | A whole number |
  | `Bool` | `Equal`, `NotEqual` | `true` or `false` |
  | `Trigger` | none | none |

- `exitTime` (normalized, optional) holds the transition until the source state's normalized time has reached
  it, or a looping source has wrapped. A transition with neither conditions nor an exit time fires at once.
- `duration` (seconds, default 0) is the cross-fade: both states play while the weights move from the source
  to the target. With 0 the change is instant.
- **Priority is the order in the file**: each update the first transition of the layer that fires wins.
  Put urgent ones (a jump from `*`) first.

### How transitions run

- While a cross-fade runs, no other transition of that layer fires: **a running transition is not
  interrupted**. A trigger set while it runs is cleared at the end of that update without effect, so the
  game sets it again (or keeps a Bool) if the input can arrive during a fade.
- A fired transition consumes the triggers in its conditions. Triggers that no transition consumed are
  cleared at the end of the update, so a trigger is a "now" signal and a Bool is a state.
- The target starts at its time 0 and moves with the next step. The new state is the layer's
  `currentState()` as soon as the transition fires, not when the fade ends.
- Parameter writes of game code are used at the next Animation step.

### Events

An event is a named moment of a state, for footsteps, effects and gameplay hooks. After each update the
Animators publish the events that time crossed: an event at normalized time `t` fires when the state's time
moves across it in the half-open span [before, after). Specifically:

- A looping state fires the events on both sides of its wrap in the same update.
- A non-looping state fires each event once, when its time passes it; once it holds its last pose it fires
  nothing more.
- Only states with a weight above 0 fire: during a cross-fade both states fire, a state that has finished
  fading out does not.
- A step longer than one loop fires each event once.

A module reads them with `AnimationService::pollEvents()`: the events of the last update, in layer order and
in update order, each with the Entity, the event name, the layer and the state. They are published for one
update; every call within an update returns the same events.

### Root motion

With `"rootMotion": "hips"` the horizontal translation (x and z) and the yaw of the named joint are taken out
of its pose: the joint stays at its rest pose's horizontal position and yaw, so the mesh does not walk away
from its Entity, and the motion is handed to the game as `AnimationPose::rootTranslation` (metres) and
`rootYaw` (radians, about +y) in world space, weighted and layered like the pose, with blend spaces and
cross-fades included. The motion of one update is applied by the [game session](runtime.md#step-order), only
while the game runs (not in the editor's scrubbing):

- An Entity with a `CharacterController` gets the translation divided by the step as its horizontal velocity;
  the vertical velocity is the controller's, so gravity and jumps keep working, and a jump comes from physics,
  not from the clip.
- Any other Entity is moved by the translation (its `LocalTransform` under a parent).
- Both turn by the yaw.
- A game that steers the character itself leaves `rootMotion` out of its graph.

### A complete example

The graph below walks and runs on the parameter `speed`, strafes in a 2D blend space, jumps on a trigger from
any state (the jump does not loop and returns to the locomotion near its end), aims with the upper body
through a masked layer, breathes through an additive layer, plays a `footstep` event twice per cycle and takes
the root motion of `hips`. The clips are the names a rigged `hero.glb` would hold. The test
`tests/animation/TestAnimationGraphExample.cpp` parses this exact text.

```json
{
  "format": "illusion.animgraph",
  "version": 1,
  "parameters": [
    {"name": "speed", "type": "Float"},
    {"name": "playRate", "type": "Float", "default": 1},
    {"name": "moveX", "type": "Float"},
    {"name": "moveY", "type": "Float"},
    {"name": "strafing", "type": "Bool"},
    {"name": "aiming", "type": "Bool"},
    {"name": "jump", "type": "Trigger"}
  ],
  "rootMotion": "hips",
  "layers": [
    {
      "name": "base",
      "entry": "locomotion",
      "states": [
        {
          "name": "locomotion",
          "speedParameter": "playRate",
          "blend1d": {
            "parameter": "speed",
            "points": [
              {"value": 0, "clip": "game:/hero.glb#animation/idle"},
              {"value": 1.5, "clip": "game:/hero.glb#animation/walk"},
              {"value": 4, "clip": "game:/hero.glb#animation/run"}
            ]
          },
          "events": [
            {"name": "footstep", "time": 0.25},
            {"name": "footstep", "time": 0.75}
          ]
        },
        {
          "name": "strafe",
          "blend2d": {
            "x": "moveX",
            "y": "moveY",
            "points": [
              {"x": 0, "y": 0, "clip": "game:/hero.glb#animation/idle"},
              {"x": -1, "y": 0, "clip": "game:/hero.glb#animation/strafe_left"},
              {"x": 1, "y": 0, "clip": "game:/hero.glb#animation/strafe_right"},
              {"x": 0, "y": 1, "clip": "game:/hero.glb#animation/walk"}
            ]
          },
          "events": [
            {"name": "footstep", "time": 0.25},
            {"name": "footstep", "time": 0.75}
          ]
        },
        {
          "name": "jump",
          "clip": "game:/hero.glb#animation/jump",
          "loop": false,
          "events": [
            {"name": "land", "time": 0.8}
          ]
        }
      ],
      "transitions": [
        {"from": "*", "to": "jump", "duration": 0.1, "conditions": [{"parameter": "jump"}]},
        {
          "from": "jump",
          "to": "locomotion",
          "duration": 0.2,
          "exitTime": 0.9
        },
        {
          "from": "locomotion",
          "to": "strafe",
          "duration": 0.25,
          "conditions": [{"parameter": "strafing", "compare": "Equal", "value": true}]
        },
        {
          "from": "strafe",
          "to": "locomotion",
          "duration": 0.25,
          "conditions": [{"parameter": "strafing", "compare": "Equal", "value": false}]
        }
      ]
    },
    {
      "name": "upper",
      "mask": [{"joint": "spine", "weight": 1}],
      "states": [
        {"name": "relaxed", "clip": "game:/hero.glb#animation/idle"},
        {"name": "aim", "clip": "game:/hero.glb#animation/aim"}
      ],
      "transitions": [
        {
          "from": "relaxed",
          "to": "aim",
          "duration": 0.15,
          "conditions": [{"parameter": "aiming", "compare": "Equal", "value": true}]
        },
        {
          "from": "aim",
          "to": "relaxed",
          "duration": 0.15,
          "conditions": [{"parameter": "aiming", "compare": "Equal", "value": false}]
        }
      ]
    },
    {
      "name": "breath",
      "blend": "Additive",
      "weight": 0.5,
      "states": [
        {"name": "breathe", "clip": "game:/hero.glb#animation/breathe"}
      ]
    }
  ]
}
```

A module drives it with `setFloat(hero, "speed", ...)`, `setBool(hero, "strafing", ...)`,
`setTrigger(hero, "jump")` and reads the `footstep` and `land` events; the
[tutorial "Animate a character"](../tutorials/animated-character.md) does it step by step.

### What is saved, what restarts

- The Animator's fields (`skeleton`, `graph`, `speed`, `time`, `loop`, `playing`, with `clip` unused) are
  saved with the scene and the game. The **live state of the graph** (the current states, their times, a
  running cross-fade, the parameter values) is not saved: a scene load, a saved game or a reload of the graph
  file starts the graph again in its entry states with the default parameters and the Animator's `time`.
- A game that wants to keep a parameter (a crouching Bool) writes it again after a load.
- Editing a `.animgraph` while the game runs reloads it: the Animators using it restart with the new graph.
  A new file that fails to parse or to bind keeps the loaded graph and is reported.

### Limits and errors

- One Animator samples at most `kMaxActiveClips` (16) clips in an update: all clips of the states with weight
  (blend spaces and cross-fades count each clip). Beyond it the lightest clips, by weight times layer
  weight, are dropped and the rest renormalized.
- Clips must exist in the model of the skeleton and the joints named by masks and `rootMotion` in its
  skeleton; a graph that fails to bind is reported with the missing name and shows the rest pose.
- `parseAnimationGraph()` validates the file as a whole and names the JSON path of the problem, so an agent
  or a test can fix it at once:

  ```text
  layers[0].transitions[1].conditions[0].parameter: unknown parameter 'sped'
  layers[0].states[2]: needs exactly one of clip, blend1d or blend2d
  parameters[3].default: a Trigger has no default
  ```

- Not interruptible transitions, sub-state machines and saving the live state are listed under
  [Not supported yet](#not-supported-yet).

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

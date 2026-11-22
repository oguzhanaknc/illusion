# Showcase Project

`samples/showcase` is an editor project whose scene, `assets:/scenes/everything.scene`, uses every saved engine
Component and gives each of their fields a value other than its default: cameras (perspective and
orthographic), directional, point and sky lights with a procedural sky, Meshes from the built-in shapes and from
a glTF model (with a second submesh), a hierarchy, a disabled entity, every collider shape (including a triangle
Mesh and a convex hull built from mesh assets), static, kinematic, dynamic and sensor bodies, a character,
joints of every type, audio sources on every bus with every attenuation and a cone, and two instances of a
prefab, one with overrides. Open it from the Welcome window or with `--project samples/showcase`.

It is also a small playground that the main Camera sees whole: shapes that drop onto the ground and a stair of
blocks, a table with a drawer that slides out and back, a trigger pad under a lamp, a hinged door in a frame,
a pendulum with a rope hanging from it, and a platform that rises and falls. Bodies whose collider has no
built-in mesh of its own get a child entity ("Door Mesh", "Pendulum Mesh", ...) sized like the collider, so the
colliders keep their exact sizes and the joint anchors stay in unscaled body space.

Its assets are made from numbers by `tools/make_assets.py` (a model with two meshes, five sounds: music,
wind, a lamp hum, a greeting and a click, and a nine-slice panel image), so the project carries no third-party licence except the font: `assets/fonts/Roboto-Regular.ttf` (Apache-2.0,
licence in `assets/fonts/LICENSE.txt`). The scene is not written by hand: `build.jsonl` builds it through Editor
commands, the same way an agent would, and saves it. It also gives the smoke sprite an import settings sidecar
(`"quality": "high"`) through `asset.set_import_settings` and a non-default window (1600x900, not resizable;
`settings/window.json`) and saved games without the cloud mirror (`settings/save.json`) through
`project.settings_set`. To rebuild it:

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

A test (`TestShowcase.cpp`) opens a copy of the project and fails when a saved Component or one of its fields
is missing from the scene or left at its default, or when the project has missing assets or unresolved prefabs.
A new Component or field therefore needs a value in `build.jsonl`. Three particle emitters cover
`ParticleEmitter`: a spark fountain over the trigger pad, a dust emitter by the blocks, which waits to be
played (`playOnStart` off), and a field of 20,000 fireflies simulated on the GPU. The `Column` covers
`Animator`: a skinned column (`assets/models/column.gltf` with the clips `sway` and `lean`, from
`tools/make_assets.py`) whose animation graph `assets/animations/column.animgraph` holds it 0.4 seconds into
`sway`, drawn bent, see [Animation](../animation.md#animation-graphs); its child `Column Lamp` covers
`BoneAttachment`, a small sphere on the column's `top` joint ([sockets](../animation.md#sockets)). The `Strider`, a
small two-legged figure (`assets/models/strider.gltf`) behind the camera, covers `LookAt` (it watches the
pendulum), `TwoBoneIk` (its left foot, and its right foot on the child `Strider Right Leg` with the pole
`Strider Knee Pole`) and `FootPlacement` ([look-at](../animation.md#look-at),
[two-bone IK](../animation.md#two-bone-ik), [foot placement](../animation.md#foot-placement)). A `Wind` entity blows the dust
(`windFactor`) across the yard. The disabled `Dusk Sky` entity holds a `SkyAtmosphere` at sunset: enable it
to replace the sky and the sun with a moving one, and the disabled `Film Look` holds `PostEffects` for a
graded, vignetted image. The disabled `Morning Mist` holds a `VolumetricFog` of low, drifting mist. The `HUD` entity is a game UI
with every UI Component: a status panel with a nine-slice image and a stack of texts (one literal, one
localized by a key of `assets/locales/en.json` and `tr.json`), a grid of coloured cells and a rotated, scaled,
clipped dial; see [Game UI](../ui.md). It shows in the Game view while playing. The Nav yard, east of the others at x = 30, covers the navigation Components: `Nav Floor` and `Nav Step` are the ground, the `Nav Surface` entity bakes it for agent type 1 and loads `assets/navigation/yard.navmesh`, `Nav Hole` cuts a square out of it, `Nav Ladder` links the floor to the top of the step, the carving `Nav Barrel` blocks the way and `Nav Walker` is an agent with a destination across the yard (see [Navigation](../navigation.md)).

## The showcase module

The project's game code, `modules/showcase`, plays the scene through the whole
[scripting API](../../reference/scripting-api.md): every function of `ModuleContext` and of the ten host
services is called from `showcase.cpp`. When the session starts, it builds a floor and a gem from mesh
geometry, adds the Jump (Space, gamepad South) and Move (WASD, left stick) actions, makes a chime from numbers
and sets up the mixer. Until you press a key or move a stick, the Player walks back and forth across the
trigger pad by itself; then WASD or the left stick walks it and Space or South jumps. While it stands on the
pad the lamp above lights up, and each time it steps on, the Voice greets it. The hinge motor opens the Door
and closes it again every four seconds (game code may change `motorSpeed` at any time), the drawer slides, the
platform rises and falls, and the Ball, Crate, Rock and Drum get pushed at fixed frames; the Ball grows to one
and a half times its size at frame 90. The module also casts rays and shapes, counts contacts and the
Trigger's visitors, chimes where something lands hard and raises the dust the first time, showers sparks
when the Player steps on the pad, stops the spark fountain at frame 270 and starts it again at 290, spawns a
lantern prefab at frame 120, switches to moon
gravity for a second from frame 200, and from frame 240 lets the Player walk through the Crate (their layers
stop colliding). It drives the HUD through `UiService`: the frame counter is a string table message with a
plural, the module hides and shows the palette, moves the focus, reads the widgets and polls the events, and
when `ui.cancel` is pressed (Escape, or East on a gamepad) it ends the game with `SceneService::requestExit()`.
Through `AnimationService` it plays the Column's graph: it leans the blend of `sway` and `lean` with a
parameter, bows the Column with a trigger at frame 30, calms it again from frame 150, counts the graph's
`peak` events and reads where the `top` joint is with `jointTransform()`. At frame 30 the Strider turns to
the lamp (`setLookAtTarget()`), its left foot reaches for a point (`setIkTarget()`) and its right foot's IK
is turned off (`setIkWeight()`). Through `NavService` it queries the yard's navmesh at frame 5, sends the Nav Walker to
its destination at frame 10, reads its status and remaining distance and counts its `Arrived` events, warps it
back at frame 200 and stops it at frame 280. What it saw is kept in the `showcase.Stats` Component of the Showcase entity.

Build it with **Modules → Build Modules** (`module.build`) and press **Play**:

```json
{"method": "module.build", "params": {}}
{"method": "play.start", "params": {"mode": "play", "fixedStep": true, "seed": 7}}
```

The module decides only from the frame number and the input, so fixed-step sessions repeat exactly. Three
checks keep the showcase in step with the engine:

- `python tools/check_api_coverage.py` (CI) fails when a function of the Doxygen group "Scripting API" is not
  called in `showcase.cpp`.
- `tests/showcase/TestShowcaseModule.cpp` runs the module for 300 frames over the real services, wrapped in
  counting proxies, and fails when a function is never called. A second test runs two fixed-step sessions
  with the same input and compares the hash of their saved state.
- `TestShowcaseGpu.cpp` compares the scene through its main Camera with a golden image
  (`showcase-game.png`, rewritten with `ILLUSION_UPDATE_GOLDENS=1`).

The project is also built as an exported game, the `showcase_game` target (`samples/CMakeLists.txt`): its module
linked statically and its data packed next to the executable, run by the [player](../runtime.md#the-player).
`TestShowcaseGame.cpp` runs it for 300 fixed steps and compares its screenshot with the golden image of the
editor's Game view.

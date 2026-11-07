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

Its assets are made from numbers by `tools/make_assets.py` (a model with two meshes and five sounds: music,
wind, a lamp hum, a greeting and a click), so the project carries no third-party licence. The scene is not written by hand: `build.jsonl` builds it through Editor
commands, the same way an agent would, and saves it. To rebuild it:

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

A test (`TestShowcase.cpp`) opens a copy of the project and fails when a saved Component or one of its fields
is missing from the scene or left at its default, or when the project has missing assets or unresolved prefabs.
A new Component or field therefore needs a value in `build.jsonl`.

## The showcase module

The project's game code, `modules/showcase`, plays the scene through the whole
[scripting API](../../reference/scripting-api.md): every function of `ModuleContext` and of the four host
services is called from `showcase.cpp`. When the session starts, it builds a floor and a gem from mesh
geometry, adds the Jump (Space, gamepad South) and Move (WASD, left stick) actions, makes a chime from numbers
and sets up the mixer. Until you press a key or move a stick, the Player walks back and forth across the
trigger pad by itself; then WASD or the left stick walks it and Space or South jumps. While it stands on the
pad the lamp above lights up, and each time it steps on, the Voice greets it. The hinge motor opens the Door
and closes it again every four seconds (game code may change `motorSpeed` at any time), the drawer slides, the
platform rises and falls, and the Ball, Crate, Rock and Drum get pushed at fixed frames; the Ball grows to one
and a half times its size at frame 90. The module also casts rays and shapes, counts contacts and the
Trigger's visitors, chimes where something lands hard, spawns a lantern prefab at frame 120, switches to moon
gravity for a second from frame 200, and from frame 240 lets the Player walk through the Crate (their layers
stop colliding). What it saw is kept in the `showcase.Stats` Component of the Showcase entity.

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

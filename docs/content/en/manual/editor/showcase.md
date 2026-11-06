# Showcase Project

`samples/showcase` is an editor project whose scene, `assets:/scenes/everything.scene`, uses every saved engine
Component and gives each of their fields a value other than its default: cameras (perspective and
orthographic), directional, point and sky lights with a procedural sky, Meshes from the built-in shapes and from
a glTF model (with a second submesh), a hierarchy, a disabled entity, every collider shape (including a triangle
Mesh and a convex hull built from mesh assets), static, kinematic, dynamic and sensor bodies, a character,
joints of every type, audio sources on every bus with every attenuation and a cone, and two instances of a
prefab, one with overrides. Open it from the Welcome window or with `--project samples/showcase`.

Its assets are made from numbers by `tools/make_assets.py` (a model with two meshes and a tone), so the project
carries no third-party licence. The scene is not written by hand: `build.jsonl` builds it through Editor
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
and sets up the mixer. Every frame it walks and jumps the Player character, pushes the Ball, Crate, Rock,
Drum and Door at fixed frames, casts rays and shapes, counts contacts and the Trigger's visitors, chimes where
something lands hard, and spawns a lantern prefab at frame 120. Near the end it lowers gravity and lets the
Crate fall through the floor. What it saw is kept in the `showcase.Stats` Component of the Showcase entity.

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

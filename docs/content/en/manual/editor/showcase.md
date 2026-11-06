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

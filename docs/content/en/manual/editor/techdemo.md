# Tech Demo Project

`samples/techdemo` is a first-person walk through a forest clearing that turns from day to night: trees and
grass bend in the wind, fireflies come out at dusk, a campfire crackles and lights the trees, neon hoops hum,
and glowing orbs roll when you push them. It brings together the [SkyAtmosphere](../rendering.md#day-and-night),
[Wind](../rendering.md#wind), [GPU particles](../particles.md#gpu-simulation), physics, audio and
[mouse look](../actions.md#mouse-look).

![The clearing at eleven in the morning](../../images/techdemo/day.png)

![The same view at night: fireflies, lanterns, neon and the campfire](../../images/techdemo/night.png)

## Running it

The models and textures of the plants, rocks and the lantern are CC0 assets from Poly Haven. They are not in
the repository: download them first (about 28 MB), then open the project:

```bash
python samples/techdemo/tools/fetch_assets.py
illusion_editor --project samples/techdemo
```

Build the module with **Modules → Build Modules** and press **Play**. Click the Game view to capture the
mouse (Shift+F1 frees it).

| Action | Keyboard and mouse | Gamepad |
| --- | --- | --- |
| Look | mouse | right stick |
| Walk | W A S D | left stick |
| Run | Shift | left stick button |
| Jump | Space | South (A, Cross) |
| Speed up time (×20) | T | North (Y, Triangle) |

A full day lasts six minutes.

## What is in it

- **Terrain**: a flat clearing in a bowl of hills, with a triangle-mesh collider, textured with the Poly Haven
  forest ground.
- **Trees**: three procedural broad-leaved trees (bark and leaf-card meshes), 46 of them around the clearing;
  their materials sway and flutter in the wind (`windSway`, `windFlutter`). About 5,000 grass tufts, ferns and
  shrubs bend with it.
- **Sky**: a `SkyAtmosphere` whose time the module advances; its sun lights the day and casts the shadows,
  the moon the night.
- **Campfire**: flame, ember and smoke emitters (the embers and smoke drift downwind), a flickering point light
  that dances with the flames and casts soft, moving [shadows](../rendering.md#point-light-shadows), and a
  looping crackle.
- **Night**: 4,000 GPU fireflies, three lanterns that brighten at dusk and cast shadows, crickets.
- **Neon**: a gate, three hoops and bars in cyan, magenta and violet, with HDR emission that blooms.
- **Physics**: the orbs and lanterns are dynamic bodies the player pushes. They stay kinematic until the
  terrain's collider is ready, so they do not fall through it while it is cooked.

## How it is built

Everything but the Poly Haven files is made by scripts with fixed seeds:

- `tools/make_assets.py` writes the terrain, trees, neon, logs, their textures, the sprites and the sounds,
  and provides `terrain_height()`.
- `tools/make_scene.py` writes `build.jsonl`, the editor commands that create the materials and the scene,
  placing everything on the ground with `terrain_height()`.
- `modules/techdemo` is the game module: walking, looking, the clock, night effects, flickering lights.

To change the scene, edit a script and rebuild:

```bash
python samples/techdemo/tools/make_assets.py
python samples/techdemo/tools/make_scene.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --project --editor-arg samples/techdemo replay samples/techdemo/build.jsonl
```

`tools/test_tools.py` tests the scripts; CI runs it.

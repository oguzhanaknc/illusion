# Tech demo project

A first-person clearing in a fir forest that changes from day to night: wind in the trees and grass, sun
shafts at dusk, fireflies, a campfire, neon lights, things to push and a creature that comes after you when it
sees or hears you. See the manual page
[Tech Demo Project](../../docs/content/en/manual/editor/techdemo.md) for the controls.

```bash
python samples/techdemo/tools/fetch_assets.py   # CC0 models and textures from Poly Haven (about 900 MB)
python samples/techdemo/tools/make_assets.py    # regenerate the procedural assets (committed)
python samples/techdemo/tools/make_scene.py     # regenerate build.jsonl, then replay it (both scenes):
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/techdemo replay samples/techdemo/build.jsonl
```

- `assets/external/` (git-ignored): the downloaded Poly Haven assets: firs, young firs, saplings and two
  broad-leaved trees; grass, ferns, bushes, nettles, flowers and weeds; rock faces, boulders, mossy rocks,
  fallen logs, stumps, roots, dry branches and bark; the fire stones, lanterns, an axe; the pine needle litter
  of the ground. `fetch_assets.py` pins every file by size and SHA-256, merges each plant's colour and alpha
  maps into RGBA leaf textures, and writes lighter copies of the trees (`*_lite.gltf`: fewer,
  larger needle and leaf cards, texture transforms baked into the UVs; the engine's levels of detail thin
  them further with distance). Poly Haven assets are CC0: free for
  any use, no attribution required. They are not redistributed in this repository.
- `assets/models`, `assets/textures`, `assets/audio`, `assets/animations`, `assets/ai`: made by
  `make_assets.py` from numbers with fixed seeds: the terrain (with `terrain_height()` for placing things), neon
  rings and bars, a log, flame and smoke sprites, looping wind, cricket and fire sounds, and the creature: a
  rigged animal of boxes with the clips `idle`, `walk` and `run` (`creature.gltf`), its animation graph, which
  blends them by `speed` (`creature.animgraph`), and its behaviour tree (`creature.behavior`: chase the player,
  search where it was last sensed and go home, wander around the den).
- The creature in `forest.scene`: `build.jsonl` adds it after the UI, so that every other id stays, with a
  `NavMeshSurface` over the clearing baked by `nav.bake` into `assets/navigation/clearing.navmesh` (committed), a
  costly `NavArea` around the campfire, and a `NavAgent` (Character drive, `speedParameter` into its graph), an
  `AiPerception` and a `BehaviorAgent`; the player gets an `AiStimulus`. The module reports the player's
  footsteps as noises (`AiService`), walks the creature while it wanders and runs it while it chases, answers
  its `growl` action, shows the HUD's alert while it chases, and stops its tree and navigation while the pause
  menu shows. `editor/tests/play/TestTechDemo.cpp` runs it on the scene with the committed assets only.
- `assets/scenes/ui.scene`: the game UI, built by the same `build.jsonl` after the forest: a HUD (title, the
  binding of the pause action, a frame-time readout, outlined so that it reads over the scene, and the alert
  "Something has picked up your trail", hidden until the creature chases you) and a modal pause
  menu, hidden at the start (Resume, Save game, Load game, the master volume, English or Turkish, Quit). The module instantiates it
  when the game starts and drives it through `UiService`; Escape or the gamepad's East button opens and closes
  the menu, and while it shows the clock stops and the walker reads no input. Its text comes from
  `assets/locales/en.json` and `tr.json`, in `assets/fonts/Roboto-Regular.ttf` (Apache-2.0, licence beside it).
  The UI needs none of the downloaded assets: `editor/tests/play/TestTechDemo.cpp` and the golden image
  `techdemo-pause-menu` (`editor/tests/gpu/TestTechDemoGpu.cpp`) run the module on it alone in CI.
- `tools/test_tools.py` tests both scripts (CI runs it).

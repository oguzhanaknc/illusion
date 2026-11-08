# Tech demo project

A first-person clearing in a fir forest that changes from day to night: wind in the trees and grass, sun
shafts at dusk, fireflies, a campfire, neon lights and things to push. See the manual page
[Tech Demo Project](../../docs/content/en/manual/editor/techdemo.md) for the controls.

```bash
python samples/techdemo/tools/fetch_assets.py   # CC0 models and textures from Poly Haven (about 900 MB)
python samples/techdemo/tools/make_assets.py    # regenerate the procedural assets (committed)
python samples/techdemo/tools/make_scene.py     # regenerate build.jsonl, then replay it:
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
- `assets/models`, `assets/textures`, `assets/audio`: made by `make_assets.py` from numbers with fixed seeds:
  the terrain (with `terrain_height()` for placing things), neon rings and bars, a log, flame and smoke
  sprites, and looping wind, cricket and fire sounds.
- `tools/test_tools.py` tests both scripts (CI runs it).

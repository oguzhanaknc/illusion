# Tech demo project

A first-person forest clearing that changes from day to night: wind in the trees and grass, fireflies, a
campfire, neon lights and things to push. See the manual page
[Tech Demo Project](../../docs/content/en/manual/editor/techdemo.md) for the controls.

```bash
python samples/techdemo/tools/fetch_assets.py   # CC0 models and textures from Poly Haven (about 28 MB)
python samples/techdemo/tools/make_assets.py    # regenerate the procedural assets (committed)
python samples/techdemo/tools/make_scene.py     # regenerate build.jsonl, then replay it:
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --project --editor-arg samples/techdemo replay samples/techdemo/build.jsonl
```

- `assets/external/` (git-ignored): the downloaded Poly Haven assets (fern, grass, shrub, mossy rocks, a
  boulder, a fallen trunk, a lantern, forest ground textures). `fetch_assets.py` pins every file by size and
  SHA-256 and merges each plant's colour and alpha maps into RGBA leaf textures. Poly Haven assets are CC0:
  free for any use, no attribution required. They are not redistributed in this repository.
- `assets/models`, `assets/textures`, `assets/audio`: made by `make_assets.py` from numbers with fixed seeds:
  the terrain (with `terrain_height()` for placing things), three trees, neon rings and bars, a log, flame
  and smoke sprites, and looping wind, cricket and fire sounds.
- `tools/test_tools.py` tests both scripts (CI runs it).

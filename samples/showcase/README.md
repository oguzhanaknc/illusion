# Showcase project

An editor project whose scene uses every saved engine Component with every field set. See
[docs/content/en/manual/editor/showcase.md](../../docs/content/en/manual/editor/showcase.md).

`assets/textures/smoke.png.import.json` is a non-default import settings sidecar (`"quality": "high"`), written
by `build.jsonl` through `asset.set_import_settings`.
`settings/window.json` (1600x900, not resizable) is written the same way by `project.settings_set` on
`illusion.WindowSettings`.
`settings/export.json` (product "Showcase", identifier `dev.illusion.showcase`, one exclude pattern) comes from
`project.settings_set` on `illusion.ExportSettings`.

The project is also built as an exported game, the CMake target `showcase_game` (`samples/CMakeLists.txt`,
`illusion_add_game()`): its module linked statically and its data packed next to the executable.

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

# Showcase project

An editor project whose scene uses every saved engine Component with every field set. See
[docs/content/en/manual/editor/showcase.md](../../docs/content/en/manual/editor/showcase.md).

`assets/textures/smoke.png.import.json` is a non-default import settings sidecar (`"quality": "high"`), written
by `build.jsonl` through `asset.set_import_settings`.

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

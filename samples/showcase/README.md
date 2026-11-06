# Showcase project

An editor project whose scene uses every saved engine Component with every field set. See
[docs/content/en/manual/editor/showcase.md](../../docs/content/en/manual/editor/showcase.md).

```bash
python samples/showcase/tools/make_assets.py
illusion_ctl --launch illusion_editor --editor-arg --headless --editor-arg --no-gpu --editor-arg --deterministic --editor-arg --project --editor-arg samples/showcase replay samples/showcase/build.jsonl
```

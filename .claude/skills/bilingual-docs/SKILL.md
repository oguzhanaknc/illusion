---
name: bilingual-docs
description: Write or update illusion documentation in English and Turkish so the CI parity check and strict MkDocs build pass. Use for any change to docs/content, user-visible behaviour, or public API.
---

# Bilingual documentation

Policy: `docs/adr/0006-documentation-languages.md`.

## Where things go

| Content | Location | Languages |
| --- | --- | --- |
| Guides, manual, tutorials | `docs/content/en/**.md` + same path in `docs/content/tr/` | EN + TR |
| Navigation | `nav:` in `mkdocs.yml` (paths without language prefix) + `nav_translations` for `tr` | EN + TR |
| API reference | `///` comments in public headers (Doxygen) | EN |
| Design decisions | `docs/adr/NNNN-*.md` | EN |

## Steps

1. Write or update the English page first.
2. Create or update the Turkish page at the same relative path:
   - Same headings, in the same order and at the same levels.
   - Same tables, admonitions (`!!! warning "..."` — the title may be translated) and images.
   - **Copy code blocks byte for byte** — including comments, which stay in English.
   - Keep technical terms in English with Turkish suffixes after an apostrophe: Entity'ler, Component'i,
     Query'leri, Job'lar, Shader'ı, Render Pass'ler, Chunk'lar, Archetype'a, Sink'ler, Asset'ler,
     Benchmark'lar, Build'ler, Preset'ler.
   - Translate everything else naturally; do not translate word for word.
3. New page? Add it to `nav:` in `mkdocs.yml`; add new nav labels to `nav_translations` under `tr`.
4. Links between pages are relative (`../manual/ecs.md`). Anchors are generated from headings, so a
   Turkish anchor differs from the English one (e.g. `#benchmarklar` vs `#benchmarks`).
5. Run the checks:

```bash
python tools/check_docs_parity.py
pip install -r docs/requirements.txt   # once
mkdocs build --strict
```

## Writing style

- Short sentences, active voice, second person ("you"). Present tense.
- Lead with what the reader can do; explain internals in a "How it works" section.
- Every code sample must compile against the current API. Prefer complete, minimal snippets.
- Use admonitions sparingly: `warning` for things that cause bugs, `note` for useful context.

# 0006. Bilingual documentation (English and Turkish)

- Status: Accepted
- Date: 2026-10-10

## Context

The documentation must serve English and Turkish readers at commercial quality. Translations drift
quickly unless the process enforces synchronisation. Turkish technical writing commonly keeps engine
terminology in English, and translating terms such as "Entity" or "Render Pass" hurts searchability and
consistency with the API.

## Decision

- User documentation is written with MkDocs Material and `mkdocs-static-i18n` in `docs/content/en` and
  `docs/content/tr` with identical file layouts. English is the source language.
- Both languages are updated in the same change. CI (`tools/check_docs_parity.py`) checks that both
  languages have the same pages, the same heading structure, the same number of tables, admonitions and
  images, and byte-identical code blocks. `mkdocs build --strict` fails on broken links and anchors.
- Turkish keeps technical terms in English (Entity, Component, System, Archetype, Chunk, Query, Job,
  Shader, Render Pass, Mesh, Material, Texture, Asset, Sink, and so on). Code and code comments are never
  translated.
- ADRs, code comments and the API reference (Doxygen) are English only.
- The MkDocs toolchain is pinned to MkDocs 1.6 and Material 9.7. The Material maintainers have announced
  that MkDocs 2.0 removes the plugin system; we stay on 1.x until a supported migration path exists.

## Consequences

- Every documentation change costs a translation; this is intended.
- The parity check cannot verify that prose matches; reviewers must compare the meaning.

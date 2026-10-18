# Contributing

illusion is developed by humans and AI agents with the same rules. The authoritative, detailed rules
for agents are in [`AGENTS.md`](https://github.com/oguzhanaknc/illusion/blob/main/AGENTS.md); this page
summarises them.

## Workflow

1. Make the change on a branch.
2. Add or update tests. Every feature and every bug fix comes with tests.
3. Update the documentation in **both** English and Turkish in the same change.
4. Run the checks below.
5. Review the diff before committing. Commit messages follow
   [Conventional Commits](https://www.conventionalcommits.org/), for example `feat(ecs): add queries`.

## Checks

```bash
python tools/check_format.py
python tools/check_docs_parity.py
cmake --preset ci-linux-clang
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug
```

CI runs the same checks on Windows, macOS and Linux, plus sanitizer builds, the documentation build
and the API reference.

## Code style

- C++20. Formatting is enforced by clang-format 22 (`python tools/check_format.py --fix`).
- Types and enums `PascalCase`; functions and variables `camelCase`; members `m_camelCase`;
  constants `kPascalCase`; macros `ILL_UPPER_CASE`.
- Files are named after their main type: `JobSystem.hpp`, `JobSystem.cpp`.
- Public headers live in `include/illusion/<module>/`; implementation details go in `src/` or in a
  `detail` namespace.
- Every public declaration has a Doxygen comment (`///`).
- No exceptions, RTTI-based logic or heap allocations in per-frame code.

## Documentation

English and Turkish pages must stay in sync; CI checks that both languages have the same pages, the
same heading structure and identical code blocks.

The Turkish documentation keeps technical terms in English, for example Entity, Component, System,
Archetype, Chunk, Query, Job, Shader, Render Pass, Mesh, Material, Texture, Asset and Sink. Write them
exactly as in the English text.

## User-visible strings

Text that players or editor users see goes through a string table, never a string literal in code (see
[Localization](manual/localization.md)):

- Add the key and its English text to the table's `en.json`, keeping keys sorted, with a `comment` when the
  context is not obvious.
- Give each sentence one key and pass values as arguments; never join strings into sentences.
- Use plural forms (`{n, plural, one {...} other {...}}`) for anything that is counted.
- Run `illusion_textc check` (CTest `text.locales` does it in CI). Changing English text makes existing
  translations stale; their `source` hash shows which ones.

Log messages, assertion messages and text for AI agents stay English and are not localized.

## Architecture Decision Records

Record any decision that is hard to reverse or affects several modules (dependencies, data formats,
threading, public API conventions):

1. Copy `docs/adr/0000-template.md` to `docs/adr/NNNN-short-title.md` with the next free number.
2. Fill in context, decision and consequences. Status starts as "Proposed".
3. Add the ADR to `docs/content/en/architecture/decisions.md` and its Turkish counterpart.
4. Set the status to "Accepted" when the change lands. Superseded ADRs are kept and marked as such.

## Dependencies

Third-party libraries are pinned to an exact version and verified with a SHA-256 hash in
`cmake/Dependencies.cmake`, and listed with their licences in `THIRD_PARTY_NOTICES.md`. Only add
dependencies with licences compatible with commercial, closed-source distribution.

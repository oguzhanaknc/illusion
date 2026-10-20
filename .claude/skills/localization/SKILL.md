---
name: localization
description: Add or change user-visible text in illusion through string tables, add a string table, or add a language. Use whenever code shows text to players or editor users, or when touching data/locales/*.json.
---

# Localization

User-visible text never appears as a string literal in engine, editor or game code. It lives in a string
table: a key, its English text in `en.json`, and translations in `<locale>.json`. Read
`docs/content/en/manual/localization.md` and ADR 0029 first.

## Adding text to an existing table

1. Open the table's `data/locales/en.json` (for example `engine/input/data/locales/en.json`).
2. Add the key in sorted position. Keys match `^[a-z][a-z0-9_]*(\.[A-Za-z0-9_]+)+$`; the first part is
   the table name. Name keys after where the text appears (`editor.menu.file.save`), not after the text.
3. Write the English text as a MessageFormat pattern:
   - one key per sentence, values as arguments: `"{name} joined {team}"`, never two keys joined in code;
   - counted things use plurals: `"{count, plural, one {# file} other {# files}}"`;
   - literal braces and apostrophes are quoted: `'{'`, `''`.
4. Add a `"comment"` (object form `{"text": ..., "comment": ...}`) when a translator could misread the
   context.
5. Use the generated constant: `game.hud.ammo` becomes `kHudAmmo` in the table's namespace, e.g.
   `localization.format(game::strings::kHudAmmo, {{"count", ammo}}, out)`.
6. Build: the header is regenerated. Run `ctest -R text.locales`.

Changing the English text of an existing key makes every translation of it stale (the check warns until
its `source` hash is updated). Change keys only when the meaning changes.

## Adding a string table

1. Create `data/locales/en.json` with `"format": "illusion.locale"`, `"version": 1`, `"locale": "en"`,
   `"displayName": "English"`, `"plural": "en"` and `"strings"`.
2. In the target's CMakeLists.txt:

   ```cmake
   illusion_add_string_table(<target> TABLE <name> SOURCE data/locales/en.json
       NAMESPACE <ns> HEADER <dir>/<Name>Strings.hpp)
   ```

   This links `illusion::text` and adds the directory to the `text.locales` check.
3. Register the table with `Localization::addTable(<ns>::kTable)` wherever the application sets up its
   localization.

## Adding a language

1. Copy `en.json` to `<locale>.json` (BCP 47 tag, e.g. `pt-BR.json`); set `locale`, `displayName`,
   `plural` (a language with built-in rules: en, de, tr, fr, es, it, pt, ru, uk, pl, cs, ar, ja, zh, ko)
   and `number` separators.
2. Translate. Record `"source"`: the xxHash64 of the English text, 16 lowercase hex digits
   (`text::sourceHashText(text::sourceHash(english))`).
3. Plurals must have every category of the locale's rule (Russian: one, few, many, other).
4. Run `illusion_textc check <dir>`; fix every error. Missing keys are warnings (English is shown).

## Rules

- Formatting must not allocate per frame: format into a `TextOutput` backed by a frame `LinearAllocator`.
- Call `Localization::applyPendingLocale()` once per frame, before UI code; never switch mid-frame.
- Search compares `searchFold()`ed text and should match localized and English titles.
- Log messages, assert messages and text for AI agents (protocol errors, command descriptions) stay
  English.
- Do not use `std::format` for translated patterns (it throws on malformed input).
- Tests for new text: format the key in English and, when behaviour depends on the language, with an
  inline locale file (see `tests/text/TestLocalization.cpp`).

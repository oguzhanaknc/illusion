# 0029. Localize with stable keys, compiled English, data-file locales and a MessageFormat subset

- Status: Accepted
- Date: 2026-10-18

## Context

The editor (M7) must ship in English and accept new languages as data files, without code changes. Text
also appears in engine libraries (key and gamepad glyph labels) and, later, in reflection metadata and
game modules. The text system must:

- never throw, assert on bad translations or allocate per frame (UI text is formatted every frame);
- handle plurals correctly in languages with more than two forms (Russian, Polish, Arabic);
- format numbers with the locale's separators;
- catch mistakes in translations at build time (CI), because translators are rarely programmers;
- let tools and agents keep working in English while the UI shows another language.

Options considered:

- **gettext (PO files, English text as the key).** Mature tooling, but the English text is the identity:
  fixing a typo orphans every translation, and identical English strings with different meanings collide.
  It needs a runtime (GPL/LGPL libintl) or a reimplementation, and its plural syntax is C expressions.
- **ICU MessageFormat via ICU4C.** Complete and correct, but ICU is about 30 MB of data and code, reports
  errors through exceptions or status codes across a large C API, and allocates heavily.
- **`std::format` patterns per language.** Already available, but it throws on malformed patterns
  (translations are untrusted input), has no plurals or selects, and positional arguments make
  translations fragile.
- **Fluent (Mozilla).** Expressive, but there is no small permissive C++ implementation, and translators
  and tools (Weblate, Crowdin, Poedit) support it less than ICU MessageFormat.

## Decision

- **Library.** `engine/text` (`illusion::text`, depends only on core): UTF-8 helpers, locale-independent
  search folding, fuzzy matching, `LocaleId` (BCP 47: language, script, region), `NumberFormat`, CLDR
  cardinal plural rules for en, de, tr, fr, es, it, pt, ru, uk, pl, cs, ar, ja, zh and ko, a MessageFormat
  subset, string tables and `Localization`.
- **Stable keys.** Strings are identified by ASCII keys (`input.key.space`, `editor.menu.file.save`), never
  by their English text. Keys match `^[a-z][a-z0-9_]*(\.[A-Za-z0-9_]+)+$`; the first part names the table.
- **English is compiled in.** Each table's English source is a locale file (`en.json`), the source of
  truth. `illusion_add_string_table()` runs `illusion_textc generate` at build time, which emits a header
  with one `constexpr TextKey` per key and a source file with the English text. A mistyped key does not
  compile, a lookup is an array index, and English is always complete.
- **Locales are data files.** A translation is `<bcp47>.json` in the same strict JSON format, loaded at run
  time. Each entry may record `source`, the xxHash64 of the English text it was translated from, so that
  changed English text marks the translation as stale.
- **Fallback per key:** the locale, its parents (`tr-TR` → `tr`), the `fallback` their files declare, then
  English. Switching languages is requested with `setLocale()` and applied by `applyPendingLocale()` at a
  frame boundary, so one frame never mixes languages. A key that falls back to English is logged once.
- **MessageFormat subset**, compiled once into an operation list: `{name}`, `{n, number}`,
  `{n, plural, =N {} one {} ... other {}}` with `#`, `{s, select, ... other {}}`, nesting up to eight
  levels, and ICU apostrophe quoting. Compilation returns errors as values with an offset; formatting
  never fails hard: a missing argument is written as `{name}` and reported. Formatting writes into a
  `TextOutput`, which can be backed by a `LinearAllocator`, and does not allocate.
- **Numbers** are shown with grouping and at most three fraction digits (rounded half to even), the
  default decimal format of most locales; the same digits select the plural form, so "1.0" shown as "1"
  takes the form of 1.
- **Pseudo-locale** `en-XA`, generated at run time from English: accented letters, about 35% padding and
  brackets, placeholders intact, for checking layouts.
- **CI check.** CTest `text.locales` runs `illusion_textc check` over every table directory in every build
  job. Errors: non-strict JSON, invalid keys, unsorted keys, patterns that do not parse, extra keys,
  arguments that differ from English, plurals missing a form of the locale's rule. Warnings (errors with
  `--strict`): missing keys, stale or missing source hashes. One parser (`LocaleFile`) serves the runtime,
  the generator and the check.
- **Search** uses `searchFold()`, which is locale-independent (I, ı, İ and i all fold to i; diacritics are
  removed), so that search matches both localized and English text.
- **Scope limits.** No text shaping or bidirectional text (the UI library does not support them), so
  Arabic, Hebrew and complex scripts are not supported for display, although Arabic plural rules exist.
  Agent-facing text (protocol errors, command descriptions) stays English.

## Consequences

- Adding a language is a data file plus `illusion_textc check`; no rebuild.
- Every user-visible string in engine and editor code goes through a table; reviews reject string
  literals in UI code. English edits are cheap, but each one makes existing translations stale until
  their `source` hash is updated.
- The MessageFormat subset excludes number styles, dates, offsets and ordinals; adding them later is
  compatible with the syntax.
- Keys known only at run time (reflection metadata, game modules) can be looked up by name with
  `Localization::find()`; generating their English defaults is left to M7e and later milestones.
- Plural rules and number formats are built in for the listed languages; another language needs a code
  change to `PluralRules.cpp` (its locale file names the rule in `plural`).

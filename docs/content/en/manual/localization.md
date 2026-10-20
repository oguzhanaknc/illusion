# Localization

illusion shows user-visible text through string tables: every string has a stable key, its English text
is compiled into the program, and translations are JSON files loaded at run time. Adding a language needs
no code change and no rebuild.

Headers: `#include <illusion/text/Localization.hpp>` (and `MessageFormat.hpp`, `PluralRules.hpp`,
`SearchFold.hpp`, `FuzzyMatcher.hpp`, `Utf8.hpp` for the building blocks). Library: `illusion::text`.

## String tables

A table is a locale file with the English source, usually `data/locales/en.json` next to the code that
uses it:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "number": {"decimal": ".", "group": ","},
  "strings": {
    "game.hud.ammo": "{count, plural, =0 {Out of ammo} one {# round} other {# rounds}}",
    "game.menu.start": {"text": "Start", "comment": "Main menu: starts a new game"}
  }
}
```

Keys are ASCII, dot-separated and sorted: `^[a-z][a-z0-9_]*(\.[A-Za-z0-9_]+)+$`. The first part names the
table. A `comment` explains the context to translators.

Declare the table in CMake. At build time `illusion_textc` turns it into a header of `TextKey` constants
and a source file with the English text:

```cmake
illusion_add_string_table(my_game
    TABLE game
    SOURCE data/locales/en.json
    NAMESPACE game::strings
    HEADER game/GameStrings.hpp)
```

The constant name is the key without its first part, in PascalCase: `game.hud.ammo` becomes
`game::strings::kHudAmmo`. A mistyped key does not compile.

## Showing text

`Localization` holds the tables and the active language. Register every table, load translations, and
call `applyPendingLocale()` once per frame, before UI code runs:

```cpp
illusion::text::Localization localization;
localization.addTable(game::strings::kTable);
if (const auto added = localization.addLocale(readText("locales/tr.json")); !added)
{
    ILL_LOG_ERROR("Game", "tr.json:{}: {}", added.error().line, added.error().message);
}
localization.setLocale(*illusion::text::LocaleId::parse("tr-TR"));

// every frame:
localization.applyPendingLocale();

std::string label;
illusion::text::TextOutput out(label);
(void)localization.format(game::strings::kHudAmmo, {{"count", ammo}}, out);
```

`setLocale()` takes effect at the next `applyPendingLocale()`, so one frame never mixes two languages.
`TextOutput` can also build text in a `LinearAllocator`, which keeps per-frame UI text off the heap:

```cpp
illusion::text::TextOutput frameText(frameArena);
(void)localization.format(game::strings::kMenuStart, {}, frameText);
drawLabel(frameText.view());
```

## Message syntax

Patterns use a subset of ICU MessageFormat:

| Syntax | Meaning |
| --- | --- |
| `{name}` | An argument: a string, or a number with the locale's separators. |
| `{n, number}` | A number. |
| `{n, plural, =0 {...} one {...} other {...}}` | Plural forms; `#` is the number. `=N` matches exactly. |
| `{s, select, female {...} male {...} other {...}}` | A choice by string value. |
| `''` | One apostrophe. |
| `'{'`, `'}'`, `'#'` | Literal braces, and a literal `#` inside plural forms. |

Plurals and selects need an `other` branch and nest up to eight levels. Never build sentences by joining
strings: give the whole sentence one key and pass the parts as arguments.

Formatting always writes the whole message. A missing argument is written as `{name}` and reported as an
error value; nothing throws or asserts. Patterns are compiled when a table or locale is added.

## Plurals and numbers

Each language has CLDR plural rules: English has `one` and `other`, Turkish `one` (n = 1) and `other`,
Russian `one`, `few`, `many` and `other`. Built-in rules cover en, de, tr, fr, es, it, pt, ru, uk, pl, cs,
ar, ja, zh and ko. A locale file names its rule in `plural`.

Numbers are shown with grouping and at most three fraction digits: `1234.5` is `1,234.5` in English and
`1.234,5` in Turkish. The displayed digits also select the plural form, so `1.0` (shown as `1`) takes the
form of 1. Editable number fields must not use the display format; they use the invariant `.`.

## Translations

A translation is a locale file named after its locale (`tr.json`, `pt-BR.json`) with the same keys:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "tr",
  "displayName": "Türkçe",
  "plural": "tr",
  "number": {"decimal": ",", "group": "."},
  "fallback": "en",
  "strings": {
    "game.hud.ammo": {"text": "{count, plural, =0 {Mermi bitti} one {# mermi} other {# mermi}}", "source": "75071fcff137de6a"},
    "game.menu.start": {"text": "Başla", "source": "6f75a38f09135d29"}
  }
}
```

`source` is the xxHash64 of the English text the translation was made from (16 lowercase hex digits).
When the English text changes, the check reports the translation as stale. `fonts` may list extra
fallback fonts the language needs.

Each key is resolved separately: the locale (`tr-TR`), its parents (`tr`), the `fallback` of their files,
then English, which is always complete. A key that falls back to English is logged once at Debug level;
`missingKeyCount()` counts them. Translations whose pattern does not compile, or that use arguments the
English text does not have, are skipped with a warning.

To add a language:

1. Copy `en.json` to `<locale>.json`.
2. Set `locale`, `displayName`, `plural` and `number`.
3. Translate the texts and record each `source` hash.
4. Run `illusion_textc check <directory>`.
5. Load the file with `addLocale()`. No code change is needed.

## Pseudo-locale

The locale `en-XA` shows every English string pseudo-localized: `[Šåvé Šçéñé~~~]`. Letters get accents,
text grows by about 35% and is bracketed, and arguments stay intact. Use it to find strings that are not
in a table (they stay plain) and layouts that cannot take longer translations.

## Checking locale files

`illusion_textc check [--strict] <directory>...` checks `en.json` and every translation in each directory.
CTest `text.locales` runs it over every string table in every build job:

| Error | Warning (error with `--strict`) |
| --- | --- |
| JSON that is not strict (comments, trailing commas) | Missing key (English is shown) |
| Invalid or unsorted keys | Stale or missing `source` hash |
| A pattern that does not parse | |
| A key that is not in English | |
| Argument names that differ from English | |
| A plural without every form of the locale's rule | |

## Search

`searchFold()` removes case and diacritics without depending on the locale, so that I, ı, İ and i all
match, and ş matches s. `FuzzyMatcher` ranks candidates for command palettes and search boxes: the
pattern's characters must appear in order, and matches at word starts, camel-case humps and in runs score
higher.

```cpp
illusion::text::FuzzyMatcher matcher(query);
for (const Command& command : commands)
{
    if (const auto score = matcher.score(command.title))
    {
        results.push_back({*score, &command});
    }
}
```

Match both the localized and the English title, so that users can search in either language.

## Input labels

The input library has a table for key and gamepad glyph labels. `glyphLabelKey()` gives the key of a
binding's label and `glyphLabel(binding, family, localization)` formats it in the active language
(register `illusion::input::strings::kTable` from `<illusion/input/InputStrings.hpp>`). The English
`glyphLabel(binding, family)` is unchanged.

## Limitations

Text is not shaped and has no bidirectional layout, so Arabic, Hebrew and complex scripts cannot be
displayed correctly. Text for AI agents (protocol errors, command descriptions) is always English.

See [ADR 0029](../architecture/decisions.md) for the design.

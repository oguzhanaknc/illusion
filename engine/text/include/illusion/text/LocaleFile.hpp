#pragma once

/// @file
/// Locale files: the strict JSON format of string tables and their translations. The same parser serves
/// the runtime (Localization), the code generator and the CI check (illusion_textc).
///
/// ```json
/// {"format": "illusion.locale", "version": 1, "locale": "tr", "displayName": "Türkçe",
///  "plural": "tr", "number": {"decimal": ",", "group": "."}, "fallback": "en",
///  "strings": {
///    "editor.menu.file.save": {"text": "Kaydet", "source": "8c1f0d5e2a3b4c6d", "comment": "File menu"}}}
/// ```
///
/// An entry is either a string (the pattern) or an object with "text" and optional "comment" and "source"
/// (the xxHash64 of the English text the translation was made from, 16 lowercase hex digits).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/text/LocaleId.hpp>
#include <illusion/text/NumberFormat.hpp>
#include <illusion/text/PluralRules.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::text
{

/// Why a locale file is invalid.
struct LocaleError
{
    u32 line = 0;        ///< 1-based line of the problem (0 if it concerns the whole file).
    u32 column = 0;      ///< 1-based byte column.
    std::string message; ///< Description in English.
};

/// One string of a locale file.
struct LocaleEntry
{
    std::string key;           ///< The key, e.g. "editor.menu.file.save".
    std::string text;          ///< The pattern (MessageFormat).
    std::string comment;       ///< Note for translators (may be empty).
    std::optional<u64> source; ///< xxHash64 of the English text this translation was made from.
};

/// A parsed locale file.
struct LocaleFile
{
    /// Current version of the format.
    static constexpr u32 kVersion = 1;

    LocaleId locale;                         ///< The locale, e.g. "tr".
    std::string displayName;                 ///< Name of the language in itself, e.g. "Türkçe".
    PluralRule plural = PluralRule::English; ///< Plural rule.
    std::string pluralName;                  ///< The rule as written in the file, e.g. "tr".
    NumberFormat number;                     ///< Number separators.
    std::optional<LocaleId> fallback;        ///< Locale to use for keys this file lacks, before English.
    std::vector<std::string> fonts;          ///< Extra fallback fonts the language needs (file names).
    std::vector<LocaleEntry> entries;        ///< Strings, in file order.

    /// Parses @p json. Checks the structure, the metadata and the syntax of every key; patterns are not
    /// compiled here. Returns the file or the first problem.
    [[nodiscard]] static Expected<LocaleFile, LocaleError> parse(std::string_view json);
};

/// True if @p key is a valid string key: at least two parts separated by dots, the first made of lowercase
/// ASCII letters, digits and underscores and starting with a letter, the others of ASCII letters, digits
/// and underscores ("input.key.left_shift").
[[nodiscard]] bool isValidTextKey(std::string_view key) noexcept;

/// The "source" hash of an English pattern: xxHash64 of its bytes.
[[nodiscard]] u64 sourceHash(std::string_view english) noexcept;

/// @p hash as 16 lowercase hex digits, as locale files store it.
[[nodiscard]] std::string sourceHashText(u64 hash);

} // namespace illusion::text

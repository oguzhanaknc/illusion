#pragma once

/// @file
/// Locale-independent folding for search: case and diacritics are removed, so that "sevgi", "Sevgİ",
/// "SEVGI" and "şevgı" all match. Only for matching; never show folded text.
///
/// ```cpp
/// text::searchFold("Işık Ayarları") == "isik ayarlari"
/// ```
///
/// ASCII letters are lowercased. Latin-1 Supplement and Latin Extended-A letters fold to ASCII (é -> e,
/// ş -> s, ğ -> g, ß -> ss, æ -> ae; I, ı, İ and i all fold to i). Greek and Cyrillic letters fold to their
/// lowercase base letter (Ά -> α, ς -> σ, Ё -> е, Й -> и). Everything else is unchanged.

#include <illusion/core/Base.hpp>

#include <string>
#include <string_view>

namespace illusion::text
{

/// The search fold of one code point: one or two code points.
struct FoldedCodePoint
{
    char32_t codePoints[2] = {}; ///< The folded code points; only the first `count` are used.
    u32 count = 0;               ///< 1 or 2.
};

/// Folds @p codePoint for search.
[[nodiscard]] FoldedCodePoint searchFold(char32_t codePoint) noexcept;

/// Appends the search fold of the UTF-8 text @p text to @p out (invalid bytes become U+FFFD).
void appendSearchFold(std::string_view text, std::string& out);

/// Returns the search fold of the UTF-8 text @p text.
[[nodiscard]] std::string searchFold(std::string_view text);

} // namespace illusion::text

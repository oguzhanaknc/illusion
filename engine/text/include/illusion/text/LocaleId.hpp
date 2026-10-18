#pragma once

/// @file
/// Locale identifiers: a BCP 47 language tag with a language, an optional script and an optional region.
///
/// ```cpp
/// const auto locale = text::LocaleId::parse("tr_tr");   // "tr-TR"
/// locale->language() == "tr"; locale->region() == "TR";
/// locale->parent()->tag() == "tr";
/// ```

#include <illusion/core/Base.hpp>

#include <optional>
#include <string_view>

namespace illusion::text
{

/// A language tag such as "en", "tr-TR", "pt-BR" or "zh-Hant-TW", in canonical case (language lowercase,
/// script title case, region uppercase). Fixed size, so it never allocates. Variants and extensions are
/// not supported. The region "XA" names the pseudo-locale (see Localization).
class LocaleId
{
public:
    /// Longest canonical tag in bytes ("zzz-Zzzz-999").
    static constexpr usize kMaxLength = 12;

    /// Creates an empty identifier (no language).
    constexpr LocaleId() noexcept = default;

    /// Parses @p tag: a language of 2 or 3 letters, then optionally a script of 4 letters and a region of 2
    /// letters or 3 digits, separated by '-' or '_', in any case. Returns nullopt for anything else.
    [[nodiscard]] static std::optional<LocaleId> parse(std::string_view tag) noexcept;

    /// The canonical tag, e.g. "tr-TR" ("" for an empty identifier).
    [[nodiscard]] std::string_view tag() const noexcept { return {m_tag, m_length}; }
    /// The language, e.g. "tr".
    [[nodiscard]] std::string_view language() const noexcept { return {m_tag, m_languageLength}; }
    /// The script, e.g. "Hant", or "".
    [[nodiscard]] std::string_view script() const noexcept;
    /// The region, e.g. "TR", or "".
    [[nodiscard]] std::string_view region() const noexcept;
    /// True for a default-constructed identifier.
    [[nodiscard]] bool empty() const noexcept { return m_length == 0; }

    /// The next more general tag: without the region, then without the script. Nullopt for a language
    /// alone ("zh-Hant-TW" -> "zh-Hant" -> "zh" -> nullopt).
    [[nodiscard]] std::optional<LocaleId> parent() const noexcept;

    /// Identifiers are equal when their canonical tags are.
    [[nodiscard]] friend bool operator==(const LocaleId& a, const LocaleId& b) noexcept
    {
        return a.tag() == b.tag();
    }

private:
    char m_tag[kMaxLength] = {};
    u8 m_length = 0;
    u8 m_languageLength = 0;
    u8 m_scriptLength = 0;
    u8 m_regionLength = 0;
};

} // namespace illusion::text

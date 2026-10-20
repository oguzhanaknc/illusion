#pragma once

/// @file
/// The active language of an application: compiled English tables, translations loaded from locale
/// files, and formatting of keys in the current language.
///
/// ```cpp
/// text::Localization localization;
/// localization.addTable(illusion::input::strings::kTable);
/// (void)localization.addLocale(readText("locales/tr.json"));
/// localization.setLocale(*text::LocaleId::parse("tr-TR"));
///
/// // once per frame, before any UI code runs:
/// localization.applyPendingLocale();
///
/// std::string label;
/// text::TextOutput out(label);
/// (void)localization.format(illusion::input::strings::kKeySpace, {}, out);
/// ```
///
/// Each key is resolved through a fallback chain: the locale, its parents ("tr-TR" -> "tr"), the
/// "fallback" each of their files declares, and English, which is compiled in and always complete. A key
/// that falls back to English is logged once (at Debug level). The pseudo-locale "en-XA" shows every
/// English string pseudo-localized (see MessageFormat::pseudoLocalized()).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/text/LocaleFile.hpp>
#include <illusion/text/LocaleId.hpp>
#include <illusion/text/MessageFormat.hpp>
#include <illusion/text/StringTable.hpp>

#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace illusion::text
{

/// The pseudo-locale "en-XA".
[[nodiscard]] LocaleId pseudoLocale() noexcept;

/// Holds the string tables and translations and resolves keys in the active language. Not thread-safe:
/// use it from the thread that runs the UI.
class Localization
{
public:
    /// Creates a localization with no tables, English active.
    Localization();
    ~Localization();

    Localization(const Localization&) = delete;            ///< Not copyable.
    Localization& operator=(const Localization&) = delete; ///< Not copyable.

    /// Registers a compiled table. Every TextKey used with this object must belong to a registered table.
    /// Registering a table twice does nothing. The table's patterns are checked when it is generated, so a
    /// pattern that does not compile is a programmer error (it asserts, and the key formats to its name).
    void addTable(const StringTableData& table);

    /// Adds the translations of a locale file (see LocaleFile), replacing an earlier file of the same
    /// locale. Translations whose pattern does not compile or uses arguments the English text does not
    /// have are skipped with a warning. Takes effect at the next applyPendingLocale().
    Expected<void, LocaleError> addLocale(std::string_view json);

    /// @copydoc addLocale(std::string_view)
    void addLocale(LocaleFile file);

    /// Locales that can be selected: English, the pseudo-locale and every added locale file, sorted.
    [[nodiscard]] std::vector<LocaleId> availableLocales() const;

    /// Requests @p locale. The switch happens at the next applyPendingLocale(), so that one frame never
    /// mixes two languages. A locale without any file shows English (with the plural rule of its language
    /// when one is built in).
    void setLocale(const LocaleId& locale);

    /// Applies a requested locale or newly added files. Call it at a frame boundary.
    void applyPendingLocale();

    /// The active locale.
    [[nodiscard]] const LocaleId& locale() const noexcept;

    /// Plural rule and number format of the active locale.
    [[nodiscard]] const MessageContext& context() const noexcept;

    /// The compiled message of @p key in the active language.
    [[nodiscard]] const MessageFormat& message(TextKey key) const;

    /// Formats @p key in the active language into @p out (see MessageFormat::format()).
    Expected<void, MessageError> format(TextKey key, std::span<const MessageArgument> arguments,
                                        TextOutput& out) const;

    /// @copydoc format(TextKey, std::span<const MessageArgument>, TextOutput&) const
    Expected<void, MessageError> format(TextKey key, std::initializer_list<MessageArgument> arguments,
                                        TextOutput& out) const
    {
        return format(key, std::span<const MessageArgument>(arguments.begin(), arguments.size()), out);
    }

    /// The message of the key called @p name in the active language, searched in every registered table;
    /// nullptr if no table has it. For keys known only at run time (reflection, modules).
    [[nodiscard]] const MessageFormat* find(std::string_view name) const;

    /// Number of distinct keys that fell back to English in the active locale and were looked up (each is
    /// logged once).
    [[nodiscard]] usize missingKeyCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::text

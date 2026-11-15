#pragma once

/// @file
/// The string tables of a game, read from its locale files (`assets:/locales/*.json`): what a GameSession
/// formats UiText keys with, also usable without one (the editor's preview of the game UI).
///
/// @code
/// illusion::GameStringTable table; // must outlive the Localization
/// illusion::text::Localization localization;
/// illusion::loadGameLocalization(files, localization, table);
/// ui.setLocalization(&localization);
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/text/StringTable.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class VirtualFileSystem;

namespace text
{
class Localization;
}

/// The "game" string table that loadGameLocalization() builds; a Localization views it, so it must outlive
/// that Localization (and it cannot move).
struct GameStringTable : NonCopyable
{
    /// An empty table.
    GameStringTable() = default;

    std::vector<std::string> keys{};              ///< The keys, sorted.
    std::vector<std::string> english{};           ///< English patterns, in key order.
    std::vector<std::string_view> keyViews{};     ///< Views of keys.
    std::vector<std::string_view> englishViews{}; ///< Views of english.
    text::StringTableData table{};                ///< The table over the views (named "game").
};

/// Registers in @p localization the input table (key and gamepad labels for UiGlyph) and, when @p files has
/// locale files in `assets:/locales`, the "game" table built into @p table: every key of every file, with
/// the English text of `en.json`, else of the first file that has the key; texts that do not compile and
/// unreadable files are skipped with a warning. The other files become translations. @p table must be
/// empty, and must outlive @p localization.
void loadGameLocalization(const VirtualFileSystem& files, text::Localization& localization,
                          GameStringTable& table);

} // namespace illusion

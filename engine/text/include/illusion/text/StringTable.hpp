#pragma once

/// @file
/// Compiled string tables: the English text of a set of keys, embedded in the program by illusion_textc,
/// and TextKey constants that refer to it.
///
/// A table is declared in CMake with `illusion_add_string_table()`, which generates a header such as:
///
/// ```cpp
/// namespace illusion::input::strings
/// {
/// extern const ::illusion::text::StringTableData kTable;
/// /// "Space" (input.key.space)
/// inline constexpr ::illusion::text::TextKey kKeySpace{&kTable, 42};
/// }
/// ```
///
/// A mistyped key is a compile error, and a lookup is an array index.

#include <illusion/core/Base.hpp>

#include <span>
#include <string_view>

namespace illusion::text
{

/// The English source of one string table. Generated; keys are sorted by their bytes.
struct StringTableData
{
    std::string_view name;                     ///< Table name, e.g. "input".
    std::span<const std::string_view> keys;    ///< Keys, sorted.
    std::span<const std::string_view> english; ///< English patterns (MessageFormat), in key order.
};

/// A key of a compiled string table.
struct TextKey
{
    const StringTableData* table = nullptr; ///< The table.
    u32 index = 0;                          ///< Index of the key in the table.

    /// The key, e.g. "input.key.space".
    [[nodiscard]] std::string_view name() const noexcept { return table->keys[index]; }
    /// The English pattern.
    [[nodiscard]] std::string_view english() const noexcept { return table->english[index]; }

    /// Keys are equal when they refer to the same entry.
    [[nodiscard]] friend constexpr bool operator==(const TextKey& a, const TextKey& b) noexcept = default;
};

} // namespace illusion::text

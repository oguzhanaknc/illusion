#pragma once

/// @file
/// Material Symbols icons. The constants are generated from the pinned `.codepoints` file into
/// `illusion/editor/ui/Icons.hpp` (`icons::kAccountTree`, ...), so an unknown icon is a compile error.
/// Names that are only known at run time (Component metadata, scripts) are looked up with findIcon().
///
/// ```cpp
/// #include <illusion/editor/ui/Icons.hpp>
///
/// ImGui::TextUnformatted(icons::kAccountTree.c_str());   // the icon alone
/// ImGui::Button(text.iconLabel(icons::kAdd, strings::kAdd, "add"));
/// ```

#include <illusion/core/Base.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion::editor::ui
{

/// One icon of the icon font: its code point and its UTF-8 encoding.
struct Icon
{
    char32_t codepoint = 0; ///< Code point in the icon font (in a private use area).
    char utf8[5] = {};      ///< UTF-8 encoding, zero-terminated.

    /// The UTF-8 encoding as a zero-terminated string.
    [[nodiscard]] constexpr const char* c_str() const noexcept { return utf8; }

    /// The UTF-8 encoding.
    [[nodiscard]] constexpr std::string_view view() const noexcept { return std::string_view(utf8); }
};

/// An icon and its name in the codepoints file, e.g. "account_tree".
struct IconEntry
{
    std::string_view name; ///< The name.
    Icon icon;             ///< The icon.
};

/// Every icon, sorted by name.
[[nodiscard]] std::span<const IconEntry> allIcons() noexcept;

/// The icon called @p name, or std::nullopt.
[[nodiscard]] std::optional<Icon> findIcon(std::string_view name) noexcept;

} // namespace illusion::editor::ui

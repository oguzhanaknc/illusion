#pragma once
/// @file
/// The editor-data folder next to the editor executables (themes, locales, fonts, licences, templates),
/// mounted read-only as "editor:".
///
/// ```cpp
/// VirtualFileSystem vfs;
/// if (ui::mountEditorData(vfs))
///     auto theme = ui::loadTheme(vfs, "dark");
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/editor/ui/Theme.hpp>

#include <array>
#include <string>
#include <string_view>

namespace illusion
{
class VirtualFileSystem;
} // namespace illusion

namespace illusion::text
{
class Localization;
} // namespace illusion::text

namespace illusion::editor::ui
{

/// The themes in editor-data/themes, by file name.
inline constexpr std::array<std::string_view, 3> kThemeNames = {"dark", "light", "high-contrast"};

/// Mounts the editor-data folder next to the executable as "editor:". Returns false (and logs) if it is
/// missing.
bool mountEditorData(VirtualFileSystem& vfs);

/// Loads "editor:/themes/<name>.json". Returns why it failed.
[[nodiscard]] Expected<Theme, std::string> loadTheme(const VirtualFileSystem& vfs, std::string_view name);

/// Adds every locale file in "editor:/locales" except the compiled English one to @p localization; files
/// that do not load are skipped with a warning. Returns the number added.
u32 loadEditorLocales(const VirtualFileSystem& vfs, text::Localization& localization);

} // namespace illusion::editor::ui

#pragma once

/// @file
/// Native message boxes, for failures that happen before a game can show its own UI.

#include <string_view>

namespace illusion
{

/// Shows a modal error message box with @p title and @p message (UTF-8) and returns when the user closes it.
/// Windows uses MessageBoxW, macOS a CoreFoundation alert, Linux `zenity` or `kdialog` when one is installed;
/// returns false when no dialog could be shown.
bool showErrorDialog(std::string_view title, std::string_view message);

} // namespace illusion

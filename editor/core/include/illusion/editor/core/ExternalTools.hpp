#pragma once

/// @file
/// External tools of the preferences (ADR 0043): the command lines that open a source file at a line in the
/// configured code editor or with the system's default application.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// The program and arguments that open @p file at @p line and @p column with the code editor command line
/// @p command (Preferences::codeEditor), e.g. `code -g {file}:{line}:{column}`.
///
/// The command is split at spaces outside double quotes (quotes are removed, so a program path may contain
/// spaces), and `{file}`, `{line}` and `{column}` are replaced in every argument; line and column 0 become
/// 1. When no argument names `{file}`, the file is appended. The error says why the command is unusable
/// (empty, or an unclosed quote).
[[nodiscard]] Expected<std::vector<std::string>, std::string>
codeEditorArguments(std::string_view command, std::string_view file, u32 line, u32 column);

/// The program and arguments that open @p file with the system's default application: `explorer` on
/// Windows, `open` on macOS and `xdg-open` elsewhere.
[[nodiscard]] std::vector<std::string> defaultOpenArguments(std::string_view file);

/// The program and arguments that reveal @p file (or the directory itself, when @p isDirectory) in the
/// system's file manager, with the file selected where the platform can: the file browser command line
/// @p command (Preferences::fileBrowser), e.g. `explorer /select,{file}`. Split and substituted like
/// codeEditorArguments(); an empty @p command uses defaultRevealArguments(). The error says why the
/// command is unusable (empty program, or an unclosed quote).
[[nodiscard]] Expected<std::vector<std::string>, std::string>
revealArguments(std::string_view command, std::string_view file, bool isDirectory);

/// The program and arguments that reveal @p file with the system's file manager, the file selected where
/// the platform can (`explorer /select,` on Windows, `open -R` on macOS, the containing folder on Linux).
/// @p isDirectory opens the directory itself instead.
[[nodiscard]] std::vector<std::string> defaultRevealArguments(std::string_view file, bool isDirectory);

} // namespace illusion::editor

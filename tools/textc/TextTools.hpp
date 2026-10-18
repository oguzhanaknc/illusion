#pragma once

// Code generation and checks for string tables, shared by illusion_textc and its unit tests.
//
// generateStringTable() turns an English locale file into a header of TextKey constants and a source file
// with the embedded English text. checkEnglish() and checkTranslation() report the problems the CI check
// (CTest text.locales) fails on.

#include <illusion/core/Expected.hpp>
#include <illusion/text/LocaleFile.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::text::tools
{

/// How serious a problem is.
enum class Severity : u8
{
    Warning, ///< Allowed while a translation is in progress (missing keys, stale source hashes).
    Error,   ///< Fails the check.
};

/// One problem found in a locale file.
struct Diagnostic
{
    Severity severity = Severity::Error; ///< Severity.
    std::string file;                    ///< File name as given to the check.
    std::string key;                     ///< The key concerned ("" for the whole file).
    std::string message;                 ///< Description in English.
};

/// Problems found by the checks.
struct CheckReport
{
    std::vector<Diagnostic> diagnostics; ///< In the order they were found.

    /// Number of errors.
    [[nodiscard]] usize errorCount() const noexcept;
    /// Number of warnings.
    [[nodiscard]] usize warningCount() const noexcept;
    /// One line per diagnostic: "<file>: <error|warning>: <key>: <message>".
    [[nodiscard]] std::string format() const;
};

/// Options of the checks.
struct CheckOptions
{
    /// Turn warnings into errors (for shipped locales).
    bool strict = false;
};

/// Checks the English source @p json (file name @p file for messages). Returns the parsed file if it can be
/// used as the reference for translations (no parse error), even if other errors were reported.
std::optional<LocaleFile> checkEnglish(std::string_view json, std::string_view file, CheckReport& report);

/// Checks the translation @p json against @p english.
void checkTranslation(const LocaleFile& english, std::string_view json, std::string_view file,
                      const CheckOptions& options, CheckReport& report);

/// What generateStringTable() needs besides the English file.
struct GenerateOptions
{
    std::string table;         ///< Table name, e.g. "input".
    std::string nameSpace;     ///< C++ namespace of the constants, e.g. "illusion::input::strings".
    std::string headerInclude; ///< Path the source file includes the header with.
    std::string sourceName;    ///< Name of the English file, for the "generated from" comment.
};

/// Generated files.
struct GeneratedTable
{
    std::string header; ///< Header with TextKey constants.
    std::string source; ///< Source file with the embedded English text.
};

/// The constant name of @p key: the parts after the first, in PascalCase, prefixed with 'k'
/// ("input.key.left_shift" -> "kKeyLeftShift").
[[nodiscard]] std::string constantName(std::string_view key);

/// Generates the table for the English file @p english. Fails if the file has errors, no strings, or two
/// keys with the same constant name.
[[nodiscard]] Expected<GeneratedTable, std::string> generateStringTable(std::string_view english,
                                                                        const GenerateOptions& options);

} // namespace illusion::text::tools

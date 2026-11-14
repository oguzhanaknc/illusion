#pragma once

/// @file
/// The platform-neutral parts of packaging and signing an export (ADR 0055): the CPack generator of each
/// package, the sign command line and the `codesign`, `notarytool` and `stapler` arguments. ProjectExport
/// uses those of the platform it runs on; tests check every platform's.

#include <illusion/core/Base.hpp>
#include <illusion/editor/core/Export.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::detail
{

/// A platform an export can target.
enum class ExportPlatform : u8
{
    Windows, ///< Windows: executable, NSIS installer, sign command.
    MacOS,   ///< macOS: application bundle, DMG, codesign and notarization.
    Linux,   ///< Linux: executable, tar.gz.
};

/// The platform of this build, which is the only one it exports for.
inline constexpr ExportPlatform kExportPlatform =
#if defined(ILL_PLATFORM_WINDOWS)
    ExportPlatform::Windows;
#elif defined(ILL_PLATFORM_MACOS)
    ExportPlatform::MacOS;
#else
    ExportPlatform::Linux;
#endif

/// "windows", "macos" or "linux".
[[nodiscard]] std::string_view exportPlatformName(ExportPlatform platform) noexcept;

/// How CPack makes a package.
struct ExportPackageFormat
{
    std::string_view generator; ///< CPack generator, e.g. "NSIS".
    std::string_view extension; ///< Extension of the file CPack writes, e.g. ".exe".
    std::string_view suffix;    ///< End of the package's name in the output directory, e.g. "-setup.exe".
};

/// The CPack format of @p package on @p platform (@p package must not be ExportPackage::None).
[[nodiscard]] ExportPackageFormat exportPackageFormat(ExportPackage package,
                                                      ExportPlatform platform) noexcept;

/// Splits a command line at spaces and tabs; double quotes group a part and are removed, backslashes are
/// ordinary characters (Windows paths need no escaping).
[[nodiscard]] std::vector<std::string> splitCommandLine(std::string_view line);

/// @p arguments with every `{file}` replaced by @p file.
[[nodiscard]] std::vector<std::string> substituteFile(const std::vector<std::string>& arguments,
                                                      std::string_view file);

/// The entitlements plist of a signed macOS game; ad hoc signatures also turn off library validation.
[[nodiscard]] std::string codesignEntitlements(bool adHoc);

/// `codesign` arguments that sign @p file with the hardened runtime (a timestamp unless @p identity is "-"),
/// with @p entitlements when it is not empty.
[[nodiscard]] std::vector<std::string> codesignArguments(std::string_view identity, std::string_view file,
                                                         std::string_view entitlements);

/// `xcrun` arguments that notarize @p package with the keychain profile @p profile and wait for the result.
[[nodiscard]] std::vector<std::string> notarizeArguments(std::string_view package, std::string_view profile);

/// `xcrun` arguments that staple the notarization ticket to @p package.
[[nodiscard]] std::vector<std::string> stapleArguments(std::string_view package);

/// makensis on PATH or in the NSIS directory of Program Files (environment variables `ProgramFiles(x86)` and
/// `ProgramFiles`).
[[nodiscard]] std::optional<std::filesystem::path> findMakensis();

} // namespace illusion::editor::detail

#pragma once

/// @file
/// Exporting a project as a game (ADR 0055): the export settings, and ProjectExport, which generates the
/// game's build, cooks and packs its assets, builds the executable against the SDK's player and packages
/// the result.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/editor/core/ModuleBuild.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class ComponentRegistry;
} // namespace illusion

namespace illusion::editor
{

/// File of ExportSettings; never part of an exported game.
inline constexpr std::string_view kExportSettingsFile = "settings:/export.json";

/// How a project is exported (`settings:/export.json`, type "illusion.ExportSettings").
struct ExportSettings
{
    std::string productName{};  ///< Name of the game and its executable ("" for the project's name).
    std::string organization{}; ///< Publisher; parent of the game's user data directory.
    std::string identifier{};   ///< Reverse-DNS identifier, e.g. "com.example.tetris" (macOS bundle id).
    std::string copyright{};    ///< Copyright line of the version resource and the bundle.
    AssetRef icon{};            ///< Square PNG of at least 256 pixels; empty for the engine's icon.
    /// Files under `assets:/` that are not shipped: paths relative to the mount, `*` and `**` wildcards. They
    /// are not cooked either, so an asset that uses an excluded file fails the export's cook.
    std::vector<std::string> exclude{};
};

/// Reflects ExportSettings as a struct of @p registry.
void reflectExportSettings(ComponentRegistry& registry);

/// What an export produces besides the game's directory.
enum class ExportPackage : u8
{
    None,      ///< The directory (the `.app` on macOS) only.
    Archive,   ///< A ZIP of it (`.tar.gz` on Linux).
    Installer, ///< An NSIS installer on Windows, a DMG on macOS, the archive on Linux.
};

/// Stable lower-case name of @p package: "none", "archive" or "installer".
[[nodiscard]] std::string_view exportPackageName(ExportPackage package) noexcept;

/// Code signing of an export; values come from command params or the environment, never from project files.
struct ExportSigning
{
    /// Windows: command line run on the executable and the installer, `{file}` replaced by the path.
    std::string signCommand{};
    std::string codesignIdentity{}; ///< macOS: identity for `codesign` ("-" signs ad hoc).
    std::string notaryProfile{};    ///< macOS: keychain profile for `notarytool`; "" skips notarization.
};

/// What ProjectExport does.
struct ExportOptions
{
    ModuleToolchain toolchain{};                 ///< The SDK's toolchain of the exported configuration.
    std::filesystem::path projectRoot{};         ///< The project to export.
    std::filesystem::path outputDirectory{};     ///< Receives the game's directory and the package.
    ExportPackage package = ExportPackage::None; ///< What to package.
    ExportSigning signing{};                     ///< Signing; empty fields leave the export unsigned.
    /// Linux: Steam Runtime the executable must run in ("" for no check, "sniper").
    std::string linuxRuntime{};
    bool clean = false; ///< Discard the export's build tree first.
};

/// One file of an exported game.
struct ExportedFile
{
    std::string path{}; ///< Relative to the output directory, with forward slashes.
    u64 bytes = 0;      ///< Size in bytes.
};

/// What an export made.
struct ExportReport
{
    std::string platform{};             ///< "windows", "macos" or "linux".
    std::string configuration{};        ///< Build configuration of the executable.
    std::filesystem::path directory{};  ///< The game's directory (the `.app` on macOS).
    std::filesystem::path package{};    ///< The archive or installer; empty for ExportPackage::None.
    std::vector<ExportedFile> files{};  ///< Every file of the directory, sorted by path.
    u64 totalBytes = 0;                 ///< Sum of the files' sizes.
    bool signedBinaries = false;        ///< The executable (and the installer) were signed.
    bool notarized = false;             ///< macOS: the package was notarized and stapled.
    std::vector<std::string> skipped{}; ///< Asset files left out by the exclude patterns, sorted.
};

/// One export of a project. Each step is a child process or a slice of work that update() advances, so an
/// async command can drive it without blocking a frame. Destroying a running export terminates its process
/// and leaves the output directory without a partial game.
class ProjectExport : NonCopyable
{
public:
    /// The step that runs.
    enum class Stage : u8
    {
        Validate,  ///< Project, settings and SDK checks.
        Generate,  ///< Writes the build of the game under `.illusion/export/<platform>`.
        Cook,      ///< Imports every asset into a fresh cache.
        Pack,      ///< Writes the `.illpak` archives.
        Configure, ///< `cmake -S ... -B ...` (after the toolchain's environment script, when it has one).
        Build,     ///< `cmake --build ...`, signing the executable when asked.
        Install,   ///< `cmake --install ...` into the output directory.
        Package,   ///< `cpack`, signing and notarization.
        Done,      ///< Finished; see succeeded().
    };

    /// Checks @p options and starts the first step. The error says what cannot be exported.
    [[nodiscard]] static Expected<std::unique_ptr<ProjectExport>, std::string> start(ExportOptions options);

    ~ProjectExport();

    /// Advances the export without blocking; returns true once it is done.
    bool update();

    /// The step that runs.
    [[nodiscard]] Stage stage() const noexcept;

    /// Progress of the whole export in [0, 1].
    [[nodiscard]] f64 progress() const noexcept;

    /// True once done when every step succeeded.
    [[nodiscard]] bool succeeded() const noexcept;

    /// Why the export failed ("" while it runs or after a success).
    [[nodiscard]] const std::string& error() const noexcept;

    /// Compiler, linker and CMake diagnostics so far, in output order.
    [[nodiscard]] const std::vector<BuildDiagnostic>& diagnostics() const noexcept;

    /// The last lines of tool output (at most 64), oldest first.
    [[nodiscard]] std::string outputTail() const;

    /// What was made; complete once succeeded().
    [[nodiscard]] const ExportReport& report() const noexcept;

private:
    ProjectExport();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Stable lower-case name of @p stage, e.g. "cook".
[[nodiscard]] std::string_view exportStageName(ProjectExport::Stage stage) noexcept;

} // namespace illusion::editor

#pragma once

/// @file
/// Building a project's game modules (ADR 0043): the toolchain description of the module SDK, the parser of
/// compiler and CMake diagnostics, and ModuleBuild, which runs CMake as a child process.

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// The `format` value of toolchain descriptions.
inline constexpr std::string_view kModuleSdkFormat = "illusion.sdk";

/// How the engine build that wrote a module SDK compiles code (`<build>/sdk/<Config>/illusion-sdk.json`).
struct ModuleToolchain
{
    std::filesystem::path cmake;        ///< The CMake program.
    std::string generator;              ///< CMake generator, e.g. "Ninja".
    std::string config;                 ///< Build configuration, e.g. "Debug".
    std::filesystem::path package;      ///< Directory of `illusionConfig.cmake` (passed as `illusion_DIR`).
    std::vector<std::string> arguments; ///< Further configure arguments: compiler, flags, make program.
    std::string modulePrefix;           ///< File name prefix of module libraries, e.g. "lib" ("" on Windows).
    std::string moduleSuffix;           ///< File name suffix of module libraries, e.g. ".dll" or ".so".
};

/// Reads a toolchain description; the error says what is wrong.
[[nodiscard]] Expected<ModuleToolchain, std::string> parseModuleToolchain(std::string_view json);

/// Reads the toolchain description file @p file.
[[nodiscard]] Expected<ModuleToolchain, std::string> readModuleToolchain(const std::filesystem::path& file);

/// How serious a build diagnostic is.
enum class DiagnosticSeverity : u8
{
    Error,   ///< The build fails.
    Warning, ///< The build goes on.
    Note,    ///< More about the diagnostic before it.
};

/// Stable lower-case name of @p severity: "error", "warning" or "note".
[[nodiscard]] std::string_view diagnosticSeverityName(DiagnosticSeverity severity) noexcept;

/// One diagnostic of a compiler, linker or CMake.
struct BuildDiagnostic
{
    std::string file; ///< Source file as printed ("" when it names none).
    u32 line = 0;     ///< 1-based line (0: none).
    u32 column = 0;   ///< 1-based column (0: none).
    DiagnosticSeverity severity = DiagnosticSeverity::Error; ///< How serious it is.
    std::string code;    ///< Tool code, e.g. "C2065" or "-Wunused" ("" for none).
    std::string message; ///< What the tool said.
};

/// Turns lines of build output into diagnostics: GCC and Clang (`file:line:col: error: ...`), MSVC and
/// clang-cl (`file(line,col): error C1234: ...`), MSVC linkers (`x.obj : error LNK2019: ...`) and CMake
/// (`CMake Error at file:line (command):` with its indented message lines).
class BuildOutputParser
{
public:
    /// Reads one line (without its line break); appends a diagnostic to @p out when the line starts one, or
    /// extends the message of the last CMake diagnostic with an indented continuation line.
    void feed(std::string_view line, std::vector<BuildDiagnostic>& out);

private:
    bool m_inCMakeMessage = false;
};

/// The progress a build line reports: Ninja's `[3/10]` or the Makefile generators' `[ 30%]`.
struct BuildProgress
{
    f64 done = 0.0;  ///< Steps done.
    f64 total = 0.0; ///< Steps in all.
};

/// The progress that @p line reports, if any.
[[nodiscard]] std::optional<BuildProgress> parseBuildProgress(std::string_view line) noexcept;

/// What ModuleBuild builds.
struct ModuleBuildOptions
{
    ModuleToolchain toolchain;             ///< The SDK's toolchain.
    std::filesystem::path sourceDirectory; ///< The project's `modules/` directory.
    std::filesystem::path buildDirectory;  ///< The build tree (`.illusion/build/modules`).
    std::filesystem::path outputDirectory; ///< Where module libraries are written.
    std::vector<std::string> targets{};    ///< Modules to build (empty: all).
    bool reconfigure = false;              ///< Configure even when the build tree has a cache.
};

/// One build of a project's modules: a CMake configure (when the build tree has no cache yet, or when
/// asked) and then `cmake --build`, each a child process whose output is read line by line. Destroying a
/// running build terminates its process.
class ModuleBuild : NonCopyable
{
public:
    /// The step that runs.
    enum class Stage : u8
    {
        Configure, ///< `cmake -S ... -B ...`.
        Build,     ///< `cmake --build ...`.
        Done,      ///< Finished; see succeeded().
    };

    /// Starts the first step. The error says why CMake could not be started.
    [[nodiscard]] static Expected<std::unique_ptr<ModuleBuild>, std::string>
    start(ModuleBuildOptions options);

    ~ModuleBuild();

    /// Reads the output that arrived and starts the next step when one ends; never blocks. Returns true
    /// once the build is done.
    bool update();

    /// The step that runs.
    [[nodiscard]] Stage stage() const noexcept;

    /// Whether this build ran a configure step.
    [[nodiscard]] bool configured() const noexcept;

    /// True once done when every step succeeded.
    [[nodiscard]] bool succeeded() const noexcept;

    /// The diagnostics so far, in output order.
    [[nodiscard]] const std::vector<BuildDiagnostic>& diagnostics() const noexcept;

    /// The progress of the build step (zero while configuring).
    [[nodiscard]] BuildProgress progress() const noexcept;

    /// The last lines of output (at most 64), oldest first.
    [[nodiscard]] std::string outputTail() const;

    /// File names of the module libraries in the output directory, sorted: of @p targets when given,
    /// otherwise every file with the toolchain's prefix and suffix.
    [[nodiscard]] std::vector<std::filesystem::path> libraries() const;

private:
    ModuleBuild();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion::editor

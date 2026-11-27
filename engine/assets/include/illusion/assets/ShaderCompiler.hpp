#pragma once

/// @file
/// The material shader compiler (ADR 0063): runs the pinned slangc as child processes on a material shader's
/// source placed between the engine's surface API (illusion/Surface.slang) and its passes, reads the
/// parameter layout from the compiler's JSON reflection and returns a ShaderAsset with SPIR-V for every pass
/// and alpha mode, or diagnostics with file, line and column. Used by the cooker and the editor; the player
/// has no compiler and loads cooked shaders.

#include <illusion/assets/ShaderAsset.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// How serious a shader diagnostic is.
enum class ShaderDiagnosticSeverity : u8
{
    Error,   ///< The shader does not compile.
    Warning, ///< The shader compiles.
    Note,    ///< More about the diagnostic before it.
};

/// Stable lower-case name of @p severity: "error", "warning" or "note".
[[nodiscard]] std::string_view shaderDiagnosticSeverityName(ShaderDiagnosticSeverity severity) noexcept;

/// A message of the shader compiler.
struct ShaderDiagnostic
{
    /// The file: the `.shader` file's virtual path, an engine shader's path, or "" when it names none.
    std::string file{};
    u32 line = 0;                                                        ///< 1-based line (0: none).
    u32 column = 0;                                                      ///< 1-based column (0: none).
    ShaderDiagnosticSeverity severity = ShaderDiagnosticSeverity::Error; ///< How serious it is.
    std::string code{};    ///< The compiler's code, e.g. "E30015" ("" for checks of the engine).
    std::string message{}; ///< What is wrong.

    /// Diagnostics compare by every field.
    friend bool operator==(const ShaderDiagnostic&, const ShaderDiagnostic&) = default;
};

/// @p diagnostic as one line in the style of GCC (`file:line:column: error: message [code]`), which editor
/// build output parsers read back.
[[nodiscard]] std::string formatShaderDiagnostic(const ShaderDiagnostic& diagnostic);

/// Reads the machine-readable diagnostics of `slangc` (`-enable-machine-readable-diagnostics`): one per
/// tab-separated line, with the detail of its `span` lines. Other non-empty lines become diagnostics without
/// a location, so a crash of the compiler is never silent. Exposed for tests.
[[nodiscard]] std::vector<ShaderDiagnostic> parseSlangDiagnostics(std::string_view output);

/// Where the compiler and the engine's shader library are.
struct ShaderCompilerDesc
{
    std::filesystem::path slangc{};  ///< The pinned `slangc` executable.
    std::filesystem::path library{}; ///< The engine's shader directory (holds `illusion/Surface.slang`).
    /// Directory for temporary files ("" for the system's temporary directory).
    std::filesystem::path workDirectory{};
    std::chrono::milliseconds timeout{120000}; ///< Longest compile run of `slangc`; longer runs are ended.
    u32 maxProcesses = 0; ///< `slangc` runs at the same time (0: the hardware threads, at most 8).
};

/// What a compile produced: the shader, or why there is none.
struct ShaderCompileResult
{
    std::optional<ShaderAsset> shader{};         ///< The shader when it compiled.
    std::vector<ShaderDiagnostic> diagnostics{}; ///< Errors, warnings and notes, in the compiler's order.

    /// True if the shader compiled.
    [[nodiscard]] bool succeeded() const noexcept { return shader.has_value(); }
};

/// Compiles material shaders with one `slangc` and one engine shader library. compile() blocks until the
/// compiler has finished, so call it off the frame thread; it is thread-safe.
class ShaderCompiler
{
public:
    /// Checks @p desc, runs `slangc -v` for its version and hashes the shader library. The error says what is
    /// missing.
    [[nodiscard]] static Expected<ShaderCompiler, std::string> create(ShaderCompilerDesc desc);

    /// The description this compiler was created with.
    [[nodiscard]] const ShaderCompilerDesc& desc() const noexcept { return m_desc; }

    /// The version `slangc` reports, e.g. "2026.19".
    [[nodiscard]] const std::string& compilerVersion() const noexcept { return m_version; }

    /// Hash of the `.slang` files of the shader library (paths and contents): the engine's shader version.
    [[nodiscard]] u64 libraryHash() const noexcept { return m_libraryHash; }

    /// Compiles @p source, the text of the `.shader` file at @p virtualPath (the file named by diagnostics),
    /// into every variant that shaderVariantExists() for its decal channels: the material passes of
    /// illusion/MaterialPass.slang for surface shaders, those of illusion/DecalPass.slang for decal shaders.
    [[nodiscard]] ShaderCompileResult compile(std::string_view virtualPath, std::string_view source) const;

private:
    ShaderCompiler() = default;

    ShaderCompilerDesc m_desc{};
    std::string m_version{};
    u64 m_libraryHash = 0;
};

} // namespace illusion

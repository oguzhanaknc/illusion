#pragma once

/// @file
/// Builds material shaders (`.shader`) on a background thread (ADR 0063): the editor recompiles a shader
/// that changed without blocking a frame, and renderers take the result when it is done. A loader compiles
/// with a ShaderCompiler when it has one, and otherwise reads the cooked shaders of the import cache (the
/// player). Requests and queries are thread-safe.

#include <illusion/assets/ShaderCompiler.hpp>
#include <illusion/core/Base.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

class VirtualFileSystem;

/// What one build of a shader produced.
struct ShaderBuild
{
    std::string path{}; ///< Virtual path of the `.shader` file.
    u64 sourceHash = 0; ///< xxhash64() of the source that was built (0 if it could not be read).
    std::optional<ShaderAsset> shader{};         ///< The shader when it built.
    std::vector<ShaderDiagnostic> diagnostics{}; ///< What the compiler reported (none for a cached shader).
    std::string error{};                         ///< Why there is no shader ("" when there is one).

    /// True if the shader built.
    [[nodiscard]] bool succeeded() const noexcept { return shader.has_value(); }
};

/// How a ShaderLoader builds.
struct ShaderLoaderDesc
{
    std::string cacheMount = "cache"; ///< Mount of the import cache ("" caches nothing).
    /// The compiler to build with; the loader makes it on its thread at the first build. Without one only
    /// shaders of the cache load.
    std::optional<ShaderCompilerDesc> compiler{};
    /// Use cached shaders only while their sources are unchanged (false for shipped, cooked games).
    bool verifySources = true;
};

/// Builds shaders on one background thread.
class ShaderLoader : NonCopyable
{
public:
    /// Starts the thread; @p files must outlive the loader.
    ShaderLoader(VirtualFileSystem& files, ShaderLoaderDesc desc);

    /// Finishes the build under way, drops the queued ones and ends the thread.
    ~ShaderLoader();

    /// Queues a build of the shader at @p virtualPath and returns at once. A build already queued for the
    /// path (not started yet) is not queued twice. Errors are logged (category "shader") when the build ends.
    void request(std::string_view virtualPath);

    /// The progress of the builds of one path.
    struct State
    {
        bool building = false; ///< A build is queued or running.
        u64 completed = 0;     ///< Builds finished since construction (also failed ones).
        /// The last finished build; null before the first. The pointee never changes.
        std::shared_ptr<const ShaderBuild> build{};
    };

    /// The progress of @p virtualPath.
    [[nodiscard]] State state(std::string_view virtualPath) const;

    /// Waits until no build is queued or running (tests, tools, loading screens).
    void waitIdle() const;

    /// Number of builds queued or running.
    [[nodiscard]] u32 pending() const;

    /// True if the loader can compile: it was given a compiler description.
    [[nodiscard]] bool canCompile() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

#pragma once

/// @file
/// Loads game and plugin modules, checks their versions and dependencies, runs them and hot-reloads shared
/// modules when their files change (see illusion/core/Module.hpp and
/// docs/adr/0027-module-system-and-hot-reload.md).
///
/// @code
/// illusion::ModuleHost modules(world, jobs);
/// modules.provide(physics); // modules reach it with context.service<illusion::PhysicsWorld>()
/// if (const auto error = modules.load("build/game/libtetris.so"))
/// {
///     ILL_LOG_ERROR("Game", "{}", *error);
/// }
/// while (running)
/// {
///     modules.reloadChanged(); // editor and development builds
///     modules.update(frameSeconds);
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/core/Version.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace illusion
{

class JobSystem;
class World;

/// How a module is linked.
enum class ModuleLinkage : u8
{
    Shared, ///< Loaded from a shared library; can be hot-reloaded.
    Static, ///< Linked into the executable (exported games); added with ModuleHost::add().
};

/// A loaded module.
struct ModuleInfo
{
    std::string name;                              ///< ModuleApi::name.
    SemanticVersion version;                       ///< ModuleApi::version.
    ModuleLinkage linkage = ModuleLinkage::Shared; ///< How it is linked.
    std::filesystem::path path;                    ///< The library it was loaded from (Shared).
    u32 reloads = 0;                               ///< Successful hot reloads so far.
};

/// Creation parameters of a ModuleHost.
struct ModuleHostDesc
{
    /// Where loaded libraries are copied to (so that the build can overwrite the originals). Empty uses a
    /// directory in the system's temporary directory; it is removed again by the destructor.
    std::filesystem::path shadowDirectory;
};

/// Loads and runs modules. Not thread-safe: use it from one thread, between frames (no Job may use the
/// World or component types of a module while it loads, reloads or unloads). The World and the Job
/// System must outlive the host.
class ModuleHost : NonCopyable
{
public:
    /// Creates a host whose modules work on @p world and @p jobs. Reflects the core Components so that the
    /// host owns them; reflect the Components of the other engine libraries the game uses
    /// (reflectPhysicsComponents(), reflectAudioComponents(), ...) before loading modules as well.
    ModuleHost(World& world, JobSystem& jobs, ModuleHostDesc desc = {});

    /// Unloads every module, the last loaded first.
    ~ModuleHost();

    /// Makes @p service available to modules as `context.service<T>()`. It must outlive the host.
    template <class T>
    void provide(T& service)
    {
        provide(typeHash<T>(), &service);
    }

    /// Loads the shared module at @p library: copies it to the shadow directory, opens it, checks its
    /// API, engine version, build settings, name and dependencies, installs the host's services and calls
    /// its load(). Returns an error message on failure (and leaves nothing loaded).
    [[nodiscard]] std::optional<std::string> load(const std::filesystem::path& library);

    /// Adds a static module (its ModuleApi from ILL_MODULE() in a static build) with the same checks.
    [[nodiscard]] std::optional<std::string> add(const ModuleApi& api);

    /// Hot-reloads the shared module @p name from its library file. The old version unloads, the new one
    /// loads and takes over the module's component types; Components of types the new version no longer
    /// registers are removed from the World. If the new version cannot load, the old one keeps running and
    /// the error is returned.
    [[nodiscard]] std::optional<std::string> reload(std::string_view name);

    /// Reloads every shared module whose library file changed since it was loaded. Errors are logged; a
    /// library that cannot be loaded yet (still being written) is tried again on the next call. Returns
    /// the number of modules reloaded.
    u32 reloadChanged();

    /// Unloads @p name. Returns false if it is not loaded or another module depends on it.
    bool unload(std::string_view name);

    /// Calls update() of every module, in load order (dependencies first).
    void update(f64 deltaSeconds);

    /// The loaded module @p name, or nullptr.
    [[nodiscard]] const ModuleInfo* find(std::string_view name) const noexcept;

    /// Number of loaded modules.
    [[nodiscard]] u32 count() const noexcept;

    /// The loaded module at @p index in load order (index < count()).
    [[nodiscard]] const ModuleInfo& module(u32 index) const noexcept;

private:
    void provide(u64 type, void* service);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

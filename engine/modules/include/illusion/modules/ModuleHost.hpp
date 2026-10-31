#pragma once

/// @file
/// Loads game and plugin modules, checks their versions and dependencies, runs them and hot-reloads shared
/// modules when their files change, on any number of Worlds (see illusion/core/Module.hpp,
/// docs/adr/0027-module-system-and-hot-reload.md and docs/adr/0041-module-api-v3.md).
///
/// @code
/// illusion::ModuleHost modules(jobs);
/// illusion::ServiceSet services;
/// services.provide(settings); // modules reach it with context.service<GameSettings>()
/// modules.attachWorld(world, illusion::WorldRole::Simulation, std::move(services));
/// if (const auto error = modules.load("build/game/libtetris.so"))
/// {
///     ILL_LOG_ERROR("Game", "{}", *error);
/// }
/// while (running)
/// {
///     modules.reloadChanged(); // editor and development builds
///     modules.update(frameSeconds);
/// }
/// modules.detachWorld(world);
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
#include <vector>

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

/// What a load or hot reload did to one Component type whose layout the module changed
/// (docs/adr/0044-component-layout-migration-on-hot-reload.md). Fields are matched by name or
/// FieldMeta::renamedFrom; converted values follow the scene file rules (ComponentJson.hpp).
struct ComponentMigration
{
    std::string type;                       ///< Stable name of the type (its C++ name if unreflected).
    u32 components = 0;                     ///< Components migrated, in all attached Worlds.
    std::vector<std::string> addedFields;   ///< New fields, which got their default value.
    std::vector<std::string> removedFields; ///< Fields of the old version that were dropped.
    std::vector<std::string> resetFields;   ///< Fields whose value the new type could not hold in at least
                                            ///< one Component; they got their default value there.
};

/// The Component types that the last load or hot reload of a module migrated.
struct MigrationReport
{
    std::vector<ComponentMigration> types; ///< One entry per migrated type, in id order.

    /// True if no type was migrated.
    [[nodiscard]] bool empty() const noexcept { return types.empty(); }
};

/// A loaded module.
struct ModuleInfo
{
    std::string name;                              ///< ModuleApi::name.
    SemanticVersion version;                       ///< ModuleApi::version.
    ModuleLinkage linkage = ModuleLinkage::Shared; ///< How it is linked.
    std::filesystem::path path;                    ///< The library it was loaded from (Shared).
    u32 reloads = 0;                               ///< Successful hot reloads so far.
    MigrationReport migration;                     ///< What the last load or reload migrated.
};

/// Creation parameters of a ModuleHost.
struct ModuleHostDesc
{
    /// Where loaded libraries are copied to (so that the build can overwrite the originals). Empty uses a
    /// directory in the system's temporary directory; it is removed again by the destructor.
    std::filesystem::path shadowDirectory;
};

/// Loads and runs modules on the Worlds attached to it. Not thread-safe: use it from one thread, between
/// frames (no Job may use a World or the component types of a module while it loads, reloads or unloads).
/// The Job System must outlive the host, and every attached World must stay alive until it is detached.
class ModuleHost : NonCopyable
{
public:
    /// Creates a host whose modules use @p jobs, with no World attached. Reflects the core Components so
    /// that the host owns them; reflect the Components of the other engine libraries the game uses
    /// (reflectPhysicsComponents(), reflectAudioComponents(), ...) before loading modules as well.
    explicit ModuleHost(JobSystem& jobs, ModuleHostDesc desc = {});

    /// Unloads every module, the last loaded first: each stops on every Simulation World, then its
    /// Components are removed from every World still attached.
    ~ModuleHost();

    /// Makes @p service available to modules as `context.service<T>()` on every World, after the
    /// services of the World itself. It must outlive the host.
    template <class T>
    void provide(T& service)
    {
        hostServices().provide(service);
    }

    /// Attaches @p world in @p role. Modules find @p services first in `context.service<T>()` for this
    /// World. A Simulation World starts every loaded module, in load order. @p world must not be attached
    /// already.
    void attachWorld(World& world, WorldRole role, ServiceSet services = {});

    /// Detaches @p world: on a Simulation World every module stops first, the last loaded first. Its
    /// Components stay; a World that outlives detaching must not keep Components of a module that unloads
    /// later. Returns false if @p world is not attached.
    bool detachWorld(World& world);

    /// True if @p world is attached.
    [[nodiscard]] bool isAttached(const World& world) const noexcept;

    /// The role @p world is attached in (@p world must be attached).
    [[nodiscard]] WorldRole roleOf(const World& world) const noexcept;

    /// Number of attached Worlds.
    [[nodiscard]] u32 worldCount() const noexcept;

    /// Loads the shared module at @p library: copies it to the shadow directory, opens it, checks its
    /// API, engine version, build settings, name and dependencies, installs the host's services, calls
    /// its load() and then its start() on every Simulation World. Returns an error message on failure (and
    /// leaves nothing loaded).
    [[nodiscard]] std::optional<std::string> load(const std::filesystem::path& library);

    /// Adds a static module (its ModuleApi from ILL_MODULE() in a static build) with the same checks.
    [[nodiscard]] std::optional<std::string> add(const ModuleApi& api);

    /// Hot-reloads the shared module @p name from its library file. The old version unloads, the new one
    /// loads and takes over the module's component types; Components of types whose layout changed are
    /// migrated by field name (ModuleInfo::migration), and Components of types the new version no longer
    /// registers are removed from every attached World. start() and stop() are not called: the running
    /// sessions continue with the new code. If the new version cannot load, or a changed type cannot be
    /// migrated (it is not reflected, or a World that is not attached uses it), the old one keeps running
    /// with its values and the error is returned.
    [[nodiscard]] std::optional<std::string> reload(std::string_view name);

    /// Reloads every shared module whose library file changed since it was loaded. Errors are logged; a
    /// library that cannot be loaded yet (still being written) is tried again on the next call. Returns
    /// the number of modules reloaded.
    u32 reloadChanged();

    /// Unloads @p name: stops it on every Simulation World, calls its unload() and removes the Components
    /// of its types from every attached World. Returns false if it is not loaded or another module depends
    /// on it.
    bool unload(std::string_view name);

    /// Calls update() of every module on every Simulation World: World by World in attach order, modules
    /// in load order (dependencies first).
    void update(f64 deltaSeconds);

    /// Calls update() of every module on @p world only, which must be attached as a Simulation World.
    void update(World& world, f64 deltaSeconds);

    /// The loaded module @p name, or nullptr.
    [[nodiscard]] const ModuleInfo* find(std::string_view name) const noexcept;

    /// Number of loaded modules.
    [[nodiscard]] u32 count() const noexcept;

    /// The loaded module at @p index in load order (index < count()).
    [[nodiscard]] const ModuleInfo& module(u32 index) const noexcept;

private:
    [[nodiscard]] ServiceSet& hostServices() noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

#pragma once

/// @file
/// The interface between the engine and a game or plugin module.
///
/// A module describes itself with a ModuleApi and exports it with ILL_MODULE(). The same module source is
/// built as a shared library that a ModuleHost loads and hot-reloads (editor, development) or linked
/// statically into a shipped game; see illusion_add_module() in cmake/IllusionModules.cmake and
/// docs/adr/0027-module-system-and-hot-reload.md.
///
/// @code
/// bool loadGame(illusion::ModuleContext& context)
/// {
///     illusion::reflect<Piece>("tetris.Piece").field("cell", &Piece::cell);
///     return true;
/// }
///
/// void updateGame(illusion::ModuleContext& context, illusion::f64 deltaSeconds)
/// {
///     context.world().query<Piece>().each([&](Piece& piece) { piece.fall += deltaSeconds; });
/// }
///
/// constexpr illusion::ModuleApi kTetris = illusion::makeModuleApi("tetris", "1.2.0", &loadGame, nullptr,
///                                                                   &updateGame);
/// ILL_MODULE(tetris, kTetris)
/// @endcode
///
/// A shared module contains its own copy of the engine's static libraries. Before any of its code runs,
/// the host installs its process-wide services into that copy (see ModuleServices), so both sides share
/// one logger, one component registry, one asset path table and one assert handler (ADR 0003).

#include <illusion/core/Assert.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Hash.hpp>
#include <illusion/core/TypeName.hpp>

#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace illusion
{

class AssetPathTable;
class ComponentRegistry;
class JobSystem;
class Logger;
class World;

/// Layout version of ModuleApi, ModuleServices and ModuleContext. A host only loads modules built with the
/// same value.
inline constexpr u32 kModuleApiVersion = 2;

/// A module that must be loaded before this one.
struct ModuleDependency
{
    const char* name = nullptr; ///< Name of the module.
    const char* minimumVersion =
        nullptr; ///< Semantic version it must be compatible with (see SemanticVersion).
};

/// The host's process-wide services, installed into a shared module's copy of the engine before the
/// module runs. Static modules share the host's copy, so installing them changes nothing.
struct ModuleServices
{
    Logger* logger = nullptr;                ///< The host's logger.
    ComponentRegistry* registry = nullptr;   ///< The host's component registry.
    AssetPathTable* assetPaths = nullptr;    ///< The host's asset path table (since API version 2).
    AssertHandler assertHandler = nullptr;   ///< Calls the host's current assert handler.
    u32 (*threadIndex)() noexcept = nullptr; ///< The host's currentThreadIndex().
    i32 (*workerIndex)() noexcept = nullptr; ///< The host's JobSystem::currentWorkerIndex().
    u32 moduleId = 0;                        ///< Identifies this load of the module (0 is the host).
};

class ModuleContext;

/// Description of a module, returned by its entry point. All strings must have static storage duration.
struct ModuleApi
{
    u32 apiVersion = kModuleApiVersion; ///< Must equal the host's kModuleApiVersion.
    const char* name = nullptr;         ///< Unique name: letters, digits, '_' and '.'.
    const char* version = nullptr;      ///< Semantic version of the module, e.g. "1.2.0".
    u32 engineMajor = 0;                ///< Engine version the module was built against.
    u32 engineMinor = 0;                ///< See engineMajor.
    u32 enginePatch = 0;                ///< See engineMajor.
    u64 buildKey = 0; ///< Compiler and configuration fingerprint (detail::moduleBuildKey()).
    std::span<const ModuleDependency> dependencies; ///< Modules that must be loaded first.

    /// Installs the host's services into the module's copy of the engine. Set by ILL_MODULE().
    void (*installServices)(const ModuleServices& services) noexcept = nullptr;

    /// Called after loading: register and reflect component types, read the World. Returning false
    /// unloads the module again. Required.
    bool (*load)(ModuleContext& context) = nullptr;

    /// Called before unloading: release everything that points into the module (event handlers, jobs).
    /// Optional.
    void (*unload)(ModuleContext& context) = nullptr;

    /// Called by ModuleHost::update() once per frame. Optional.
    void (*update)(ModuleContext& context, f64 deltaSeconds) = nullptr;
};

/// What a module sees of its host. Owned by the ModuleHost; valid during the call that receives it.
class ModuleContext
{
public:
    /// One registered service.
    struct Service
    {
        u64 type = 0;           ///< typeHash<T>() of the service type.
        void* object = nullptr; ///< The service.
    };

    /// Creates a context (used by ModuleHost).
    ModuleContext(World& world, JobSystem& jobs, const std::vector<Service>& services, std::string_view name,
                  bool reloading) noexcept
        : m_world(&world)
        , m_jobs(&jobs)
        , m_services(&services)
        , m_name(name)
        , m_reloading(reloading)
    {
    }

    /// The World the host simulates. Keep game state in its Components: it survives hot reloads.
    [[nodiscard]] World& world() const noexcept { return *m_world; }

    /// The host's Job System. Jobs a module submits must finish before its update() returns.
    [[nodiscard]] JobSystem& jobs() const noexcept { return *m_jobs; }

    /// The host's service of type @p T (see ModuleHost::provide()), or nullptr.
    template <class T>
    [[nodiscard]] T* service() const noexcept
    {
        constexpr u64 type = typeHash<T>();
        for (const Service& service : *m_services)
        {
            if (service.type == type)
            {
                return static_cast<T*>(service.object);
            }
        }
        return nullptr;
    }

    /// Name of the module.
    [[nodiscard]] std::string_view moduleName() const noexcept { return m_name; }

    /// True while load() or unload() runs as part of a hot reload.
    [[nodiscard]] bool isReloading() const noexcept { return m_reloading; }

private:
    World* m_world;
    JobSystem* m_jobs;
    const std::vector<Service>* m_services;
    std::string_view m_name;
    bool m_reloading;
};

/// Implementation details. Not part of the public API.
namespace detail
{

/// Fingerprint of the compiler and of the settings that change the layout or behaviour of engine types.
/// A shared module must match its host exactly, because both contain a copy of the engine.
[[nodiscard]] constexpr u64 moduleBuildKey() noexcept
{
    constexpr std::string_view fingerprint =
#if defined(ILL_COMPILER_MSVC)
        "msvc-" ILL_STRINGIFY(_MSC_FULL_VER)
#elif defined(ILL_COMPILER_CLANG)
        "clang-" ILL_STRINGIFY(__clang_major__) "." ILL_STRINGIFY(__clang_minor__)
#else
        "gcc-" ILL_STRINGIFY(__GNUC__) "." ILL_STRINGIFY(__GNUC_MINOR__)
#endif
#if defined(NDEBUG)
            "|release"
#else
            "|debug"
#endif
#if defined(ILLUSION_ENABLE_ASSERTS)
            "|asserts"
#endif
#if defined(_ITERATOR_DEBUG_LEVEL)
            "|idl" ILL_STRINGIFY(_ITERATOR_DEBUG_LEVEL)
#elif defined(_GLIBCXX_ASSERTIONS) || defined(_GLIBCXX_DEBUG)
            "|glibcxx-checked"
#endif
        ;
    return fnv1a64(fingerprint);
}

/// Id of the module this copy of the engine belongs to (0 in the host and in static modules).
[[nodiscard]] u32 currentModuleId() noexcept;

/// Installs @p services into this copy of the engine. Used through ModuleApi::installServices.
void installModuleServices(const ModuleServices& services) noexcept;

/// The calling binary's services, for the host to hand to modules.
[[nodiscard]] ModuleServices hostModuleServices() noexcept;

} // namespace detail

/// Builds a ModuleApi for the engine this code is compiled against. @p load is required; @p unload and
/// @p update may be null.
[[nodiscard]] constexpr ModuleApi makeModuleApi(const char* name, const char* version,
                                                bool (*load)(ModuleContext&), void (*unload)(ModuleContext&),
                                                void (*update)(ModuleContext&, f64),
                                                std::span<const ModuleDependency> dependencies = {}) noexcept
{
    ModuleApi api;
    api.name = name;
    api.version = version;
    api.engineMajor = ILLUSION_VERSION_MAJOR;
    api.engineMinor = ILLUSION_VERSION_MINOR;
    api.enginePatch = ILLUSION_VERSION_PATCH;
    api.buildKey = detail::moduleBuildKey();
    api.dependencies = dependencies;
    api.installServices = &detail::installModuleServices;
    api.load = load;
    api.unload = unload;
    api.update = update;
    return api;
}

} // namespace illusion

/// Name of the entry point of a shared module.
#define ILL_MODULE_ENTRY_NAME "illusionModuleEntry"

#if defined(ILL_PLATFORM_WINDOWS)
/// Exports a function from a shared module.
#define ILL_MODULE_EXPORT __declspec(dllexport)
#else
/// Exports a function from a shared module.
#define ILL_MODULE_EXPORT __attribute__((visibility("default")))
#endif

#if defined(ILLUSION_SHARED_MODULE)
/// Exports @p api (a constant ModuleApi) as the module's entry point. In a shared module (built by
/// illusion_add_module() with SHARED) this is the exported function "illusionModuleEntry"; in a static
/// module it is illusionModuleEntry_<id>(), which the game passes to ModuleHost::add().
#define ILL_MODULE(id, api)                                                                                  \
    extern "C" ILL_MODULE_EXPORT const ::illusion::ModuleApi* illusionModuleEntry()                          \
    {                                                                                                        \
        return &(api);                                                                                       \
    }
#else
/// Exports @p api (a constant ModuleApi) as the module's entry point: illusionModuleEntry_<id>() in a
/// static module, which the game passes to ModuleHost::add(), or the exported function
/// "illusionModuleEntry" in a shared module (built by illusion_add_module() with SHARED).
#define ILL_MODULE(id, api)                                                                                  \
    extern "C" const ::illusion::ModuleApi* ILL_CONCAT(illusionModuleEntry_, id)()                           \
    {                                                                                                        \
        return &(api);                                                                                       \
    }
#endif

/// Declares the entry point of the static module @p id, so that the game can call
/// `host.add(*illusionModuleEntry_<id>())`.
#define ILL_DECLARE_STATIC_MODULE(id)                                                                        \
    extern "C" const ::illusion::ModuleApi* ILL_CONCAT(illusionModuleEntry_, id)()

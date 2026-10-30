#include "SharedLibrary.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/modules/ModuleHost.hpp>

#include <algorithm>
#include <format>
#include <random>
#include <system_error>
#include <utility>
#include <vector>

namespace illusion
{
namespace
{

bool isValidModuleName(std::string_view name) noexcept
{
    if (name.empty())
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '.';
    });
}

std::string describeEngine(u32 major, u32 minor, u32 patch)
{
    return std::format("{}.{}.{}", major, minor, patch);
}

} // namespace

struct ModuleHost::Impl
{
    struct Module
    {
        ModuleInfo info;
        const ModuleApi* api = nullptr;
        std::unique_ptr<detail::SharedLibrary> library;    // null for static modules
        std::filesystem::path shadow;                      // the copy that is loaded
        std::filesystem::file_time_type writeTime{};       // of info.path when it was loaded
        std::filesystem::file_time_type failedWriteTime{}; // last version that failed to reload
        u32 id = 0;                                        // 0 for static modules (they are the host)
    };

    // A shared library opened but not yet running.
    struct Opened
    {
        std::unique_ptr<detail::SharedLibrary> library;
        const ModuleApi* api = nullptr;
        std::filesystem::path shadow;
        std::filesystem::file_time_type writeTime{};
    };

    // A World attached to the host.
    struct Attached
    {
        World* world = nullptr;
        WorldRole role = WorldRole::Edit;
        ServiceSet services;
    };

    Impl(JobSystem& jobSystem, ModuleHostDesc settings)
        : jobs(jobSystem)
    {
        if (settings.shadowDirectory.empty())
        {
            std::error_code ignored;
            const u64 token = (static_cast<u64>(std::random_device{}()) << 32) ^ std::random_device{}();
            shadowDirectory = std::filesystem::temp_directory_path(ignored) /
                              std::format("illusion-modules-{:016x}", token);
            ownsShadowDirectory = true;
        }
        else
        {
            shadowDirectory = std::move(settings.shadowDirectory);
        }
    }

    ~Impl()
    {
        while (!modules.empty()) // each stops on every Simulation World first
        {
            unloadAt(modules.size() - 1);
        }
        if (ownsShadowDirectory)
        {
            std::error_code ignored;
            std::filesystem::remove_all(shadowDirectory, ignored);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // The context of load() and unload(), which see no World.
    [[nodiscard]] ModuleContext contextFor(const Module& module, bool reloading) const noexcept
    {
        return ModuleContext(nullptr, WorldRole::Edit, jobs, nullptr, services, module.info.name, reloading);
    }

    // The context of start(), stop() and update() on @p attached.
    [[nodiscard]] ModuleContext contextFor(const Module& module, Attached& attached) const noexcept
    {
        return ModuleContext(attached.world, attached.role, jobs, &attached.services, services,
                             module.info.name, false);
    }

    [[nodiscard]] Attached* findWorld(const World& world) noexcept
    {
        const auto it = std::find_if(worlds.begin(), worlds.end(),
                                     [&world](const Attached& attached) { return attached.world == &world; });
        return it != worlds.end() ? &*it : nullptr;
    }

    // --- Sessions ---------------------------------------------------------------------------------------

    // Runs start() of @p module on every Simulation World, in attach order.
    void startEverywhere(const Module& module)
    {
        if (module.api->start == nullptr)
        {
            return;
        }
        for (Attached& attached : worlds)
        {
            if (attached.role == WorldRole::Simulation)
            {
                ModuleContext context = contextFor(module, attached);
                module.api->start(context);
            }
        }
    }

    // Runs stop() of @p module on every Simulation World, the last attached first.
    void stopEverywhere(const Module& module)
    {
        if (module.api->stop == nullptr)
        {
            return;
        }
        for (auto it = worlds.rbegin(); it != worlds.rend(); ++it)
        {
            if (it->role == WorldRole::Simulation)
            {
                ModuleContext context = contextFor(module, *it);
                module.api->stop(context);
            }
        }
    }

    // Runs start() of every module on @p attached if it is a Simulation World, in load order.
    void startAll(Attached& attached)
    {
        if (attached.role != WorldRole::Simulation)
        {
            return;
        }
        for (const std::unique_ptr<Module>& module : modules)
        {
            if (module->api->start != nullptr)
            {
                ModuleContext context = contextFor(*module, attached);
                module->api->start(context);
            }
        }
    }

    // Runs stop() of every module on @p attached if it is a Simulation World, the last loaded first.
    void stopAll(Attached& attached)
    {
        if (attached.role != WorldRole::Simulation)
        {
            return;
        }
        for (auto it = modules.rbegin(); it != modules.rend(); ++it)
        {
            if ((*it)->api->stop != nullptr)
            {
                ModuleContext context = contextFor(**it, attached);
                (*it)->api->stop(context);
            }
        }
    }

    // Runs update() of every module on @p attached, in load order.
    void updateAll(Attached& attached, f64 deltaSeconds)
    {
        for (const std::unique_ptr<Module>& module : modules)
        {
            if (module->api->update != nullptr)
            {
                ModuleContext context = contextFor(*module, attached);
                module->api->update(context, deltaSeconds);
            }
        }
    }

    [[nodiscard]] Module* find(std::string_view name) noexcept
    {
        const auto it =
            std::find_if(modules.begin(), modules.end(),
                         [name](const std::unique_ptr<Module>& module) { return module->info.name == name; });
        return it != modules.end() ? it->get() : nullptr;
    }

    // --- Checks ---------------------------------------------------------------------------------------

    // Checks @p api against the host. @p reloading is the name of the module it replaces (empty when it
    // is new).
    [[nodiscard]] std::optional<std::string> validate(const ModuleApi& api, ModuleLinkage linkage,
                                                      std::string_view reloading, SemanticVersion& version)
    {
        if (api.apiVersion != kModuleApiVersion)
        {
            return std::format("the module uses module API version {}, the engine version {}", api.apiVersion,
                               kModuleApiVersion);
        }
        if (api.name == nullptr || !isValidModuleName(api.name))
        {
            return std::string("the module has no valid name (letters, digits, '_' and '.')");
        }
        const std::string_view name = api.name;
        const std::optional<SemanticVersion> parsed =
            api.version != nullptr ? SemanticVersion::parse(api.version) : std::nullopt;
        if (!parsed)
        {
            return std::format("module '{}' has no valid semantic version", name);
        }
        version = *parsed;
        const SemanticVersion& engine = engineVersion();
        if (api.engineMajor != engine.major || api.engineMinor != engine.minor ||
            api.enginePatch != engine.patch)
        {
            return std::format("module '{}' was built against engine {}, this is {}", name,
                               describeEngine(api.engineMajor, api.engineMinor, api.enginePatch),
                               engine.toString());
        }
        if (linkage == ModuleLinkage::Shared && api.buildKey != detail::moduleBuildKey())
        {
            return std::format("module '{}' was built with another compiler or configuration; rebuild it",
                               name);
        }
        if (api.load == nullptr || (linkage == ModuleLinkage::Shared && api.installServices == nullptr))
        {
            return std::format("module '{}' has no load function", name);
        }
        if (!reloading.empty() ? name != reloading : find(name) != nullptr)
        {
            return !reloading.empty()
                       ? std::format("the library of module '{}' now contains module '{}'", reloading, name)
                       : std::format("module '{}' is already loaded", name);
        }
        for (const ModuleDependency& dependency : api.dependencies)
        {
            const std::string_view needed = dependency.name != nullptr ? dependency.name : "";
            const std::optional<SemanticVersion> minimum =
                dependency.minimumVersion != nullptr ? SemanticVersion::parse(dependency.minimumVersion)
                                                     : std::nullopt;
            if (needed.empty() || !minimum)
            {
                return std::format("module '{}' has an invalid dependency", name);
            }
            const Module* loaded = find(needed);
            if (loaded == nullptr)
            {
                return std::format("module '{}' needs module '{}', which is not loaded", name, needed);
            }
            if (!loaded->info.version.isCompatibleWith(*minimum))
            {
                return std::format("module '{}' needs '{}' {} (compatible), but {} is loaded", name, needed,
                                   minimum->toString(), loaded->info.version.toString());
            }
        }
        return std::nullopt;
    }

    // --- Shared libraries -----------------------------------------------------------------------------

    // Copies @p path to the shadow directory and opens the copy, so that the build can replace the
    // original while it is loaded (Windows locks loaded files) and so that a reload never gets the
    // already loaded image back.
    [[nodiscard]] std::optional<std::string> open(const std::filesystem::path& path, Opened& out)
    {
        std::error_code error;
        out.writeTime = std::filesystem::last_write_time(path, error);
        if (error)
        {
            return std::format("cannot read module library {}: {}", path.string(), error.message());
        }
        std::filesystem::create_directories(shadowDirectory, error);
        out.shadow = shadowDirectory /
                     std::format("{}-{}{}", path.stem().string(), ++copies, path.extension().string());
        if (!std::filesystem::copy_file(path, out.shadow, std::filesystem::copy_options::overwrite_existing,
                                        error))
        {
            return std::format("cannot copy module library {}: {}", path.string(), error.message());
        }
        std::string message;
        out.library = detail::SharedLibrary::open(out.shadow, message);
        if (!out.library)
        {
            removeShadow(out.shadow);
            return std::format("cannot open module library {}: {}", path.string(), message);
        }
        using Entry = const ModuleApi* (*)();
        const auto entry = out.library->function<Entry>(ILL_MODULE_ENTRY_NAME);
        out.api = entry != nullptr ? entry() : nullptr;
        if (out.api == nullptr)
        {
            close(out);
            return std::format("{} is not an illusion module (no {} function)", path.string(),
                               ILL_MODULE_ENTRY_NAME);
        }
        return std::nullopt;
    }

    void close(Opened& opened)
    {
        opened.library.reset();
        removeShadow(opened.shadow);
    }

    static void removeShadow(const std::filesystem::path& shadow)
    {
        std::error_code ignored;
        std::filesystem::remove(shadow, ignored);
    }

    // Installs the host's services into a shared module's copy of the engine.
    void install(const ModuleApi& api, u32 id) const
    {
        ModuleServices installed = detail::hostModuleServices();
        installed.moduleId = id;
        api.installServices(installed);
    }

    // --- Component types --------------------------------------------------------------------------------

    // Removes every Component whose type belongs to @p module from every attached World and detaches the
    // types from the module's code. The module's library must still be open.
    void releaseTypes(u32 module)
    {
        ComponentRegistry& registry = ComponentRegistry::instance();
        std::vector<ComponentId> types;
        registry.typesOfModule(module, types);
        if (!types.empty())
        {
            std::vector<Entity> entities;
            for (const Attached& attached : worlds)
            {
                attached.world->entities(entities);
                for (const Entity entity : entities)
                {
                    for (const ComponentId type : types)
                    {
                        attached.world->remove(entity, type);
                    }
                }
            }
            ILL_LOG_INFO("Modules", "removed the Components of {} type(s) whose code is unloaded",
                         types.size());
        }
        registry.orphanModule(module);
    }

    // A message naming the types whose layout a registration during the last load() changed, if any.
    [[nodiscard]] static std::optional<std::string> layoutConflicts(std::string_view name)
    {
        std::vector<std::string> types;
        if (!ComponentRegistry::instance().takeLayoutConflicts(types))
        {
            return std::nullopt;
        }
        std::string list;
        for (const std::string& type : types)
        {
            list += list.empty() ? type : ", " + type;
        }
        return std::format("module '{}' changed the layout of {}; restart to apply that change", name, list);
    }

    // --- Lifetime -------------------------------------------------------------------------------------

    [[nodiscard]] std::optional<std::string> run(std::unique_ptr<Module> module)
    {
        ModuleContext context = contextFor(*module, false);
        const bool loaded = module->api->load(context);
        std::optional<std::string> error = layoutConflicts(module->info.name);
        if (!loaded || error)
        {
            if (loaded && module->api->unload != nullptr)
            {
                module->api->unload(context);
            }
            if (module->id != 0)
            {
                releaseTypes(module->id);
                module->library.reset();
                removeShadow(module->shadow);
            }
            return error ? error : std::format("module '{}' failed to load", module->info.name);
        }
        ILL_LOG_INFO("Modules", "loaded module '{}' {} ({})", module->info.name,
                     module->info.version.toString(),
                     module->info.linkage == ModuleLinkage::Shared ? "shared" : "static");
        modules.push_back(std::move(module));
        startEverywhere(*modules.back());
        return std::nullopt;
    }

    void unloadAt(usize index)
    {
        Module& module = *modules[index];
        stopEverywhere(module);
        if (module.api->unload != nullptr)
        {
            ModuleContext context = contextFor(module, false);
            module.api->unload(context);
        }
        if (module.id != 0)
        {
            releaseTypes(module.id);
            module.library.reset();
            removeShadow(module.shadow);
        }
        ILL_LOG_INFO("Modules", "unloaded module '{}'", module.info.name);
        modules.erase(modules.begin() + static_cast<std::ptrdiff_t>(index));
    }

    [[nodiscard]] std::optional<std::string> reload(Module& module)
    {
        if (module.info.linkage != ModuleLinkage::Shared)
        {
            return std::format("module '{}' is static and cannot be reloaded", module.info.name);
        }
        Opened opened;
        if (std::optional<std::string> error = open(module.info.path, opened))
        {
            return error;
        }
        SemanticVersion version;
        if (std::optional<std::string> error =
                validate(*opened.api, ModuleLinkage::Shared, module.info.name, version))
        {
            close(opened);
            return error;
        }

        // The old version lets go, then the new one takes over its component types. The old library
        // stays open until then: Components of types that the new version drops still need its code.
        ComponentRegistry& registry = ComponentRegistry::instance();
        std::vector<ComponentId> ownedTypes;
        registry.typesOfModule(module.id, ownedTypes);
        std::vector<ComponentInfo> ownedInfos;
        ownedInfos.reserve(ownedTypes.size());
        for (const ComponentId type : ownedTypes)
        {
            ownedInfos.push_back(registry.info(type));
        }
        if (module.api->unload != nullptr)
        {
            ModuleContext context = contextFor(module, true);
            module.api->unload(context);
        }
        registry.retireModule(module.id);
        const u32 id = nextId++;
        install(*opened.api, id);
        ModuleContext context = contextFor(module, true);
        const bool loaded = opened.api->load(context);
        std::optional<std::string> error = layoutConflicts(module.info.name);
        if (!loaded || error)
        {
            // Roll back: the new version lets go, the old one gets its types back and loads again.
            if (loaded && opened.api->unload != nullptr)
            {
                opened.api->unload(context);
            }
            for (usize i = 0; i < ownedTypes.size(); ++i)
            {
                registry.restoreType(ownedTypes[i], ownedInfos[i]);
            }
            registry.restoreModule(module.id);
            releaseTypes(id); // types only the new version registered
            close(opened);
            const bool restored = module.api->load(context);
            ILL_CHECK(restored, "module '{}' could not be restored after a failed reload", module.info.name);
            return error ? error
                         : std::format("the new version of module '{}' failed to load", module.info.name);
        }
        releaseTypes(module.id); // the types the new version did not take over
        module.library.reset();
        removeShadow(module.shadow);

        module.library = std::move(opened.library);
        module.api = opened.api;
        module.shadow = std::move(opened.shadow);
        module.writeTime = opened.writeTime;
        module.id = id;
        module.info.version = version;
        ++module.info.reloads;
        ILL_LOG_INFO("Modules", "reloaded module '{}' {}", module.info.name, version.toString());
        return std::nullopt;
    }

    JobSystem& jobs;
    std::filesystem::path shadowDirectory;
    bool ownsShadowDirectory = false;
    ServiceSet services;                          // the host's, searched after each World's own
    std::vector<Attached> worlds;                 // attach order
    std::vector<std::unique_ptr<Module>> modules; // load order: dependencies first
    u32 nextId = 1;
    u64 copies = 0;
};

ModuleHost::ModuleHost(JobSystem& jobs, ModuleHostDesc desc)
    : m_impl(std::make_unique<Impl>(jobs, std::move(desc)))
{
    // The host's own code must own the engine's types, or they would go away with the first module that
    // happened to use them first.
    reflectCoreComponents();
}

ModuleHost::~ModuleHost() = default;

ServiceSet& ModuleHost::hostServices() noexcept
{
    return m_impl->services;
}

void ModuleHost::attachWorld(World& world, WorldRole role, ServiceSet services)
{
    ILL_ASSERT(m_impl->findWorld(world) == nullptr, "the World is attached to this ModuleHost already");
    m_impl->worlds.push_back(Impl::Attached{&world, role, std::move(services)});
    m_impl->startAll(m_impl->worlds.back());
}

bool ModuleHost::detachWorld(World& world)
{
    Impl::Attached* attached = m_impl->findWorld(world);
    if (attached == nullptr)
    {
        return false;
    }
    m_impl->stopAll(*attached);
    m_impl->worlds.erase(m_impl->worlds.begin() + (attached - m_impl->worlds.data()));
    return true;
}

bool ModuleHost::isAttached(const World& world) const noexcept
{
    return m_impl->findWorld(world) != nullptr;
}

WorldRole ModuleHost::roleOf(const World& world) const noexcept
{
    const Impl::Attached* attached = m_impl->findWorld(world);
    ILL_ASSERT(attached != nullptr, "the World is not attached to this ModuleHost");
    return attached->role;
}

u32 ModuleHost::worldCount() const noexcept
{
    return static_cast<u32>(m_impl->worlds.size());
}

std::optional<std::string> ModuleHost::load(const std::filesystem::path& library)
{
    ILL_PROFILE_SCOPE();
    Impl& impl = *m_impl;
    Impl::Opened opened;
    if (std::optional<std::string> error = impl.open(library, opened))
    {
        return error;
    }
    SemanticVersion version;
    if (std::optional<std::string> error = impl.validate(*opened.api, ModuleLinkage::Shared, {}, version))
    {
        impl.close(opened);
        return error;
    }
    auto module = std::make_unique<Impl::Module>();
    module->info.name = opened.api->name;
    module->info.version = version;
    module->info.linkage = ModuleLinkage::Shared;
    module->info.path = std::filesystem::absolute(library);
    module->api = opened.api;
    module->library = std::move(opened.library);
    module->shadow = std::move(opened.shadow);
    module->writeTime = opened.writeTime;
    module->id = impl.nextId++;
    impl.install(*module->api, module->id);
    return impl.run(std::move(module));
}

std::optional<std::string> ModuleHost::add(const ModuleApi& api)
{
    Impl& impl = *m_impl;
    SemanticVersion version;
    if (std::optional<std::string> error = impl.validate(api, ModuleLinkage::Static, {}, version))
    {
        return error;
    }
    auto module = std::make_unique<Impl::Module>();
    module->info.name = api.name;
    module->info.version = version;
    module->info.linkage = ModuleLinkage::Static;
    module->api = &api;
    return impl.run(std::move(module));
}

std::optional<std::string> ModuleHost::reload(std::string_view name)
{
    ILL_PROFILE_SCOPE();
    Impl::Module* module = m_impl->find(name);
    if (module == nullptr)
    {
        return std::format("module '{}' is not loaded", name);
    }
    return m_impl->reload(*module);
}

u32 ModuleHost::reloadChanged()
{
    ILL_PROFILE_SCOPE();
    u32 reloaded = 0;
    for (const std::unique_ptr<Impl::Module>& module : m_impl->modules)
    {
        if (module->info.linkage != ModuleLinkage::Shared)
        {
            continue;
        }
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(module->info.path, error);
        if (error || writeTime == module->writeTime || writeTime == module->failedWriteTime)
        {
            continue;
        }
        if (const std::optional<std::string> failure = m_impl->reload(*module))
        {
            module->failedWriteTime = writeTime; // retried when the file changes again
            ILL_LOG_ERROR("Modules", "{}", *failure);
            continue;
        }
        ++reloaded;
    }
    return reloaded;
}

bool ModuleHost::unload(std::string_view name)
{
    Impl& impl = *m_impl;
    const auto it = std::find_if(
        impl.modules.begin(), impl.modules.end(),
        [name](const std::unique_ptr<Impl::Module>& module) { return module->info.name == name; });
    if (it == impl.modules.end())
    {
        return false;
    }
    for (const std::unique_ptr<Impl::Module>& other : impl.modules)
    {
        for (const ModuleDependency& dependency : other->api->dependencies)
        {
            if (other.get() != it->get() && dependency.name != nullptr && dependency.name == name)
            {
                ILL_LOG_WARN("Modules", "cannot unload module '{}': module '{}' depends on it", name,
                             other->info.name);
                return false;
            }
        }
    }
    impl.unloadAt(static_cast<usize>(it - impl.modules.begin()));
    return true;
}

void ModuleHost::update(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    for (Impl::Attached& attached : m_impl->worlds)
    {
        if (attached.role == WorldRole::Simulation)
        {
            m_impl->updateAll(attached, deltaSeconds);
        }
    }
}

void ModuleHost::update(World& world, f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    Impl::Attached* attached = m_impl->findWorld(world);
    ILL_ASSERT(attached != nullptr && attached->role == WorldRole::Simulation,
               "ModuleHost::update(World&) needs an attached Simulation World");
    m_impl->updateAll(*attached, deltaSeconds);
}

const ModuleInfo* ModuleHost::find(std::string_view name) const noexcept
{
    const Impl::Module* module = m_impl->find(name);
    return module != nullptr ? &module->info : nullptr;
}

u32 ModuleHost::count() const noexcept
{
    return static_cast<u32>(m_impl->modules.size());
}

const ModuleInfo& ModuleHost::module(u32 index) const noexcept
{
    ILL_ASSERT(index < count(), "module index {} out of range", index);
    return m_impl->modules[index]->info;
}

} // namespace illusion

#include "TestSupport.hpp"
#include "modules/TestModuleShared.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/modules/ModuleHost.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

// Copies a test library to @p target and gives it a new modification time, as a build would.
void install(const std::filesystem::path& library, const std::filesystem::path& target)
{
    const bool existed = std::filesystem::exists(target);
    const auto previous =
        existed ? std::filesystem::last_write_time(target) : std::filesystem::file_time_type{};
    std::filesystem::copy_file(library, target, std::filesystem::copy_options::overwrite_existing);
    // File systems with coarse timestamps would otherwise report the same time for a quick rebuild.
    std::filesystem::last_write_time(target, existed ? previous + std::chrono::seconds(2)
                                                     : std::filesystem::last_write_time(target));
}

// The id of a module's Component type. The tests reach module types the way an editor does, by stable
// name: host code that used them as C++ types would make the host own them.
ComponentId moduleType(std::string_view stableName)
{
    return ComponentRegistry::instance().findByStableName(stableName);
}

// Sum of the "value" fields (an i32 at offset 0) of every test.ModuleCounter.
i32 counterTotal(World& world)
{
    const ComponentId counter = moduleType("test.ModuleCounter");
    std::vector<Entity> entities;
    world.entities(entities);
    i32 total = 0;
    for (const Entity entity : entities)
    {
        if (const void* data = world.componentData(entity, counter))
        {
            total += *static_cast<const i32*>(data);
        }
    }
    return total;
}

u32 countWith(World& world, ComponentId type)
{
    std::vector<Entity> entities;
    world.entities(entities);
    u32 count = 0;
    for (const Entity entity : entities)
    {
        const std::span<const ComponentId> types = world.componentsOf(entity);
        if (std::find(types.begin(), types.end(), type) != types.end())
        {
            ++count;
        }
    }
    return count;
}

// Messages logged by the modules (through the host's logger), down to Info.
struct LogCapture
{
    std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>(256);
    LogLevel previous = Logger::instance().level();

    LogCapture()
    {
        Logger::instance().addSink(sink);
        Logger::instance().setLevel(LogLevel::Info);
    }
    ~LogCapture()
    {
        Logger::instance().setLevel(previous);
        Logger::instance().removeSink(sink.get());
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] bool contains(std::string_view text) const
    {
        for (const MemorySink::Entry& entry : sink->entries())
        {
            if (entry.text.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }
};

struct Fixture
{
    test::TempDirectory directory;
    std::filesystem::path library = directory.path / std::filesystem::path(TEST_MODULE_V1).filename();
    JobSystem jobs{JobSystemDesc{2, 256}};
    World world;
    test::ModuleControl control;
    std::unique_ptr<ModuleHost> host;

    Fixture()
    {
        ModuleHostDesc desc;
        desc.shadowDirectory = directory.path / "shadow";
        host = std::make_unique<ModuleHost>(world, jobs, desc);
        host->provide(control);
    }
};

bool loadStatic(ModuleContext&)
{
    return true;
}

i32 g_staticUpdates = 0;

void updateStatic(ModuleContext&, f64)
{
    ++g_staticUpdates;
}

constexpr ModuleApi kStaticModule =
    makeModuleApi("static_test", "2.3.0", &loadStatic, nullptr, &updateStatic);

} // namespace

ILL_MODULE(static_test, kStaticModule)
ILL_DECLARE_STATIC_MODULE(static_test);

TEST_CASE("a shared module loads with the host's services and runs every update")
{
    Fixture f;
    LogCapture log;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    const ModuleInfo* info = f.host->find("test_module");
    REQUIRE(info != nullptr);
    CHECK(info->version == SemanticVersion{1, 0, 0, {}});
    CHECK(info->linkage == ModuleLinkage::Shared);
    CHECK(f.host->count() == 1);
    CHECK(f.control.loads == 1);
    CHECK(log.contains("variant 1 loaded"));                     // logged through the host's logger
    CHECK(std::filesystem::exists(f.directory.path / "shadow")); // the loaded copy

    f.host->update(1.0 / 60.0);
    CHECK(f.control.updates == 1);
    CHECK(f.control.logger == &Logger::instance());
    CHECK(f.control.registry == &ComponentRegistry::instance());
    // An AssetRef made in the module is recorded in the host's table.
    CHECK(f.control.assetPaths == &AssetPathTable::instance());
    CHECK(AssetPathTable::instance().path(AssetId{f.control.asset}) == "game:/module.glb#mesh/1");
    CHECK(f.control.counterId == moduleType("test.ModuleCounter"));
    CHECK(f.control.transformId == componentId<Transform>());
    CHECK(f.control.threadIndex == currentThreadIndex());
    CHECK(f.control.workerIndex.load() >= 0);
    CHECK(counterTotal(f.world) == 2); // two counters, +1 each

#if ILL_ASSERTS_ENABLED
    {
        // Assertions in the module reach the handler the host installs, even after the module loaded.
        test::ScopedAssertCatcher catcher;
        f.control.assertInUpdate = true;
        CHECK_THROWS_AS(f.host->update(0.0), test::AssertionFailure);
        f.control.assertInUpdate = false;
    }
#endif

    CHECK(f.host->unload("test_module"));
    CHECK(f.control.unloads == 1);
    CHECK(f.host->count() == 0);
    CHECK(countWith(f.world, moduleType("test.ModuleCounter")) == 0); // its Components went with its code
    CHECK_FALSE(f.host->unload("test_module"));

    // The reflection metadata the module registered outlives its code.
    const TypeReflection* reflection =
        ComponentRegistry::instance().reflection(moduleType("test.ModuleCounter"));
    REQUIRE(reflection != nullptr);
    CHECK(reflection->meta.displayName == "Module counter");
    CHECK(reflection->meta.icon == "counter_1");
    CHECK(reflection->meta.description == "Counts module updates.");
    REQUIRE(reflection->fields.size() == 1);
    CHECK(reflection->fields[0].name == "value");
    CHECK(reflection->fields[0].meta.displayName == "Counter value");
    CHECK(reflection->fields[0].meta.category == "State");
}

TEST_CASE("types the host uses belong to the host and their Components outlive the module")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    const ComponentId shared = componentId<test::SharedComponent>(); // the host takes the type over
    CHECK(ComponentRegistry::instance().info(shared).module == 0);
    CHECK(ComponentRegistry::instance().info(componentId<Transform>()).module == 0);
    CHECK(f.host->unload("test_module"));
    REQUIRE(countWith(f.world, shared) == 1);
    std::vector<Entity> entities;
    f.world.entities(entities);
    for (const Entity entity : entities)
    {
        if (const test::SharedComponent* component = f.world.tryGet<test::SharedComponent>(entity))
        {
            CHECK(component->value == 11);
        }
        f.world.destroy(entity); // destroys it with the host's code
    }
}

TEST_CASE("hot reload swaps the code, keeps World data and removes types the new version dropped")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    f.host->update(0.0);
    CHECK(counterTotal(f.world) == 2);
    const ComponentId legacy = moduleType("test.LegacyComponent");
    CHECK(countWith(f.world, legacy) == 1);
    CHECK(f.host->reloadChanged() == 0); // nothing changed yet

    install(TEST_MODULE_V2, f.library);
    CHECK(f.host->reloadChanged() == 1);
    const ModuleInfo* info = f.host->find("test_module");
    REQUIRE(info != nullptr);
    CHECK(info->version == SemanticVersion{1, 1, 0, {}});
    CHECK(info->reloads == 1);
    CHECK(f.control.unloads == 1);
    CHECK(f.control.loads == 2);
    CHECK(f.control.reloadingSeen);
    CHECK(countWith(f.world, legacy) == 0);                           // dropped by version 2
    CHECK(countWith(f.world, moduleType("test.ModuleCounter")) == 2); // kept

    f.host->update(0.0);
    CHECK(f.control.variant == 2);
    CHECK(counterTotal(f.world) == 2 + 4); // the new code adds 2 to each counter

    // Structural changes move the module's Components with the new version's code.
    std::vector<Entity> entities;
    f.world.entities(entities);
    for (const Entity entity : entities)
    {
        f.world.emplace<Transform>(entity);
    }
    for (const Entity entity : entities)
    {
        f.world.destroy(entity);
    }
    CHECK(f.world.aliveCount() == 0);
}

TEST_CASE("a reload that changes a component's layout is rolled back")
{
    Fixture f;
    LogCapture log;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    f.host->update(0.0);

    install(TEST_MODULE_V3, f.library);
    CHECK(f.host->reloadChanged() == 0);
    CHECK(log.contains("changed the layout of"));
    CHECK(f.host->find("test_module")->version == SemanticVersion{1, 0, 0, {}});
    CHECK(f.host->reloadChanged() == 0); // the same file is not tried again
    f.host->update(0.0);
    CHECK(f.control.variant == 1);
    CHECK(counterTotal(f.world) == 4);
    CHECK(countWith(f.world, moduleType("test.LegacyComponent")) == 1);

    install(TEST_MODULE_V2, f.library); // a fixed build reloads
    CHECK(f.host->reloadChanged() == 1);
    f.host->update(0.0);
    CHECK(f.control.variant == 2);
}

TEST_CASE("a library that cannot be loaded keeps the old version running")
{
    Fixture f;
    LogCapture log;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));

    install(TEST_NOT_A_MODULE, f.library);
    CHECK(f.host->reloadChanged() == 0);
    CHECK(log.contains("is not an illusion module"));
    f.host->update(0.0);
    CHECK(f.control.variant == 1);

    const std::optional<std::string> error = f.host->reload("test_module");
    REQUIRE(error);
    CHECK(error->find("is not an illusion module") != std::string::npos);
    CHECK(f.host->reload("missing"));
}

TEST_CASE("incompatible modules are rejected")
{
    Fixture f;
    CHECK(f.host->load(f.directory.path / "missing.so")->find("cannot read") != std::string::npos);
    install(TEST_NOT_A_MODULE, f.library);
    CHECK(f.host->load(f.library)->find("is not an illusion module") != std::string::npos);
    install(TEST_MODULE_V4, f.library);
    CHECK(f.host->load(f.library)->find("another compiler or configuration") != std::string::npos);
    CHECK(f.host->count() == 0);

    ModuleApi api = makeModuleApi("broken", "1.0.0", &loadStatic, nullptr, nullptr);
    api.apiVersion = kModuleApiVersion + 1;
    CHECK(f.host->add(api)->find("module API version") != std::string::npos);
    api = makeModuleApi("broken", "1.0.0", &loadStatic, nullptr, nullptr);
    api.engineMajor += 1;
    CHECK(f.host->add(api)->find("built against engine") != std::string::npos);
    CHECK(f.host->add(makeModuleApi("bad name!", "1.0.0", &loadStatic, nullptr, nullptr)));
    CHECK(f.host->add(makeModuleApi("broken", "1.0", &loadStatic, nullptr, nullptr))
              ->find("semantic version") != std::string::npos);
    CHECK(
        f.host->add(makeModuleApi("broken", "1.0.0", nullptr, nullptr, nullptr))->find("no load function") !=
        std::string::npos);
    REQUIRE_FALSE(f.host->add(kStaticModule));
    CHECK(f.host->add(kStaticModule)->find("already loaded") != std::string::npos);
    CHECK(f.host->count() == 1);
}

TEST_CASE("a module whose load fails is unloaded again and leaves no Components")
{
    Fixture f;
    f.control.failLoad = true;
    install(TEST_MODULE_V1, f.library);
    CHECK(f.host->load(f.library)->find("failed to load") != std::string::npos);
    CHECK(f.host->count() == 0);
    CHECK(f.world.aliveCount() == 0);
    f.control.failLoad = false;
    CHECK_FALSE(f.host->load(f.library));
}

TEST_CASE("dependencies are checked and keep modules loaded")
{
    Fixture f;
    static constexpr std::array kNeedsTest{ModuleDependency{"test_module", "1.0.0"}};
    static constexpr std::array kNeedsNewer{ModuleDependency{"test_module", "2.0.0"}};
    static constexpr ModuleApi kDependent =
        makeModuleApi("dependent", "1.0.0", &loadStatic, nullptr, nullptr, kNeedsTest);
    static constexpr ModuleApi kTooNew =
        makeModuleApi("too_new", "1.0.0", &loadStatic, nullptr, nullptr, kNeedsNewer);
    CHECK(f.host->add(kDependent)->find("which is not loaded") != std::string::npos);

    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    CHECK_FALSE(f.host->add(kDependent));
    CHECK(f.host->add(kTooNew)->find("needs 'test_module' 2.0.0") != std::string::npos);
    CHECK(f.host->module(0).name == "test_module");
    CHECK(f.host->module(1).name == "dependent");

    CHECK_FALSE(f.host->unload("test_module")); // "dependent" needs it
    CHECK(f.host->unload("dependent"));
    CHECK(f.host->unload("test_module"));
}

TEST_CASE("static modules run inside the host and cannot be reloaded")
{
    Fixture f;
    g_staticUpdates = 0;
    REQUIRE_FALSE(f.host->add(*illusionModuleEntry_static_test()));
    const ModuleInfo* info = f.host->find("static_test");
    REQUIRE(info != nullptr);
    CHECK(info->linkage == ModuleLinkage::Static);
    CHECK(info->version == SemanticVersion{2, 3, 0, {}});
    f.host->update(0.0);
    f.host->update(0.0);
    CHECK(g_staticUpdates == 2);
    CHECK(f.host->reload("static_test")->find("static") != std::string::npos);
    CHECK(f.host->reloadChanged() == 0);
}

TEST_CASE("destroying the host unloads every module")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    REQUIRE_FALSE(f.host->add(kStaticModule));
    f.host.reset();
    CHECK(f.control.unloads == 1);
    CHECK(countWith(f.world, moduleType("test.ModuleCounter")) == 0); // the entities stay, empty
    CHECK_FALSE(std::filesystem::exists(f.directory.path / "shadow" / f.library.filename()));
}

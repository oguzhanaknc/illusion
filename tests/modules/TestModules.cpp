#include "TestSupport.hpp"
#include "modules/CountingServices.hpp"
#include "modules/TestModuleShared.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/core/Thread.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/modules/ModuleHost.hpp>
#include <illusion/physics/PhysicsWorldService.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <limits>
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

// The entities of @p world that have a Component of @p type.
std::vector<Entity> entitiesWith(World& world, ComponentId type)
{
    std::vector<Entity> entities;
    world.entities(entities);
    std::erase_if(entities,
                  [&world, type](Entity entity) { return world.componentData(entity, type) == nullptr; });
    return entities;
}

// The JSON of @p field of @p entity's Component of @p type, read through the type's current reflection:
// "<none>" without the Component, "<error>" for a field the type does not have.
std::string fieldJson(World& world, Entity entity, ComponentId type, std::string_view field)
{
    const TypeReflection* reflection = ComponentRegistry::instance().reflection(type);
    const void* data = world.componentData(entity, type);
    if (reflection == nullptr || data == nullptr)
    {
        return "<none>";
    }
    JsonMutDocument doc;
    const Expected<JsonMutValue, ValueError> value = getPath(doc, *reflection, data, field);
    return value ? value->dump() : std::string("<error>");
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
    World world;       // attached as a Simulation World
    World editWorld;   // attached by the tests that need it; outlives the host
    World secondWorld; // see editWorld
    test::ModuleControl control;
    std::unique_ptr<ModuleHost> host;

    Fixture()
    {
        ModuleHostDesc desc;
        desc.shadowDirectory = directory.path / "shadow";
        host = std::make_unique<ModuleHost>(jobs, desc);
        host->provide(control);
        host->attachWorld(world, WorldRole::Simulation);
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

// A static module that adds the WorldTag of every World it updates to g_tagSum.
i32 g_tagSum = 0;

void updateTagged(ModuleContext& context, f64)
{
    if (const test::WorldTag* tag = context.service<test::WorldTag>())
    {
        g_tagSum += tag->value;
    }
}

constexpr ModuleApi kTaggedModule = makeModuleApi("tagged", "1.0.0", &loadStatic, nullptr, &updateTagged);

// A static module whose load() wrongly uses the World.
bool loadUsingWorld(ModuleContext& context)
{
    return context.world().aliveCount() == 0;
}

constexpr ModuleApi kWorldInLoad = makeModuleApi("world_in_load", "1.0.0", &loadUsingWorld, nullptr, nullptr);

// A static module that logs its start() and stop() calls into g_events.
std::string g_events;

void startLogged(ModuleContext& context)
{
    g_events += context.service<test::WorldTag>() != nullptr ? "start-tagged;" : "start;";
}

void stopLogged(ModuleContext& context)
{
    g_events += context.service<test::WorldTag>() != nullptr ? "stop-tagged;" : "stop;";
}

constexpr ModuleApi kLoggedModule =
    makeModuleApi("logged", "1.0.0", {.load = &loadStatic, .start = &startLogged, .stop = &stopLogged});

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

TEST_CASE("a reload that widens a Component migrates its values and keeps the session running")
{
    Fixture f;
    LogCapture log;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    f.host->update(0.0);
    CHECK(counterTotal(f.world) == 2);
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId counter = moduleType("test.ModuleCounter");
    const u32 narrow = registry.info(counter).size;
    const u32 types = registry.count();

    install(TEST_MODULE_V3, f.library);
    CHECK(f.host->reloadChanged() == 1);
    CHECK(moduleType("test.ModuleCounter") == counter);
    CHECK(registry.info(counter).size > narrow);
    CHECK(registry.count() == types);
    const ModuleInfo* info = f.host->find("test_module");
    REQUIRE(info != nullptr);
    CHECK(info->version == SemanticVersion{1, 1, 0, {}});
    REQUIRE(info->migration.types.size() == 1); // the other types kept their layout
    const ComponentMigration& migration = info->migration.types[0];
    CHECK(migration.type == "test.ModuleCounter");
    CHECK(migration.components == 2);
    CHECK(migration.addedFields.empty());
    CHECK(migration.removedFields.empty());
    CHECK(migration.resetFields.empty());
    CHECK(log.contains("migrated 2 Component(s)"));
    CHECK(counterTotal(f.world) == 2);
    CHECK(f.control.starts == 1); // the session keeps running

    f.host->update(0.0);
    CHECK(f.control.variant == 3);
    CHECK(f.control.counterId == counter);
    CHECK(counterTotal(f.world) == 2 + 6);

    install(TEST_MODULE_V2, f.library); // narrowing again migrates back
    CHECK(f.host->reloadChanged() == 1);
    CHECK(registry.info(counter).size == narrow);
    f.host->update(0.0);
    CHECK(counterTotal(f.world) == 8 + 4);
}

TEST_CASE("a reload migrates moved, widened, renamed, narrowed, removed and added fields in every World")
{
    Fixture f;
    LogCapture log;
    f.host->attachWorld(f.editWorld, WorldRole::Edit);
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId layout = moduleType("test.LayoutComponent");
    REQUIRE(registry.reflection(layout) != nullptr);

    // A Component of the edit World, made the way the editor makes one.
    const Entity edited = f.editWorld.create();
    const auto twelve = JsonDocument::parse("12");
    REQUIRE(twelve);
    REQUIRE(setPath(*registry.reflection(layout), f.editWorld.emplaceDefault(edited, layout), "count",
                    twelve->root()));

    const Entity first = f.control.layoutTarget;
    const std::vector<Entity> owners = entitiesWith(f.world, layout);
    REQUIRE(owners.size() == 2);
    const Entity second = owners[0] == first ? owners[1] : owners[0];
    const std::string target = fieldJson(f.world, second, layout, "target");
    CHECK(target == std::format("\"{}\"", first.toBits()));
    const u32 types = registry.count();

    install(TEST_MODULE_V5, f.library);
    REQUIRE_FALSE(f.host->reload("test_module"));
    CHECK(moduleType("test.LayoutComponent") == layout);
    CHECK(registry.count() == types);
    CHECK(registry.info(layout).alignment == 8);
    CHECK(registry.reflection(layout)->version == 2);

    CHECK(fieldJson(f.world, first, layout, "count") == "41");
    CHECK(fieldJson(f.world, second, layout, "count") == "-3");
    CHECK(fieldJson(f.world, first, layout, "velocity") == "2.5");
    CHECK(fieldJson(f.world, second, layout, "velocity") == "0.25");
    CHECK(fieldJson(f.world, first, layout, "small") == "9"); // 300 does not fit a u8
    CHECK(fieldJson(f.world, second, layout, "small") == "7");
    CHECK(fieldJson(f.world, first, layout, "label") == "\"new\"");
    CHECK(fieldJson(f.world, first, layout, "target") == "null");
    CHECK(fieldJson(f.world, second, layout, "target") == target);
    CHECK(fieldJson(f.world, second, layout, "removed") == "<error>");
    CHECK(fieldJson(f.editWorld, edited, layout, "count") == "12");
    CHECK(fieldJson(f.editWorld, edited, layout, "label") == "\"new\"");

    const ModuleInfo* info = f.host->find("test_module");
    REQUIRE(info != nullptr);
    REQUIRE(info->migration.types.size() == 1);
    const ComponentMigration& migration = info->migration.types[0];
    CHECK(migration.type == "test.LayoutComponent");
    CHECK(migration.components == 3);
    CHECK(migration.addedFields == std::vector<std::string>{"label"});
    CHECK(migration.removedFields == std::vector<std::string>{"removed"});
    CHECK(migration.resetFields == std::vector<std::string>{"small"});
    CHECK(log.contains("field 'small' of 'test.LayoutComponent'"));

    // The new code runs, and structural changes move the migrated Components with it.
    f.host->update(0.0);
    CHECK(f.control.variant == 5);
    f.world.emplace<Transform>(second);
    CHECK(fieldJson(f.world, second, layout, "label") == "\"new\"");
    f.world.destroy(first);
    f.world.destroy(second);
    f.editWorld.destroy(edited);
}

TEST_CASE("a reload that changes the layout of an unreflected Component in use is rolled back")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    f.host->update(0.0);
    const ComponentId layout = moduleType("test.LayoutComponent");

    install(TEST_MODULE_V6, f.library);
    const std::optional<std::string> error = f.host->reload("test_module");
    REQUIRE(error);
    CHECK(error->find("reflect the type to migrate it") != std::string::npos);
    CHECK(f.host->find("test_module")->version == SemanticVersion{1, 0, 0, {}});
    std::vector<ComponentId> staged;
    ComponentRegistry::instance().stagedTypes(staged);
    CHECK(staged.empty());
    f.host->update(0.0);
    CHECK(f.control.variant == 1);
    CHECK(counterTotal(f.world) == 4);
    CHECK(fieldJson(f.world, f.control.layoutTarget, layout, "count") == "41");

    install(TEST_MODULE_V5, f.library); // a reflected change still migrates
    REQUIRE_FALSE(f.host->reload("test_module"));
    CHECK(fieldJson(f.world, f.control.layoutTarget, layout, "count") == "41");
}

TEST_CASE("a value that cannot be snapshot rolls the reload back with every value kept")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId layout = moduleType("test.LayoutComponent");
    const u32 size = registry.info(layout).size;
    const TypeReflection* reflection = registry.reflection(layout);
    REQUIRE(reflection != nullptr);
    REQUIRE(reflection->field("speed") != nullptr);
    auto* speed = reinterpret_cast<f32*>(
        static_cast<std::byte*>(f.world.componentData(f.control.layoutTarget, layout)) +
        reflection->field("speed")->offset);
    *speed = std::numeric_limits<f32>::quiet_NaN(); // JSON cannot hold it

    install(TEST_MODULE_V5, f.library);
    const std::optional<std::string> error = f.host->reload("test_module");
    REQUIRE(error);
    CHECK(error->find("cannot migrate component 'test.LayoutComponent'") != std::string::npos);
    CHECK(registry.info(layout).size == size);
    CHECK(fieldJson(f.world, f.control.layoutTarget, layout, "count") == "41");
    CHECK(f.host->find("test_module")->reloads == 0);

    *speed = 1.0f;
    REQUIRE_FALSE(f.host->reload("test_module"));
    CHECK(fieldJson(f.world, f.control.layoutTarget, layout, "count") == "41");
}

TEST_CASE("a type that a World outside the host uses migrates once that World's archetypes are purged")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    const ComponentId layout = moduleType("test.LayoutComponent");
    World detached;
    const Entity entity = detached.create();
    static_cast<void>(detached.emplaceDefault(entity, layout));

    install(TEST_MODULE_V5, f.library);
    const std::optional<std::string> error = f.host->reload("test_module");
    REQUIRE(error);
    CHECK(error->find("not attached to the module host") != std::string::npos);

    detached.destroy(entity);
    CHECK(detached.purgeArchetypesWith(layout) == 1);
    REQUIRE_FALSE(f.host->reload("test_module"));
    CHECK(fieldJson(f.world, f.control.layoutTarget, layout, "count") == "41");
}

TEST_CASE("fifty migrating reloads keep the registry size, the type id and the values")
{
    Fixture f;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId layout = moduleType("test.LayoutComponent");
    const u32 types = registry.count();
    for (int i = 0; i < 25; ++i)
    {
        install(TEST_MODULE_V5, f.library);
        REQUIRE_FALSE(f.host->reload("test_module"));
        install(TEST_MODULE_V1, f.library);
        REQUIRE_FALSE(f.host->reload("test_module"));
    }
    CHECK(registry.count() == types);
    CHECK(moduleType("test.LayoutComponent") == layout);
    CHECK(f.host->find("test_module")->reloads == 50);
    CHECK(fieldJson(f.world, f.control.layoutTarget, layout, "count") == "41");
    f.host->update(0.0);
    CHECK(counterTotal(f.world) == 2);
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
    CHECK(f.control.stops == 1);
    CHECK(f.control.unloads == 1);
    CHECK(countWith(f.world, moduleType("test.ModuleCounter")) == 0); // the entities stay, empty
    CHECK_FALSE(std::filesystem::exists(f.directory.path / "shadow" / f.library.filename()));
}

TEST_CASE("start and stop run once per Simulation World and hot reloads keep the session running")
{
    Fixture f;
    f.host->attachWorld(f.editWorld, WorldRole::Edit);
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    CHECK(f.control.starts == 1); // the Simulation World only
    CHECK(f.editWorld.aliveCount() == 0);
    CHECK(f.world.aliveCount() == 6); // shared, 2 counters, 2 layouts, opaque
    CHECK_FALSE(f.control.worldInLoad);

    f.host->update(0.0);
    CHECK(f.control.updates == 1); // never on the edit World
    CHECK(counterTotal(f.world) == 2);

    install(TEST_MODULE_V2, f.library);
    CHECK(f.host->reloadChanged() == 1);
    CHECK(f.control.starts == 1); // a reload neither stops nor restarts the session
    CHECK(f.control.stops == 0);
    CHECK_FALSE(f.control.worldInLoad);

    f.host->attachWorld(f.secondWorld, WorldRole::Simulation);
    CHECK(f.control.starts == 2);
    CHECK(f.host->worldCount() == 3);
    f.host->update(0.0);
    CHECK(f.control.updates == 3);
    f.host->update(f.secondWorld, 0.0);
    CHECK(f.control.updates == 4);
    CHECK(counterTotal(f.world) == 2 + 4);       // two counters: +1 by version 1, +2 by version 2
    CHECK(counterTotal(f.secondWorld) == 2 + 2); // one counter, two updates of version 2

    CHECK(f.host->detachWorld(f.secondWorld));
    CHECK(f.control.stops == 1);
    CHECK_FALSE(f.host->detachWorld(f.secondWorld));
    CHECK(f.secondWorld.aliveCount() == 5); // detaching leaves the World alone
    // A detached World is no longer stripped: remove the module's Components while its code is loaded.
    std::vector<Entity> entities;
    f.secondWorld.entities(entities);
    for (const Entity entity : entities)
    {
        f.secondWorld.remove(entity, moduleType("test.ModuleCounter"));
    }

    CHECK(f.host->unload("test_module"));
    CHECK(f.control.stops == 2);
    CHECK(f.control.starts == 2);
    CHECK(f.control.simulationOnly);
}

TEST_CASE("unloading strips module Components from the edit and the play World before orphaning")
{
    Fixture f;
    f.host->attachWorld(f.editWorld, WorldRole::Edit);
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    // The edit World holds module Components the way a scene loaded in the editor does.
    const ComponentId counter = moduleType("test.ModuleCounter");
    const ComponentId legacy = moduleType("test.LegacyComponent");
    const Entity edited = f.editWorld.create();
    REQUIRE(f.editWorld.emplaceDefault(edited, counter) != nullptr);
    REQUIRE(f.editWorld.emplaceDefault(edited, legacy) != nullptr);

    install(TEST_MODULE_V2, f.library);
    CHECK(f.host->reloadChanged() == 1);
    CHECK(countWith(f.editWorld, legacy) == 0); // dropped by version 2, in both Worlds
    CHECK(countWith(f.world, legacy) == 0);
    CHECK(countWith(f.editWorld, counter) == 1);

    CHECK(f.host->unload("test_module"));
    CHECK(countWith(f.editWorld, counter) == 0);
    CHECK(countWith(f.world, counter) == 0);
    // Structural changes in both Worlds after the code is gone must not reach it.
    f.editWorld.destroy(edited);
    std::vector<Entity> entities;
    f.world.entities(entities);
    for (const Entity entity : entities)
    {
        f.world.destroy(entity);
    }
    CHECK(f.editWorld.aliveCount() == 0);
    CHECK(f.world.aliveCount() == 0);
}

TEST_CASE("a Simulation World attached after the modules starts them, and detaching stops them")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{1, 64}};
    World play;
    World preview;
    g_events.clear();
    ModuleHost host(jobs, ModuleHostDesc{directory.path / "shadow"});
    REQUIRE_FALSE(host.add(kLoggedModule));
    host.attachWorld(preview, WorldRole::Preview);
    CHECK(g_events.empty()); // Preview Worlds do not run modules
    ServiceSet services;
    test::WorldTag tag{1};
    services.provide(tag);
    host.attachWorld(play, WorldRole::Simulation, std::move(services));
    CHECK(g_events == "start-tagged;");
    CHECK(host.isAttached(play));
    CHECK(host.roleOf(play) == WorldRole::Simulation);
    CHECK(host.roleOf(preview) == WorldRole::Preview);
    CHECK(host.detachWorld(play));
    CHECK(g_events == "start-tagged;stop-tagged;");
    CHECK_FALSE(host.isAttached(play));
    CHECK(host.unload("logged"));
    CHECK(g_events == "start-tagged;stop-tagged;"); // no Simulation World left to stop on
}

TEST_CASE("services are found in the World's set first, then in the host's")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{1, 64}};
    World a;
    World b;
    World c;
    test::WorldTag tagA{1};
    test::WorldTag tagB{10};
    test::WorldTag hostTag{100};
    g_tagSum = 0;
    ModuleHost host(jobs, ModuleHostDesc{directory.path / "shadow"});
    host.provide(hostTag);
    ServiceSet servicesA;
    servicesA.provide(tagA);
    servicesA.provide(tagA); // replaces, does not add
    CHECK(servicesA.size() == 1);
    CHECK(servicesA.find<test::WorldTag>() == &tagA);
    CHECK(servicesA.find<test::ModuleControl>() == nullptr);
    ServiceSet servicesB;
    servicesB.provide(tagB);
    host.attachWorld(a, WorldRole::Simulation, std::move(servicesA));
    host.attachWorld(b, WorldRole::Simulation, std::move(servicesB));
    host.attachWorld(c, WorldRole::Simulation);
    REQUIRE_FALSE(host.add(kTaggedModule));
    host.update(0.0);
    CHECK(g_tagSum == 1 + 10 + 100);
    host.update(b, 0.0);
    CHECK(g_tagSum == 1 + 10 + 100 + 10);
    CHECK(host.detachWorld(a));
    CHECK(host.detachWorld(b));
    CHECK(host.detachWorld(c));
    CHECK(host.worldCount() == 0);
}

TEST_CASE("a shared module calls every function of the service interfaces through the host's vtables")
{
    Fixture f;
    test::CountingPhysics physics;
    test::CountingAudio audio;
    test::CountingInput input;
    test::CountingScenes scenes;
    ServiceSet services;
    services.provide<PhysicsService>(physics);
    services.provide<AudioService>(audio);
    services.provide<InputService>(input);
    services.provide<SceneService>(scenes);
    f.host->attachWorld(f.secondWorld, WorldRole::Simulation, std::move(services));
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    f.control.callServices = true;
    f.host->update(f.world, 0.0); // a World without the services: the module skips them
    CHECK(std::ranges::all_of(physics.calls, [](u32 n) { return n == 0; }));
    f.host->update(f.secondWorld, 0.0);
    CHECK(physics.everyFunctionCalledOnce());
    CHECK(audio.everyFunctionCalledOnce());
    CHECK(input.everyFunctionCalledOnce());
    CHECK(scenes.everyFunctionCalledOnce());
    CHECK(f.host->detachWorld(f.secondWorld));
}

TEST_CASE("geometry a module creates through PhysicsService outlives hot reloads and unloading")
{
    // Regression test, meant for ASan builds: with an engine copy inside the module, the mesh shape would
    // carry a vtable into the closed library. Through the service the host's code creates it.
    Fixture f;
    reflectPhysicsComponents(); // the host owns the engine's types before any module loads
    PhysicsWorld physics(f.secondWorld, f.jobs);
    PhysicsWorldService physicsService(physics);
    ServiceSet services;
    services.provide<PhysicsService>(physicsService);
    f.host->attachWorld(f.secondWorld, WorldRole::Simulation, std::move(services));
    f.control.createMesh = true;
    install(TEST_MODULE_V1, f.library);
    REQUIRE_FALSE(f.host->load(f.library));
    REQUIRE(f.control.mesh.valid());
    physics.update(test::kStep);
    CHECK(physics.bodyCount() == 1);

    install(TEST_MODULE_V2, f.library);
    REQUIRE(f.host->reloadChanged() == 1); // the first library is closed now
    CHECK(physics.isMeshValid(f.control.mesh));
    const Entity ball = f.secondWorld.create(test::at(Vec3(0.0f, 1.0f, 0.0f)),
                                             test::body(MotionType::Dynamic), test::sphere(0.25f));
    test::simulate(physics, 1.0);
    const f32 restingHeight = f.secondWorld.get<Transform>(ball).position.y; // the ball rests on the mesh
    CHECK(restingHeight > 0.2f);
    CHECK(restingHeight < 0.3f);
    const std::optional<RaycastHit> hit =
        physics.raycast(Vec3(2.0f, 5.0f, 2.0f), Vec3(0.0f, -1.0f, 0.0f), 10.0f);
    REQUIRE(hit);
    CHECK(hit->entity == f.control.meshEntity);
    physics.rebuild(f.control.meshEntity); // a new body for the same host-owned geometry

    CHECK(f.host->unload("test_module"));
    CHECK(f.host->detachWorld(f.secondWorld));
    test::simulate(physics, 0.1);
    f.secondWorld.destroy(f.control.meshEntity);
    physics.update(test::kStep);
    physics.destroyMesh(f.control.mesh);
    CHECK_FALSE(physics.isMeshValid(f.control.mesh));
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("load() sees no World and asserts when it asks for one")
{
    test::TempDirectory directory;
    JobSystem jobs{JobSystemDesc{1, 64}};
    World world;
    World other;
    ModuleHost host(jobs, ModuleHostDesc{directory.path / "shadow"});
    host.attachWorld(world, WorldRole::Simulation);
    {
        test::ScopedAssertCatcher catcher;
        CHECK_THROWS_AS(static_cast<void>(host.add(kWorldInLoad)), test::AssertionFailure);
        CHECK_THROWS_AS(host.attachWorld(world, WorldRole::Edit), test::AssertionFailure); // twice
        CHECK_THROWS_AS(host.update(other, 0.0), test::AssertionFailure);                  // not attached
    }
    CHECK(host.count() == 0);
    CHECK(host.detachWorld(world));
}
#endif

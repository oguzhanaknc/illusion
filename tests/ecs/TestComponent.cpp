#include "TestComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/ecs/Component.hpp>

#include <doctest/doctest.h>

#include <thread>
#include <vector>

using namespace illusion;
using namespace ecs_test;

namespace component_test
{
struct OnlyHere
{
    double value;
};
struct OtherLayout
{
    int a;
};
} // namespace component_test

TEST_CASE("componentId is stable and distinct per type")
{
    const ComponentId position = componentId<Position>();
    const ComponentId velocity = componentId<Velocity>();
    CHECK(position != velocity);
    CHECK(componentId<Position>() == position);
    CHECK(ComponentRegistry::instance().find(typeHash<Position>()) == position);
}

TEST_CASE("ComponentInfo describes the type")
{
    const ComponentInfo& info = ComponentRegistry::instance().info(componentId<Position>());
    CHECK(info.name == "ecs_test::Position");
    CHECK(info.size == sizeof(Position));
    CHECK(info.alignment == alignof(Position));
    CHECK(info.trivial);
    CHECK_FALSE(info.isTag());

    const ComponentInfo& name = ComponentRegistry::instance().info(componentId<Name>());
    CHECK_FALSE(name.trivial);

    const ComponentInfo& tag = ComponentRegistry::instance().info(componentId<PlayerTag>());
    CHECK(tag.isTag());
    CHECK(tag.size == 0);
}

TEST_CASE("ComponentInfo relocate and destroy manage lifetimes")
{
    test::LifetimeCounter::reset();
    const ComponentInfo info = makeComponentInfo<test::LifetimeCounter>();
    alignas(test::LifetimeCounter) std::byte a[sizeof(test::LifetimeCounter)];
    alignas(test::LifetimeCounter) std::byte b[sizeof(test::LifetimeCounter)];
    ::new (a) test::LifetimeCounter(5);
    info.relocate(b, a);
    CHECK(test::LifetimeCounter::alive == 1);
    CHECK(reinterpret_cast<test::LifetimeCounter*>(b)->value == 5);
    info.destroy(b);
    CHECK(test::LifetimeCounter::alive == 0);
}

TEST_CASE("A private registry deduplicates by hash and copies names")
{
    ComponentRegistry registry;
    CHECK(registry.count() == 0);
    const ComponentId a = registry.registerType<component_test::OnlyHere>();
    const ComponentId b = registry.registerType<component_test::OnlyHere>();
    CHECK(a == b);
    CHECK(registry.count() == 1);
    CHECK(registry.find(typeHash<component_test::OnlyHere>()) == a);
    CHECK(registry.find(12345) == kInvalidComponentId);

    // The registry owns its copy of the name.
    ComponentInfo info = makeComponentInfo<component_test::OtherLayout>();
    std::string temporaryName = "component_test::Renamed";
    info.name = temporaryName;
    info.hash = fnv1a64(temporaryName);
    const ComponentId renamed = registry.registerType(info);
    temporaryName.assign("garbage garbage garbage");
    CHECK(registry.info(renamed).name == "component_test::Renamed");
}

TEST_CASE("contains() and tryInfo() validate ids without asserting")
{
    ComponentRegistry registry;
    CHECK_FALSE(registry.contains(0));
    CHECK(registry.tryInfo(0) == nullptr);
    const ComponentId id = registry.registerType<component_test::OnlyHere>();
    CHECK(registry.contains(id));
    REQUIRE(registry.tryInfo(id) != nullptr);
    CHECK(registry.tryInfo(id) == &registry.info(id));
    CHECK(registry.tryInfo(id)->name == typeName<component_test::OnlyHere>());
    CHECK_FALSE(registry.contains(static_cast<ComponentId>(id + 1)));
    CHECK(registry.tryInfo(static_cast<ComponentId>(id + 1)) == nullptr);
    CHECK_FALSE(registry.contains(kInvalidComponentId));
    CHECK(registry.tryInfo(kInvalidComponentId) == nullptr);
}

TEST_CASE("Registering a type with a conflicting layout is fatal")
{
    ComponentRegistry registry;
    registry.registerType<component_test::OtherLayout>();
    ComponentInfo conflicting = makeComponentInfo<component_test::OnlyHere>();
    conflicting.name = typeName<component_test::OtherLayout>();
    conflicting.hash = typeHash<component_test::OtherLayout>();
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS_AS(registry.registerType(conflicting), test::AssertionFailure);
}

TEST_CASE("Concurrent registration yields one id per type")
{
    ComponentRegistry registry;
    std::vector<ComponentId> ids(8);
    std::vector<std::thread> threads;
    for (usize t = 0; t < ids.size(); ++t)
    {
        threads.emplace_back(
            [&registry, &ids, t] { ids[t] = registry.registerType<component_test::OnlyHere>(); });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    for (const ComponentId id : ids)
    {
        CHECK(id == ids[0]);
    }
    CHECK(registry.count() == 1);
}

namespace component_test
{

// Lifetime functions that tell which "module" registered a type.
int g_destroyedBy = 0;

template <int Module>
void destroyFrom(void*) noexcept
{
    g_destroyedBy = Module;
}

ComponentInfo moduleInfo(u32 module, void (*destroy)(void*) noexcept)
{
    ComponentInfo info = makeComponentInfo<OnlyHere>();
    info.module = module;
    info.destroy = destroy;
    return info;
}

} // namespace component_test

TEST_CASE("a retired module's types are taken over by the next registration")
{
    using namespace component_test;
    ComponentRegistry registry;
    const ComponentId id = registry.registerType(moduleInfo(1, &destroyFrom<1>));
    CHECK(registry.info(id).module == 1);

    // Without retirement another registration does not take over.
    CHECK(registry.registerType(moduleInfo(2, &destroyFrom<2>)) == id);
    CHECK(registry.info(id).module == 1);

    std::vector<ComponentId> owned;
    registry.typesOfModule(1, owned);
    CHECK(owned == std::vector<ComponentId>{id});

    const ComponentInfo before = registry.info(id);
    registry.retireModule(1);
    CHECK(registry.registerType(moduleInfo(2, &destroyFrom<2>)) == id);
    CHECK(registry.info(id).module == 2);
    registry.info(id).destroy(nullptr);
    CHECK(g_destroyedBy == 2);

    // A rollback gives the type back.
    registry.restoreType(id, before);
    registry.restoreModule(1);
    CHECK(registry.info(id).module == 1);
    registry.info(id).destroy(nullptr);
    CHECK(g_destroyedBy == 1);
    CHECK(registry.registerType(moduleInfo(3, &destroyFrom<2>)) == id); // no longer retired
    CHECK(registry.info(id).module == 1);
}

TEST_CASE("orphaned types are taken over by any later registration")
{
    using namespace component_test;
    ComponentRegistry registry;
    const ComponentId id = registry.registerType(moduleInfo(4, &destroyFrom<1>));
    registry.orphanModule(4);
    std::vector<ComponentId> owned;
    registry.typesOfModule(4, owned);
    CHECK(owned.empty());
    CHECK(registry.info(id).construct == nullptr);
    CHECK(registry.registerType(moduleInfo(5, &destroyFrom<2>)) == id);
    CHECK(registry.info(id).module == 5);
    registry.info(id).destroy(nullptr);
    CHECK(g_destroyedBy == 2);
}

TEST_CASE("a takeover with another layout is reported instead of applied")
{
    using namespace component_test;
    ComponentRegistry registry;
    const ComponentId id = registry.registerType(moduleInfo(6, &destroyFrom<1>));
    registry.retireModule(6);
    ComponentInfo wider = moduleInfo(7, &destroyFrom<2>);
    wider.size += 8;
    CHECK(registry.registerType(wider) == id);
    CHECK(registry.registerType(wider) == id);
    CHECK(registry.info(id).module == 6); // not taken over
    std::vector<std::string> conflicts;
    CHECK(registry.takeLayoutConflicts(conflicts));
    CHECK(conflicts == std::vector<std::string>{std::string(typeName<OnlyHere>())});
    CHECK_FALSE(registry.takeLayoutConflicts(conflicts));
}

TEST_CASE("a registration by the host takes a type over from a module")
{
    using namespace component_test;
    ComponentRegistry registry;
    const ComponentId id = registry.registerType(moduleInfo(8, &destroyFrom<1>));
    CHECK(registry.registerType(moduleInfo(0, &destroyFrom<2>)) == id);
    CHECK(registry.info(id).module == 0);
    registry.info(id).destroy(nullptr);
    CHECK(g_destroyedBy == 2);
    registry.orphanModule(8); // the module unloads: the host keeps the type
    CHECK(registry.info(id).module == 0);
    CHECK(registry.registerType(moduleInfo(9, &destroyFrom<1>)) == id);
    CHECK(registry.info(id).module == 0);
}

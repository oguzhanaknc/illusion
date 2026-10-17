#include "ReflectedComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>

using namespace illusion;
using namespace reflection_test;

namespace reflection_test
{

struct NotDefaultConstructible
{
    explicit NotDefaultConstructible(int v) noexcept
        : value(v)
    {
    }
    int value;
};

struct Renamed
{
    f32 value = 0.0f;
};

struct OtherRenamed
{
    f32 value = 0.0f;
};

} // namespace reflection_test

namespace
{

template <class T, class M>
u32 offsetIn(M T::* member)
{
    const T object{};
    return static_cast<u32>(reinterpret_cast<const std::byte*>(&(object.*member)) -
                            reinterpret_cast<const std::byte*>(&object));
}

} // namespace

TEST_CASE("reflection describes fields by stable name, type and offset")
{
    reflectTestComponents();
    ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId id = componentId<Everything>();
    CHECK(registry.findByStableName("test.Everything") == id);
    CHECK(registry.findByStableName("test.Missing") == kInvalidComponentId);

    const ComponentReflection* reflection = registry.reflection(id);
    REQUIRE(reflection != nullptr);
    CHECK(reflection->name == "test.Everything");
    CHECK(reflection->version == 3);
    REQUIRE(reflection->fields.size() == 15);
    CHECK(reflection->fields[0].name == "flag");
    CHECK(reflection->fields[0].type == FieldType::Bool);

    const FieldInfo* vec3 = reflection->field("vec3");
    REQUIRE(vec3 != nullptr);
    CHECK(vec3->type == FieldType::Vector3);
    CHECK(vec3->offset == offsetIn(&Everything::vec3));
    CHECK(vec3->offset > reflection->fields[0].offset);
    CHECK(reflection->field("text")->type == FieldType::String);
    CHECK(reflection->field("target")->type == FieldType::Entity);
    CHECK(reflection->field("quat")->type == FieldType::Quaternion);
    CHECK(reflection->field("missing") == nullptr);

    const FieldInfo* team = reflection->field("team");
    REQUIRE(team != nullptr);
    CHECK(team->type == FieldType::Enum);
    CHECK(team->enumSize == 2);
    CHECK(team->enumSigned);
    REQUIRE(team->enumValues.size() == 3);
    CHECK(team->enumValues[0].name == "Red");
    CHECK(team->enumValues[2].value == 7);
    CHECK_FALSE(reflection->field("flags")->enumSigned);

    // Tags can be reflected and have no fields.
    const ComponentReflection* tag = registry.reflection(componentId<Selected>());
    REQUIRE(tag != nullptr);
    CHECK(tag->fields.empty());
    CHECK(registry.reflection(componentId<Unreflected>()) == nullptr);

    CHECK(fieldTypeName(FieldType::Vector3) == "Vector3");
    CHECK(fieldTypeName(FieldType::Enum) == "Enum");
}

TEST_CASE("enum fields are read and written with their underlying size and sign")
{
    reflectTestComponents();
    const ComponentReflection& reflection =
        *ComponentRegistry::instance().reflection(componentId<Everything>());
    Everything value;
    value.team = Team::Red;
    CHECK(readEnumField(&value, *reflection.field("team")) == -1);
    writeEnumField(&value, *reflection.field("team"), 7);
    CHECK(value.team == Team::Blue);
    writeEnumField(&value, *reflection.field("flags"), 1);
    CHECK(value.flags == Flags::Visible);
    CHECK(readEnumField(&value, *reflection.field("flags")) == 1);
    // Neighbouring fields are untouched.
    CHECK(value.text == "default");
}

TEST_CASE("reflecting a type again replaces its reflection and stable name")
{
    ComponentRegistry registry;
    reflect<Renamed>("test.OldName", 1, registry).field("value", &Renamed::value);
    const ComponentId id = registry.findByStableName("test.OldName");
    REQUIRE(id != kInvalidComponentId);

    reflect<Renamed>("test.NewName", 2, registry).field("value", &Renamed::value);
    CHECK(registry.findByStableName("test.OldName") == kInvalidComponentId);
    CHECK(registry.findByStableName("test.NewName") == id);
    CHECK(registry.reflection(id)->version == 2);
    CHECK(registry.reflection(id)->fields.size() == 1);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("reflection rejects invalid and duplicate names")
{
    ComponentRegistry registry;
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS(reflect<Renamed>("bad name", 1, registry));
    CHECK_THROWS(reflect<Renamed>("", 1, registry));
    CHECK_THROWS(reflect<Renamed>("test.Renamed", 1, registry).field("1st", &Renamed::value));
    CHECK_THROWS(reflect<Renamed>("test.Renamed", 1, registry)
                     .field("value", &Renamed::value)
                     .field("value", &Renamed::value));
    // A stable name belongs to one type.
    reflect<Renamed>("test.Shared", 1, registry);
    CHECK_THROWS(reflect<OtherRenamed>("test.Shared", 1, registry));
}
#endif

TEST_CASE("core components are reflected under illusion names")
{
    reflectCoreComponents();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const ComponentId id = registry.findByStableName("illusion.Transform");
    CHECK(id == componentId<Transform>());
    const ComponentReflection* reflection = registry.reflection(id);
    REQUIRE(reflection != nullptr);
    REQUIRE(reflection->fields.size() == 3);
    CHECK(reflection->field("position")->offset == offsetIn(&Transform::position));
    CHECK(reflection->field("scale")->offset == offsetIn(&Transform::scale));
    CHECK(reflection->field("rotation")->type == FieldType::Quaternion);
    CHECK(reflection->field("scale")->type == FieldType::Vector3);
}

TEST_CASE("component info default-constructs default constructible types")
{
    const ComponentInfo everything = makeComponentInfo<Everything>();
    REQUIRE(everything.construct != nullptr);
    alignas(Everything) std::byte storage[sizeof(Everything)];
    everything.construct(storage);
    auto* value = std::launder(reinterpret_cast<Everything*>(storage));
    CHECK(value->text == "default");
    CHECK(value->u32Value == 5);
    everything.destroy(value);

    CHECK(makeComponentInfo<NotDefaultConstructible>().construct == nullptr);
}

TEST_CASE("worlds expose components by id")
{
    reflectTestComponents();
    World world;
    const Entity a = world.create(Everything{}, Selected{});
    const Entity b = world.create();
    const Entity c = world.create(Follow{a, 3.0f});
    world.destroy(b);

    std::vector<Entity> entities;
    world.entities(entities);
    REQUIRE(entities.size() == 2);
    CHECK(entities[0] == a);
    CHECK(entities[1] == c);

    const std::span<const ComponentId> components = world.componentsOf(a);
    CHECK(components.size() == 2);
    CHECK(std::is_sorted(components.begin(), components.end()));

    const ComponentId everything = componentId<Everything>();
    CHECK(world.componentData(a, everything) == &world.get<Everything>(a));
    CHECK(world.componentData(c, everything) == nullptr);
    CHECK(world.componentData(a, componentId<Selected>()) == nullptr); // tags have no data
    const World& constWorld = world;
    CHECK(constWorld.componentData(c, componentId<Follow>()) == &world.get<Follow>(c));

    auto* added = static_cast<Everything*>(world.emplaceDefault(c, everything));
    REQUIRE(added != nullptr);
    CHECK(added->text == "default");
    CHECK(world.has<Everything>(c));
    CHECK(world.emplaceDefault(c, componentId<Selected>()) == nullptr);
    CHECK(world.has<Selected>(c));

    CHECK(world.remove(c, everything));
    CHECK_FALSE(world.remove(c, everything));
    CHECK_FALSE(world.has<Everything>(c));
    CHECK(&world.registry() == &ComponentRegistry::instance());
}

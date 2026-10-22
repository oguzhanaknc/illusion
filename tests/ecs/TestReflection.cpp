#include "ReflectedComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

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

// --- Reflection v2: metadata, units, values and structs -----------------------------------------------

namespace reflection_test
{

struct Settings
{
    f32 strength = 0.5f;
    std::string label = "settings";
};

struct Holder
{
    Settings settings;
    std::array<f32, 32> curve{}; // larger than FieldValue's inline storage
    f32 angle = 0.25f;
    Vec3 color{1.0f};
};

struct Unregistered
{
    int value = 0;
};

struct HasUnregistered
{
    Unregistered nested;
};

struct RuntimeState
{
    u32 handle = 0;
};

} // namespace reflection_test

TEST_CASE("metadata strings are copied into the registry")
{
    ComponentRegistry registry;
    {
        std::string label = "Strength of the effect";
        std::string description = "How strong it is.";
        std::string icon = "bolt";
        std::string required = "illusion.Transform";
        const std::string_view requiredList[] = {required};
        reflectStruct<Settings>("test.Settings", 1, registry.structs())
            .field("strength", &Settings::strength,
                   {.displayName = label, .description = description, .min = 0.0})
            .field("label", &Settings::label);
        reflect<Holder>("test.Holder", 1, registry)
            .meta({.displayName = label,
                   .category = "Tests/Reflection",
                   .icon = icon,
                   .requiredComponents = requiredList})
            .field("settings", &Holder::settings)
            .field("curve", &Holder::curve)
            .field("angle", &Holder::angle,
                   {.unit = Unit::Radians,
                    .displayUnit = Unit::Degrees,
                    .softMax = 1.0,
                    .hint = FieldHint::Angle})
            .field("color", &Holder::color, {.hint = FieldHint::Color, .advanced = true});
        label.assign(label.size(), 'x');
        description.assign(description.size(), 'x');
        icon.assign(icon.size(), 'x');
        required.assign(required.size(), 'x');
    }
    const TypeReflection& holder = *registry.reflection(registry.registerType<Holder>());
    CHECK(holder.meta.displayName == "Strength of the effect");
    CHECK(holder.meta.icon == "bolt");
    CHECK(holder.meta.category == "Tests/Reflection");
    REQUIRE(holder.meta.requiredComponents.size() == 1);
    CHECK(holder.meta.requiredComponents[0] == "illusion.Transform");
    CHECK(holder.size == sizeof(Holder));

    const FieldInfo& settings = *holder.field("settings");
    CHECK(settings.type == FieldType::Struct);
    REQUIRE(settings.structType != nullptr);
    CHECK(settings.structType->name == "test.Settings");
    CHECK(settings.structType->field("strength")->meta.displayName == "Strength of the effect");
    CHECK(settings.structType->field("strength")->meta.description == "How strong it is.");
    CHECK(settings.structType->field("strength")->meta.min == 0.0);

    const FieldInfo& curve = *holder.field("curve");
    CHECK(curve.type == FieldType::FixedArray);
    CHECK(curve.count == 32);
    REQUIRE(curve.element != nullptr);
    CHECK(curve.element->type == FieldType::F32);

    const FieldInfo& angle = *holder.field("angle");
    CHECK(angle.meta.unit == Unit::Radians);
    CHECK(angle.meta.displayUnit == Unit::Degrees);
    CHECK(angle.meta.softMax == 1.0);
    CHECK_FALSE(angle.meta.max.has_value());
    CHECK(holder.field("color")->meta.advanced);
    CHECK(fieldHintName(holder.field("color")->meta.hint) == "Color");
}

TEST_CASE("display names come from metadata or from the stable names")
{
    reflectCoreComponents();
    reflectTestComponents();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const TypeReflection& transform = *registry.reflection(componentId<Transform>());
    CHECK(displayNameOf(transform) == "Transform");
    CHECK(displayNameOf(*transform.field("position")) == "Position");
    const TypeReflection& everything = *registry.reflection(componentId<Everything>());
    CHECK(displayNameOf(everything) == "Everything");
    CHECK(displayNameOf(*everything.field("f32Value")) == "F 32 Value");
    FieldInfo field;
    field.name = "castShadows";
    CHECK(displayNameOf(field) == "Cast shadows");
    field.name = "fovY";
    CHECK(displayNameOf(field) == "Fov Y");
    field.name = "use_HDR_output";
    CHECK(displayNameOf(field) == "Use HDR output");
    field.meta.displayName = "Custom";
    CHECK(displayNameOf(field) == "Custom");
}

TEST_CASE("units have symbols and convert within one quantity")
{
    CHECK(unitSymbol(Unit::None).empty());
    CHECK(unitSymbol(Unit::Degrees) == "deg");
    CHECK(unitSymbol(Unit::MetersPerSecond) == "m/s");
    for (u32 i = 1; i <= static_cast<u32>(Unit::Decibels); ++i)
    {
        const auto unit = static_cast<Unit>(i);
        CAPTURE(unitSymbol(unit));
        CHECK(unitFromSymbol(unitSymbol(unit)) == unit);
        CHECK(convertUnit(2.0, unit, unit) == 2.0);
    }
    CHECK_FALSE(unitFromSymbol("").has_value());
    CHECK_FALSE(unitFromSymbol("feet").has_value());
    CHECK(convertUnit(180.0, Unit::Degrees, Unit::Radians).value() == doctest::Approx(3.14159265358979));
    CHECK(convertUnit(1.0, Unit::Radians, Unit::Degrees).value() == doctest::Approx(57.2957795));
    CHECK(convertUnit(0.25, Unit::Ratio, Unit::Percent).value() == doctest::Approx(25.0));
    CHECK(convertUnit(50.0, Unit::Percent, Unit::Ratio).value() == doctest::Approx(0.5));
    CHECK_FALSE(convertUnit(1.0, Unit::Meters, Unit::Seconds).has_value());
    CHECK_FALSE(convertUnit(1.0, Unit::Degrees, Unit::Percent).has_value());
    CHECK_FALSE(convertUnit(1.0, Unit::None, Unit::Meters).has_value());
    CHECK(fieldTypeName(FieldType::Optional) == "Optional");
    CHECK(fieldTypeName(FieldType::Json) == "Json");
}

TEST_CASE("field values copy, compare and write back reflected fields of any type")
{
    ComponentRegistry registry;
    reflectStruct<Settings>("test.Settings", 1, registry.structs())
        .field("strength", &Settings::strength)
        .field("label", &Settings::label);
    reflect<Holder>("test.Holder", 1, registry)
        .field("settings", &Holder::settings)
        .field("curve", &Holder::curve)
        .field("angle", &Holder::angle, {.unit = Unit::Radians, .hint = FieldHint::Angle});
    const TypeReflection& type = *registry.reflection(registry.registerType<Holder>());

    Holder a;
    a.settings.label = "a much longer label that does not fit any small string buffer";
    a.curve[31] = 5.0f;
    Holder b;
    const FieldInfo& settings = *type.field("settings");
    const FieldInfo& curve = *type.field("curve");
    const FieldInfo& angle = *type.field("angle");
    CHECK_FALSE(fieldsEqual(&a, &b, settings));
    CHECK_FALSE(fieldsEqual(&a, &b, curve));
    CHECK(fieldsEqual(&a, &b, angle));

    FieldValue value = readField(&a, settings);
    REQUIRE(value.holds<Settings>());
    CHECK(value.get<Settings>()->label == a.settings.label);
    CHECK(value.get<f32>() == nullptr);
    writeField(&b, settings, value);
    CHECK(b.settings.label == a.settings.label);
    CHECK(fieldsEqual(&a, &b, settings));

    // Large values live on the heap; copies and moves keep them intact.
    FieldValue big = readField(&a, curve);
    FieldValue copy = big;
    CHECK(copy == big);
    FieldValue moved = std::move(big);
    CHECK(big.empty()); // NOLINT(bugprone-use-after-move): moved-from values are documented as empty
    CHECK(moved == copy);
    CHECK(moved.get<std::array<f32, 32>>()->at(31) == 5.0f);
    writeField(&b, curve, moved);
    CHECK(b.curve[31] == 5.0f);

    // Inline values move too.
    FieldValue small = readField(&a, angle);
    FieldValue other(std::move(small));
    CHECK(small.empty()); // NOLINT(bugprone-use-after-move)
    CHECK(*other.get<f32>() == 0.25f);
    other = copy; // a value of another type replaces it
    CHECK(other == copy);
    other = FieldValue();
    CHECK(other.empty());
    CHECK(other == FieldValue());
    CHECK_FALSE(other == copy);

    const FieldValue fresh(settings);
    CHECK(fresh.get<Settings>()->label == "settings");
    CHECK(defaultValue(type, settings) == fresh);
    CHECK(defaultValue(type, angle) == readField(&b, angle));

#if ILL_ASSERTS_ENABLED
    test::ScopedAssertCatcher catcher;
    CHECK_THROWS(writeField(&b, angle, copy));
#endif
}

TEST_CASE("struct reflections keep their ids and old reflections stay valid")
{
    ComponentRegistry registry;
    StructRegistry& structs = registry.structs();
    const StructId id =
        reflectStruct<Settings>("test.Settings", 1, structs).field("strength", &Settings::strength).id();
    CHECK(structs.find(typeHash<Settings>()) == id);
    CHECK(structs.findByStableName("test.Settings") == id);
    CHECK(structs.count() == 1);
    const StructReflection* old = structs.reflection(id);
    REQUIRE(old != nullptr);

    reflectStruct<Settings>("test.SettingsV2", 2, structs)
        .field("strength", &Settings::strength)
        .field("label", &Settings::label);
    CHECK(structs.findByStableName("test.Settings") == kInvalidStructId);
    CHECK(structs.findByStableName("test.SettingsV2") == id);
    CHECK(structs.reflection(id)->fields.size() == 2);
    CHECK(structs.reflection(id)->version == 2);
    CHECK(structs.reflection(kInvalidStructId) == nullptr);
    // The replaced reflection is still readable.
    CHECK(old->name == "test.Settings");
    CHECK(old->fields.size() == 1);
    CHECK(old->fields[0].name == "strength");

    const ComponentId holder = registry.registerType<Holder>();
    reflect<Holder>("test.Holder", 1, registry).field("angle", &Holder::angle);
    const TypeReflection* first = registry.reflection(holder);
    reflect<Holder>("test.Holder", 2, registry);
    CHECK(first->version == 1);
    CHECK(first->fields.size() == 1);
    CHECK(registry.reflection(holder)->fields.empty());
}

TEST_CASE("runtime state is reflected as transient")
{
    ComponentRegistry registry;
    reflectRuntime<RuntimeState>("test.RuntimeState", {.displayName = "Runtime state"}, registry)
        .field("handle", &RuntimeState::handle, {.readOnly = true});
    const TypeReflection& type = *registry.reflection(registry.registerType<RuntimeState>());
    CHECK(type.meta.transient);
    CHECK(type.meta.displayName == "Runtime state");
    CHECK(type.field("handle")->meta.readOnly);
}

#if ILL_ASSERTS_ENABLED
TEST_CASE("reflection rejects inconsistent metadata and unreflected structs")
{
    ComponentRegistry registry;
    test::ScopedAssertCatcher catcher;
    const auto holder = [&](u32 version = 1) {
        return reflect<Holder>("test.Holder", version, registry);
    };
    CHECK_THROWS(holder().field("angle", &Holder::angle, {.hint = FieldHint::Angle}));
    CHECK_THROWS(holder().field("angle", &Holder::angle, {.hint = FieldHint::Color}));
    CHECK_THROWS(holder().field("angle", &Holder::angle, {.min = 2.0, .max = 1.0}));
    CHECK_THROWS(
        holder().field("angle", &Holder::angle, {.unit = Unit::Meters, .displayUnit = Unit::Degrees}));
    CHECK_THROWS(holder().field("angle", &Holder::angle, {.step = 0.0}));
    CHECK_THROWS(holder().field("angle", &Holder::angle, {.hint = FieldHint::LayerMask}));
    CHECK_THROWS(holder().field("curve", &Holder::curve, {.hint = FieldHint::Multiline}));
    CHECK_THROWS(holder().field("settings", &Holder::settings, {.min = 0.0}));
    CHECK_THROWS(
        holder().field("angle", &Holder::angle, {.renamedFrom = "color"}).field("color", &Holder::color));
    CHECK_THROWS(holder(2).migrate(2, [](JsonMutDocument&, JsonMutValue) {}));
    CHECK_THROWS(holder(3)
                     .migrate(1, [](JsonMutDocument&, JsonMutValue) {})
                     .migrate(1, [](JsonMutDocument&, JsonMutValue) {}));
    const std::string_view badRequirement[] = {"bad name"};
    CHECK_THROWS(holder().meta({.requiredComponents = badRequirement}));
    CHECK_THROWS(reflect<HasUnregistered>("test.HasUnregistered", 1, registry)
                     .field("nested", &HasUnregistered::nested));
    reflectStruct<Settings>("test.Shared", 1, registry.structs());
    CHECK_THROWS(reflectStruct<Unregistered>("test.Shared", 1, registry.structs()));
}
#endif

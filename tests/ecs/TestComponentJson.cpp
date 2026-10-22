#include "ReflectedComponents.hpp"
#include "TestSupport.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using namespace illusion;

namespace json_test
{

enum class Tiny : i8
{
    Low = -3,
    High = 4,
};

enum class Medium : u16
{
    A = 1,
    B = 60000,
};

enum class Wide : i32
{
    Negative = -100000,
    Positive = 100000,
};

enum class Huge : u64
{
    Big = u64{1} << 62,
};

enum class HugeSigned : i64
{
    Negative = -(i64{1} << 40),
};

enum class Layers : u32
{
    None = 0,
    Ground = 1,
    Water = 2,
    Air = 4,
};

} // namespace json_test

template <>
struct illusion::EnumReflection<json_test::Tiny>
{
    static constexpr std::array kValues{illusion::EnumValue{"Low", -3}, illusion::EnumValue{"High", 4}};
};

template <>
struct illusion::EnumReflection<json_test::Medium>
{
    static constexpr std::array kValues{illusion::EnumValue{"A", 1}, illusion::EnumValue{"B", 60000}};
};

template <>
struct illusion::EnumReflection<json_test::Wide>
{
    static constexpr std::array kValues{illusion::EnumValue{"Negative", -100000},
                                        illusion::EnumValue{"Positive", 100000}};
};

template <>
struct illusion::EnumReflection<json_test::Huge>
{
    static constexpr std::array kValues{illusion::EnumValue{"Big", illusion::i64{1} << 62}};
};

template <>
struct illusion::EnumReflection<json_test::HugeSigned>
{
    static constexpr std::array kValues{illusion::EnumValue{"Negative", -(illusion::i64{1} << 40)}};
};

template <>
struct illusion::EnumReflection<json_test::Layers>
{
    static constexpr bool kFlags = true;
    static constexpr std::array kValues{illusion::EnumValue{"None", 0}, illusion::EnumValue{"Ground", 1},
                                        illusion::EnumValue{"Water", 2}, illusion::EnumValue{"Air", 4}};
};

namespace json_test
{

struct Point
{
    Vec3 position{0.0f};
    f32 weight = 1.0f;
    std::string label = "p";
};

struct Route
{
    std::vector<Point> points;
    std::array<Point, 2> ends{};
    std::optional<Point> pivot;
};

// Every field type reflection v2 adds, plus metadata that changes how values are read.
struct Gadget
{
    i8 tiny = -1;
    i16 small = -300;
    u8 byte = 200;
    u16 word = 60000;
    u64 big = (u64{1} << 63) + 5;
    Tiny tinyEnum = Tiny::High;
    Medium mediumEnum = Medium::B;
    Wide wideEnum = Wide::Negative;
    Huge hugeEnum = Huge::Big;
    HugeSigned hugeSignedEnum = HugeSigned::Negative;
    Layers layers = static_cast<Layers>(5); // Ground | Air
    Point point;
    std::array<f32, 3> weights{1.0f, 2.0f, 3.0f};
    std::vector<i32> numbers{1, 2};
    std::vector<Route> routes;
    std::optional<f32> limit;
    std::optional<std::string> note = "hello";
    JsonText extra{R"({"a":[1,2]})"};
    f32 angle = 0.5f;
    Vec3 tint{1.0f};
    Vec4 glow{0.0f, 0.0f, 0.0f, 1.0f};
    u32 layer = 3;
    Entity target;
};

// Version 2 of a type whose version 1 called "brightness" "power" and stored "angle" in degrees.
struct Lamp
{
    f32 brightness = 1.0f;
    f32 angle = 0.0f;
};

void reflectJsonTypes()
{
    reflectStruct<Point>("test.Point")
        .field("position", &Point::position, {.unit = Unit::Meters})
        .field("weight", &Point::weight, {.min = 0.0})
        .field("label", &Point::label);
    reflectStruct<Route>("test.Route")
        .field("points", &Route::points)
        .field("ends", &Route::ends)
        .field("pivot", &Route::pivot);
    reflect<Gadget>("test.Gadget")
        .field("tiny", &Gadget::tiny)
        .field("small", &Gadget::small)
        .field("byte", &Gadget::byte)
        .field("word", &Gadget::word)
        .field("big", &Gadget::big)
        .field("tinyEnum", &Gadget::tinyEnum)
        .field("mediumEnum", &Gadget::mediumEnum)
        .field("wideEnum", &Gadget::wideEnum)
        .field("hugeEnum", &Gadget::hugeEnum)
        .field("hugeSignedEnum", &Gadget::hugeSignedEnum)
        .field("layers", &Gadget::layers)
        .field("point", &Gadget::point)
        .field("weights", &Gadget::weights, {.min = 0.0})
        .field("numbers", &Gadget::numbers)
        .field("routes", &Gadget::routes)
        .field("limit", &Gadget::limit, {.unit = Unit::Seconds, .min = 0.0})
        .field("note", &Gadget::note, {.hint = FieldHint::Multiline})
        .field("extra", &Gadget::extra)
        .field("angle", &Gadget::angle,
               {.unit = Unit::Radians, .displayUnit = Unit::Degrees, .hint = FieldHint::Angle})
        .field("tint", &Gadget::tint, {.hint = FieldHint::Color})
        .field("glow", &Gadget::glow, {.hint = FieldHint::HdrColor})
        .field("layer", &Gadget::layer, {.min = 0.0, .max = 15.0, .hint = FieldHint::Layer})
        .field("target", &Gadget::target);
    reflect<Lamp>("test.Lamp", 2)
        .field("brightness", &Lamp::brightness, {.renamedFrom = "power"})
        .field("angle", &Lamp::angle, {.unit = Unit::Radians, .hint = FieldHint::Angle})
        .migrate(1, [](JsonMutDocument& doc, JsonMutValue object) {
            const JsonMutValue degrees = object.find("angle");
            if (const std::optional<f64> value = degrees.asF64())
            {
                object.set("angle", doc.makeF64(*value * std::numbers::pi / 180.0));
            }
        });
}

const TypeReflection& gadgetType()
{
    return *ComponentRegistry::instance().reflection(componentId<Gadget>());
}

Gadget filledGadget()
{
    Gadget gadget;
    gadget.tiny = std::numeric_limits<i8>::min();
    gadget.small = std::numeric_limits<i16>::max();
    gadget.byte = std::numeric_limits<u8>::max();
    gadget.word = 1;
    gadget.big = std::numeric_limits<u64>::max();
    gadget.tinyEnum = Tiny::Low;
    gadget.mediumEnum = Medium::A;
    gadget.wideEnum = Wide::Positive;
    gadget.hugeEnum = static_cast<Huge>(7); // no name: kept as a number
    gadget.hugeSignedEnum = HugeSigned::Negative;
    gadget.layers = static_cast<Layers>(2 | 8); // Water and a bit without a name
    gadget.point = Point{Vec3(1.0f, 2.0f, 3.0f), 0.25f, "origin"};
    gadget.weights = {0.5f, 0.0f, 9.0f};
    gadget.numbers = {-1, 0, 7, 9};
    Route route;
    route.points = {Point{Vec3(1.0f), 2.0f, "a"}, Point{Vec3(-1.0f), 3.0f, "b"}};
    route.ends[1].label = "end";
    route.pivot = Point{Vec3(0.5f), 0.0f, "pivot"};
    gadget.routes = {route, Route{}};
    gadget.limit = 2.5f;
    gadget.note.reset();
    gadget.extra.text = R"({"nested":{"list":[true,null,"x"]},"n":-1.5})";
    gadget.angle = 1.25f;
    gadget.tint = Vec3(0.25f, 0.5f, 0.75f);
    gadget.glow = Vec4(4.0f, 2.0f, 1.0f, 0.5f);
    gadget.layer = 15;
    gadget.target = Entity{12, 3};
    return gadget;
}

std::string written(const TypeReflection& type, const void* object, const ValueWriteOptions& options = {})
{
    JsonMutDocument doc;
    auto value = writeValue(doc, type, object, options);
    INFO((value ? std::string() : value.error().pointer + " " + value.error().message));
    REQUIRE(value.hasValue());
    doc.setRoot(*value);
    std::string text;
    REQUIRE(doc.write(text, JsonWriteOptions{.pretty = false}).hasValue());
    return text;
}

JsonDocument parsed(std::string_view text)
{
    auto doc = JsonDocument::parse(text);
    INFO((doc ? std::string() : doc.error().message));
    REQUIRE(doc.hasValue());
    return std::move(*doc);
}

// Reads @p json into a default Gadget and returns the result.
ValueReadResult readGadget(Gadget& gadget, std::string_view json, const ValueReadOptions& options = {})
{
    const JsonDocument doc = parsed(json);
    return readValue(gadgetType(), &gadget, doc.root(), options);
}

std::string firstError(const ValueReadResult& result)
{
    return result.errors.empty() ? std::string("(none)")
                                 : result.errors.front().pointer + ": " + result.errors.front().message;
}

} // namespace json_test

using namespace json_test;

TEST_CASE("every field type survives a JSON round trip")
{
    reflectJsonTypes();
    const Gadget source = filledGadget();
    const std::string text = written(gadgetType(), &source);

    Gadget copy;
    const ValueReadResult result = readGadget(copy, text);
    INFO(firstError(result));
    REQUIRE(result.ok());
    CHECK(result.unknownKeys.empty());
    CHECK(valuesEqual(*ComponentRegistry::instance().reflection(componentId<Gadget>())->field("routes"),
                      &copy.routes, &source.routes));
    for (const FieldInfo& field : gadgetType().fields)
    {
        CAPTURE(field.name);
        CHECK(fieldsEqual(&copy, &source, field));
    }
    CHECK(copy.tiny == source.tiny);
    CHECK(copy.big == source.big);
    CHECK(copy.hugeEnum == source.hugeEnum);
    CHECK(copy.layers == source.layers);
    REQUIRE(copy.routes.size() == 2);
    CHECK(copy.routes[0].points[1].label == "b");
    CHECK(copy.routes[0].pivot->label == "pivot");
    CHECK_FALSE(copy.routes[1].pivot.has_value());
    CHECK_FALSE(copy.note.has_value());
    CHECK(copy.extra == source.extra);
    CHECK(copy.target == source.target);
    CHECK(written(gadgetType(), &copy) == text); // and the text is stable
}

TEST_CASE("values are written in their documented JSON forms")
{
    reflectJsonTypes();
    const Gadget gadget = filledGadget();
    const JsonDocument doc = parsed(written(gadgetType(), &gadget));
    const JsonValue root = doc.root();
    CHECK(root.find("big").asString() == "18446744073709551615"); // u64 as a decimal string
    CHECK(root.find("tinyEnum").asString() == "Low");
    CHECK(root.find("hugeEnum").asU64() == 7u);
    CHECK(root.find("hugeSignedEnum").asString() == "Negative");
    CHECK(root.find("layers").dump() == R"(["Water",8])");
    CHECK(root.find("note").isNull());
    CHECK(root.find("limit").asF64() == 2.5);
    CHECK(root.find("extra").dump() == R"({"nested":{"list":[true,null,"x"]},"n":-1.5})");
    CHECK(root.find("point").dump() == R"({"position":[1.0,2.0,3.0],"weight":0.25,"label":"origin"})");
    CHECK(root.pointer("/routes/0/ends/1/label").asString() == "end");
    CHECK(root.find("target").asString() == std::to_string(Entity{12, 3}.toBits()));

    Gadget empty = gadget;
    empty.target = kNullEntity;
    empty.layers = Layers::None;
    const JsonDocument second = parsed(written(gadgetType(), &empty, {.u64AsString = false}));
    CHECK(second.root().find("target").isNull());
    CHECK(second.root().find("layers").dump() == "[]");
    CHECK(second.root().find("big").asU64() == std::numeric_limits<u64>::max());
}

TEST_CASE("enum fields of every size and sign round-trip through names and numbers")
{
    reflectJsonTypes();
    Gadget gadget;
    const ValueReadResult result =
        readGadget(gadget, R"({"tinyEnum": -3, "mediumEnum": "B", "wideEnum": -100000,
                                                          "hugeEnum": 4611686018427387904,
                                                          "hugeSignedEnum": -1099511627776})");
    INFO(firstError(result));
    REQUIRE(result.ok());
    CHECK(gadget.tinyEnum == Tiny::Low);
    CHECK(gadget.mediumEnum == Medium::B);
    CHECK(gadget.wideEnum == Wide::Negative);
    CHECK(gadget.hugeEnum == Huge::Big);
    CHECK(gadget.hugeSignedEnum == HugeSigned::Negative);
    const TypeReflection& type = gadgetType();
    CHECK(readEnumField(&gadget, *type.field("tinyEnum")) == -3);
    CHECK(readEnumField(&gadget, *type.field("mediumEnum")) == 60000);
    CHECK(readEnumField(&gadget, *type.field("hugeEnum")) == i64{1} << 62);
    CHECK(type.field("layers")->type == FieldType::Flags);
    CHECK(type.field("tinyEnum")->enumSize == 1);
    CHECK(type.field("hugeSignedEnum")->enumSize == 8);
}

TEST_CASE("merge mode leaves other fields untouched and replace mode resets them")
{
    reflectJsonTypes();
    Gadget gadget = filledGadget();
    ValueReadResult result =
        readGadget(gadget, R"({"tiny": 5, "point": {"weight": 4}})", {.mode = JsonReadMode::Merge});
    REQUIRE(result.ok());
    CHECK(gadget.tiny == 5);
    CHECK(gadget.point.weight == 4.0f);
    CHECK(gadget.point.label == "origin"); // nested structs merge too
    CHECK(gadget.small == std::numeric_limits<i16>::max());
    CHECK(gadget.routes.size() == 2);

    result = readGadget(gadget, R"({"tiny": 6})");
    REQUIRE(result.ok());
    CHECK(gadget.tiny == 6);
    CHECK(gadget.small == -300);
    CHECK(gadget.point.label == "p");
    CHECK(gadget.routes.empty());
    CHECK(gadget.note == "hello");
}

TEST_CASE("a failed read leaves the value unchanged and reports every problem by pointer")
{
    reflectJsonTypes();
    Gadget gadget = filledGadget();
    const Gadget before = gadget;
    const ValueReadResult result =
        readGadget(gadget, R"({"tiny": 1, "point": {"position": [1, "x", 3]}, "weights": [1, -2, 3],
                              "routes": [{"points": [{"weight": true}]}], "layer": 16, "unknown": 1,
                              "layers": ["Ground", "Fire"], "numbers": [1, 2.5]})",
                   {.mode = JsonReadMode::Merge});
    CHECK_FALSE(result.ok());
    REQUIRE(result.errors.size() == 6);
    CHECK(result.errors[0].pointer == "/point/position/1");
    CHECK(result.errors[0].message == "must be a number");
    CHECK(result.errors[1].pointer == "/weights/1");
    CHECK(result.errors[1].message == "must be at least 0");
    CHECK(result.errors[2].pointer == "/routes/0/points/0/weight");
    CHECK(result.errors[2].message == "must be a number");
    CHECK(result.errors[3].pointer == "/layer");
    CHECK(result.errors[3].message == "must be at most 15");
    CHECK(result.errors[4].pointer == "/layers/1");
    CHECK(result.errors[4].message == "has unknown value 'Fire'");
    CHECK(result.errors[5].pointer == "/numbers/1");
    CHECK(result.errors[5].message == "must be an integer");
    REQUIRE(result.unknownKeys.size() == 1);
    CHECK(result.unknownKeys[0] == "/unknown");
    for (const FieldInfo& field : gadgetType().fields)
    {
        CAPTURE(field.name);
        CHECK(fieldsEqual(&gadget, &before, field)); // nothing changed, not even "tiny"
    }
}

TEST_CASE("integers are range-checked and u64 values are read from strings and numbers")
{
    reflectJsonTypes();
    struct Case
    {
        std::string_view json;
        std::string_view error;
    };
    const Case cases[] = {
        {R"({"tiny": 128})", "/tiny: is out of range"},
        {R"({"tiny": -129})", "/tiny: is out of range"},
        {R"({"byte": -1})", "/byte: must not be negative"},
        {R"({"word": 65536})", "/word: is out of range"},
        {R"({"small": 18446744073709551615})", "/small: is out of range"},
        {R"({"big": "12x"})", "/big: must be an integer or a decimal string"},
        {R"({"big": ""})", "/big: must be an integer or a decimal string"},
        {R"({"big": 1.5})", "/big: must be an integer"},
        {R"({"limit": -1})", "/limit: must be at least 0"},
        {R"({"weights": [1, 2]})", "/weights: must be an array of 3 values"},
        {R"({"target": "x"})", "/target: must be null or an entity handle"},
        {R"({"point": 1})", "/point: must be an object"},
        {R"({"numbers": {}})", "/numbers: must be an array"},
        {R"({"layers": "Air"})", "/layers: must be an array of value names"},
        {R"({"note": 5})", "/note: must be a string"},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.json);
        Gadget gadget;
        CHECK(firstError(readGadget(gadget, c.json)) == c.error);
    }

    Gadget gadget;
    REQUIRE(readGadget(gadget, R"({"big": "18446744073709551615", "layers": 3})").ok());
    CHECK(gadget.big == std::numeric_limits<u64>::max());
    CHECK(gadget.layers == static_cast<Layers>(3));
    REQUIRE(readGadget(gadget, R"({"big": 18446744073709551614})").ok());
    CHECK(gadget.big == std::numeric_limits<u64>::max() - 1);
}

TEST_CASE("unit-tagged numbers are converted to the field's unit")
{
    reflectJsonTypes();
    Gadget gadget;
    ValueReadResult result = readGadget(gadget, R"({"angle": {"deg": 90}, "limit": {"s": 3}})");
    INFO(firstError(result));
    REQUIRE(result.ok());
    CHECK(gadget.angle == doctest::Approx(std::numbers::pi / 2.0));
    CHECK(gadget.limit == 3.0f);

    CHECK(firstError(readGadget(gadget, R"({"angle": {"m": 1}})")) ==
          "/angle: cannot be given in m (its unit is rad)");
    CHECK(firstError(readGadget(gadget, R"({"tint": {"deg": 1}})")) ==
          "/tint: must be an array of 3 numbers");
    CHECK(firstError(readGadget(gadget, R"({"point": {"weight": {"kg": 1}}})")) ==
          "/point/weight: has no unit; write a plain number");
    CHECK(firstError(readGadget(gadget, R"({"angle": {"furlong": 1}})")) ==
          "/angle: must be a number or a unit-tagged number such as {\"deg\": 60}");
    CHECK(firstError(readGadget(gadget, R"({"angle": {"deg": 1, "rad": 2}})")) == "/angle: must be a number");
}

TEST_CASE("colour fields accept sRGB hex strings")
{
    reflectJsonTypes();
    Gadget gadget;
    const ValueReadResult result = readGadget(gadget, R"({"tint": "#ff8000", "glow": "#FFFFFF80"})");
    INFO(firstError(result));
    REQUIRE(result.ok());
    CHECK(gadget.tint.r == 1.0f);
    CHECK(gadget.tint.g == doctest::Approx(0.21586f).epsilon(0.001)); // sRGB 128 is linear 0.216
    CHECK(gadget.tint.b == 0.0f);
    CHECK(gadget.glow.r == 1.0f);
    CHECK(gadget.glow.a == doctest::Approx(128.0f / 255.0f)); // alpha is linear
    REQUIRE(readGadget(gadget, R"({"glow": "#000000"})").ok());
    CHECK(gadget.glow.a == 1.0f);

    CHECK(firstError(readGadget(gadget, R"({"tint": "#ff800080"})")) ==
          "/tint: must be a colour such as \"#ff8000\"");
    CHECK(firstError(readGadget(gadget, R"({"tint": "#ff80zz"})")) ==
          "/tint: must be a colour such as \"#ff8000\"");
    CHECK(firstError(readGadget(gadget, R"({"point": {"position": "#ff8000"}})")) ==
          "/point/position: must be an array of 3 numbers");
}

TEST_CASE("entity fields go through the entity resolver")
{
    reflectJsonTypes();
    struct NamedEntities final : EntityResolver
    {
        JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
        {
            return entity.isNull() ? doc.makeNull() : doc.makeString("e" + std::to_string(entity.index));
        }
        Expected<Entity, std::string> readEntity(JsonValue value) const override
        {
            const std::optional<std::string_view> text = value.asString();
            if (!text || !text->starts_with('e'))
            {
                return unexpected(std::string("must be a name such as \"e1\""));
            }
            return Entity{static_cast<u32>(std::stoul(std::string(text->substr(1)))), 0};
        }
    };
    const NamedEntities names;
    Gadget gadget;
    gadget.target = Entity{42, 0};
    const std::string text = written(gadgetType(), &gadget, {.entities = &names});
    CHECK(text.find(R"("target":"e42")") != std::string::npos);
    Gadget copy;
    const JsonDocument doc = parsed(text);
    REQUIRE(readValue(gadgetType(), &copy, doc.root(), {.entities = &names}).ok());
    CHECK(copy.target == Entity{42, 0});
    CHECK(firstError(readGadget(copy, R"({"target": 3})", {.entities = &names})) ==
          "/target: must be a name such as \"e1\"");

    // Without a resolver, handles are process-local bits.
    REQUIRE(readGadget(copy, R"({"target": 4294967298})").ok());
    CHECK(copy.target == Entity{2, 1});
    REQUIRE(readGadget(copy, R"({"target": null})").ok());
    CHECK(copy.target.isNull());
}

TEST_CASE("a component's JSON is the same as in scene files")
{
    reflectJsonTypes();
    reflection_test::reflectTestComponents();
    reflectCoreComponents();
    World world;
    Transform transform;
    transform.position = Vec3(1.0f, -2.0f, 0.125f);
    transform.rotation = glm::normalize(Quat(0.9f, 0.1f, -0.3f, 0.2f));
    reflection_test::Everything everything;
    everything.u64Value = 77;
    everything.team = reflection_test::Team::Blue;
    const Entity entity = world.create(transform, everything);
    world.get<reflection_test::Everything>(entity).target = entity;
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    const JsonDocument scene = parsed(saved.json);

    // Scene files write references as ids, like the editor's resolver.
    const GuidEntityResolver ids(world);
    const ValueWriteOptions sceneOptions{.entities = &ids};
    const ComponentRegistry& registry = ComponentRegistry::instance();
    CHECK(written(*registry.reflection(componentId<Transform>()), &world.get<Transform>(entity),
                  sceneOptions) == scene.root().pointer("/entities/0/components/illusion.Transform").dump());
    CHECK(written(*registry.reflection(componentId<reflection_test::Everything>()),
                  &world.get<reflection_test::Everything>(entity),
                  sceneOptions) == scene.root().pointer("/entities/0/components/test.Everything").dump());
}

TEST_CASE("values are read and written by dotted paths")
{
    reflectJsonTypes();
    Gadget gadget = filledGadget();
    const TypeReflection& type = gadgetType();
    JsonMutDocument doc;
    const auto get = [&](std::string_view path) {
        auto value = getPath(doc, type, &gadget, path);
        return value ? value->dump() : "error " + value.error().pointer + ": " + value.error().message;
    };
    CHECK(get("tiny") == "-128");
    CHECK(get("point.label") == R"("origin")");
    CHECK(get("routes.0.points.1.weight") == "3.0");
    CHECK(get("routes.0.pivot.label") == R"("pivot")"); // optionals are looked through
    CHECK(get("routes.0.ends.1") == R"({"position":[0.0,0.0,0.0],"weight":1.0,"label":"end"})");
    CHECK(get("weights.2") == "9.0");
    CHECK(get("routes.1.pivot.label") == "error /routes/1/pivot: is empty");
    CHECK(get("routes.5") == "error /routes: has no element 5 (size 2)");
    CHECK(get("routes.x") == "error /routes: is an array; 'x' is not an index");
    CHECK(get("nope") == "error : has no field 'nope'");
    CHECK(get("tiny.x") == "error /tiny: has no field 'x'");
    CHECK(get("point..label") == "error /point: has an empty path segment");
    CHECK(get("").starts_with(R"({"tiny":-128,)"));

    const auto set = [&](std::string_view path, std::string_view json,
                         JsonReadMode mode = JsonReadMode::Merge) {
        const JsonDocument value = parsed(json);
        auto result = setPath(type, &gadget, path, value.root(), {.mode = mode});
        return result ? std::string("ok") : result.error().pointer + ": " + result.error().message;
    };
    CHECK(set("routes.0.points.1.weight", "8") == "ok");
    CHECK(gadget.routes[0].points[1].weight == 8.0f);
    CHECK(set("point", R"({"weight": 6})") == "ok");
    CHECK(gadget.point.label == "origin"); // merged
    CHECK(set("point", R"({"weight": 6})", JsonReadMode::Replace) == "ok");
    CHECK(gadget.point.label == "p");
    CHECK(set("angle", R"({"deg": 180})") == "ok");
    CHECK(gadget.angle == doctest::Approx(std::numbers::pi));
    CHECK(set("layer", "99") == "/layer: must be at most 15");
    CHECK(gadget.layer == 15);
    CHECK(set("point", R"({"weight": 1, "position": [0, 0]})") ==
          "/point/position: must be an array of 3 numbers");
    CHECK(gadget.point.weight == 6.0f); // unchanged
    CHECK(set("routes.1.pivot", R"({"label": "new"})") == "ok");
    REQUIRE(gadget.routes[1].pivot.has_value());
    CHECK(gadget.routes[1].pivot->label == "new");
    CHECK(set("", R"({"tiny": 2})", JsonReadMode::Merge) == "ok");
    CHECK(gadget.tiny == 2);
    CHECK(set("", R"({"tiny": "x"})") == "/tiny: must be an integer");
}

TEST_CASE("old data is renamed and migrated, newer data loads with a warning")
{
    reflectJsonTypes();
    const TypeReflection& type = *ComponentRegistry::instance().reflection(componentId<Lamp>());
    Lamp lamp;
    const JsonDocument v1 = parsed(R"({"power": 3, "angle": 90})");
    ValueReadResult result = readValue(type, &lamp, v1.root(), {.savedVersion = 1});
    INFO(firstError(result));
    REQUIRE(result.ok());
    CHECK(result.unknownKeys.empty());
    CHECK(lamp.brightness == 3.0f);
    CHECK(lamp.angle == doctest::Approx(std::numbers::pi / 2.0));

    // The current version is not migrated, but still accepts the old field name.
    result = readValue(type, &lamp, v1.root());
    REQUIRE(result.ok());
    CHECK(lamp.angle == 90.0f);
    CHECK(lamp.brightness == 3.0f);

    const JsonDocument v3 = parsed(R"({"brightness": 2, "colour": "red"})");
    result = readValue(type, &lamp, v3.root(), {.savedVersion = 3});
    REQUIRE(result.ok());
    CHECK(lamp.brightness == 2.0f);
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0] == "'test.Lamp' was saved with version 3, newer than version 2; fields this "
                                "version does not know are skipped");
    CHECK(result.unknownKeys == std::vector<std::string>{"/colour"});

    // Scene files pass the saved version on.
    World world;
    const SceneLoadResult loaded = loadScene(world, R"({"format": "illusion.scene", "version": 1,
        "types": {"test.Lamp": 1}, "entities": [{"test.Lamp": {"power": 5, "angle": 180}}]})");
    INFO(loaded.error);
    REQUIRE(loaded.ok());
    CHECK(world.get<Lamp>(loaded.entities[0]).brightness == 5.0f);
    CHECK(world.get<Lamp>(loaded.entities[0]).angle == doctest::Approx(std::numbers::pi));
}

TEST_CASE("struct defaults equal default-constructed values")
{
    reflectJsonTypes();
    const StructRegistry& structs = ComponentRegistry::instance().structs();
    const StructReflection* route = structs.reflection(structs.find(typeHash<Route>()));
    REQUIRE(route != nullptr);
    CHECK(route->name == "test.Route");
    Route value;
    value.points.resize(3);
    const JsonDocument empty = parsed("{}");
    REQUIRE(readValue(*route, &value, empty.root()).ok());
    const Route fresh;
    for (const FieldInfo& field : route->fields)
    {
        CAPTURE(field.name);
        CHECK(fieldsEqual(&value, &fresh, field));
        CHECK(defaultValue(*route, field) == readField(&fresh, field));
    }
    const TypeReflection& gadget = gadgetType();
    const Gadget defaults;
    for (const FieldInfo& field : gadget.fields)
    {
        CAPTURE(field.name);
        CHECK(defaultValue(gadget, field) == readField(&defaults, field));
    }
}

TEST_CASE("malformed JSON never asserts")
{
    reflectJsonTypes();
    constexpr std::string_view kValues[] = {"null",
                                            "true",
                                            "0",
                                            "-1",
                                            "1.5",
                                            "1e300",
                                            "\"\"",
                                            "\"#ff00ff\"",
                                            "[]",
                                            "[1, 2, 3]",
                                            "[[1], {}]",
                                            "{}",
                                            "{\"deg\": 1}",
                                            "{\"points\": [1]}",
                                            "18446744073709551615",
                                            "-9223372036854775808"};
    const TypeReflection& type = gadgetType();
    usize failures = 0;
    for (const FieldInfo& field : type.fields)
    {
        for (const std::string_view value : kValues)
        {
            const std::string json =
                std::string("{\"") + std::string(field.name) + "\": " + std::string(value) + "}";
            CAPTURE(json);
            Gadget gadget;
            const ValueReadResult result = readGadget(gadget, json);
            if (!result.ok())
            {
                ++failures;
            }
            CHECK((result.ok() || !result.errors.front().message.empty()));
            const JsonDocument parsedValue = parsed(value);
            (void)setPath(type, &gadget, field.name, parsedValue.root());
        }
    }
    CHECK(failures > 0);
    // The top level must be an object.
    Gadget gadget;
    CHECK(firstError(readGadget(gadget, "[1]")) == ": must be an object");
}

TEST_CASE("values that JSON cannot store fail with a pointer")
{
    reflectJsonTypes();
    Gadget gadget;
    Route route;
    route.points.resize(2);
    route.points[1].weight = std::numeric_limits<f32>::infinity();
    gadget.routes.push_back(route);
    JsonMutDocument doc;
    auto value = writeValue(doc, gadgetType(), &gadget);
    REQUIRE_FALSE(value.hasValue());
    CHECK(value.error().pointer == "/routes/0/points/1/weight");
    CHECK(value.error().message == "is not finite");

    gadget.routes.clear();
    gadget.extra.text = "{oops";
    value = writeValue(doc, gadgetType(), &gadget);
    REQUIRE_FALSE(value.hasValue());
    CHECK(value.error().pointer == "/extra");
    CHECK(value.error().message == "holds text that is not valid JSON");
}

TEST_CASE("tags read and write as empty objects")
{
    reflection_test::reflectTestComponents();
    const TypeReflection& tag =
        *ComponentRegistry::instance().reflection(componentId<reflection_test::Selected>());
    JsonMutDocument doc;
    auto value = writeValue(doc, tag, nullptr);
    REQUIRE(value.hasValue());
    CHECK(value->dump() == "{}");
    const JsonDocument json = parsed(R"({"extra": 1})");
    const ValueReadResult result = readValue(tag, nullptr, json.root());
    CHECK(result.ok());
    CHECK(result.unknownKeys == std::vector<std::string>{"/extra"});
}

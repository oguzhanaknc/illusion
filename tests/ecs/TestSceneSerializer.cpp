#include "ReflectedComponents.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <initializer_list>
#include <limits>
#include <string>

using namespace illusion;
using namespace reflection_test;

namespace
{

void reflectAll()
{
    reflectTestComponents();
    reflectCoreComponents();
}

std::string sceneWith(std::string_view entities, std::string_view types = "{}")
{
    return std::string(R"({"format": "illusion.scene", "version": 1, "types": )")
        .append(types)
        .append(R"(, "entities": )")
        .append(entities)
        .append("}");
}

} // namespace

TEST_CASE("scenes round-trip every reflected field type")
{
    reflectAll();
    World source;
    Everything value;
    value.flag = true;
    value.i32Value = std::numeric_limits<i32>::min();
    value.u32Value = std::numeric_limits<u32>::max();
    value.i64Value = std::numeric_limits<i64>::min();
    value.u64Value = std::numeric_limits<u64>::max();
    value.f32Value = 0.3f;
    value.f64Value = 1.0 / 3.0;
    value.vec2 = Vec2(-1.5f, 1e-7f);
    value.vec3 = Vec3(1e30f, -0.0f, 3.25f);
    value.vec4 = Vec4(0.1f, 0.2f, 0.3f, 0.4f);
    value.quat = glm::normalize(Quat(0.9f, 0.1f, -0.3f, 0.2f));
    value.text = "quote \" backslash \\ newline \n unicode \xC3\xA7";
    value.team = Team::Red;
    value.flags = Flags::Visible;
    const Entity first = source.create(value, Selected{});
    source.get<Everything>(first).target = first; // refers to itself
    source.create(Follow{first, 5.5f}, Transform{});

    const SceneSaveResult saved = saveScene(source);
    REQUIRE(saved.ok());
    CHECK(saved.entities == 2);
    CHECK(saved.skippedComponents == 0);

    World loaded;
    loaded.create(); // existing entities are kept
    const SceneLoadResult result = loadScene(loaded, saved.json);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.warnings.empty());
    REQUIRE(result.entities.size() == 2);
    CHECK(loaded.aliveCount() == 3);

    const Everything& copy = loaded.get<Everything>(result.entities[0]);
    CHECK(copy.flag);
    CHECK(copy.i32Value == value.i32Value);
    CHECK(copy.u32Value == value.u32Value);
    CHECK(copy.i64Value == value.i64Value);
    CHECK(copy.u64Value == value.u64Value);
    CHECK(copy.f32Value == value.f32Value); // floats round-trip exactly
    CHECK(copy.f64Value == value.f64Value);
    CHECK(copy.vec2 == value.vec2);
    CHECK(copy.vec3 == value.vec3);
    CHECK(copy.vec4 == value.vec4);
    CHECK(copy.quat == value.quat);
    CHECK(copy.text == value.text);
    CHECK(copy.target == result.entities[0]);
    CHECK(copy.team == Team::Red);
    CHECK(copy.flags == Flags::Visible);
    CHECK(loaded.has<Selected>(result.entities[0]));
    CHECK(loaded.get<Follow>(result.entities[1]).leader == result.entities[0]);
    CHECK(loaded.get<Follow>(result.entities[1]).distance == 5.5f);
    CHECK(loaded.has<Transform>(result.entities[1]));

    // Saving the loaded entities again gives the same text.
    CHECK(saveScene(loaded, result.entities).json == saved.json);
}

TEST_CASE("scene files are readable, stable JSON")
{
    reflectAll();
    World world;
    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 0.5f);
    const Entity leader = world.create(transform);
    world.create(Follow{leader, 0.1f});
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json == R"({
  "format": "illusion.scene",
  "version": 1,
  "types": {
    "illusion.Transform": 1,
    "test.Follow": 1
  },
  "entities": [
    {
      "illusion.Transform": {
        "position": [1.0, 2.0, 0.5],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      }
    },
    {
      "test.Follow": {
        "leader": 0,
        "distance": 0.1
      }
    }
  ]
}
)");
}

TEST_CASE("saving a subset nulls references to entities outside it")
{
    reflectAll();
    World world;
    const Entity leader = world.create(Transform{});
    const Entity follower = world.create(Follow{leader, 1.0f}, Unreflected{3});
    const Entity list[] = {follower};
    const SceneSaveResult saved = saveScene(world, list);
    REQUIRE(saved.ok());
    CHECK(saved.entities == 1);
    CHECK(saved.skippedComponents == 1); // Unreflected
    CHECK(saved.json.find("\"leader\": null") != std::string::npos);

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 1);
    CHECK(loaded.get<Follow>(result.entities[0]).leader.isNull());
    CHECK_FALSE(loaded.has<Unreflected>(result.entities[0]));
}

TEST_CASE("loading tolerates missing and unknown fields and types")
{
    reflectAll();
    World world;
    const SceneLoadResult result = loadScene(world, sceneWith(R"([
            {"test.Follow": {"distance": 4, "speed": 2}, "game.Removed": {"x": 1}},
            {"test.Follow": {"speed": 3}, "game.Removed": {}}
        ])",
                                                              R"({"test.Follow": 0, "game.Removed": 1})"));
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 2);
    const Follow& follow = world.get<Follow>(result.entities[0]);
    CHECK(follow.distance == 4.0f); // integers are accepted for floats
    CHECK(follow.leader.isNull());  // missing fields keep their defaults
    CHECK(world.get<Follow>(result.entities[1]).distance == 2.0f);
    // Each problem is reported once.
    REQUIRE(result.warnings.size() == 3);
    CHECK(result.warnings[0] == "'test.Follow' was saved with version 0, loading as version 1");
    CHECK(result.warnings[1] == "unknown field 'test.Follow.speed' skipped");
    CHECK(result.warnings[2] == "unknown component type 'game.Removed' skipped");
}

TEST_CASE("enum fields accept names and numbers")
{
    reflectAll();
    World world;
    const SceneLoadResult result =
        loadScene(world, sceneWith(R"([{"test.Everything": {"team": "Blue", "flags": 1}},
                                       {"test.Everything": {"team": 99}}])"));
    REQUIRE(result.ok());
    CHECK(world.get<Everything>(result.entities[0]).team == Team::Blue);
    CHECK(world.get<Everything>(result.entities[0]).flags == Flags::Visible);
    // Values without a name survive a round trip as numbers.
    CHECK(static_cast<int>(world.get<Everything>(result.entities[1]).team) == 99);
    const SceneSaveResult saved = saveScene(world, std::span(result.entities).subspan(1));
    CHECK(saved.json.find("\"team\": 99") != std::string::npos);
}

TEST_CASE("invalid scenes fail without changing the world")
{
    reflectAll();
    World world;
    const Entity existing = world.create(Transform{});

    struct Case
    {
        std::string json;
        std::string error;
    };
    const Case cases[] = {
        {"{\"format\": \"illusion.scene\",\n  \"version\": 1, oops}", "invalid JSON at line 2, column 17"},
        {"[]", "the scene is not a JSON object"},
        {R"({"format": "other", "version": 1, "entities": []})", "not an illusion scene"},
        {R"({"format": "illusion.scene", "version": 2, "entities": []})", "unsupported scene version"},
        {R"({"format": "illusion.scene", "version": 1, "entities": {}})", "\"entities\" must be an array"},
        {sceneWith("[]", "[]"), "\"types\" must be an object"},
        {sceneWith("[]", R"({"test.Follow": -1})"), "version of type 'test.Follow'"},
        {sceneWith("[1]"), "entity 0 is not an object"},
        {sceneWith(R"([{}, {"test.Follow": 3}])"), "entity 1: component 'test.Follow' must be an object"},
        {sceneWith(R"([{"test.Follow": {}, "test.Follow": {}}])"),
         "entity 0: component 'test.Follow' appears twice"},
        {sceneWith(R"([{"test.Follow": {"leader": 1}}])"),
         "entity 0: field 'leader' of 'test.Follow' must be null or the index of an entity in this scene"},
        {sceneWith(R"([{"test.Follow": {"distance": "far"}}])"),
         "field 'distance' of 'test.Follow' must be a number"},
        {sceneWith(R"([{"test.Everything": {"flag": 1}}])"), "must be true or false"},
        {sceneWith(R"([{"test.Everything": {"u32Value": -1}}])"), "must not be negative"},
        {sceneWith(R"([{"test.Everything": {"u32Value": 4294967296}}])"), "is out of range"},
        {sceneWith(R"([{"test.Everything": {"i32Value": -2147483649}}])"), "is out of range"},
        {sceneWith(R"([{"test.Everything": {"i32Value": 1.5}}])"), "must be an integer"},
        {sceneWith(R"([{"test.Everything": {"vec3": [1, 2]}}])"), "must be an array of 3 numbers"},
        {sceneWith(R"([{"test.Everything": {"vec3": [1, 2, 1e39]}}])"),
         "field 'vec3' of 'test.Everything' is out of range"},
        {sceneWith(R"([{"test.Everything": {"f32Value": -1e300}}])"),
         "field 'f32Value' of 'test.Everything' is out of range"},
        {sceneWith(R"([{"test.Everything": {"quat": [1, 2, 3, "w"]}}])"),
         "field 'quat' of 'test.Everything' must be a number (at /quat/3)"},
        {sceneWith(R"([{"test.Everything": {"quat": [1, 2, 3]}}])"), "must be an array of 4 numbers"},
        {sceneWith(R"([{"test.Everything": {"text": 5}}])"), "must be a string"},
        {sceneWith(R"([{"test.Everything": {"team": "Green"}}])"), "has unknown value 'Green'"},
        {sceneWith(R"([{"test.Everything": {"team": true}}])"), "must be a value name"},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.json);
        const SceneLoadResult result = loadScene(world, c.json);
        CHECK_FALSE(result.ok());
        CHECK(result.error.find(c.error) != std::string::npos);
        CHECK(result.entities.empty());
        CHECK(result.warnings.empty());
        CHECK(world.aliveCount() == 1); // entities created before the error were removed
    }
    CHECK(world.isAlive(existing));
}

TEST_CASE("saving fails for values JSON cannot store")
{
    reflectAll();
    World world;
    Everything value;
    value.vec3.y = std::numeric_limits<f32>::infinity();
    const Entity entity = world.create(value);
    const SceneSaveResult saved = saveScene(world);
    CHECK_FALSE(saved.ok());
    CHECK(saved.error == "entity 0: field 'vec3' of 'test.Everything' is not finite");
    CHECK(saved.json.empty());

    world.get<Everything>(entity).vec3.y = 0.0f;
    world.get<Everything>(entity).f64Value = std::numeric_limits<f64>::quiet_NaN();
    CHECK(saveScene(world).error == "entity 0: field 'f64Value' of 'test.Everything' is not finite");
}

TEST_CASE("empty worlds save and load")
{
    reflectAll();
    World world;
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.entities == 0);
    const SceneLoadResult result = loadScene(world, saved.json);
    CHECK(result.ok());
    CHECK(result.entities.empty());
}

TEST_CASE("long strings round-trip")
{
    reflectAll();
    World world;
    Everything value;
    value.text = std::string(100000, 'x');
    world.create(value);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    CHECK(loaded.get<Everything>(result.entities[0]).text == value.text);
}

// --- The JSON writer reproduces scene files (M7e rebuilds the serializer on top of it)
// ----------------------

namespace
{

std::string rewritten(std::string_view json)
{
    auto doc = JsonDocument::parse(json);
    INFO((doc ? std::string() : doc.error().message));
    REQUIRE(doc.hasValue());
    std::string out;
    REQUIRE(doc->write(out).hasValue());
    return out;
}

} // namespace

TEST_CASE("the JSON writer rewrites scene documents byte for byte")
{
    // The format documented in docs/content/en/manual/scenes.md.
    constexpr std::string_view kDocumented = R"({
  "format": "illusion.scene",
  "version": 1,
  "types": {
    "illusion.Transform": 1,
    "game.Spinner": 1
  },
  "entities": [
    {
      "illusion.Transform": {
        "position": [0.0, 1.0, 0.0],
        "rotation": [0.0, 0.0, 0.0, 1.0],
        "scale": [1.0, 1.0, 1.0]
      },
      "game.Spinner": {
        "axis": [0.0, 1.0, 0.0],
        "speed": 2.5,
        "team": "Blue",
        "target": null
      }
    }
  ]
}
)";
    CHECK(rewritten(kDocumented) == kDocumented);

    reflectAll();
    World world;
    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 0.5f);
    const Entity leader = world.create(transform);
    world.create(Follow{leader, 0.1f});
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(rewritten(saved.json) == saved.json);
}

TEST_CASE("the JSON writer matches saveScene() for every field type")
{
    reflectAll();
    World world;
    Everything value;
    value.flag = true;
    value.i32Value = std::numeric_limits<i32>::min();
    value.u32Value = std::numeric_limits<u32>::max();
    value.i64Value = std::numeric_limits<i64>::min();
    value.u64Value = std::numeric_limits<u64>::max();
    value.f32Value = 0.3f;
    value.f64Value = 1.0 / 3.0;
    value.vec2 = Vec2(-1.5f, 1e-7f);
    value.vec3 = Vec3(1e30f, -0.0f, 3.25f);
    value.vec4 = Vec4(0.1f, 0.2f, 0.3f, 0.4f);
    value.quat = glm::normalize(Quat(0.9f, 0.1f, -0.3f, 0.2f));
    value.text = "quote \" backslash \\ newline \n tab \t control \x01 unicode \xC3\xA7 / slash";
    value.team = Team::Red;
    value.flags = static_cast<Flags>(9); // no name: written as a number
    const Entity first = world.create(value, Selected{});
    world.get<Everything>(first).target = first;
    world.create(Follow{first, 5.5f}, Transform{});
    world.create(); // an entity without components
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(rewritten(saved.json) == saved.json);
}

TEST_CASE("a scene built with JsonMutDocument matches saveScene()")
{
    reflectAll();
    World world;
    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 0.5f);
    const Entity leader = world.create(transform);
    world.create(Follow{leader, 0.1f});
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());

    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("format", doc.makeString("illusion.scene")).add("version", doc.makeU64(1));
    root.addObject("types").add("illusion.Transform", doc.makeU64(1)).add("test.Follow", doc.makeU64(1));
    JsonMutValue entities = root.addArray("entities");
    JsonMutValue transformObject = entities.pushObject().addObject("illusion.Transform");
    const auto vector = [&](std::initializer_list<f32> values) {
        JsonMutValue array = doc.makeArray();
        for (const f32 component : values)
        {
            array.push(doc.makeF32(component));
        }
        return array;
    };
    transformObject.add("position", vector({1.0f, 2.0f, 0.5f}))
        .add("rotation", vector({0.0f, 0.0f, 0.0f, 1.0f}))
        .add("scale", vector({1.0f, 1.0f, 1.0f}));
    entities.pushObject()
        .addObject("test.Follow")
        .add("leader", doc.makeU64(0))
        .add("distance", doc.makeF32(0.1f));

    std::string out;
    REQUIRE(doc.write(out).hasValue());
    CHECK(out == saved.json);
}

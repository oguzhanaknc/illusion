#include "ReflectedComponents.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <format>
#include <limits>
#include <string>

using namespace illusion;
using namespace reflection_test;

namespace scene_test
{

// Settings saved with a scene.
struct Look
{
    f32 exposure = 1.0f;
    Entity focus;
    std::string mood = "calm";
};

} // namespace scene_test

namespace
{

void reflectAll()
{
    reflectTestComponents();
    reflectCoreComponents();
    reflectStruct<scene_test::Look>("test.Look", 2)
        .field("exposure", &scene_test::Look::exposure)
        .field("focus", &scene_test::Look::focus)
        .field("mood", &scene_test::Look::mood);
}

std::string sceneWith(std::string_view entities, std::string_view types = "{}")
{
    return std::string(R"({"format": "illusion.scene", "version": 1, "types": )")
        .append(types)
        .append(R"(, "entities": )")
        .append(entities)
        .append("}");
}

std::string sceneV2(std::string_view entities, std::string_view types = "{}",
                    std::string_view settings = "{}")
{
    return std::string(R"({"format": "illusion.scene", "version": 2, "types": )")
        .append(types)
        .append(R"(, "settings": )")
        .append(settings)
        .append(R"(, "entities": )")
        .append(entities)
        .append("}");
}

Everything everyFieldType()
{
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
    return value;
}

// Re-writes a JSON document in the canonical layout.
std::string rewritten(std::string_view json)
{
    auto doc = JsonDocument::parse(json);
    INFO((doc ? std::string() : doc.error().message));
    REQUIRE(doc.hasValue());
    std::string out;
    REQUIRE(doc->write(out).hasValue());
    return out;
}

// A version 1 file, as saveScene() wrote it before ids existed.
constexpr std::string_view kVersion1 = R"({
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
)";

} // namespace

TEST_CASE("scenes round-trip every reflected field type")
{
    reflectAll();
    World source;
    const Entity first = source.create(everyFieldType(), Selected{}, Name{"First"});
    source.get<Everything>(first).target = first; // refers to itself
    const Entity second = source.create(Follow{first, 5.5f}, Transform{});
    GuidGenerator guids(7);
    CHECK(assignMissingGuids(source, guids) == 2);

    const SceneSaveResult saved = saveScene(source);
    REQUIRE(saved.ok());
    CHECK(saved.entities == 2);
    CHECK(saved.skippedComponents == 0);

    World loaded;
    loaded.create(); // existing entities are kept
    const SceneLoadResult result = loadScene(loaded, saved.json);
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.version == 2);
    CHECK(result.warnings.empty());
    REQUIRE(result.entities.size() == 2);
    CHECK(loaded.aliveCount() == 3);

    // Entities are saved in order of id; the file's ids are kept.
    const GuidEntityResolver ids(loaded);
    const Entity copyOfFirst = ids.find(source.get<EntityGuid>(first));
    const Entity copyOfSecond = ids.find(source.get<EntityGuid>(second));
    REQUIRE_FALSE(copyOfFirst.isNull());
    REQUIRE_FALSE(copyOfSecond.isNull());
    CHECK(result.fileIds[0].value < result.fileIds[1].value);
    for (usize i = 0; i < 2; ++i)
    {
        CHECK(loaded.get<EntityGuid>(result.entities[i]) == result.fileIds[i]);
    }

    const Everything value = everyFieldType();
    const Everything& copy = loaded.get<Everything>(copyOfFirst);
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
    CHECK(copy.target == copyOfFirst);
    CHECK(copy.team == Team::Red);
    CHECK(copy.flags == Flags::Visible);
    CHECK(loaded.has<Selected>(copyOfFirst));
    CHECK(loaded.get<Name>(copyOfFirst).value == "First");
    CHECK(loaded.get<Follow>(copyOfSecond).leader == copyOfFirst);
    CHECK(loaded.get<Follow>(copyOfSecond).distance == 5.5f);
    CHECK(loaded.has<Transform>(copyOfSecond));

    // Saving the loaded entities again gives the same text.
    CHECK(saveScene(loaded, {.subset = result.entities}).json == saved.json);
}

TEST_CASE("scene files are readable, stable JSON")
{
    reflectAll();
    World world;
    Transform transform;
    transform.position = Vec3(1.0f, 2.0f, 0.5f);
    const Entity leader = world.create(transform, Name{"Leader"}, EntityGuid{2});
    world.create(Follow{leader, 0.1f}, EntityGuid{1});
    world.create(EntityGuid{0xabcdef});
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json == R"({
  "format": "illusion.scene",
  "version": 2,
  "types": {
    "illusion.Name": 1,
    "illusion.Transform": 1,
    "test.Follow": 1
  },
  "settings": {},
  "entities": [
    {
      "id": "0000000000000001",
      "components": {
        "test.Follow": {
          "leader": "0000000000000002",
          "distance": 0.1
        }
      }
    },
    {
      "id": "0000000000000002",
      "components": {
        "illusion.Name": {
          "value": "Leader"
        },
        "illusion.Transform": {
          "position": [1.0, 2.0, 0.5],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "id": "0000000000abcdef",
      "components": {}
    }
  ]
}
)");
    CHECK(rewritten(saved.json) == saved.json);
}

TEST_CASE("version 1 files load with new ids and re-save as version 2 deterministically")
{
    reflectAll();
    World world;
    GuidGenerator guids(42);
    const SceneLoadResult result = loadScene(world, kVersion1, {.guids = &guids});
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.version == 1);
    CHECK(result.fileIds.empty());
    REQUIRE(result.entities.size() == 2);
    CHECK(world.get<Follow>(result.entities[1]).leader == result.entities[0]); // indices are remapped
    CHECK(toString(world.get<EntityGuid>(result.entities[0])) == "a759ea27d4727622");

    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json == R"({
  "format": "illusion.scene",
  "version": 2,
  "types": {
    "illusion.Transform": 1,
    "test.Follow": 1
  },
  "settings": {},
  "entities": [
    {
      "id": "a759ea27d4727622",
      "components": {
        "illusion.Transform": {
          "position": [1.0, 2.0, 0.5],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "id": "bdd732262feb6e95",
      "components": {
        "test.Follow": {
          "leader": "a759ea27d4727622",
          "distance": 0.1
        }
      }
    }
  ]
}
)");

    // The same seed gives the same file, and a second save of the version 2 file is byte-identical.
    World again;
    GuidGenerator sameSeed(42);
    REQUIRE(loadScene(again, kVersion1, {.guids = &sameSeed}).ok());
    CHECK(saveScene(again).json == saved.json);
    World reopened;
    REQUIRE(loadScene(reopened, saved.json).ok());
    CHECK(saveScene(reopened).json == saved.json);
}

TEST_CASE("saving needs a unique EntityGuid on every saved entity")
{
    reflectAll();
    World world;
    const Entity withId = world.create(Transform{}, EntityGuid{5});
    const Entity withoutId = world.create(Transform{});
    SceneSaveResult saved = saveScene(world);
    CHECK_FALSE(saved.ok());
    CHECK(saved.json.empty());
    CHECK(saved.error ==
          std::format("the entity with handle index {} has no EntityGuid; call assignMissingGuids() "
                      "before saving",
                      withoutId.index));

    world.emplace<EntityGuid>(withoutId, EntityGuid{5});
    saved = saveScene(world);
    CHECK(saved.error == "id 0000000000000005 is used by two saved entities");

    const Entity list[] = {withId};
    CHECK(saveScene(world, {.subset = list}).ok());
}

TEST_CASE("saving a subset nulls references to entities outside it")
{
    reflectAll();
    World world;
    const Entity leader = world.create(Transform{});
    const Entity follower = world.create(Follow{leader, 1.0f}, Unreflected{3});
    assignMissingGuids(world);
    const Entity list[] = {follower};
    const SceneSaveResult saved = saveScene(world, {.subset = list});
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

TEST_CASE("subsets can be saved in the given order")
{
    reflectAll();
    World world;
    const Entity a = world.create(EntityGuid{3});
    const Entity b = world.create(EntityGuid{1});
    const Entity c = world.create(EntityGuid{2});
    const Entity list[] = {a, c, b};
    const SceneSaveResult byId = saveScene(world, {.subset = list});
    const SceneSaveResult asGiven = saveScene(world, {.subset = list, .order = SceneOrder::AsGiven});
    const SceneSaveResult worldOrder = saveScene(world, {.order = SceneOrder::AsGiven});

    World loaded;
    const auto ids = [&loaded](const SceneSaveResult& saved) {
        const SceneLoadResult result = loadScene(loaded, saved.json, {.ids = IdPolicy::Regenerate});
        REQUIRE(result.ok());
        std::vector<u64> values;
        for (const EntityGuid id : result.fileIds)
        {
            values.push_back(id.value);
        }
        return values;
    };
    CHECK(ids(byId) == std::vector<u64>{1, 2, 3});
    CHECK(ids(asGiven) == std::vector<u64>{3, 2, 1});
    CHECK(ids(worldOrder) == std::vector<u64>{3, 1, 2});
}

namespace
{

// File ids of @p saved in file order.
std::vector<u64> fileIdsOf(const SceneSaveResult& saved)
{
    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json, {.ids = IdPolicy::Regenerate});
    REQUIRE(result.ok());
    std::vector<u64> values;
    for (const EntityGuid id : result.fileIds)
    {
        values.push_back(id.value);
    }
    return values;
}

} // namespace

TEST_CASE("scenes save in hierarchy pre-order by default")
{
    reflectAll();
    World world;
    // Roots 9 and 5 (in that order), 5's children 3 then 1, 3's child 8; 7 and 2 have no Hierarchy.
    const Entity loose = world.create(EntityGuid{7});
    const Entity rootA = world.create(EntityGuid{9}, Transform{});
    const Entity rootB = world.create(EntityGuid{5}, Transform{});
    const Entity childLate = world.create(EntityGuid{1}, Transform{});
    const Entity childEarly = world.create(EntityGuid{3}, Transform{});
    const Entity grandchild = world.create(EntityGuid{8}, Transform{});
    world.create(EntityGuid{2});
    REQUIRE(reorder(world, rootA, kLastSibling));
    REQUIRE(setParent(world, childEarly, rootB));
    REQUIRE(setParent(world, childLate, rootB));
    REQUIRE(setParent(world, grandchild, childEarly));
    CHECK(fileIdsOf(saveScene(world)) == std::vector<u64>{9, 5, 3, 8, 1, 2, 7});
    CHECK(fileIdsOf(saveScene(world, {.order = SceneOrder::ById})) == std::vector<u64>{1, 2, 3, 5, 7, 8, 9});

    // A subset without the parent makes its children roots, ordered by their own order.
    const Entity subset[] = {grandchild, childLate, childEarly, loose};
    CHECK(fileIdsOf(saveScene(world, {.subset = subset})) == std::vector<u64>{3, 8, 1, 7});
}

TEST_CASE("the hierarchy save order is stable after deletes and slot reuse")
{
    reflectAll();
    World world;
    GuidGenerator guids(3);
    std::vector<Entity> children;
    const Entity root = world.create(Transform{});
    for (int i = 0; i < 6; ++i)
    {
        children.push_back(world.create(Transform{}));
        REQUIRE(setParent(world, children.back(), root));
    }
    assignMissingGuids(world, guids);
    const std::vector<u64> before = fileIdsOf(saveScene(world));

    // Delete two children; their slots are reused by new entities appended last.
    world.destroy(children[1]);
    world.destroy(children[4]);
    REQUIRE(reorder(world, children[0], 0)); // closes the gaps
    const Entity added = world.create(Transform{});
    REQUIRE(setParent(world, added, root));
    assignMissingGuids(world, guids);
    std::vector<u64> expected = before;
    expected.erase(expected.begin() + 5); // children[4]
    expected.erase(expected.begin() + 2); // children[1]
    expected.push_back(world.get<EntityGuid>(added).value);
    const SceneSaveResult saved = saveScene(world);
    CHECK(fileIdsOf(saved) == expected);
    CHECK(saveScene(world).json == saved.json);
}

TEST_CASE("Regenerate gives loaded entities new ids and remaps references inside the file")
{
    reflectAll();
    const std::string scene = sceneV2(R"([
        {"id": "00000000000000aa", "components": {"test.Follow": {"leader": "00000000000000bb"}}},
        {"id": "00000000000000bb", "components": {"test.Follow": {"leader": null}}}
    ])");
    World world;
    const SceneLoadResult opened = loadScene(world, scene);
    REQUIRE(opened.ok());
    CHECK(world.get<EntityGuid>(opened.entities[0]).value == 0xaa);

    // Keeping the ids again would duplicate them.
    const SceneLoadResult twice = loadScene(world, scene);
    CHECK(twice.error == "entity 0: id 00000000000000aa already exists in the world");
    CHECK(world.aliveCount() == 2);

    GuidGenerator guids(1);
    const SceneLoadResult pasted = loadScene(world, scene, {.ids = IdPolicy::Regenerate, .guids = &guids});
    INFO(pasted.error);
    REQUIRE(pasted.ok());
    REQUIRE(pasted.entities.size() == 2);
    CHECK(pasted.fileIds[0].value == 0xaa);
    CHECK(pasted.fileIds[1].value == 0xbb);
    const EntityGuid newId = world.get<EntityGuid>(pasted.entities[0]);
    CHECK(newId.value != 0xaa);
    CHECK(newId.value != 0xbb);
    CHECK(world.get<Follow>(pasted.entities[0]).leader == pasted.entities[1]);
    CHECK(world.get<Follow>(opened.entities[0]).leader == opened.entities[1]);

    // Every id in the World is unique, so it saves.
    CHECK(saveScene(world).ok());
}

TEST_CASE("Components of unknown types survive loading and saving byte for byte")
{
    reflectAll();
    constexpr std::string_view kScene = R"({
  "format": "illusion.scene",
  "version": 2,
  "types": {
    "game.Removed": 4,
    "test.Follow": 1
  },
  "settings": {
    "game.Weather": {
      "rain": 0.5
    }
  },
  "entities": [
    {
      "id": "00000000000000aa",
      "components": {
        "game.Removed": {
          "nested": {
            "list": [1, 2.5, "three", null, true],
            "ref": "00000000000000aa"
          }
        },
        "test.Follow": {
          "leader": "00000000000000aa",
          "distance": 3.0
        },
        "zzz.Last": {
          "big": 18446744073709551615
        }
      }
    },
    {
      "id": "00000000000000bb",
      "components": {
        "game.Removed": {}
      }
    }
  ]
}
)";
    REQUIRE(rewritten(kScene) == kScene); // the input is in the canonical layout
    World world;
    const SceneLoadResult result = loadScene(world, kScene);
    INFO(result.error);
    REQUIRE(result.ok());
    REQUIRE(result.warnings.size() == 3);
    CHECK(result.warnings[0] == "unknown component type 'game.Removed' preserved");
    CHECK(result.warnings[1] == "unknown component type 'zzz.Last' preserved");
    CHECK(result.warnings[2] == "unknown settings type 'game.Weather' preserved");

    const UnknownComponents& unknown = world.get<UnknownComponents>(result.entities[0]);
    REQUIRE(unknown.components.size() == 2);
    CHECK(unknown.components[0].type == "game.Removed");
    CHECK(unknown.components[0].version == 4u);
    CHECK(unknown.components[1].type == "zzz.Last");
    CHECK_FALSE(unknown.components[1].version.has_value());
    CHECK(world.get<Follow>(result.entities[0]).leader == result.entities[0]);
    REQUIRE(result.preservedSettings.size() == 1);
    CHECK(result.preservedSettings[0].type == "game.Weather");
    CHECK(result.preservedSettings[0].value.text == R"({"rain":0.5})");

    const SceneSaveResult saved = saveScene(world, {.preservedSettings = result.preservedSettings});
    REQUIRE(saved.ok());
    CHECK(saved.json == kScene);
}

TEST_CASE("UnknownComponents is runtime state that engine queries do not see")
{
    reflectAll();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const TypeReflection* reflection = registry.reflection(componentId<UnknownComponents>());
    REQUIRE(reflection != nullptr);
    CHECK(reflection->name == "illusion.UnknownComponents");
    CHECK(reflection->meta.transient);

    World world;
    const SceneLoadResult result = loadScene(world, sceneV2(R"([
        {"id": "0000000000000001", "components": {"game.Mover": {"speed": 2}}},
        {"id": "0000000000000002", "components": {"illusion.Transform": {}}}
    ])"));
    REQUIRE(result.ok());
    CHECK(world.query<Transform>().count() == 1);
    u32 visited = 0;
    world.query<Transform>().each([&](Entity entity, Transform&) {
        CHECK(entity == result.entities[1]);
        ++visited;
    });
    CHECK(visited == 1);
    CHECK(world.query<UnknownComponents>().count() == 1); // only when asked for by name
}

TEST_CASE("settings round-trip and change only when the whole scene loads")
{
    reflectAll();
    World world;
    const Entity camera = world.create(Transform{}, EntityGuid{9});
    scene_test::Look look;
    look.exposure = 2.5f;
    look.focus = camera;
    look.mood = "storm";
    const SceneSettings settings[] = {sceneSettings(look)};
    const SceneSaveResult saved = saveScene(world, {.settings = settings});
    REQUIRE(saved.ok());
    CHECK(saved.json.find(R"("test.Look": 2)") != std::string::npos);
    CHECK(saved.json.find(R"("focus": "0000000000000009")") != std::string::npos);

    World loaded;
    scene_test::Look target;
    const SceneSettingsTarget targets[] = {sceneSettingsTarget(target)};
    const SceneLoadResult result =
        loadScene(loaded, saved.json, {.ids = IdPolicy::Regenerate, .settings = targets});
    INFO(result.error);
    REQUIRE(result.ok());
    CHECK(result.preservedSettings.empty());
    CHECK(target.exposure == 2.5f);
    CHECK(target.mood == "storm");
    CHECK(target.focus == result.entities[0]); // remapped like Component fields

    // Settings that no target asks for are preserved; a failed load changes no target.
    scene_test::Look untouched;
    const SceneSettingsTarget untouchedTargets[] = {sceneSettingsTarget(untouched)};
    const SceneLoadResult notAsked = loadScene(loaded, saved.json, {.ids = IdPolicy::Regenerate});
    REQUIRE(notAsked.ok());
    REQUIRE(notAsked.preservedSettings.size() == 1);
    CHECK(notAsked.preservedSettings[0].type == "test.Look");
    CHECK(notAsked.preservedSettings[0].version == 2u);
    CHECK(notAsked.warnings.empty());

    const u32 alive = loaded.aliveCount();
    const SceneLoadResult failed = loadScene(loaded,
                                             sceneV2(R"([{"id": "0000000000000003", "components": {}}])",
                                                     "{}", R"({"test.Look": {"exposure": 4, "mood": 5}})"),
                                             {.settings = untouchedTargets});
    CHECK(failed.error == "settings: field 'mood' of 'test.Look' must be a string");
    CHECK(untouched.exposure == 1.0f);
    CHECK(loaded.aliveCount() == alive);
}

TEST_CASE("a preserved type saved with two versions fails to save")
{
    reflectAll();
    World world;
    const SceneLoadResult first = loadScene(
        world, sceneV2(R"([{"id": "0000000000000001", "components": {"game.X": {}}}])", R"({"game.X": 1})"));
    const SceneLoadResult second = loadScene(
        world, sceneV2(R"([{"id": "0000000000000002", "components": {"game.X": {}}}])", R"({"game.X": 2})"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(saveScene(world).error ==
          "'game.X' would be saved with versions 1 and 2; load the scene again to update its preserved data");
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
    CHECK(world.get<UnknownComponents>(result.entities[0]).components[0].value.text == R"({"x":1})");
    // Each problem is reported once.
    REQUIRE(result.warnings.size() == 3);
    CHECK(result.warnings[0] == "'test.Follow' was saved with version 0, loading as version 1");
    CHECK(result.warnings[1] == "unknown field 'test.Follow.speed' skipped");
    CHECK(result.warnings[2] == "unknown component type 'game.Removed' preserved");

    const SceneLoadResult v2 = loadScene(world, sceneV2(R"([{"id": "0000000000000001", "parent": 3}])"));
    REQUIRE(v2.ok());
    CHECK(v2.warnings == std::vector<std::string>{"unknown entity member 'parent' skipped"});
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
    const SceneSaveResult saved = saveScene(world, {.subset = std::span(result.entities).subspan(1)});
    CHECK(saved.json.find("\"team\": 99") != std::string::npos);
}

TEST_CASE("names are checked when they are read")
{
    reflectAll();
    World world;
    const auto load = [&world](std::string_view name) {
        return loadScene(world, sceneV2(std::format(R"([{{"id": "0000000000000001", "components":
                                                       {{"illusion.Name": {{"value": "{}"}}}}}}])",
                                                    name)));
    };
    CHECK(load("a/b").error == "entity 0: field 'value' of 'illusion.Name' must not contain '/'");
    CHECK(load("tab\\there").error ==
          "entity 0: field 'value' of 'illusion.Name' must not contain control characters");
    CHECK(load(std::string(257, 'n')).error ==
          "entity 0: field 'value' of 'illusion.Name' must not be longer than 256 bytes");
    const SceneLoadResult valid = load("Player \xC3\xA7");
    REQUIRE(valid.ok());
    CHECK(world.get<Name>(valid.entities[0]).value == "Player \xC3\xA7");
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
        {R"({"format": "illusion.scene", "version": 3, "entities": []})",
         "unsupported scene version (this build reads 1 and 2)"},
        {R"({"format": "illusion.scene", "entities": []})", "unsupported scene version"},
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
        {sceneWith(R"([{"game.X": {}, "game.X": {}}])"), "entity 0: 'game.X' appears twice"},
        {sceneV2("[3]"), "entity 0 is not an object"},
        {sceneV2(R"([{"components": {}}])"), "entity 0: \"id\" must be a string of 16 hexadecimal digits"},
        {sceneV2(R"([{"id": 5}])"), "entity 0: \"id\" must be a string of 16 hexadecimal digits"},
        {sceneV2(R"([{"id": "0000000000000000"}])"),
         "entity 0: \"id\" must be a string of 16 hexadecimal digits"},
        {sceneV2(R"([{"id": "000000000000001"}])"),
         "entity 0: \"id\" must be a string of 16 hexadecimal digits"},
        {sceneV2(R"([{"id": "000000000000000g"}])"),
         "entity 0: \"id\" must be a string of 16 hexadecimal digits"},
        {sceneV2(R"([{"id": "00000000000000aa"}, {"id": "00000000000000AA"}])"),
         "entity 1: id 00000000000000aa appears twice"},
        {sceneV2(R"([{"id": "0000000000000001", "components": []}])"),
         "entity 0: \"components\" must be an object"},
        {sceneV2(R"([{"id": "0000000000000001", "components": {"illusion.EntityGuid": {"value": "1"}}}])"),
         "entity 0: 'illusion.EntityGuid' must be written as the entity's \"id\""},
        {sceneV2(
             R"([{"id": "0000000000000001", "components": {"test.Follow": {"leader": "0000000000000002"}}}])"),
         "entity 0: field 'leader' of 'test.Follow' must be null or the id of an entity in this scene"},
        {sceneV2(R"([{"id": "0000000000000001", "components": {"test.Follow": {"leader": 0}}}])"),
         "must be null or the id of an entity in this scene"},
        {sceneV2("[]", "{}", "[]"), "\"settings\" must be an object"},
        {sceneV2("[]", "{}", R"({"game.A": 1, "game.A": 2})"), "settings: 'game.A' appears twice"},
    };
    for (const Case& c : cases)
    {
        CAPTURE(c.json);
        const SceneLoadResult result = loadScene(world, c.json);
        CHECK_FALSE(result.ok());
        CHECK(result.error.find(c.error) != std::string::npos);
        CHECK(result.entities.empty());
        CHECK(result.fileIds.empty());
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
    const Entity entity = world.create(value, EntityGuid{0x12});
    const SceneSaveResult saved = saveScene(world);
    CHECK_FALSE(saved.ok());
    CHECK(saved.error == "entity 0000000000000012: field 'vec3' of 'test.Everything' is not finite");
    CHECK(saved.json.empty());

    world.get<Everything>(entity).vec3.y = 0.0f;
    world.get<Everything>(entity).f64Value = std::numeric_limits<f64>::quiet_NaN();
    CHECK(saveScene(world).error ==
          "entity 0000000000000012: field 'f64Value' of 'test.Everything' is not finite");

    world.get<Everything>(entity).f64Value = 0.0;
    scene_test::Look look;
    look.exposure = std::numeric_limits<f32>::infinity();
    const SceneSettings settings[] = {sceneSettings(look)};
    CHECK(saveScene(world, {.settings = settings}).error ==
          "settings: field 'exposure' of 'test.Look' is not finite");
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
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    CHECK(loaded.get<Everything>(result.entities[0]).text == value.text);
}

TEST_CASE("the JSON writer rewrites scene documents byte for byte")
{
    // The format documented in docs/content/en/manual/scenes.md.
    constexpr std::string_view kDocumented = R"({
  "format": "illusion.scene",
  "version": 2,
  "types": {
    "game.Spinner": 1,
    "illusion.Name": 1,
    "illusion.Transform": 1
  },
  "settings": {},
  "entities": [
    {
      "id": "3f9a0c1e5b7d2a40",
      "components": {
        "game.Spinner": {
          "axis": [0.0, 1.0, 0.0],
          "speed": 2.5,
          "team": "Blue",
          "target": "9b21d07c4e6f1a35"
        },
        "illusion.Name": {
          "value": "Turret"
        },
        "illusion.Transform": {
          "position": [0.0, 1.0, 0.0],
          "rotation": [0.0, 0.0, 0.0, 1.0],
          "scale": [1.0, 1.0, 1.0]
        }
      }
    },
    {
      "id": "9b21d07c4e6f1a35",
      "components": {
        "illusion.Name": {
          "value": "Player"
        }
      }
    }
  ]
}
)";
    CHECK(rewritten(kDocumented) == kDocumented);

    reflectAll();
    World world;
    const Entity first = world.create(everyFieldType(), Selected{});
    world.get<Everything>(first).target = first;
    world.create(Follow{first, 5.5f}, Transform{});
    world.create(); // an entity without components
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(rewritten(saved.json) == saved.json);
}

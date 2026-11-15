#include <illusion/core/Json.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace illusion;

namespace save_test
{

// Game state saved on an entity.
struct Health
{
    i32 points = 100;
    f32 armor = 0.0f;
    Entity target;
};

// Version 2 counts coins; version 1 saved cents.
struct Purse
{
    i32 coins = 0;
};

} // namespace save_test

namespace
{

using save_test::Health;
using save_test::Purse;

void reflectAll()
{
    reflectCoreComponents();
    registerSaveComponents();
    reflect<Health>("test.SaveHealth")
        .field("points", &Health::points)
        .field("armor", &Health::armor)
        .field("target", &Health::target);
    reflect<Purse>("test.SavePurse", 2)
        .field("coins", &Purse::coins)
        .migrate(1, [](JsonMutDocument& doc, JsonMutValue object) {
            const std::optional<i64> cents = object.find("cents").asI64();
            static_cast<void>(object.remove("cents"));
            object.set("coins", doc.makeI64(cents.value_or(0) / 100));
        });
}

// The live entity named @p name, or the null entity.
Entity named(World& world, std::string_view name)
{
    Entity found;
    world.query<const Name>().each([&](Entity entity, const Name& value) {
        if (value.value == name)
        {
            found = entity;
        }
    });
    return found;
}

Entity child(World& world, std::string_view name, Entity parent, Health health = {})
{
    const Entity entity = world.create(Transform{}, Name{std::string(name)}, health);
    REQUIRE(setParent(world, entity, parent).hasValue());
    return entity;
}

std::vector<Entity> sorted(std::vector<Entity> entities)
{
    std::sort(entities.begin(), entities.end(), [](Entity a, Entity b) { return a.toBits() < b.toBits(); });
    return entities;
}

} // namespace

TEST_CASE("the saved set is every tagged entity with all its descendants")
{
    reflectAll();
    World world;
    const Entity level = world.create(Transform{}, Name{"Level"});
    const Entity player = child(world, "Player", level);
    world.emplace<Saved>(player);
    const Entity weapon = child(world, "Weapon", player);
    const Entity scope = child(world, "Scope", weapon);
    world.emplace<Saved>(scope); // tagged twice through its ancestor: listed once
    const Entity loose = world.create(Saved{});
    child(world, "Rock", level);

    std::vector<Entity> saved;
    collectSavedEntities(world, saved);
    CHECK(sorted(saved) == sorted({player, weapon, scope, loose}));

    World empty;
    collectSavedEntities(empty, saved);
    CHECK(saved.empty());
}

TEST_CASE("a saved subtree under an unsaved parent round-trips and keeps its parent")
{
    reflectAll();
    World world;
    const Entity level = world.create(Transform{}, Name{"Level"});
    const Entity rock = child(world, "Rock", level);
    const Entity player = child(world, "Player", level, Health{80, 0.5f, rock});
    world.emplace<Saved>(player);
    child(world, "Weapon", player, Health{30, 0.0f, player});
    world.get<LocalTransform>(player).position = Vec3(1.0f, 2.0f, 3.0f);

    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());
    CHECK(captured->entities == 2);
    CHECK(captured->world.find("Rock") == std::string::npos); // unsaved entities are not written

    world.get<Health>(player).points = 5;
    world.get<LocalTransform>(player).position = Vec3(0.0f);
    CHECK(destroyRecursive(world, named(world, "Weapon")) == 1);

    const Expected<SaveApplied, SaveFailure> applied = applySave(world, captured->world);
    REQUIRE(applied.hasValue());
    CHECK(applied->replaced == 1);
    CHECK(applied->entities.size() == 2);
    CHECK_FALSE(world.isAlive(player));
    CHECK(world.isAlive(level));
    CHECK(world.isAlive(rock));
    CHECK(world.aliveCount() == 4);

    const Entity restored = named(world, "Player");
    REQUIRE(world.isAlive(restored));
    CHECK(world.has<Saved>(restored));
    CHECK(world.get<Health>(restored).points == 80);
    CHECK(world.get<Health>(restored).target == rock);
    CHECK(world.get<Hierarchy>(restored).parent == level);
    CHECK(world.get<LocalTransform>(restored).position == Vec3(1.0f, 2.0f, 3.0f));
    const Entity weapon = named(world, "Weapon");
    REQUIRE(world.isAlive(weapon));
    CHECK(world.get<Hierarchy>(weapon).parent == restored);
    CHECK(world.get<Health>(weapon).target == restored);

    HierarchyIndex index;
    updateTransformHierarchy(world, index);
    CHECK(index.children(level).size() == 2);
}

TEST_CASE("applying a save destroys entities spawned since and recreates those destroyed since")
{
    reflectAll();
    World world;
    const Entity kept = world.create(Name{"Kept"}, Health{10, 0.0f, Entity{}}, Saved{});
    const Entity lost = world.create(Name{"Lost"}, Health{20, 0.0f, Entity{}}, Saved{});
    const Entity scenery = world.create(Name{"Scenery"});
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());
    const EntityGuid lostId = world.get<EntityGuid>(lost);

    world.destroy(lost);
    world.create(Name{"Spawned"}, Health{30, 0.0f, Entity{}}, Saved{});
    world.get<Health>(kept).points = 11;

    const Expected<SaveApplied, SaveFailure> applied = applySave(world, captured->world);
    REQUIRE(applied.hasValue());
    CHECK(applied->replaced == 2);
    CHECK(world.isAlive(scenery));
    CHECK(named(world, "Spawned").isNull());
    CHECK(world.get<Health>(named(world, "Kept")).points == 10);
    const Entity back = named(world, "Lost");
    REQUIRE(world.isAlive(back));
    CHECK(world.get<EntityGuid>(back) == lostId);
    CHECK(world.aliveCount() == 3);
}

TEST_CASE("prefab instances in a save round-trip with their prefab links")
{
    reflectAll();
    const std::string prefab =
        R"({"format": "illusion.prefab", "version": 2, "entities": [
            {"id": "00000000000000a1", "components": {"illusion.Name": {"value": "Crate"},
              "illusion.Hierarchy": {"parent": null, "order": 0}, "illusion.LocalTransform": {},
              "illusion.Transform": {}}},
            {"id": "00000000000000a2", "components": {"illusion.Name": {"value": "Lid"},
              "illusion.Hierarchy": {"parent": "00000000000000a1", "order": 0}, "illusion.LocalTransform": {},
              "test.SaveHealth": {"points": 7, "target": "00000000000000a1"}}}]})";
    const std::string scene = R"({"format": "illusion.scene", "version": 2, "entities": [
        {"prefab": "game:/crate.prefab", "id": "0000000000001000"}]})";
    World world;
    SceneLoadOptions options;
    options.prefabs = [&](std::string_view) -> std::optional<std::string> {
        return prefab;
    };
    const SceneLoadResult loaded = loadScene(world, scene, options);
    REQUIRE(loaded.ok());
    const Entity crate = named(world, "Crate");
    world.emplace<Saved>(crate);
    const PrefabMember lidMember = world.get<PrefabMember>(named(world, "Lid"));

    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());
    world.get<Health>(named(world, "Lid")).points = 1;

    const Expected<SaveApplied, SaveFailure> applied = applySave(world, captured->world);
    REQUIRE(applied.hasValue());
    CHECK_FALSE(world.isAlive(crate));
    const Entity root = named(world, "Crate");
    const Entity lid = named(world, "Lid");
    REQUIRE(world.isAlive(root));
    REQUIRE(world.isAlive(lid));
    CHECK(world.get<PrefabInstance>(root).prefab.path() == "game:/crate.prefab");
    CHECK(world.get<PrefabInstance>(root).instance == EntityGuid{0x1000});
    CHECK(world.get<PrefabMember>(lid).instance == lidMember.instance);
    CHECK(world.get<PrefabMember>(lid).local == lidMember.local);
    CHECK(world.get<Health>(lid).points == 7);
    CHECK(world.get<Health>(lid).target == root);
    CHECK(world.get<Hierarchy>(lid).parent == root);
    CHECK(world.aliveCount() == 2);
}

TEST_CASE("a save that fails to apply changes nothing")
{
    reflectAll();
    World world;
    const Entity level = world.create(Name{"Level"});
    const Entity player = world.create(Name{"Player"}, Health{50, 0.0f, Entity{}}, Saved{});
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());
    world.get<Health>(player).points = 51;

    std::string broken = captured->world;
    const usize points = broken.find("\"points\": 50");
    REQUIRE(points != std::string::npos);
    broken.replace(points, 12, "\"points\": \"many\"");
    for (const std::string& json : {broken, std::string("not json"), std::string("{}")})
    {
        CAPTURE(json);
        const Expected<SaveApplied, SaveFailure> applied = applySave(world, json);
        REQUIRE_FALSE(applied.hasValue());
        CHECK(applied.error().error == SaveError::InvalidScene);
        CHECK_FALSE(applied.error().message.empty());
        CHECK(world.isAlive(player));
        CHECK(world.isAlive(level));
        CHECK(world.get<Health>(player).points == 51);
        CHECK(world.aliveCount() == 2);
    }
}

TEST_CASE("type migrations run when a save is applied")
{
    reflectAll();
    World world;
    world.create(Name{"Bank"}, Purse{3}, Saved{});
    const std::string json = R"({"format": "illusion.scene", "version": 2, "types": {"test.SavePurse": 1},
        "entities": [{"id": "0000000000000042", "components": {"illusion.Name": {"value": "Bank"},
          "illusion.Saved": {}, "test.SavePurse": {"cents": 1250}}}]})";

    const Expected<SaveApplied, SaveFailure> applied = applySave(world, json);
    REQUIRE(applied.hasValue());
    const Entity bank = named(world, "Bank");
    CHECK(world.get<EntityGuid>(bank) == EntityGuid{0x42});
    CHECK(world.get<Purse>(bank).coins == 12);
    CHECK(std::any_of(applied->warnings.begin(), applied->warnings.end(), [](const std::string& warning) {
        return warning.find("'test.SavePurse' was saved with version 1") != std::string::npos;
    }));
}

TEST_CASE("components of types the game no longer has are dropped with a warning")
{
    reflectAll();
    World world;
    const std::string json = R"({"format": "illusion.scene", "version": 2, "entities": [
        {"id": "0000000000000042", "components": {"illusion.Saved": {}, "test.Gone": {"x": 1}}}]})";
    const Expected<SaveApplied, SaveFailure> applied = applySave(world, json);
    REQUIRE(applied.hasValue());
    REQUIRE(applied->entities.size() == 1);
    CHECK_FALSE(world.has<UnknownComponents>(applied->entities[0]));
    REQUIRE(applied->warnings.size() == 1);
    CHECK(applied->warnings[0] == "component type 'test.Gone' of entity 0000000000000042 dropped");
}

TEST_CASE("capturing fails for values that a save cannot store")
{
    reflectAll();
    World world;
    world.create(Health{1, std::numeric_limits<f32>::infinity(), Entity{}}, Saved{});
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE_FALSE(captured.hasValue());
    CHECK(captured.error().error == SaveError::InvalidScene);
    CHECK(captured.error().message.find("armor") != std::string::npos);
}

TEST_CASE("a world without saved entities captures an empty World section")
{
    reflectAll();
    World world;
    const Entity scenery = world.create(Name{"Scenery"});
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());
    CHECK(captured->entities == 0);
    world.create(Saved{});
    const Expected<SaveApplied, SaveFailure> applied = applySave(world, captured->world);
    REQUIRE(applied.hasValue());
    CHECK(applied->replaced == 1);
    CHECK(world.aliveCount() == 1);
    CHECK(world.isAlive(scenery));
}

TEST_CASE("a captured World section round-trips through a slot file")
{
    reflectAll();
    World world;
    const Entity player = world.create(Name{"Player"}, Health{64, 0.0f, Entity{}}, Saved{});
    const Expected<SaveCapture, SaveFailure> captured = captureSave(world);
    REQUIRE(captured.hasValue());
    std::vector<std::byte> file;
    REQUIRE(encodeSaveFile({.title = "Save"}, 0, captured->world, file).hasValue());
    world.get<Health>(player).points = 1;

    const Expected<DecodedSave, SaveError> decoded = decodeSaveFile(file);
    REQUIRE(decoded.hasValue());
    const Expected<SaveApplied, SaveFailure> applied = applySave(world, decoded->world);
    REQUIRE(applied.hasValue());
    CHECK(world.get<Health>(named(world, "Player")).points == 64);
}

#include "ReflectedComponents.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <format>
#include <map>
#include <string>

using namespace illusion;
using namespace reflection_test;

namespace
{

constexpr EntityGuid kRoot{0xa1};
constexpr EntityGuid kChild{0xa2};
constexpr EntityGuid kInstance{0x1000};

void reflectAll()
{
    reflectTestComponents();
    reflectCoreComponents();
}

// A prefab file with the given entities.
std::string prefabFile(std::string_view entities, std::string_view types = "{}")
{
    return std::format(R"({{"format": "illusion.prefab", "version": 2, "types": {}, "entities": {}}})", types,
                       entities);
}

// A scene file with the given entities.
std::string sceneFile(std::string_view entities)
{
    return std::format(R"({{"format": "illusion.scene", "version": 2, "entities": {}}})", entities);
}

// The crate prefab: a root "Crate" and a child "Lid" that follows the root at distance 2.
std::string cratePrefab(std::string_view lidName = "Lid", float distance = 2.0f)
{
    return prefabFile(std::format(
        R"([{{"id": "00000000000000a1", "components": {{"illusion.Name": {{"value": "Crate"}},
              "illusion.Hierarchy": {{"parent": null, "order": 0}}, "illusion.LocalTransform": {{}},
              "illusion.Transform": {{}}}}}},
            {{"id": "00000000000000a2", "components": {{"illusion.Name": {{"value": "{}"}},
              "illusion.Hierarchy": {{"parent": "00000000000000a1", "order": 0}}, "illusion.LocalTransform": {{}},
              "test.Follow": {{"leader": "00000000000000a1", "distance": {}}}}}}}])",
        lidName, distance));
}

// Prefab files by path, for SceneLoadOptions::prefabs and SceneSaveOptions::prefabs.
struct Library
{
    std::map<std::string, std::string, std::less<>> files;

    [[nodiscard]] PrefabSource source() const
    {
        return [this](std::string_view path) -> std::optional<std::string> {
            const auto it = files.find(path);
            return it != files.end() ? std::optional<std::string>(it->second) : std::nullopt;
        };
    }

    [[nodiscard]] SceneLoadOptions load() const
    {
        SceneLoadOptions options;
        options.prefabs = source();
        return options;
    }

    [[nodiscard]] SceneSaveOptions save() const
    {
        SceneSaveOptions options;
        options.prefabs = source();
        return options;
    }
};

// The entity of @p world with id @p guid (null if none).
Entity byGuid(const World& world, EntityGuid guid)
{
    std::vector<Entity> all;
    world.entities(all);
    for (const Entity entity : all)
    {
        const EntityGuid* id = world.tryGet<EntityGuid>(entity);
        if (id != nullptr && *id == guid)
        {
            return entity;
        }
    }
    return Entity{};
}

// The entity of @p world named @p name (null if none).
Entity byName(const World& world, std::string_view name)
{
    std::vector<Entity> all;
    world.entities(all);
    for (const Entity entity : all)
    {
        const Name* found = world.tryGet<Name>(entity);
        if (found != nullptr && found->value == name)
        {
            return entity;
        }
    }
    return Entity{};
}

// The saved entities array of a scene file.
JsonDocument parsed(const std::string& json)
{
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json);
    REQUIRE(doc.hasValue());
    return std::move(doc).value();
}

std::string text(EntityGuid guid)
{
    return toString(guid);
}

} // namespace

TEST_CASE("prefab entity ids derive deterministically from the instance and local ids")
{
    CHECK(derivePrefabGuid(kInstance, kRoot) == derivePrefabGuid(kInstance, kRoot));
    CHECK(derivePrefabGuid(kInstance, kRoot) != derivePrefabGuid(kInstance, kChild));
    CHECK(derivePrefabGuid(kInstance, kRoot) != derivePrefabGuid(EntityGuid{0x1001}, kRoot));
    CHECK(derivePrefabGuid(kRoot, kInstance) != derivePrefabGuid(kInstance, kRoot));
    CHECK_FALSE(derivePrefabGuid(EntityGuid{}, EntityGuid{}).isNull());
}

TEST_CASE("instantiatePrefab creates linked entities with derived ids and remapped references")
{
    reflectAll();
    World world;
    const Entity holder = world.create();
    world.emplace<EntityGuid>(holder, EntityGuid{0x77});
    world.emplace<Transform>(holder, Transform{Vec3(10.0f, 0.0f, 0.0f)});
    world.emplace<LocalTransform>(holder, LocalTransform{Vec3(10.0f, 0.0f, 0.0f)});
    world.emplace<Hierarchy>(holder);

    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, holder, kInstance);
    REQUIRE(placed.ok());
    REQUIRE(placed.entities.size() == 2);
    CHECK(placed.entities.front() == placed.root);
    CHECK(world.get<EntityGuid>(placed.root) == derivePrefabGuid(kInstance, kRoot));
    CHECK(world.get<PrefabInstance>(placed.root).instance == kInstance);
    CHECK(world.get<PrefabInstance>(placed.root).prefab.path() == "assets:/crate.prefab");
    CHECK(world.get<Hierarchy>(placed.root).parent == holder);

    const Entity lid = placed.entities[1];
    CHECK(world.get<EntityGuid>(lid) == derivePrefabGuid(kInstance, kChild));
    CHECK(world.get<PrefabMember>(lid).instance == kInstance);
    CHECK(world.get<PrefabMember>(lid).local == kChild);
    CHECK(world.get<PrefabMember>(placed.root).local == kRoot);
    CHECK_FALSE(world.has<PrefabInstance>(lid));
    CHECK(world.get<Follow>(lid).leader == placed.root);
    CHECK(world.get<Hierarchy>(lid).parent == placed.root);

    SUBCASE("the same instance id cannot be used twice")
    {
        const usize before = world.aliveCount();
        const PrefabInstantiateResult again =
            instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, Entity{}, kInstance);
        CHECK_FALSE(again.ok());
        CHECK(again.error.find("already exists") != std::string::npos);
        CHECK(world.aliveCount() == before);
    }
    SUBCASE("an invalid prefab is an error that adds nothing")
    {
        const usize before = world.aliveCount();
        const PrefabInstantiateResult bad = instantiatePrefab(
            world, PrefabData{"assets:/bad.prefab", prefabFile("[]")}, Entity{}, EntityGuid{0x2000});
        CHECK_FALSE(bad.ok());
        CHECK(bad.error.find("exactly one root") != std::string::npos);
        CHECK(world.aliveCount() == before);
    }
}

TEST_CASE("a scene saves an instance as a record with only its overrides and loads it back")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    World world;
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, Entity{}, kInstance);
    REQUIRE(placed.ok());

    SUBCASE("an untouched instance has no overrides")
    {
        const SceneSaveResult saved = saveScene(world, library.save());
        REQUIRE(saved.ok());
        const JsonDocument doc = parsed(saved.json);
        const JsonValue entities = doc.root().find("entities");
        REQUIRE(entities.size() == 1);
        CHECK(entities[0].find("prefab").asString() == "assets:/crate.prefab");
        CHECK(entities[0].find("id").asString() == text(kInstance));
        CHECK(entities[0].find("overrides").isAbsent());
        CHECK(saved.entities == 2);
    }

    SUBCASE("changed fields, added and removed Components and removed entities are overrides")
    {
        const Entity lid = placed.entities[1];
        world.get<Follow>(lid).distance = 7.0f;
        world.get<LocalTransform>(placed.root).position = Vec3(1.0f, 2.0f, 3.0f);
        world.emplace<Selected>(placed.root);
        (void)world.remove<Name>(lid);

        const SceneSaveResult saved = saveScene(world, library.save());
        REQUIRE(saved.ok());
        const JsonDocument doc = parsed(saved.json);
        const JsonValue overrides = doc.root().pointer("/entities/0/overrides");
        REQUIRE(overrides.isObject());
        const JsonValue lidOverrides = overrides.find(text(kChild));
        CHECK(lidOverrides.pointer("/test.Follow/distance").asF64() == doctest::Approx(7.0));
        CHECK(
            lidOverrides.pointer("/test.Follow/leader").isAbsent()); // field level: unchanged fields stay out
        CHECK(lidOverrides.find("illusion.Name").isNull());
        const JsonValue rootOverrides = overrides.find(text(kRoot));
        CHECK(rootOverrides.pointer("/illusion.LocalTransform/position/1").asF64() == doctest::Approx(2.0));
        CHECK(rootOverrides.find("test.Selected").isObject());
        CHECK(rootOverrides.find("illusion.Name").isAbsent());

        World loaded;
        const SceneLoadResult result = loadScene(loaded, saved.json, library.load());
        REQUIRE(result.ok());
        CHECK(result.entities.size() == 2);
        const Entity loadedLid = byGuid(loaded, derivePrefabGuid(kInstance, kChild));
        const Entity loadedRoot = byGuid(loaded, derivePrefabGuid(kInstance, kRoot));
        REQUIRE_FALSE(loadedLid.isNull());
        CHECK(loaded.get<Follow>(loadedLid).distance == 7.0f);
        CHECK(loaded.get<Follow>(loadedLid).leader == loadedRoot);
        CHECK_FALSE(loaded.has<Name>(loadedLid));
        CHECK(loaded.has<Selected>(loadedRoot));
        CHECK(loaded.get<LocalTransform>(loadedRoot).position == Vec3(1.0f, 2.0f, 3.0f));
        CHECK(loaded.get<PrefabMember>(loadedLid).local == kChild);

        // Saving again gives the same file.
        const SceneSaveResult again = saveScene(loaded, library.save());
        REQUIRE(again.ok());
        CHECK(again.json == saved.json);
    }

    SUBCASE("a removed entity is a null override")
    {
        world.destroy(placed.entities[1]);
        const SceneSaveResult saved = saveScene(world, library.save());
        REQUIRE(saved.ok());
        const JsonDocument doc = parsed(saved.json);
        CHECK(doc.root().pointer("/entities/0/overrides").find(text(kChild)).isNull());
        World loaded;
        const SceneLoadResult result = loadScene(loaded, saved.json, library.load());
        REQUIRE(result.ok());
        CHECK(result.entities.size() == 1);
        CHECK(byName(loaded, "Lid").isNull());
    }

    SUBCASE("saving an instance without a prefab source fails")
    {
        const SceneSaveResult saved = saveScene(world);
        CHECK_FALSE(saved.ok());
        CHECK(saved.error.find("SceneSaveOptions::prefabs") != std::string::npos);
    }

    SUBCASE("saving only the root saves the whole instance")
    {
        const std::array subset{placed.root};
        SceneSaveOptions options = library.save();
        options.subset = std::span<const Entity>(subset);
        const SceneSaveResult saved = saveScene(world, options);
        REQUIRE(saved.ok());
        CHECK(saved.entities == 2);
    }

    SUBCASE("members saved without their root are plain entities")
    {
        const std::array subset{placed.entities[1]};
        SceneSaveOptions options = library.save();
        options.subset = std::span<const Entity>(subset);
        const SceneSaveResult saved = saveScene(world, options);
        REQUIRE(saved.ok());
        const JsonDocument doc = parsed(saved.json);
        CHECK(doc.root().pointer("/entities/0/prefab").isAbsent());
        CHECK(doc.root().pointer("/entities/0/components/illusion.Name/value").asString() == "Lid");
    }
}

TEST_CASE("a prefab change reaches instances except their overridden fields")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    World world;
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, Entity{}, kInstance);
    REQUIRE(placed.ok());
    world.get<Name>(placed.entities[1]).value = "Top";
    const SceneSaveResult saved = saveScene(world, library.save());
    REQUIRE(saved.ok());

    library.files["assets:/crate.prefab"] = cratePrefab("Cover", 5.0f);
    World loaded;
    REQUIRE(loadScene(loaded, saved.json, library.load()).ok());
    const Entity lid = byGuid(loaded, derivePrefabGuid(kInstance, kChild));
    CHECK(loaded.get<Name>(lid).value == "Top");     // overridden
    CHECK(loaded.get<Follow>(lid).distance == 5.0f); // follows the prefab
}

TEST_CASE("scene entities refer to and parent under instance entities")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    World world;
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, Entity{}, kInstance);
    REQUIRE(placed.ok());
    const Entity sticker = world.create();
    world.emplace<EntityGuid>(sticker, EntityGuid{0x55});
    world.emplace<Name>(sticker, Name{"Sticker"});
    world.emplace<Follow>(sticker, Follow{placed.entities[1], 1.0f});
    REQUIRE(setParent(world, sticker, placed.entities[1]).hasValue());

    const SceneSaveResult saved = saveScene(world, library.save());
    REQUIRE(saved.ok());
    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json, library.load());
    REQUIRE(result.ok());
    const Entity loadedSticker = byName(loaded, "Sticker");
    const Entity lid = byGuid(loaded, derivePrefabGuid(kInstance, kChild));
    CHECK(loaded.get<Follow>(loadedSticker).leader == lid);
    CHECK(loaded.get<Hierarchy>(loadedSticker).parent == lid);
    CHECK_FALSE(loaded.has<PrefabMember>(loadedSticker));
    // The sticker is not an override: the lid's record stays clean.
    const JsonDocument doc = parsed(saved.json);
    CHECK(doc.root().pointer("/entities/0/overrides").isAbsent());
}

TEST_CASE("nested prefabs expand with outer overrides taking precedence")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    // A cart: a root with a crate instance (id 0xb0) as its child, whose lid distance it overrides.
    const std::string lidKey = text(derivePrefabGuid(EntityGuid{0xb0}, kChild));
    const std::string crateRootKey = text(derivePrefabGuid(EntityGuid{0xb0}, kRoot));
    library.files["assets:/cart.prefab"] = prefabFile(std::format(
        R"([{{"id": "00000000000000c1", "components": {{"illusion.Name": {{"value": "Cart"}},
              "illusion.Hierarchy": {{"parent": null, "order": 0}}, "illusion.LocalTransform": {{}}}}}},
            {{"prefab": "assets:/crate.prefab", "id": "00000000000000b0", "overrides": {{
              "00000000000000a1": {{"illusion.Hierarchy": {{"parent": "00000000000000c1"}}}},
              "00000000000000a2": {{"test.Follow": {{"distance": 3}}}}}}}}])"));

    World world;
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/cart.prefab", library.files["assets:/cart.prefab"]},
                          Entity{}, kInstance, library.source());
    REQUIRE(placed.ok());
    CHECK(placed.entities.size() == 3);
    const EntityGuid lidGuid = derivePrefabGuid(kInstance, EntityGuid{*parseGuid(lidKey)});
    const Entity lid = byGuid(world, lidGuid);
    REQUIRE_FALSE(lid.isNull());
    CHECK(world.get<Follow>(lid).distance == 3.0f);
    CHECK(world.get<PrefabMember>(lid).instance == kInstance); // the outermost instance
    CHECK(world.get<PrefabMember>(lid).local.value == parseGuid(lidKey)->value);
    const Entity crateRoot = byGuid(world, derivePrefabGuid(kInstance, *parseGuid(crateRootKey)));
    CHECK(world.get<Hierarchy>(crateRoot).parent == placed.root);
    // The nested instance's root keeps its own PrefabInstance; its members name the outer instance.
    CHECK(world.get<PrefabInstance>(crateRoot).instance == derivePrefabGuid(kInstance, EntityGuid{0xb0}));
    CHECK(world.get<PrefabMember>(crateRoot).instance == kInstance);

    SUBCASE("the scene overrides a nested entity on top of the cart")
    {
        world.get<Follow>(lid).distance = 9.0f;
        const SceneSaveResult saved = saveScene(world, library.save());
        REQUIRE(saved.ok());
        const JsonDocument doc = parsed(saved.json);
        CHECK(doc.root()
                  .pointer("/entities/0/overrides")
                  .find(lidKey)
                  .pointer("/test.Follow/distance")
                  .asF64() == doctest::Approx(9.0));
        World loaded;
        REQUIRE(loadScene(loaded, saved.json, library.load()).ok());
        CHECK(loaded.get<Follow>(byGuid(loaded, lidGuid)).distance == 9.0f);
    }
    SUBCASE("instantiating again with the same id gives the same ids")
    {
        World other;
        const PrefabInstantiateResult again =
            instantiatePrefab(other, PrefabData{"assets:/cart.prefab", library.files["assets:/cart.prefab"]},
                              Entity{}, kInstance, library.source());
        REQUIRE(again.ok());
        CHECK_FALSE(byGuid(other, lidGuid).isNull());
    }
}

TEST_CASE("a variant is a prefab whose root is an overridden instance")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    library.files["assets:/red-crate.prefab"] = prefabFile(
        R"([{"prefab": "assets:/crate.prefab", "id": "00000000000000d0", "overrides": {
              "00000000000000a1": {"illusion.Name": {"value": "Red crate"}}}}])");
    World world;
    const PrefabInstantiateResult placed = instantiatePrefab(
        world, PrefabData{"assets:/red-crate.prefab", library.files["assets:/red-crate.prefab"]}, Entity{},
        kInstance, library.source());
    REQUIRE(placed.ok());
    CHECK(world.get<Name>(placed.root).value == "Red crate");
    CHECK(world.get<PrefabInstance>(placed.root).prefab.path() == "assets:/red-crate.prefab");
    CHECK(placed.entities.size() == 2);
}

TEST_CASE("records whose prefab is missing, cyclic or too deep are kept unresolved and saved unchanged")
{
    reflectAll();
    Library library;
    const std::string scene = sceneFile(
        R"([{"prefab": "assets:/gone.prefab", "id": "0000000000001000", "overrides": {
              "00000000000000a1": {"illusion.Name": {"value": "Kept"}}}},
            {"id": "0000000000000055", "components": {"test.Follow": {"leader": "1234567812345678"}}}])");

    SUBCASE("a missing prefab")
    {
        World world;
        SceneLoadOptions options = library.load();
        options.assetExists = [](std::string_view) {
            return false;
        };
        const SceneLoadResult result = loadScene(world, scene, options);
        REQUIRE(result.ok());
        REQUIRE(result.entities.size() == 2);
        CHECK(world.has<UnresolvedPrefab>(result.entities[0]));
        CHECK(world.get<EntityGuid>(result.entities[0]) == kInstance);
        CHECK(std::find(result.missingAssets.begin(), result.missingAssets.end(), "assets:/gone.prefab") !=
              result.missingAssets.end());
        // References to the missing instance's entities read as null.
        CHECK(world.get<Follow>(result.entities[1]).leader.isNull());
        CHECK(result.clearedReferences == 1);
        CHECK(std::any_of(result.warnings.begin(), result.warnings.end(), [](const std::string& warning) {
            return warning.find("cleared") != std::string::npos;
        }));

        const SceneSaveResult saved = saveScene(world);
        REQUIRE(saved.ok());
        const JsonDocument doc = parsed(saved.json);
        // Entities without a Hierarchy are saved by id: 0x55, then the record 0x1000.
        CHECK(doc.root().pointer("/entities/1/prefab").asString() == "assets:/gone.prefab");
        CHECK(doc.root().pointer("/entities/1/id").asString() == text(kInstance));
        CHECK(doc.root().pointer("/entities/1/overrides/00000000000000a1/illusion.Name/value").asString() ==
              "Kept");
    }
    SUBCASE("a prefab that contains itself")
    {
        library.files["assets:/loop.prefab"] = prefabFile(
            R"([{"id": "00000000000000e1", "components": {"illusion.Hierarchy": {"parent": null, "order": 0}}},
                {"prefab": "assets:/loop.prefab", "id": "00000000000000e2"}])");
        World world;
        const PrefabInstantiateResult placed =
            instantiatePrefab(world, PrefabData{"assets:/loop.prefab", library.files["assets:/loop.prefab"]},
                              Entity{}, kInstance, library.source());
        REQUIRE(placed.ok());
        REQUIRE(placed.entities.size() == 2);
        CHECK(world.has<UnresolvedPrefab>(placed.entities[1]));
        CHECK(std::any_of(placed.warnings.begin(), placed.warnings.end(), [](const std::string& warning) {
            return warning.find("contains itself") != std::string::npos;
        }));
    }
}

TEST_CASE("nesting stops at the depth limit")
{
    reflectAll();
    Library library;
    // Each level's root is an entity with the next level's instance as its child.
    for (u32 level = 0; level <= kMaxPrefabDepth; ++level)
    {
        const std::string inner =
            level == kMaxPrefabDepth
                ? std::string()
                : std::format(
                      R"(, {{"prefab": "assets:/level{}.prefab", "id": "00000000000000f2", "overrides":
                                   {{"00000000000000f1": {{"illusion.Hierarchy": {{"parent": "00000000000000f1"}}}}}}}})",
                      level + 1);
        library.files[std::format("assets:/level{}.prefab", level)] = prefabFile(std::format(
            R"([{{"id": "00000000000000f1", "components": {{"illusion.Name": {{"value": "Level {}"}},
                  "illusion.Hierarchy": {{"parent": null, "order": 0}}}}}}{}])",
            level, inner));
    }
    World world;
    const SceneLoadResult result =
        loadScene(world, sceneFile(R"([{"prefab": "assets:/level0.prefab", "id": "0000000000001000"}])"),
                  library.load());
    REQUIRE(result.ok());
    // Levels 0..15 expand (depth 1..16); the record of level 16 is kept unresolved.
    CHECK_FALSE(byName(world, std::format("Level {}", kMaxPrefabDepth - 1)).isNull());
    CHECK(byName(world, std::format("Level {}", kMaxPrefabDepth)).isNull());
    CHECK(std::any_of(result.warnings.begin(), result.warnings.end(), [](const std::string& warning) {
        return warning.find("nest deeper than 16") != std::string::npos;
    }));
}

TEST_CASE("overrides of entities the prefab no longer has are reported and dropped")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    World world;
    const SceneLoadResult result = loadScene(
        world, sceneFile(R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000", "overrides": {
                       "00000000000000ff": {"illusion.Name": {"value": "Ghost"}}}}])"),
        library.load());
    REQUIRE(result.ok());
    CHECK(result.entities.size() == 2);
    CHECK(std::any_of(result.warnings.begin(), result.warnings.end(), [](const std::string& warning) {
        return warning.find("match no entity of the prefab") != std::string::npos;
    }));
    const SceneSaveResult saved = saveScene(world, library.save());
    REQUIRE(saved.ok());
    CHECK(parsed(saved.json).root().pointer("/entities/0/overrides").isAbsent());
}

TEST_CASE("invalid records and overrides are load errors that add nothing")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    const char* const cases[] = {
        R"([{"prefab": 3, "id": "0000000000001000"}])",
        R"([{"prefab": "assets:/crate.prefab#mesh/0", "id": "0000000000001000"}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "zz"}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000", "overrides": []}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000", "overrides": {"x": {}}}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000", "overrides": {"00000000000000a1": 1}}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000",
             "overrides": {"00000000000000a1": {"illusion.Name": 1}}}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000",
             "overrides": {"00000000000000a1": {"illusion.EntityGuid": {}}}}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000",
             "overrides": {"00000000000000a1": {"illusion.Name": {"value": 5}}}}])",
        R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000"},
            {"prefab": "assets:/crate.prefab", "id": "0000000000001000"}])",
    };
    for (const char* entities : cases)
    {
        CAPTURE(entities);
        World world;
        const SceneLoadResult result = loadScene(world, sceneFile(entities), library.load());
        CHECK_FALSE(result.ok());
        CHECK(world.aliveCount() == 0);
    }
}

TEST_CASE("expanded saves keep prefab links for exact snapshots")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    World world;
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, Entity{}, kInstance);
    REQUIRE(placed.ok());
    SceneSaveOptions options;
    options.prefabMode = PrefabSaveMode::Expanded;
    const SceneSaveResult saved = saveScene(world, options);
    REQUIRE(saved.ok());
    const JsonDocument doc = parsed(saved.json);
    CHECK(doc.root().find("entities").size() == 2);
    CHECK(doc.root().pointer("/entities/0/prefabLink/prefab").asString() == "assets:/crate.prefab");

    World restored;
    REQUIRE(loadScene(restored, saved.json).ok());
    const Entity root = byGuid(restored, derivePrefabGuid(kInstance, kRoot));
    const Entity lid = byGuid(restored, derivePrefabGuid(kInstance, kChild));
    CHECK(restored.get<PrefabInstance>(root).instance == kInstance);
    CHECK(restored.get<PrefabMember>(lid).local == kChild);
    // The restored instance saves as the same record.
    const SceneSaveResult again = saveScene(restored, library.save());
    REQUIRE(again.ok());
    CHECK(parsed(again.json).root().find("entities").size() == 1);

    SUBCASE("copies with new ids are plain entities")
    {
        SceneLoadOptions copy;
        copy.ids = IdPolicy::Regenerate;
        World pasted;
        const SceneLoadResult result = loadScene(pasted, saved.json, copy);
        REQUIRE(result.ok());
        CHECK_FALSE(pasted.has<PrefabMember>(result.entities[0]));
    }
}

TEST_CASE("pasting a scene with instance records twice gives distinct linked instances")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    const std::string scene = sceneFile(R"([{"prefab": "assets:/crate.prefab", "id": "0000000000001000"}])");
    World world;
    SceneLoadOptions options = library.load();
    options.ids = IdPolicy::Regenerate;
    const SceneLoadResult first = loadScene(world, scene, options);
    const SceneLoadResult second = loadScene(world, scene, options);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(world.aliveCount() == 4);
    CHECK(world.get<PrefabMember>(first.entities[0]).instance !=
          world.get<PrefabMember>(second.entities[0]).instance);
    CHECK(first.fileIds == second.fileIds);
    const Entity lid = first.entities[1];
    CHECK(world.get<EntityGuid>(lid) == derivePrefabGuid(world.get<PrefabMember>(lid).instance, kChild));
}

TEST_CASE("prefab files are written with one root")
{
    reflectAll();
    World world;
    const Entity root = world.create();
    world.emplace<EntityGuid>(root, kRoot);
    world.emplace<Name>(root, Name{"Root"});
    SceneSaveOptions options;
    options.prefabFile = true;
    const SceneSaveResult saved = saveScene(world, options);
    REQUIRE(saved.ok());
    CHECK(parsed(saved.json).root().find("format").asString() == "illusion.prefab");
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/root.prefab", saved.json}, Entity{}, kInstance);
    CHECK(placed.ok());

    const Entity second = world.create();
    world.emplace<EntityGuid>(second, kChild);
    const std::array both{root, second};
    options.subset = std::span<const Entity>(both);
    const SceneSaveResult twoRoots = saveScene(world, options);
    CHECK_FALSE(twoRoots.ok());
    CHECK(twoRoots.error.find("exactly one root") != std::string::npos);
}

TEST_CASE("members a changed prefab no longer has are saved as plain entities, keeping references to them")
{
    reflectAll();
    Library library;
    library.files["assets:/crate.prefab"] = cratePrefab();
    World world;
    const PrefabInstantiateResult placed =
        instantiatePrefab(world, PrefabData{"assets:/crate.prefab", cratePrefab()}, Entity{}, kInstance);
    REQUIRE(placed.ok());
    const Entity sticker = world.create();
    world.emplace<EntityGuid>(sticker, EntityGuid{0x55});
    world.emplace<Name>(sticker, Name{"Sticker"});
    REQUIRE(setParent(world, sticker, placed.entities[1]).hasValue());

    // The prefab loses its lid after the instance was made.
    library.files["assets:/crate.prefab"] = prefabFile(
        R"([{"id": "00000000000000a1", "components": {"illusion.Name": {"value": "Crate"},
             "illusion.Hierarchy": {"parent": null, "order": 0}, "illusion.LocalTransform": {}}}])");
    const SceneSaveResult saved = saveScene(world, library.save());
    REQUIRE(saved.ok());
    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json, library.load());
    REQUIRE(result.ok());
    const Entity lid = byName(loaded, "Lid");
    REQUIRE_FALSE(lid.isNull());
    CHECK_FALSE(loaded.has<PrefabMember>(lid));
    CHECK(loaded.get<Hierarchy>(byName(loaded, "Sticker")).parent == lid);
    CHECK(loaded.get<PrefabMember>(byName(loaded, "Crate")).local == kRoot);
}

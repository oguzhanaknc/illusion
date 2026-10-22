#include "ReflectedComponents.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace illusion;
using namespace reflection_test;

namespace guid_test
{

struct Labels
{
    std::vector<std::string> names;
    std::optional<std::string> title;
};

std::string_view validateName(const void* value) noexcept
{
    return entityNameError(*static_cast<const std::string*>(value));
}

} // namespace guid_test

TEST_CASE("ids are written as 16 hex digits and parsed strictly")
{
    CHECK(toString(EntityGuid{0x3f9a0c1e5b7d2a40}) == "3f9a0c1e5b7d2a40");
    CHECK(toString(EntityGuid{1}) == "0000000000000001");
    CHECK(toString(EntityGuid{~u64{0}}) == "ffffffffffffffff");
    CHECK(parseGuid("3f9a0c1e5b7d2a40") == EntityGuid{0x3f9a0c1e5b7d2a40});
    CHECK(parseGuid("3F9A0C1E5B7D2A40") == EntityGuid{0x3f9a0c1e5b7d2a40});
    CHECK(parseGuid("ffffffffffffffff") == EntityGuid{~u64{0}});
    for (const std::string_view invalid :
         {"", "0000000000000000", "3f9a0c1e5b7d2a4", "3f9a0c1e5b7d2a400", "3f9a0c1e5b7d2a4g",
          " f9a0c1e5b7d2a40", "0x9a0c1e5b7d2a40", "-f9a0c1e5b7d2a40"})
    {
        CAPTURE(invalid);
        CHECK_FALSE(parseGuid(invalid).has_value());
    }
    CHECK(EntityGuid{}.isNull());
    CHECK_FALSE(EntityGuid{1}.isNull());
}

TEST_CASE("a seeded generator gives the same ids on every platform")
{
    GuidGenerator a(42);
    GuidGenerator b(42);
    GuidGenerator other(43);
    CHECK(toString(a.next()) == "a759ea27d4727622");
    CHECK(toString(a.next()) == "bdd732262feb6e95");
    CHECK(toString(a.next()) == "28efe333b266f103");
    CHECK(toString(b.next()) == "a759ea27d4727622");
    CHECK(other.next() != EntityGuid{0xa759ea27d4727622});
}

TEST_CASE("a generator never repeats an id and never makes the null id")
{
    GuidGenerator secure;
    constexpr usize kCount = 1'000'000;
    std::vector<u64> ids(kCount);
    for (u64& id : ids)
    {
        id = secure.next().value;
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    CHECK(ids.front() != 0);

    // Seed 0 maps the first counter to 0, which is skipped.
    GuidGenerator zero(0);
    CHECK_FALSE(zero.next().isNull());
}

TEST_CASE("generators can be shared between threads")
{
    GuidGenerator generator(5);
    constexpr usize kThreads = 4;
    constexpr usize kPerThread = 20000;
    std::vector<std::vector<u64>> made(kThreads);
    {
        std::vector<std::jthread> threads;
        for (usize t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&generator, &list = made[t]] {
                list.reserve(kPerThread);
                for (usize i = 0; i < kPerThread; ++i)
                {
                    list.push_back(generator.next().value);
                }
            });
        }
    }
    std::vector<u64> all;
    for (const std::vector<u64>& list : made)
    {
        all.insert(all.end(), list.begin(), list.end());
    }
    std::sort(all.begin(), all.end());
    CHECK(all.size() == kThreads * kPerThread);
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());

    // Together the threads made exactly the first ids of the sequence.
    GuidGenerator sequence(5);
    std::vector<u64> expected(kThreads * kPerThread);
    for (u64& id : expected)
    {
        id = sequence.next().value;
    }
    std::sort(expected.begin(), expected.end());
    CHECK(all == expected);
}

TEST_CASE("assignMissingGuids gives ids in entity order to entities without one")
{
    World world;
    const Entity a = world.create(Transform{});
    const Entity b = world.create(EntityGuid{77});
    const Entity c = world.create();
    const Entity d = world.create(Transform{}, Name{"d"});
    world.destroy(a);
    const Entity e = world.create(Transform{}); // reuses a's slot: lowest index

    GuidGenerator guids(42);
    CHECK(assignMissingGuids(world, guids) == 3);
    CHECK(world.get<EntityGuid>(b).value == 77);
    CHECK(toString(world.get<EntityGuid>(e)) == "a759ea27d4727622");
    CHECK(toString(world.get<EntityGuid>(c)) == "bdd732262feb6e95");
    CHECK(toString(world.get<EntityGuid>(d)) == "28efe333b266f103");
    CHECK(world.has<Transform>(d));
    CHECK(world.get<Name>(d).value == "d");
    CHECK(assignMissingGuids(world, guids) == 0);
    CHECK(assignMissingGuids(world) == 0);

    World empty;
    CHECK(assignMissingGuids(empty) == 0);
}

TEST_CASE("GuidEntityResolver writes and reads Entity fields as ids")
{
    reflectTestComponents();
    reflectCoreComponents();
    World world;
    const Entity leader = world.create(EntityGuid{0xab});
    const Entity follower = world.create(Follow{leader, 2.0f}, EntityGuid{0xcd});
    const Entity anonymous = world.create();
    GuidEntityResolver ids(world);
    CHECK(ids.find(EntityGuid{0xab}) == leader);
    CHECK(ids.find(EntityGuid{0x99}).isNull());
    CHECK(ids.guidOf(follower) == EntityGuid{0xcd});
    CHECK(ids.guidOf(anonymous).isNull());

    const TypeReflection& follow = *ComponentRegistry::instance().reflection(componentId<Follow>());
    JsonMutDocument doc;
    auto written = writeValue(doc, follow, &world.get<Follow>(follower), {.entities = &ids});
    REQUIRE(written.hasValue());
    CHECK(written->dump() == R"({"leader":"00000000000000ab","distance":2.0})");

    Follow read;
    auto json = JsonDocument::parse(R"({"leader": "00000000000000CD"})");
    REQUIRE(json.hasValue());
    REQUIRE(readValue(follow, &read, json->root(), {.entities = &ids}).ok());
    CHECK(read.leader == follower);

    // References to entities without an id, or ids the World does not have, are null or rejected.
    world.get<Follow>(follower).leader = anonymous;
    written = writeValue(doc, follow, &world.get<Follow>(follower), {.entities = &ids});
    REQUIRE(written.hasValue());
    CHECK(written->dump() == R"({"leader":null,"distance":2.0})");
    for (const char* invalid :
         {R"({"leader": "0000000000000099"})", R"({"leader": 171})", R"({"leader": "ab"})"})
    {
        CAPTURE(invalid);
        auto bad = JsonDocument::parse(invalid);
        REQUIRE(bad.hasValue());
        const ValueReadResult result = readValue(follow, &read, bad->root(), {.entities = &ids});
        REQUIRE_FALSE(result.ok());
        CHECK(result.errors[0].message == "must be null or the id of an entity in this world");
    }

    // The index follows the World after refresh().
    world.destroy(leader);
    CHECK(ids.find(EntityGuid{0xab}).isNull());
    const Entity late = world.create(EntityGuid{0xef});
    CHECK(ids.find(EntityGuid{0xef}).isNull());
    ids.refresh();
    CHECK(ids.find(EntityGuid{0xef}) == late);
}

TEST_CASE("entity names reject slashes, control characters and long text")
{
    CHECK(isValidEntityName(""));
    CHECK(isValidEntityName("Player 1"));
    CHECK(isValidEntityName("\xC3\x9Cnl\xC3\xBC \xE2\x9C\x93"));
    CHECK(isValidEntityName(std::string(kMaxEntityNameBytes, 'x')));
    CHECK(entityNameError(std::string(kMaxEntityNameBytes + 1, 'x')) == "must not be longer than 256 bytes");
    CHECK(entityNameError("a/b") == "must not contain '/'");
    CHECK(entityNameError("line\nbreak") == "must not contain control characters");
    CHECK(entityNameError(std::string_view("nul\0", 4)) == "must not contain control characters");
    CHECK(entityNameError("del\x7F") == "must not contain control characters");

    // The editor's paths and JSON reads apply the same rule.
    reflectCoreComponents();
    const TypeReflection& name = *ComponentRegistry::instance().reflection(componentId<Name>());
    Name value{"Old"};
    auto json = JsonDocument::parse(R"("a/b")");
    REQUIRE(json.hasValue());
    const auto set = setPath(name, &value, "value", json->root());
    REQUIRE_FALSE(set.hasValue());
    CHECK(set.error().pointer == "/value");
    CHECK(set.error().message == "must not contain '/'");
    CHECK(value.value == "Old");
}

TEST_CASE("field validators run on every element of arrays and optionals")
{
    reflectStruct<guid_test::Labels>("test.Labels")
        .field("names", &guid_test::Labels::names, {.validate = guid_test::validateName})
        .field("title", &guid_test::Labels::title, {.validate = guid_test::validateName});
    const StructRegistry& structs = ComponentRegistry::instance().structs();
    const StructReflection& type = *structs.reflection(structs.find(typeHash<guid_test::Labels>()));
    guid_test::Labels labels;
    const auto read = [&](const char* text) {
        auto json = JsonDocument::parse(text);
        REQUIRE(json.hasValue());
        const ValueReadResult result = readValue(type, &labels, json->root());
        return result.ok() ? std::string("ok") : result.errors[0].pointer + " " + result.errors[0].message;
    };
    CHECK(read(R"({"names": ["a", "b"], "title": "c"})") == "ok");
    CHECK(read(R"({"names": ["a", "b/c"]})") == "/names/1 must not contain '/'");
    CHECK(read(R"({"title": "x/y"})") == "/title must not contain '/'");
    CHECK(labels.names == std::vector<std::string>{"a", "b"}); // failed reads change nothing
    CHECK(labels.title == "c");

    reflectCoreComponents();
    const TypeReflection& guid = *ComponentRegistry::instance().reflection(componentId<EntityGuid>());
    CHECK(guid.name == "illusion.EntityGuid");
    CHECK(guid.fields[0].meta.readOnly);
}

TEST_CASE("forEachMatching iterates chunks by ComponentId")
{
    World world;
    constexpr u32 kCount = 1000; // several chunks
    for (u32 i = 0; i < kCount; ++i)
    {
        if (i % 3 == 0)
        {
            world.create(EntityGuid{i + 1}, Transform{});
        }
        else
        {
            world.create(EntityGuid{i + 1});
        }
    }
    world.create(Transform{});
    const ComponentId guidId = componentId<EntityGuid>();
    const ComponentId transformId = componentId<Transform>();

    ComponentMask all;
    all.set(guidId);
    u64 sum = 0;
    u32 seen = 0;
    u32 chunks = 0;
    world.forEachMatching(all, {}, [&](RawChunk& chunk) {
        CHECK(world.isIterating());
        CHECK(chunk.has(guidId));
        CHECK(chunk.size() == chunk.entities().size());
        const auto* guids = static_cast<const EntityGuid*>(chunk.column(guidId));
        for (u32 i = 0; i < chunk.size(); ++i)
        {
            CHECK(world.get<EntityGuid>(chunk.entities()[i]) == guids[i]);
            sum += guids[i].value;
        }
        seen += chunk.size();
        ++chunks;
    });
    CHECK_FALSE(world.isIterating());
    CHECK(seen == kCount);
    CHECK(sum == u64{kCount} * (kCount + 1) / 2);
    CHECK(chunks > 2);

    // Excluded types, and the const overload.
    ComponentMask none;
    none.set(transformId);
    const World& constWorld = world;
    seen = 0;
    constWorld.forEachMatching(all, none, [&](const RawChunk& chunk) {
        CHECK_FALSE(chunk.has(transformId));
        CHECK(chunk.column(transformId) == nullptr);
        seen += chunk.size();
    });
    CHECK(seen == kCount - (kCount + 2) / 3);

    // An empty mask matches every entity.
    seen = 0;
    world.forEachMatching({}, {}, [&](const RawChunk& chunk) { seen += chunk.size(); });
    CHECK(seen == world.aliveCount());
}

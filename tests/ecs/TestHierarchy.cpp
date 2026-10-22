#include "TestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/core/Random.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

bool near(Vec3 a, Vec3 b, f32 epsilon = 1e-4f)
{
    return glm::length(a - b) <= epsilon * std::max(1.0f, glm::length(b));
}

bool near(const Quat& a, const Quat& b, f32 epsilon = 1e-4f)
{
    return std::abs(std::abs(glm::dot(glm::normalize(a), glm::normalize(b))) - 1.0f) <= epsilon;
}

Transform placed(Vec3 position, f32 angle = 0.0f, Vec3 scale = Vec3(1.0f))
{
    return Transform{position, glm::angleAxis(angle, glm::normalize(Vec3(0.3f, 1.0f, 0.2f))), scale};
}

u32 orderOf(const World& world, Entity entity)
{
    return world.get<Hierarchy>(entity).order;
}

std::vector<Entity> asVector(std::span<const Entity> entities)
{
    return {entities.begin(), entities.end()};
}

} // namespace

TEST_CASE("component versions grow when a component type is added, removed or announced")
{
    World world;
    const u64 start = world.componentVersion<Hierarchy>();
    const Entity entity = world.create(Hierarchy{});
    CHECK(world.componentVersion<Hierarchy>() == start + 1);
    const u64 transformVersion = world.componentVersion<Transform>();
    world.emplace<Transform>(entity);
    CHECK(world.componentVersion<Transform>() == transformVersion + 1);
    CHECK(world.componentVersion<Hierarchy>() == start + 1); // other types are unaffected
    world.get<Hierarchy>(entity).order = 3;                  // writing values alone changes nothing
    CHECK(world.componentVersion<Hierarchy>() == start + 1);
    world.markChanged<Hierarchy>();
    CHECK(world.componentVersion<Hierarchy>() == start + 2);
    CHECK(world.remove<Hierarchy>(entity));
    CHECK(world.componentVersion<Hierarchy>() == start + 3);
    CHECK_FALSE(world.remove<Hierarchy>(entity));
    CHECK(world.componentVersion<Hierarchy>() == start + 3);
    world.destroy(entity);
    CHECK(world.componentVersion<Transform>() == transformVersion + 2);
    world.create(Transform{});
    const u64 beforeClear = world.componentVersion<Transform>();
    world.clear();
    CHECK(world.componentVersion<Transform>() == beforeClear + 1);
}

TEST_CASE("setParent keeps the world placement and keeps sibling orders dense")
{
    World world;
    const Entity parent = world.create(placed(Vec3(1.0f, 2.0f, 3.0f), 0.8f, Vec3(2.0f)));
    const Entity a = world.create(placed(Vec3(4.0f, 0.0f, 0.0f), -0.3f));
    const Entity b = world.create(placed(Vec3(0.0f, 5.0f, 0.0f)));
    const Entity c = world.create(placed(Vec3(0.0f, 0.0f, 6.0f)));
    const Transform aBefore = world.get<Transform>(a);

    REQUIRE(setParent(world, a, parent));
    REQUIRE(setParent(world, b, parent));
    REQUIRE(setParent(world, c, parent, ParentMode::KeepWorld, 0)); // inserted first
    CHECK(parentOf(world, a) == parent);
    CHECK(world.has<Hierarchy>(parent)); // the parent became a root
    CHECK(parentOf(world, parent).isNull());
    CHECK(orderOf(world, c) == 0);
    CHECK(orderOf(world, a) == 1);
    CHECK(orderOf(world, b) == 2);

    // The Transform did not move, and combining the new LocalTransform gives it back.
    CHECK(world.get<Transform>(a).position == aBefore.position);
    const Transform derived =
        combine(world.get<Transform>(parent), world.get<LocalTransform>(a).asTransform());
    CHECK(near(derived.position, aBefore.position));
    CHECK(near(derived.rotation, aBefore.rotation));
    CHECK(near(derived.scale, aBefore.scale));

    // Moving a to the root closes the gap among its old siblings.
    REQUIRE(detach(world, a));
    CHECK(parentOf(world, a).isNull());
    CHECK(orderOf(world, c) == 0);
    CHECK(orderOf(world, b) == 1);
    CHECK(orderOf(world, parent) == 0);
    CHECK(orderOf(world, a) == 1);
    CHECK(near(world.get<LocalTransform>(a).position, aBefore.position));
}

TEST_CASE("setParent with KeepLocal moves the child with its new parent")
{
    World world;
    const Entity parent = world.create(placed(Vec3(10.0f, 0.0f, 0.0f)));
    const Entity child = world.create(placed(Vec3(1.0f, 0.0f, 0.0f)), LocalTransform{Vec3(0.0f, 2.0f, 0.0f)});
    REQUIRE(setParent(world, child, parent, ParentMode::KeepLocal));
    CHECK(world.get<LocalTransform>(child).position == Vec3(0.0f, 2.0f, 0.0f));
    CHECK(near(world.get<Transform>(child).position, Vec3(10.0f, 2.0f, 0.0f)));

    // An entity without any transform gets both.
    const Entity bare = world.create();
    REQUIRE(setParent(world, bare, parent));
    CHECK(world.has<LocalTransform>(bare));
    CHECK(near(world.get<Transform>(bare).position, Vec3(10.0f, 0.0f, 0.0f)));
}

TEST_CASE("setParent rejects cycles and dead entities without changing anything")
{
    World world;
    const Entity a = world.create(Transform{});
    const Entity b = world.create(Transform{});
    const Entity c = world.create(Transform{});
    REQUIRE(setParent(world, b, a));
    REQUIRE(setParent(world, c, b));
    const u64 version = world.componentVersion<Hierarchy>();

    const auto self = setParent(world, a, a);
    REQUIRE_FALSE(self);
    CHECK(self.error() == HierarchyError::Cycle);
    const auto descendant = setParent(world, a, c);
    REQUIRE_FALSE(descendant);
    CHECK(descendant.error() == HierarchyError::Cycle);
    CHECK(world.componentVersion<Hierarchy>() == version);
    CHECK(parentOf(world, a).isNull());

    const Entity dead = world.create();
    world.destroy(dead);
    CHECK(setParent(world, a, dead).error() == HierarchyError::DeadEntity);
    CHECK(setParent(world, dead, a).error() == HierarchyError::DeadEntity);
    CHECK(reorder(world, dead, 0).error() == HierarchyError::DeadEntity);
    CHECK_FALSE(hierarchyErrorMessage(HierarchyError::Cycle).empty());
    CHECK_FALSE(hierarchyErrorMessage(HierarchyError::DeadEntity).empty());
}

TEST_CASE("reorder moves an entity among its siblings")
{
    World world;
    const Entity parent = world.create(Transform{});
    std::vector<Entity> children;
    for (int i = 0; i < 4; ++i)
    {
        children.push_back(world.create(Transform{}));
        REQUIRE(setParent(world, children.back(), parent));
    }
    REQUIRE(reorder(world, children[3], 1));
    HierarchyIndex index;
    index.update(world);
    CHECK(asVector(index.children(parent)) ==
          std::vector<Entity>{children[0], children[3], children[1], children[2]});
    REQUIRE(reorder(world, children[0], kLastSibling));
    index.update(world);
    CHECK(asVector(index.children(parent)) ==
          std::vector<Entity>{children[3], children[1], children[2], children[0]});
    for (u32 i = 0; i < 4; ++i)
    {
        CHECK(orderOf(world, index.children(parent)[i]) == i);
    }
    // An entity without a Hierarchy becomes the last root.
    const Entity loose = world.create();
    REQUIRE(reorder(world, loose, 0));
    CHECK(world.has<Hierarchy>(loose));
    CHECK(parentOf(world, loose).isNull());
}

TEST_CASE("destroyRecursive destroys a subtree and closes the gap among siblings")
{
    World world;
    const Entity root = world.create(Transform{});
    const Entity a = world.create(Transform{});
    const Entity b = world.create(Transform{});
    const Entity grandchild = world.create(Transform{});
    const Entity greatGrandchild = world.create(Transform{});
    REQUIRE(setParent(world, a, root));
    REQUIRE(setParent(world, b, root));
    REQUIRE(setParent(world, grandchild, a));
    REQUIRE(setParent(world, greatGrandchild, grandchild));

    CHECK(destroyRecursive(world, a) == 3);
    CHECK_FALSE(world.isAlive(a));
    CHECK_FALSE(world.isAlive(grandchild));
    CHECK_FALSE(world.isAlive(greatGrandchild));
    CHECK(world.isAlive(b));
    CHECK(orderOf(world, b) == 0);
    CHECK(destroyRecursive(world, a) == 0);
    CHECK(destroyRecursive(world, root) == 2);
    CHECK(world.aliveCount() == 0);
}

TEST_CASE("the index orders children, numbers depths and lists subtrees in pre-order")
{
    World world;
    GuidGenerator guids(7);
    const Entity root = world.create(Transform{});
    const Entity a = world.create(Transform{});
    const Entity b = world.create(Transform{});
    const Entity a1 = world.create(Transform{});
    const Entity a2 = world.create(Transform{});
    const Entity other = world.create(Transform{});
    REQUIRE(setParent(world, a, root));
    REQUIRE(setParent(world, b, root));
    REQUIRE(setParent(world, a1, a));
    REQUIRE(setParent(world, a2, a));
    REQUIRE(reorder(world, other, kLastSibling));
    assignMissingGuids(world, guids);

    HierarchyIndex index;
    CHECK_FALSE(index.isCurrent(world));
    CHECK(index.update(world));
    CHECK(index.isCurrent(world));
    CHECK_FALSE(index.update(world)); // nothing changed
    CHECK(index.size() == 6);
    CHECK(asVector(index.roots()) == std::vector<Entity>{root, other});
    CHECK(asVector(index.children(a)) == std::vector<Entity>{a1, a2});
    CHECK(index.children(b).empty());
    CHECK(index.parent(a2) == a);
    CHECK(index.parent(root).isNull());
    CHECK(index.depth(a2) == 2);
    CHECK(asVector(index.preOrder()) == std::vector<Entity>{root, a, a1, a2, b, other});
    CHECK(asVector(index.subtree(a)) == std::vector<Entity>{a, a1, a2});
    REQUIRE(index.levelCount() == 3);
    CHECK(asVector(index.level(0)) == std::vector<Entity>{root, other});
    CHECK(asVector(index.level(1)) == std::vector<Entity>{a, b});
    CHECK(asVector(index.level(2)) == std::vector<Entity>{a1, a2});

    std::vector<std::pair<Entity, u32>> visited;
    index.depthFirst(a, [&](Entity entity, u32 depth) { visited.emplace_back(entity, depth); });
    CHECK(visited == std::vector<std::pair<Entity, u32>>{{a, 0}, {a1, 1}, {a2, 1}});
    std::vector<Entity> children;
    index.forEachChild(root, [&](Entity child) { children.push_back(child); });
    CHECK(children == std::vector<Entity>{a, b});

    const Entity unrelated = world.create(Transform{});
    CHECK(index.update(world) == false); // other components do not matter
    CHECK_FALSE(index.contains(unrelated));
    CHECK(index.subtree(unrelated).empty());
    CHECK(index.depth(unrelated) == 0);
}

TEST_CASE("the index sorts equal orders by id and notices parents without a Hierarchy dying")
{
    World world;
    const Entity parent = world.create(Transform{}); // no Hierarchy
    const Entity first = world.create(EntityGuid{2}, Hierarchy{parent, 0});
    const Entity second = world.create(EntityGuid{1}, Hierarchy{parent, 0});
    HierarchyIndex index;
    index.update(world);
    CHECK(asVector(index.children(parent)) == std::vector<Entity>{second, first});
    CHECK(asVector(index.roots()) == std::vector<Entity>{parent});

    world.destroy(parent);
    CHECK_FALSE(index.isCurrent(world));
    CHECK(index.update(world));
    CHECK(asVector(index.roots()) == std::vector<Entity>{second, first}); // orphans are roots
}

TEST_CASE("Hierarchy cycles written directly are indexed as roots")
{
    World world;
    const Entity a = world.create(EntityGuid{1});
    const Entity b = world.create(EntityGuid{2});
    world.emplace<Hierarchy>(a, Hierarchy{b, 0});
    world.emplace<Hierarchy>(b, Hierarchy{a, 0});
    HierarchyIndex index;
    index.update(world);
    CHECK(index.size() == 2);
    CHECK(asVector(index.roots()) == std::vector<Entity>{a});
    CHECK(index.parent(b) == a);
    // The editor API refuses to make such cycles.
    CHECK(setParent(world, a, b).error() == HierarchyError::Cycle);
}

TEST_CASE("transforms propagate through 8 levels with non-uniform scale")
{
    World world;
    HierarchyIndex index;
    std::vector<Entity> chain;
    chain.push_back(
        world.create(placed(Vec3(1.0f, 0.0f, 0.0f), 0.2f, Vec3(1.0f, 2.0f, 0.5f)), LocalTransform{}));
    for (int depth = 1; depth < 8; ++depth)
    {
        const Entity child = world.create(Transform{});
        REQUIRE(setParent(world, child, chain.back(), ParentMode::KeepLocal));
        world.get<LocalTransform>(child) = LocalTransform::from(
            placed(Vec3(0.5f, 1.0f, -0.25f), 0.1f * static_cast<f32>(depth), Vec3(1.1f, 0.9f, 1.0f)));
        chain.push_back(child);
    }
    updateTransformHierarchy(world, index);

    // Positions are exact: they match the product of the parent matrices. Rotations multiply.
    Transform expected = world.get<Transform>(chain[0]);
    Mat4 matrix = expected.matrix();
    for (usize depth = 1; depth < chain.size(); ++depth)
    {
        const Transform local = world.get<LocalTransform>(chain[depth]).asTransform();
        const Vec3 position = Vec3(matrix * Vec4(local.position, 1.0f));
        expected = combine(expected, local);
        matrix = matrix * local.matrix();
        const Transform& actual = world.get<Transform>(chain[depth]);
        CHECK(near(actual.position, expected.position));
        CHECK(near(actual.rotation, expected.rotation));
        CHECK(near(actual.scale, expected.scale));
        if (depth == 1)
        {
            CHECK(near(actual.position, position)); // exact below an unsheared parent
        }
    }
    // The root's LocalTransform mirrors its Transform.
    CHECK(world.get<LocalTransform>(chain[0]).position == world.get<Transform>(chain[0]).position);

    // Moving the root moves the whole chain on the next update.
    world.get<Transform>(chain[0]).position += Vec3(0.0f, 10.0f, 0.0f);
    const Vec3 tipBefore = world.get<Transform>(chain.back()).position;
    updateTransformHierarchy(world, index);
    CHECK(near(world.get<Transform>(chain.back()).position, tipBefore + Vec3(0.0f, 10.0f, 0.0f)));
}

TEST_CASE("world-space authorities write their LocalTransform and missing Transforms are added")
{
    World world;
    HierarchyIndex index;
    const Entity parent = world.create(placed(Vec3(5.0f, 0.0f, 0.0f), 0.0f, Vec3(2.0f)));
    const Entity body = world.create(Transform{});
    REQUIRE(setParent(world, body, parent));
    world.emplace<WorldSpaceAuthority>(body);
    world.get<Transform>(body).position = Vec3(9.0f, 4.0f, 0.0f); // moved by "physics"
    const Entity child = world.create(LocalTransform{Vec3(1.0f, 0.0f, 0.0f)}, Hierarchy{body, 0});

    updateTransformHierarchy(world, index);
    CHECK(world.get<Transform>(body).position == Vec3(9.0f, 4.0f, 0.0f));
    CHECK(near(world.get<LocalTransform>(body).position, Vec3(2.0f, 2.0f, 0.0f)));
    REQUIRE(world.has<Transform>(child));
    CHECK(near(world.get<Transform>(child).position, Vec3(10.0f, 4.0f, 0.0f))); // the body's scale is 1
}

TEST_CASE("parallel propagation equals serial propagation on 100k entities")
{
    constexpr u32 kCount = 100000;
    JobSystem jobs(JobSystemDesc{.workerCount = 4});
    World serialWorld;
    World parallelWorld;
    for (World* world : {&serialWorld, &parallelWorld})
    {
        // A forest of depth 8: entity i's parent is (i - 1) / 4 among the first ones, so levels are wide.
        std::vector<Entity> entities;
        entities.reserve(kCount);
        Random values(42);
        for (u32 i = 0; i < kCount; ++i)
        {
            const Entity parent = i == 0 ? Entity{} : entities[(i - 1) / 4];
            const LocalTransform local{Vec3(values.uniformF32(), values.uniformF32(), values.uniformF32()),
                                       glm::angleAxis(values.uniformF32(), Vec3(0.0f, 1.0f, 0.0f)),
                                       Vec3(1.0f, 1.0f + 0.01f * values.uniformF32(), 1.0f)};
            entities.push_back(
                world->create(Hierarchy{parent, i == 0 ? 0u : (i - 1) % 4}, local, Transform{}));
        }
    }
    HierarchyIndex serialIndex;
    HierarchyIndex parallelIndex;
    updateTransformHierarchy(serialWorld, serialIndex);
    updateTransformHierarchy(parallelWorld, parallelIndex, &jobs);
    CHECK(serialIndex.levelCount() == parallelIndex.levelCount());
    CHECK(serialIndex.levelCount() >= 8);

    std::vector<Entity> serialEntities;
    std::vector<Entity> parallelEntities;
    serialWorld.entities(serialEntities);
    parallelWorld.entities(parallelEntities);
    REQUIRE(serialEntities.size() == parallelEntities.size());
    usize mismatches = 0;
    for (usize i = 0; i < serialEntities.size(); ++i)
    {
        const Transform& a = serialWorld.get<Transform>(serialEntities[i]);
        const Transform& b = parallelWorld.get<Transform>(parallelEntities[i]);
        mismatches += a.position != b.position || a.rotation != b.rotation || a.scale != b.scale ? 1u : 0u;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("steady-state hierarchy updates do not allocate")
{
    JobSystem jobs(JobSystemDesc{.workerCount = 2});
    World world;
    HierarchyIndex index;
    // Pairs of a root and a child; every 100th root is Disabled.
    for (u32 i = 0; i < 2500; ++i)
    {
        const Entity root = world.create(Hierarchy{Entity{}, i}, LocalTransform{}, Transform{});
        world.create(Hierarchy{root, 0}, LocalTransform{Vec3(1.0f)}, Transform{});
        if (i % 100 == 0)
        {
            world.emplace<Disabled>(root);
        }
    }
    updateTransformHierarchy(world, index, &jobs);
    updateTransformHierarchy(world, index, &jobs);
    const test::ScopedAllocationCounter counter;
    updateTransformHierarchy(world, index, &jobs);
    updateTransformHierarchy(world, index);
    CHECK(counter.count() == 0);
}

TEST_CASE("Disabled makes a subtree Inactive until it is enabled again")
{
    World world;
    HierarchyIndex index;
    const Entity root = world.create(Transform{});
    const Entity child = world.create(Transform{});
    const Entity grandchild = world.create(Transform{});
    const Entity sibling = world.create(Transform{});
    const Entity loose = world.create(Transform{}, Disabled{}); // no Hierarchy
    REQUIRE(setParent(world, child, root));
    REQUIRE(setParent(world, grandchild, child));
    REQUIRE(setParent(world, sibling, root));

    updateTransformHierarchy(world, index);
    CHECK(world.has<Inactive>(loose));
    CHECK_FALSE(world.has<Inactive>(child));

    setEnabled(world, child, false);
    CHECK(world.has<Disabled>(child));
    CHECK_FALSE(isEnabledInHierarchy(world, grandchild)); // known before the update
    CHECK(isEnabledInHierarchy(world, sibling));
    updateTransformHierarchy(world, index);
    CHECK(world.has<Inactive>(child));
    CHECK(world.has<Inactive>(grandchild));
    CHECK_FALSE(world.has<Inactive>(root));
    CHECK_FALSE(world.has<Inactive>(sibling));

    // Steady state: no structural change.
    const u64 version = world.structuralVersion();
    updateTransformHierarchy(world, index);
    CHECK(world.structuralVersion() == version);

    // Inactive removed by hand comes back; disabling twice is harmless.
    world.remove<Inactive>(grandchild);
    setEnabled(world, child, false);
    updateTransformHierarchy(world, index);
    CHECK(world.has<Inactive>(grandchild));

    // Moving the grandchild out of the disabled subtree activates it.
    REQUIRE(setParent(world, grandchild, sibling));
    updateTransformHierarchy(world, index);
    CHECK_FALSE(world.has<Inactive>(grandchild));

    setEnabled(world, child, true);
    setEnabled(world, loose, true);
    updateTransformHierarchy(world, index);
    CHECK_FALSE(world.has<Inactive>(child));
    CHECK_FALSE(world.has<Inactive>(loose));
    CHECK_FALSE(world.has<Disabled>(child));
}

TEST_CASE("makeValidEntityName replaces reserved characters and keeps whole characters")
{
    CHECK(makeValidEntityName("Arm/Left\tHand") == "Arm_Left_Hand");
    CHECK(makeValidEntityName("Çatı") == "Çatı");
    // 255 ASCII bytes followed by a two-byte character: the character does not fit and is dropped whole.
    const std::string longName = std::string(255, 'a') + "ç";
    const std::string valid = makeValidEntityName(longName);
    CHECK(valid == std::string(255, 'a'));
    CHECK(isValidEntityName(valid));
}

TEST_CASE("Hierarchy, LocalTransform and Disabled are saved; Inactive and authorities are not")
{
    reflectCoreComponents();
    World world;
    HierarchyIndex index;
    GuidGenerator guids(11);
    const Entity root = world.create(placed(Vec3(1.0f, 2.0f, 3.0f), 0.5f));
    const Entity child = world.create(placed(Vec3(4.0f, 0.0f, 0.0f)));
    REQUIRE(setParent(world, child, root));
    setEnabled(world, child, false);
    world.emplace<WorldSpaceAuthority>(root);
    updateTransformHierarchy(world, index);
    assignMissingGuids(world, guids);

    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.json.find("illusion.Hierarchy") != std::string::npos);
    CHECK(saved.json.find("illusion.Disabled") != std::string::npos);
    CHECK(saved.json.find("illusion.Inactive") == std::string::npos);
    CHECK(saved.json.find("illusion.WorldSpaceAuthority") == std::string::npos);
    // The child's Transform is derived, so only the root saves one.
    usize transforms = 0;
    for (usize at = saved.json.find("\"illusion.Transform\""); at != std::string::npos;
         at = saved.json.find("\"illusion.Transform\"", at + 1))
    {
        ++transforms;
    }
    CHECK(transforms == 2); // the root's and the "types" table entry

    World loaded;
    HierarchyIndex loadedIndex;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 2);
    const Entity loadedRoot = result.entities[0];
    const Entity loadedChild = result.entities[1];
    CHECK(parentOf(loaded, loadedChild) == loadedRoot);
    CHECK_FALSE(loaded.has<Transform>(loadedChild));
    updateTransformHierarchy(loaded, loadedIndex);
    CHECK(near(loaded.get<Transform>(loadedChild).position, world.get<Transform>(child).position));
    CHECK(loaded.has<Inactive>(loadedChild));
}

# ECS

illusion stores game state in an Entity Component System (ECS). An **Entity** is a lightweight handle,
a **Component** is plain data attached to an Entity, and systems are functions that run **Queries**
over every Entity with a given set of Components.

Header: `#include <illusion/ecs/World.hpp>`

## Components

Any movable C++ type can be a Component. Keep Components small and free of behaviour:

```cpp
namespace game
{
struct Position
{
    float x = 0, y = 0, z = 0;
};

struct Velocity
{
    float x = 0, y = 0, z = 0;
};

struct Player // empty type: a tag
{
};
} // namespace game
```

Rules:

- Move construction and destruction must not throw.
- Each Component type needs a unique fully qualified name. Do not declare Components in anonymous
  namespaces or inside functions.
- Empty types are **tags**: they take no memory and are used for filtering.
- Alignment up to 64 bytes is supported.

Component types are registered automatically the first time they are used. To save Components in
scene files, reflect them (see [Reflection](reflection.md) and [Scenes](scenes.md)).

## Entities

```cpp
illusion::World world;

illusion::Entity player = world.create(game::Position{}, game::Velocity{1, 0, 0}, game::Player{});
illusion::Entity empty = world.create();

world.emplace<game::Velocity>(empty, 0.0f, 1.0f, 0.0f); // add, constructing in place
world.set(empty, game::Position{5, 0, 0});              // add or assign
game::Position& position = world.get<game::Position>(empty);
bool moving = world.has<game::Velocity>(empty);
world.remove<game::Velocity>(empty);

world.destroy(empty);
bool alive = world.isAlive(empty); // false: the handle is stale
```

An Entity handle contains an index and a generation. When an Entity is destroyed its slot is reused
with a new generation, so old handles are detected safely by `isAlive()`.

Handles are valid only in the World and process that created them. Entities that are saved or referred
to from outside, by scene files or the editor, also carry a persistent id, the `EntityGuid` Component, and
optionally a `Name`; see [Entity ids](scenes.md#entity-ids).

!!! warning "References are short-lived"
    References and pointers returned by `get()`, `tryGet()` and `emplace()` are invalidated by the next
    structural change (creating or destroying Entities, adding or removing Components).

## Queries

A Query visits every Entity that has all of the listed Components. Declare read-only access with
`const`:

```cpp
world.query<game::Position, const game::Velocity>().each(
    [](game::Position& position, const game::Velocity& velocity) {
        position.x += velocity.x;
        position.y += velocity.y;
        position.z += velocity.z;
    });
```

The callable may also take the Entity as its first parameter: `[](illusion::Entity e, game::Position& p)`.

Filters select Entities by Components that are not accessed:

```cpp
auto players = world.query<game::Position>().with<game::Player>();
auto others = world.query<game::Position>().without<game::Player>();
illusion::u32 playerCount = players.count();
```

Matching Archetypes are cached per World, so building a Query every frame does not allocate.

### Chunk iteration

`eachChunk()` hands you the contiguous arrays of one Chunk at a time, which is the best shape for SIMD
and for the compiler's auto-vectoriser:

```cpp
world.query<game::Position, const game::Velocity>().eachChunk(
    [](illusion::ChunkView<game::Position, const game::Velocity>& chunk) {
        std::span<game::Position> positions = chunk.get<game::Position>();
        std::span<const game::Velocity> velocities = chunk.get<const game::Velocity>();
        for (illusion::u32 i = 0; i < chunk.size(); ++i)
        {
            positions[i].x += velocities[i].x;
        }
    });
```

### Parallel iteration

`parallelEach()` processes Chunks on the Job System and returns when every Chunk is done:

```cpp
illusion::JobSystem jobs;
world.query<game::Position, const game::Velocity>().parallelEach(
    jobs, [](game::Position& position, const game::Velocity& velocity) { position.x += velocity.x; });
```

The callable runs concurrently and must only touch the Components it receives plus data that is safe to
share between threads.

Serializers and tools that work with `ComponentId`s instead of C++ types iterate the same Chunks with
`World::forEachMatching()`; see [Type-erased access](scenes.md#type-erased-access).

## Structural changes during iteration

Creating or destroying Entities and adding or removing Components while a Query iterates is not
allowed (an assert fires in Debug). Record the changes in a `CommandBuffer` and apply them afterwards:

```cpp
illusion::CommandBuffer commands;
world.query<const game::Position>().each([&](illusion::Entity e, const game::Position& p) {
    if (p.y < -100.0f)
    {
        commands.destroy(e);
    }
});
commands.apply(world);
```

`CommandBuffer` supports `destroy()`, `set()`, `remove<T>()` and `create(components...)`. Commands run in
recording order. The buffer keeps its memory between uses, so reusing one per system does not
allocate in steady state.

## Structural version

`World::structuralVersion()` grows by one whenever the set of Entities or the set of Components of an
Entity changes: creating or destroying an Entity, adding a Component it did not have, removing one it
had, and clearing a non-empty World, directly or through a `CommandBuffer`. Writing Component values does
not change it. Tools use it to rebuild caches only when the layout of the World changed:

```cpp
if (world.structuralVersion() != m_outlinerVersion)
{
    rebuildOutliner(world);
    m_outlinerVersion = world.structuralVersion();
}
```

`World::componentVersion<T>()` is the same idea for one Component type: it grows when an Entity gains or
loses `T`, and when code announces with `world.markChanged<T>()` that it wrote values of `T`. Caches built
from one type, such as the `HierarchyIndex` below, use it so that unrelated changes do not invalidate them.

## Transform hierarchy

Header: `#include <illusion/ecs/Hierarchy.hpp>`

An Entity with a `Hierarchy` Component has a parent (or is a root) and a position among its siblings. Its
`LocalTransform` places it relative to its parent. `Transform` stays the world-space placement that every
System reads: `updateTransformHierarchy()` derives it from the `LocalTransform` once per frame.

```cpp
illusion::World world;
illusion::HierarchyIndex hierarchy;

const illusion::Entity car = world.create(illusion::Name{"Car"}, illusion::Transform{});
const illusion::Entity wheel = world.create(illusion::Name{"Wheel"}, illusion::Transform{});
if (auto attached = illusion::setParent(world, wheel, car); !attached)
{
    ILL_LOG_WARN("Game", "cannot attach: {}", illusion::hierarchyErrorMessage(attached.error()));
}

// Every frame, after game code and physics moved entities and before rendering:
world.get<illusion::Transform>(car).position.x += 1.0f;
illusion::updateTransformHierarchy(world, hierarchy, &jobs); // the wheel follows the car
```

- `setParent(child, parent, mode, index)` moves `child` under `parent` (the null Entity makes it a root) at
  position `index` among its siblings (`kLastSibling` appends). `ParentMode::KeepWorld` keeps the child
  where it is in the world and computes its `LocalTransform`; `KeepLocal` keeps the `LocalTransform` and
  moves the child with its new parent. A missing `Hierarchy`, `LocalTransform` or `Transform` is added.
  Cycles and dead entities are rejected with a `HierarchyError` and change nothing.
- `detach()` makes an Entity a root, `reorder()` moves it among its siblings, and `destroyRecursive()`
  destroys it with all its descendants. Sibling orders stay dense (0, 1, 2, ...).
- `parentOf()` returns the parent. These functions scan the entities that have a `Hierarchy`, so they are
  meant for editing and gameplay events, not for every Entity every frame.
- For a root, `Transform` is the truth and its `LocalTransform` (if it has one) is kept equal to it. A
  parent without a `Transform` counts as the identity.
- An Entity with the runtime tag `WorldSpaceAuthority` keeps the `Transform` a System wrote, and its
  `LocalTransform` is computed from it, so its children follow it. Physics adds the tag to dynamic bodies
  and characters.
- Placements combine as position, rotation and scale (`combine()`, inverted by `relativeTo()`). Positions
  are exact. A `Transform` cannot express shear, so the rotation and scale of a rotated child under a
  non-uniformly scaled parent only approximate the product of their matrices.

`HierarchyIndex` holds the child lists, the depth-first pre-order and the depth levels. It is rebuilt only
when the hierarchy changed (`componentVersion<Hierarchy>()`) or a parent without a `Hierarchy` died, so
`update()` is cheap every frame and entities without a `Hierarchy` cost nothing. Use it to walk the tree:

```cpp
hierarchy.update(world);
for (const illusion::Entity root : hierarchy.roots())
{
    hierarchy.depthFirst(root, [&](illusion::Entity entity, illusion::u32 depth) {
        drawOutlinerRow(entity, depth);
    });
}
```

Children are ordered by `Hierarchy::order`, then by `EntityGuid`; roots without a `Hierarchy` come last.
Code that writes `Hierarchy` values directly instead of through these functions must call
`world.markChanged<illusion::Hierarchy>()` afterwards.

## Disabled and Inactive

`setEnabled(world, entity, false)` adds the `Disabled` tag, which is saved in scenes. At the next
`updateTransformHierarchy()` the Entity and all its descendants get the runtime tag `Inactive`, and lose
it again when they are enabled or moved out of the disabled subtree. Engine Systems skip Inactive
entities:

- the renderer does not draw Inactive meshes, lights or sky lights;
- physics removes their bodies, characters and joints, and creates them again from the current `Transform`
  when they become active (velocities start from zero);
- audio stops their sources; sources with `playOnStart` start again when they become active.

Game Systems should skip them too:

```cpp
world.query<Enemy, const illusion::Transform>().without<illusion::Inactive>().each(
    [](Enemy& enemy, const illusion::Transform& transform) { enemy.think(transform); });
```

`isEnabledInHierarchy()` answers immediately, before the next update, by walking the parent chain.

## How it works

Entities with exactly the same set of Component types share an **Archetype**. An Archetype stores its
Entities in 16 KiB **Chunks**; inside a Chunk each Component type has its own tightly packed array
(structure of arrays). Adding or removing a Component moves the Entity to another Archetype. Removing
an Entity moves the last Entity of its Archetype into the hole, so Chunks stay dense.

See [ADR 0004](../architecture/decisions.md) for the reasoning behind this design.

## Performance notes

- Prefer Queries over many `get()` calls: Queries stream through contiguous memory.
- Avoid frequent add/remove of Components on many Entities each frame; use a tag or a flag field when
  state toggles often.
- Mark read-only Components `const` in Queries. It documents intent and will let the scheduler run
  systems in parallel in the future.

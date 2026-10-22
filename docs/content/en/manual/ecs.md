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

---
name: ecs-components
description: How to write ECS components, systems, queries and command buffers in illusion correctly and fast. Use when adding gameplay or engine data to the ECS or writing code that iterates entities.
---

# Writing ECS code

Headers: `<illusion/ecs/World.hpp>`, `<illusion/ecs/CommandBuffer.hpp>`. Design: `docs/adr/0004-archetype-ecs.md`.

## Components

```cpp
namespace game // named namespace: ids are derived from the fully qualified name
{
struct Health
{
    f32 current = 100.0f;
    f32 maximum = 100.0f;
};
struct Dead {}; // tag: no storage, used for filtering
} // namespace game
```

Checklist:

- [ ] Plain data; no virtual functions; no owning raw pointers.
- [ ] Nothrow move constructor and destructor (`static_assert(std::is_nothrow_move_constructible_v<T>)`
      is enforced by the `ComponentType` concept).
- [ ] Unique fully qualified name; never in an anonymous namespace or a function body.
- [ ] Alignment ≤ 64. Size well below 16 KiB (a chunk must hold at least one row; aim for many).
- [ ] Prefer small, focused components. Split hot and cold data into separate components.
- [ ] Types used as ECS components in shared engine code belong in a public header of their module.
- [ ] Saved in scenes? Reflect it at startup with a stable, prefixed name and every persistent field
      (`reflect<T>("module.Name").field("name", &T::member)`); add it to the module's `reflect*Components()`
      function and to a scene round-trip test. Enum fields need an `EnumReflection` specialization declared
      before use (`kFlags = true` for bit sets). Structs used as fields are reflected first with
      `reflectStruct<T>()`. See `docs/content/en/manual/reflection.md`.
- [ ] Saved entities need an `EntityGuid`: call `assignMissingGuids(world)` before `saveScene()`. Entity
      fields are saved as ids; keep other references to entities outside the World as `EntityGuid`s and
      resolve them with `GuidEntityResolver`. Rules that values must follow when read (like `Name`) go in
      `FieldMeta::validate`. Iterate by `ComponentId` with `World::forEachMatching()`.
- [ ] Metadata is required for editor-visible types: `meta({.displayName, .category, .icon, .description,
      .docsAnchor})`; angles get `.unit = Unit::Radians, .displayUnit = Unit::Degrees, .hint =
      FieldHint::Angle`; colours get `FieldHint::Color` or `HdrColor`; physical quantities get a unit; valid
      ranges get `min`/`max` (rejected when reading JSON), slider ranges `softMin`/`softMax`.
      `tests/ecs/TestReflectionSweep.cpp` checks the engine's types.
- [ ] Runtime-only state (handles, caches) is reflected with `reflectRuntime<T>("module.Name")`: it is
      transient and never saved.
- [ ] Placement: read and write the world-space `Transform`; a child's `Transform` is derived from its
      `LocalTransform` by `updateTransformHierarchy()` (run it after game code and physics, before
      rendering). A System that writes `Transform` in world space for entities that may have a parent adds
      the `WorldSpaceAuthority` tag. Reparent with `setParent()`/`reorder()`/`destroyRecursive()`; after
      writing `Hierarchy` values directly call `world.markChanged<Hierarchy>()`. Caches built from one
      Component type check `World::componentVersion<T>()`.
- [ ] Engine and game Systems that act on entities skip `Inactive` ones (`.without<illusion::Inactive>()`);
      a System that owns external resources (bodies, voices) releases them when an Entity becomes Inactive
      and recreates them when it becomes active. Test it with `setEnabled()`.
- [ ] Changed the meaning of saved data? Bump the reflection version and add `.migrate(oldVersion, fn)`;
      renamed a field? Add `.renamedFrom = "oldName"`. Test loading the old JSON.

## Systems (for now: free functions)

```cpp
void applyDamage(illusion::World& world, illusion::CommandBuffer& commands)
{
    ILL_PROFILE_SCOPE();
    world.query<game::Health>().without<game::Dead>().each([&](illusion::Entity e, game::Health& h) {
        if (h.current <= 0.0f)
        {
            commands.set(e, game::Dead{});
        }
    });
}
// caller: applyDamage(world, commands); commands.apply(world);
```

Rules:

- Use `const T` in the query for components you only read.
- No `create/destroy/emplace/set/remove` on the world inside `each`/`eachChunk`/`parallelEach`: record into a
  `CommandBuffer` and `apply()` after iteration. Debug builds assert.
- `parallelEach(jobs, fn)`: `fn` runs concurrently. Only touch the components passed in and thread-safe
  shared data. Use one `CommandBuffer` per worker if you need deferred changes (index with
  `JobSystem::currentWorkerIndex() + 1`, slot 0 for non-workers).
- Never hold `T&`/`T*` from `get`/`tryGet`/`emplace` across a structural change.
- Use `eachChunk` + `std::span` for tight numeric loops the compiler can vectorize.
- Keep a `CommandBuffer` alive across frames (member of the system) so it reuses memory.

## Testing ECS code

- Put test components in `tests/ecs/TestComponents.hpp` (namespace `ecs_test`) or in a uniquely named
  namespace in the test file.
- Use `test::LifetimeCounter` as a component to prove constructions == destructions.
- Cover chunk boundaries: create more entities than one chunk holds (e.g. 5000+ small components).
- Add a benchmark for any new hot iteration pattern in `benchmarks/BenchEcs.cpp`.

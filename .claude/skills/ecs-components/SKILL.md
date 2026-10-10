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

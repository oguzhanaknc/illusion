# 0004. Archetype-based ECS with chunked structure-of-arrays storage

- Status: Accepted
- Date: 2026-10-10

## Context

Game state must be iterated by many systems every frame, often in parallel, for tens or hundreds of
thousands of entities. The two main designs are:

- Sparse-set ECS: one dense array per component type. Adding and removing components is cheap, but
  iterating over several components needs an indirection per entity.
- Archetype ECS: entities are grouped by their exact component set. Iteration is linear over the
  matching groups, but adding or removing a component moves the entity to another group.

## Decision

Use an archetype ECS:

- An archetype owns all entities with exactly the same component set (tags included).
- Entities are stored in 16 KiB chunks allocated from a per-world pool. Inside a chunk, entity handles
  come first, followed by one tightly packed array per component (structure of arrays). Components may
  be aligned up to 64 bytes.
- Rows are dense: removing a row moves the archetype's last row into the hole.
- Entity handles are (index, generation) pairs; stale handles are detected.
- Adding or removing a component moves the entity between archetypes along cached graph edges. Creating
  an entity with components looks the archetype up by its sorted signature, so no intermediate archetypes
  are created.
- Queries match archetypes by component bit masks. The matching archetype list is cached per world and
  updated incrementally when archetypes are created, so queries do not allocate in steady state.
- Structural changes during iteration are forbidden and asserted; `CommandBuffer` defers them.
- Empty component types are tags without storage.

## Consequences

- Iteration is cache-friendly and parallelizes naturally per chunk (`Query::parallelEach`).
- Adding and removing components is more expensive than in a sparse-set ECS (the whole row moves).
  Frequently toggled state should use a field instead of adding and removing components.
- `get<T>(entity)` costs a slot lookup plus a column lookup; prefer queries in hot loops.
- Up to 256 component types per process (`kMaxComponentTypes`); raise it when needed.
- Future work: change detection, system scheduling with read/write sets, relationships, and archetype
  cleanup for long-running worlds.

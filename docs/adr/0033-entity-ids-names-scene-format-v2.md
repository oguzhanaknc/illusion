# 0033. Persistent entity ids, names and scene format v2

- Status: Accepted
- Date: 2026-10-22
- Supersedes in part: [0022](0022-reflection-and-scene-files.md) (unknown types, index references)

## Context

`Entity` handles (index + generation) are process-local: they change when a scene is loaded again, so
nothing outside the running World can hold on to one. Version 1 scene files store Entity fields as indices
into the file's entity array, which breaks as soon as a file is merged, a selection is pasted into another
scene, or the editor (M7i) needs to name an entity in an undo record, a journal, a JSON-RPC call or a
prefab override. Entities have no names either, and ADR 0022 drops unknown Component types on load, so a
scene opened while a game module is not loaded silently loses that module's data when it is saved again.

## Decision

- **Identity Components.** `EntityGuid{u64 value}` (`illusion.EntityGuid`, read-only, never 0) is the
  persistent id of an Entity. `Name{std::string value}` (`illusion.Name`) is an optional label; reading it
  from JSON rejects `/` (reserved for paths), control characters and more than 256 bytes, through a new
  field validator, `FieldMeta::validate`, that every JSON read applies. Both are ordinary Components, so
  Worlds that do not need them pay nothing.
- **Generating ids.** `GuidGenerator` maps a counter through the splitmix64 finalizer, a bijection on 64
  bits, offset by a 64-bit key: one generator never repeats an id, and ids of different generators collide
  only by chance. The default key comes from `secureRandomBytes()`; a seeded generator gives the same
  sequence everywhere, for tests and reproducible tools. `next()` is one relaxed atomic add, so a generator
  can be shared between threads. `assignMissingGuids(World&)` gives ids to entities without one.
- **Persistent references.** `GuidEntityResolver` is the `EntityResolver` (ADR 0032) for a World: Entity
  fields are written as the target's id, or `null`. Ids are written as 16 lower-case hex digits, which keeps
  them fixed-width and readable; they are opaque, not numbers.
- **Type-erased iteration.** `World::forEachMatching(all, none, fn)` calls `fn` once per chunk with the
  entity handles and raw column pointers, so serializers and tools iterate by `ComponentId` at query speed.
- **Scene format version 2:**
  - Every entity is `{"id": "...", "components": {...}}`; saving requires an `EntityGuid` on every saved
    entity (call `assignMissingGuids()` first) and fails on duplicates. Components are written in order
    of stable name and entities in order of id, so files are deterministic across processes and diff well.
    Hierarchy pre-order replaces the id order in M7g.
  - Entity fields are ids or `null`; references outside the saved set are `null`. U64 values are decimal
    strings, as everywhere else in the JSON value API.
  - A top-level `"settings"` object holds reflected structs by stable name (render, physics or audio
    settings of the scene); the `"types"` table lists their versions next to those of Components.
  - **Unknown Component types are preserved.** Their JSON and saved version are kept in the runtime
    Component `UnknownComponents`, which no engine System queries, and written back unchanged. Unknown
    settings are returned to the caller and can be saved back the same way. Unknown fields of known types
    are still dropped with a warning, after `renamedFrom` is applied.
  - `SceneLoadOptions::ids` keeps the file's ids (`IdPolicy::Keep`, the default; ids that already exist in
    the World are a load error) or gives every entity a new id (`Regenerate`, for paste and instancing).
    References inside the file are remapped either way; `SceneLoadResult::fileIds` maps file ids to the
    new entities. Duplicate ids in a file are a load error.
  - Version 1 files still load: their entities get new ids and index references are remapped. Saving
    always writes version 2.

## Consequences

- Entities created by code have no id until `assignMissingGuids()` runs; the editor's documents (M7i)
  call it, and game code that saves scenes must too. Saving fails with a clear message otherwise.
- References inside preserved (unknown) Components are not remapped by `Regenerate`, because their types
  are unknown. They stay valid with `Keep`, the policy used to open and save documents.
- A settings struct and a Component that share a stable name must share its version too; saving fails
  otherwise. Stable names are prefixed per module, so this does not happen in practice.
- 64-bit random ids are collision-free in practice for scenes and projects (a collision needs billions of
  entities); duplicate detection on load turns the remaining risk into an error instead of corruption.
- ADR 0022's rules on dropping unknown types and on index references are superseded; its reflection and
  tolerant, atomic loading stand.

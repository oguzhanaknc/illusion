# 0022. Component reflection with stable names and JSON scene files

- Status: Accepted; field types, metadata, migrations and the JSON value API extended by
  [0032](0032-reflection-v2.md)
- Date: 2026-10-17

## Context

Scenes, the editor (M7) and its AI-facing command API need to save, inspect and edit Components without
knowing their C++ types. C++ type names differ between compilers and change when code is refactored
(ADR 0003), so they cannot appear in saved data. Scene files should be readable, diff and merge well,
and keep loading when Components gain or lose fields.

## Decision

- **Explicit reflection.** `reflect<T>("module.Name", version).field("name", &T::member)...` registers a
  stable name and fields (type, offset) in the `ComponentRegistry`, which owns copies of all strings so
  hot-reloaded modules can be unloaded. No macros or code generation; unsupported member types fail to
  compile. Enumerations are described by an `EnumReflection<E>` specialization.
- **Field types:** bool, 32/64-bit integers, f32/f64, Vec2/3/4, Quat, `std::string`, `Entity` and
  reflected enumerations. Components must be default constructible; `ComponentInfo::construct` lets the
  World create them by id.
- **Type-erased World API:** `entities()`, `componentsOf()`, `componentData()`, `emplaceDefault()` and
  `remove(Entity, ComponentId)` for serialization, the editor and tools.
- **JSON scene files** read and written with yyjson (MIT, C, no exceptions, shortest round-trip floats):
  a `format`/`version` header, a `types` table of reflection versions, and an `entities` array of
  objects keyed by stable Component name, with fields keyed by name. Entity fields are array indices
  remapped on load. Output is deterministic, indented by two spaces, with scalar arrays on one line.
- **Tolerant, atomic loading:** missing fields keep defaults; unknown types and fields are skipped with
  a warning; type errors fail the whole load and remove the entities created so far. Non-finite floats
  fail saving.
- **Semantic versions:** `SemanticVersion` (semver 2.0.0 parsing, precedence, compatibility) and
  `engineVersion()` from the CMake project version, for the module system (M6).

## Consequences

- Only reflected Components are saved. `MeshRenderer` and `SkyLight` hold GPU handles; saving them needs
  asset references in scenes, planned with the editor (M7, in ROADMAP.md).
- Reflection versions are recorded but there are no migration hooks yet; a version mismatch only warns.
  Migrations can be added per type when the first incompatible change needs one.
- JSON suits authoring; shipped games may later cook scenes to a binary format with the same reflection.
- yyjson is linked into the core library; its headers stay private.

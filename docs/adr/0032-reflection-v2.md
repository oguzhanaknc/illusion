# 0032. Reflection v2: metadata, struct reflection, a JSON value API and migrations

- Status: Accepted
- Date: 2026-10-22
- Extends: [0022](0022-reflection-and-scene-files.md)

## Context

ADR 0022 gave Components stable names and a fixed set of field types, enough for scene files. The editor
(M7m inspector), its typed command API (M7i, JSON-RPC and MCP) and settings files need more:

- How to show a field: a label, a unit (angles are stored in radians but people think in degrees), a
  valid range, a widget (colour picker, slider, layer picker), and whether it is read-only or advanced.
- Plain structs (render, audio and import settings, materials) as fields of Components and as files of
  their own, plus arrays, optionals, small integers, flags and free-form JSON for command parameters.
- One JSON encoding for scenes, commands and tools, with errors that say exactly which value is wrong,
  partial edits (merge), and access to a single value by path.
- A way to change saved data without breaking old files.

## Decision

- **Metadata in the reflection call.** `field(name, member, FieldMeta{...})` adds labels, description,
  category, `unit`/`displayUnit`, hard (`min`/`max`) and soft ranges, `step`, a `FieldHint`, `readOnly`,
  `hidden`, `advanced` and `renamedFrom`. `meta(TypeMeta{...})` adds a display name, a category path, a
  Material Symbols icon, a description, a docs anchor, required Components, `singleton` and `transient`.
  Metadata is validated at registration (an `Angle` needs an angle unit, colours are vectors, ranges are
  numeric). Labels are English defaults; the editor's localization uses them as the source strings of
  dynamic keys (`field.<type>.<field>.label`).
- **One reflection model for Components and structs.** `TypeReflection` (aliased as
  `ComponentReflection` and `StructReflection`) describes both. `ComponentRegistry` owns a
  `StructRegistry` (`reflectStruct<T>(name, version)`), so modules that share the host's registry share
  its structs. A field's `ValueType` is a small tree (element types for `std::array`, `std::vector`,
  `std::optional`; a pointer to the struct's reflection) built at compile time and deep-copied into the
  registry with every string.
- **New field types:** I8, I16, U8, U16, Struct, FixedArray, Array, Flags (an enumeration with
  `EnumReflection::kFlags`), Optional and Json (`JsonText`). The numbering of the existing types is kept.
- **Append-only storage.** Reflecting a type again publishes a new reflection but keeps the old one alive
  until the registry is destroyed, so `TypeReflection`/`FieldInfo` pointers (in undo entries, inspectors,
  `FieldValue`s) never dangle. Reloads add a few kilobytes each, which is negligible. A reloaded module
  whose type changed layout (rolled back by the module host, ADR 0027) is reflected but not published.
- **Values.** `FieldValue` owns a copy of one field of any type (inline up to 48 bytes) through generated
  `ValueOps`; `readField`, `writeField`, `fieldsEqual` and `defaultValue` work on any field.
- **JSON value API** (`ComponentJson.hpp`): `writeValue`/`readValue` for whole objects in Replace or Merge
  mode, `getPath`/`setPath` with dotted paths. Reading is atomic by default, collects every error with a
  JSON Pointer, lists unknown members, never asserts on malformed input, accepts unit-tagged numbers
  (`{"deg": 60}`) and sRGB colours (`"#rrggbb"`), and enforces hard ranges. `u64` values are written as
  decimal strings so that clients which store numbers as doubles stay exact. Entity fields go through an
  `EntityResolver` (scene indices now, persistent ids in M7f).
- **Scenes on top.** `SceneSerializer` writes and reads through this API with a scene index resolver and
  numeric `u64`s, keeping version 1 output byte for byte. Transient types are counted as skipped on save
  and skipped with a warning on load.
- **Migrations.** `migrate(fromVersion, fn(JsonMutDocument&, JsonMutValue))` registers steps; when the
  saved version is older, `renamedFrom` renames run first, then every step from that version on.
  `JsonMutValue` gains `asBool`/`asI64`/`asU64`/`asF64`/`asString` so that steps can read values. Newer
  data loads as far as its fields are known, with a warning. Only the top-level object is migrated;
  nested structs are read at their current version.
- **Runtime state** gets stable names through `reflectRuntime<T>()` (`TypeMeta::transient`):
  `MotionHistory`, `PhysicsBody`, `CharacterBody`, `PhysicsJoint`, `AudioPlayback`.

## Consequences

- Every engine Component and settings struct carries metadata; a test sweeps the registry for missing
  icons, docs anchors, units on angles and hints on colours, and checks that every default value is valid
  JSON for its own type.
- The `Asset` field type and `FieldMeta::assetKind` planned with this work arrive with `AssetRef` in M7h
  (ADR 0035), so that no field type exists without a C++ type behind it.
- Reflected Components must now be copyable (they are plain data already); reflected structs must be
  default constructible, copyable and nothrow movable.
- Range checks make scene loading stricter: a file with a value outside a field's hard range fails to load
  with a message that names the value. The ranges are deliberately wide.
- Nested struct versions are not recorded in files; a struct whose meaning changes inside a Component needs
  a migration of the Component.
- Function tables (`ValueOps`, `ContainerOps`, migrations) of a module's types stay in its code; metadata
  outlives the module but values of its types must not be used after it unloads (M7t revisits reload).

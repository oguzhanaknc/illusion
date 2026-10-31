# 0044. In-place Component layout migration on hot reload

- Status: Accepted
- Date: 2026-10-31
- Supersedes: [0027](0027-module-system-and-hot-reload.md) in part (the rollback of reloads that change a
  Component's layout)

## Context

ADR 0027 lets a reloaded module take its Component types over from the old version: the registry swaps the
lifetime functions and the existing Components keep their bytes. That is only correct when the bytes mean the
same thing to both versions. A change to the size or alignment of a type was detected and rolled the reload
back ("restart to apply that change"); a change that kept the size (two fields swapped, an `i32` turned into
an `f32`) was not detected at all, and the new code read the old bytes. While a game is being written,
adding a field to a Component is the most common change, and the editor's Play mode (ADR 0042) and the
project module builds (ADR 0043) make hot reload the normal way to try it.

Every saved Component is reflected (ADR 0022), and the scene file code already reads a reflected value by
field name with renames (`FieldMeta::renamedFrom`), version migrations, enums by name, numeric conversions
and defaults for fields the data does not mention. Options considered:

- **Byte-level migration from the two reflections** (copy each field by name, convert in place): fast, but a
  second copy of every conversion rule the JSON reader already has, and it cannot run the type's migration
  steps, which work on JSON.
- **Keep rejecting the reload**: safe, but the most common edit needs a restart and the play session is lost.
- **Migrate through the reflected JSON form**: write each Component with the old reflection, read it back
  with the new one. One set of rules for scene files and hot reload; slower, but reloads are rare and the
  cost is per Component of a changed type only.

## Decision

- **Staged takeovers.** While a module is retired (reloading) or after its types were orphaned, a
  registration of one of its types by another module no longer takes the type over at once: the registry
  stages the new `ComponentInfo`, and a reflection of a staged type is built but not published. The module
  host decides for each staged type after the new `load`:
  - the size, alignment and reflected fields (names, offsets, kinds, in order) are unchanged, or the type
    is unreflected with the same size and alignment: the takeover is committed as before;
  - otherwise the type is **migrated**.
  `ComponentRegistry::commitStaged()` applies a staged registration (replacing the layout when it differs,
  which needs that no archetype of any World uses the type) and publishes its reflection;
  `discardStaged()` drops them all on rollback. The `ComponentId` and the stable name do not change, so
  `componentId<T>()` caches in every module stay valid.
- **Archetype use counts.** Archetypes count, in the registry, how many archetypes use each type.
  `World::purgeArchetypesWith(id)` destroys the World's (empty) archetypes that contain the type, drops the
  edges and the query cache. A type whose use count stays above zero after the attached Worlds were purged is
  used by a World the host does not know: the reload is rolled back.
- **Migration** of a type, while the old library is still open:
  1. every Component of the type in every attached World is written to JSON with the old reflection (Entity
     fields as process-local handles, AssetRef fields as paths);
  2. the Components are removed with the old code, the archetypes purged, the staged layout committed;
  3. each Component is added again, default-constructed by the new code, and read from its JSON with the new
     reflection in `Replace` mode, with the old reflection's version as the saved version, so the new
     version's renames and migration steps apply.
  Snapshots are taken for every migrating type before anything changes, so a failure there (a value that
  JSON cannot hold, such as a NaN) rolls back with nothing touched; after step 2 nothing can fail.
- **Field policy.** Fields are matched by name or `renamedFrom`.
  - A **new** field gets its default value.
  - A **removed** field is dropped.
  - A **retyped** field is converted with the scene file rules: any integer width when the value fits,
    integers to floating point, enums by value name, strings to asset paths.
  - A retyped value that the new type **cannot hold** (out of range, a fraction for an integer, a vector of
    another length, outside `FieldMeta` limits) is reset to the field's default.
  Nothing is clamped or guessed. The `MigrationReport` (`ModuleHost::lastMigration()`) lists each migrated
  type with its number of Components and its added, removed and reset fields; the host logs a warning for
  every reset field.
- **What still rolls back**: an unreflected type whose layout changed while Components of it exist ("reflect
  the type to migrate it"), a type used by a World that is not attached, and a snapshot that fails. The old
  version gets its types and values back and loads again, as in ADR 0027.

## Consequences

- Adding, removing, renaming, reordering or retyping fields of a reflected Component survives a hot reload
  in the edit and play Worlds; a running play session continues with the migrated values.
- Same-size layout changes, which used to be reinterpreted silently, are now detected and migrated.
- A migration costs a JSON round trip per Component of a changed type (about 5 µs per Component with the
  library loads, `modules/migrate` benchmark); reloads that change no layout cost what they did.
- Unreflected module Components cannot change their layout while they exist; module authors are told to
  reflect them.
- Values that cannot be converted are reset, not kept: the report and the log say which.
- Only Component types migrate. Reflected structs used as fields keep the struct registry's rule that a
  struct's size never changes in a process; changing one still needs a restart.

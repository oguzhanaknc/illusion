# 0046. Prefabs as instance records with field-level overrides and nesting

- Status: Accepted
- Date: 2026-11-05

## Context

M7v adds prefabs: reusable groups of entities saved as `.prefab` assets and placed in scenes as instances.
An instance must follow later changes to its prefab, except for the values the scene changed on purpose
(overrides), and prefabs must nest (a prefab containing instances of other prefabs). The format has to
survive raw edits of scene files by people and agents, keep stable ids for the editor's commands, undo and
references (ADR 0033), and keep working when a prefab file is missing.

Options considered:

- **Expand instances into the scene and keep a link per entity.** Easy to load, but a prefab change does
  not reach saved scenes until each is opened and re-saved, and the file does not say which values are
  overrides.
- **Store overrides as a list of edit operations** recorded by the editor. Exact, but raw edits of a scene
  file bypass the recording, so overrides go stale.
- **Store an instance as a record and compute overrides at save time by comparing the instance with a fresh
  expansion of its prefab.** Prefab changes reach every scene on load; overrides are always what differs.

## Decision

- **Files.** A prefab file has the scene v2 layout with `"format": "illusion.prefab"` and exactly one root
  (one entity, or one instance record, whose parent is not in the file). A scene or prefab stores an
  instance as an entry of its `entities` array:
  `{"prefab": "assets:/crate.prefab", "id": "<instance id>", "overrides": {...}}`.
- **Ids.** The entities of an instance get `derivePrefabGuid(instance, local)`, a 64-bit mix of the instance
  id and the entity's id inside the prefab file (its *local* id). An entity of a nested instance has, as its
  local id in the outer prefab, `derivePrefabGuid(nested record id, its local id in the nested prefab)`.
  Instantiating the same prefab with the same instance id always gives the same ids.
- **Overrides** are keyed by local id; each value is `null` (the entity was removed from the instance) or an
  object of Components, each `null` (removed) or a patch: objects merge field by field, any other value
  replaces. A Component the prefab entity does not have is added from its default value. Entities added under
  an instance are ordinary scene entities whose `Hierarchy` parent is an instance entity; moving an instance
  is an override of its root's `Hierarchy` and `LocalTransform`.
- **Loading** expands records (`SceneLoadOptions::prefabs` reads prefab files), applies overrides with the
  file's entity ids, and marks entities with the runtime Components `PrefabInstance` (root: prefab and
  instance id) and `PrefabMember` (every entity: instance id and local id). Nesting is limited to depth 16
  and cycles are load errors.
- **Saving** (`PrefabSaveMode::Records`, the default) writes one record per instance whose root is saved,
  with the overrides found by expanding the prefab again into a scratch World and comparing Component JSON.
  `PrefabSaveMode::Expanded` writes every entity with a `"prefabLink"` instead, for editor snapshots (undo,
  transactions) that must restore links exactly. Members saved without their root are written as plain
  entities.
- **Missing prefabs pass through.** A record whose prefab cannot be read becomes one entity with the runtime
  Component `UnresolvedPrefab`, which saves the record again unchanged; entity fields that referred to the
  missing instance's entities become null with a warning.
- **Variants** are prefabs whose root is an instance record; they need nothing else.

## Consequences

- Saving a scene with instances reads their prefab files, and loading reads them again: a scene costs one
  prefab expansion per instance for each.
- Overrides are always correct after raw edits, at the price of field-level granularity defined by the JSON
  form: arrays and other non-object values are overridden as a whole.
- Override values of Component types that are not registered are dropped with a warning; such Components on
  prefab entities themselves are preserved as before (`UnknownComponents`).
- Editor commands (`prefab.*`), the Inspector's override display, prefab edit mode and propagation of prefab
  changes into open scenes build on this format (M7v-2 and later).

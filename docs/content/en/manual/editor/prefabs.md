# Prefabs

A prefab is a group of entities saved as a `.prefab` asset that you place in scenes as *instances*. An
instance stays linked to its prefab: change the prefab and every instance follows, except for the values the
scene changed on purpose, its *overrides*. The file format, nesting and how missing prefabs are kept are
described in [Scenes](../scenes.md#prefab-instances) (ADR 0046).

Everything here is an [Editor command](../../reference/editor-commands.md), so people and AI agents work with
prefabs the same way. The Asset Browser lists prefabs with the kind `Prefab`.

## Creating a prefab

`prefab.create` saves an entity and its descendants as a prefab file and replaces them with an instance at the
same place. References from other entities to the replaced ones follow to the instance's entities, so nothing
in the scene breaks. Pass `"replace": false` to only write the file, and `"overwrite": true` to replace an
existing one.

```json
{"entity": "/Crate", "path": "assets:/props/crate.prefab"}
```

The root keeps its place in the world as its own placement in the prefab. Instances inside the saved entities
stay instances: the new prefab nests them.

## Placing instances

`prefab.instantiate` adds an instance of a prefab file, as a root or under `parent` at `index`. Each instance
gets a new id, from which the ids of its entities derive.

```json
{"path": "assets:/props/crate.prefab", "parent": "/Level", "index": 0}
```

## Overrides

Edit an instance like any entity: rename it, change Component values, add or remove Components, delete
entities or add children. Saving the scene records only what differs from the prefab.

- `prefab.overrides` lists them for any entity of an instance, in the form the scene saves them: by the
  entity's id in the prefab file, Component patches (`null` for a removed Component) or `null` for a removed
  entity.
- `prefab.revert` gives an entity of an instance its prefab's values again: one `component`, or every
  Component but its `illusion.Hierarchy` (Components the prefab lacks are removed, ones it has are added back).

```json
{"entity": "/Level/Crate/Lid", "component": "illusion.Name"}
```

## Applying overrides to the prefab

`prefab.apply` writes what an instance changes into its prefab file: one `component` of an entity, every
Component of the entity, or with `"all": true` the whole instance, entities removed from it included. The
root's placement and the hierarchy stay in the scene. Every other instance in the open scene then follows the
new prefab and keeps its own overrides; the file write and those edits are one undoable step.

```json
{"entity": "/Level/Crate", "all": true}
```

## Copying and duplicating

Copying a whole instance (its root) to the clipboard and pasting it gives a new linked instance with the same
overrides. Entities of an instance copied without its root, and entities made by `entity.duplicate`, are plain
copies.

## Unpacking

`prefab.unpack` turns an instance into plain entities with the same ids and values, no longer linked to the
prefab; instances nested in it are unpacked too. Like every edit, creating, placing, reverting and unpacking
can be undone.

## References and moves

A scene refers to the prefab of each of its instances, and through it to what the prefab refers to:
`asset.references` and `asset.move` see both. Moving an asset rewrites prefab files and the scenes that
instance them in one step, without turning the move into overrides.

## Current limits

- If a prefab file changes on disk (outside `prefab.apply`) while a scene with its instances is open, the open
  instances update when the scene is opened again. Entities that the prefab no longer has are then saved as
  plain entities.

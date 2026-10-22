# 0034. Transform hierarchy with world-space truth, Disabled and Inactive

- Status: Accepted
- Date: 2026-10-22

## Context

Until now every Entity was placed in world space by its `Transform`, and nothing could switch a group of
entities off. The editor (M7i–M7l) needs parents and children (drag to reparent, local and world gizmos, an
outliner), glTF models lose their node structure when they are spawned, and games need to group, move and
disable objects as one. The renderer, physics, audio and game code all read `Transform` today, and their
inner loops are tuned for contiguous Component arrays; a hierarchy must not slow down worlds that do not
use it. Scene files (ADR 0033) refer to entities by id, so a parent link can be saved, but the file's
entity order is still by id, which scatters a model's nodes through the file.

## Decision

- **Components.** `Hierarchy{Entity parent; u32 order}` (`illusion.Hierarchy`) gives an Entity its parent
  (null for a root) and its position among its siblings; roots that have a Hierarchy are ordered too.
  `LocalTransform` (`illusion.LocalTransform`) is the placement relative to the parent. No intrusive
  first-child/next-sibling links: the only invariant stored per Entity is its own parent and order, which
  the editor can write, undo and save without touching other entities.
- **`Transform` stays the world-space truth.** Every System keeps reading `Transform`.
  `updateTransformHierarchy(World&, HierarchyIndex&, JobSystem*)` derives it from `LocalTransform` once per
  frame, level by level from the roots (wide levels on the Job System); for roots it copies `Transform` into
  `LocalTransform`. Composition is TRS (`combine()`): positions are exact, and the shear a non-uniformly
  scaled parent would give a rotated child is dropped, as in other engines with TRS transforms.
- **World-space authorities.** An Entity whose `Transform` a System writes in world space carries the
  runtime tag `WorldSpaceAuthority`; propagation writes its `LocalTransform` from its `Transform` instead
  (`relativeTo()`), so its children follow it. Physics adds the tag to dynamic bodies and characters.
- **Derived index.** `HierarchyIndex` builds CSR child lists, pre-order and depth levels from the Hierarchy
  Components. It is rebuilt only when `World::componentVersion<Hierarchy>()` changes or a parent without a
  Hierarchy dies; otherwise `update()` is O(1) and allocates nothing. The new per-type
  `World::componentVersion()` grows when an Entity gains or loses that type, and when `markChanged()`
  announces written values; the Hierarchy API calls it, and tools that write Hierarchy values directly must
  too. Cycles written directly are indexed as roots with a warning; the API rejects them with an error value.
- **Editing API** (`Hierarchy.hpp`): `setParent(child, parent, KeepWorld | KeepLocal, index)`, `detach`,
  `reorder`, `destroyRecursive`, `setEnabled`, `isEnabledInHierarchy`, `parentOf`; errors are
  `Expected<void, HierarchyError>`. Sibling orders stay dense. These functions scan the entities that have
  a Hierarchy (linear time): they are editor and gameplay operations, not per-frame work.
- **Disabled and Inactive.** `Disabled` is a saved tag. `Inactive` is a runtime tag meaning "Disabled on
  itself or on an ancestor"; the update adds and removes it through a `CommandBuffer`, only after a change
  to Hierarchy, Disabled or Inactive Components. The renderer culls with `without<Inactive>()`, physics
  removes bodies, characters and joints of Inactive entities and creates them again from the current
  `Transform` when they become active, and audio stops their sources (`playOnStart` sources start again).
- **Scenes.** The default save order becomes the hierarchy's depth-first pre-order (`SceneOrder::Hierarchy`):
  roots by order (roots without a Hierarchy last), ties by id. A child's `Transform` is derived and not
  saved. Loading does not derive it; `updateTransformHierarchy()` does on the next frame.
- **glTF.** `spawnModel()` creates one Entity per node with its Name, Hierarchy and LocalTransform, and one
  child per submesh for meshes with several submeshes.
- **Math.** `Math.hpp` gains `Ray`, `Plane`, `Obb`, ray intersections with boxes, spheres, planes, triangles
  and oriented boxes, ray-segment closest points, `screenToRay()` for reversed-Z perspective and orthographic
  cameras, `orthographicReversedZ()` and Euler conversions with a continuity hint, for picking and gizmos.

## Consequences

- Worlds without Hierarchy Components pay nothing per frame (`hierarchy/flat`); a 100k-entity hierarchy 9
  levels deep propagates in about 5 ns per Entity on a desktop (`hierarchy/propagate`, budget 30).
- Systems read `Transform` unchanged, but they see a child's new placement only after
  `updateTransformHierarchy()` ran; the recommended frame order is game code, physics, hierarchy update,
  rendering. A kinematic body under a moving parent follows it one frame late.
- Re-enabling a physics body resets its velocity, because the body is created again.
- Entities write their own Hierarchy, so sibling orders can be stale if code writes them directly without
  the API; the index then sorts by order, id and handle index and stays deterministic.
- `Inactive` changes are structural changes, so disabling large subtrees every frame is expensive; toggle
  rarely, or use a flag field for state that flips often.
- Scene files saved before M7g load unchanged; files saved now list parents before children.

## Alternatives considered

- **Intrusive links** (first child, next sibling): O(1) child iteration without an index, but every
  reparent writes four entities and the links must be kept consistent through undo, paste and loading.
- **`LocalTransform` as the only truth and a separate world matrix Component**: every System would have to
  change, and physics would need two-way synchronisation anyway.
- **A matrix-based world transform keeping shear**: exact for non-uniform scale, but `Transform` would no
  longer decompose; renderers that need the exact matrix can multiply local matrices along the index.
- **Hierarchy changes detected through `structuralVersion()`**: any created bullet would rebuild the index;
  the per-type version limits rebuilds to real hierarchy changes.

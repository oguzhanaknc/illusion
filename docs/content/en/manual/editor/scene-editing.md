# Outliner and Inspector

The Outliner and the Inspector are the two primary panels for building and editing a scene in `illusion_editor`. Every action in both panels runs an [editor command](../../reference/editor-commands.md), so edits made by hand and edits made by an AI agent share the same undo history, validation rules and transactions.

## Outliner

The Outliner lists every entity in the open scene as a tree ordered by hierarchy (`HierarchyIndex`). Disabled entities (and descendants of disabled entities, which carry the `Inactive` tag) are drawn in dimmed italics.

| Control | What it does |
| --- | --- |
| Search bar | Filters the tree as you type. Plain text fuzzy-matches entity names; tokens filter by Component, state or origin. |
| **+** button | Opens a menu to create an empty entity (`entity.create`), a 3D primitive (`entity.create_primitive`), a light (`entity.create_light`) or a camera (`entity.create_camera`). |
| Expand arrow | Collapses or expands an entity's children; **Left** and **Right** arrow keys collapse, expand or step into children. |
| Eye icon | Toggles editor-only visibility (`view.set_entities` with `hidden`). Hidden entities stay in the scene file unchanged. |
| Lock icon | Toggles editor-only locking (`view.set_entities` with `locked`). Locked entities have read-only Transform and Component fields in the Inspector. |

### Search filters

Combine any number of tokens with a name query:

| Token | Matches entities that |
| --- | --- |
| `has:Component` or `t:Type` | have a Component whose stable name or display name contains the token (for example `has:Light` or `t:RigidBody`). |
| `disabled:true` / `disabled:false` | are disabled (`Disabled` or `Inactive`) or active. |
| `prefab:true` | belong to a prefab instance. |

When a filter is active, matching entities are shown as a flat list with their full hierarchy path on the right.

### Selection, renaming and drag-and-drop

- **Click** selects an entity (`selection.set`); **Ctrl+click** toggles it in the selection (`selection.add` / `selection.remove`); **Shift+click** selects the range from the anchor row; **Up** and **Down** move through visible rows.
- Press **F2** (or choose **Rename** from the context menu) to rename the primary selected entity inline (`entity.rename`).
- **Drag** one or more entities onto another row to reparent them (`hierarchy.set_parent`), or onto the thin gap above or below a row to reorder them among siblings (`hierarchy.reorder`). Hold **Alt** while dropping to keep the local transform instead of the world transform (`keepWorld: false`). Drag onto the empty area at the bottom of the panel to unparent to the scene root.
- **Drag** an entity from the Outliner onto any `Entity` reference field in the Inspector to assign it.

```json
{"method": "hierarchy.set_parent", "params": {"entities": ["/Crate"], "parent": "/Room", "keepWorld": true}}
{"method": "view.set_entities", "params": {"entities": ["/Room"], "hidden": false, "locked": true}}
```

## Inspector

The Inspector shows and edits the current selection. Click the **pin** button in the top-right corner of the panel to lock the Inspector to its current entities while you change the selection elsewhere.

### Entity header

At the top of the Inspector:

- The **enabled** check box toggles the `Disabled` tag on every inspected entity (`entity.set_enabled`).
- The **name** field renames the primary entity (`entity.rename`) or shows how many entities are inspected.
- The **lock** button toggles `view.set_entities` (`locked`) on the inspected entities.
- The **ID** button copies the primary entity's 16-hex `EntityGuid` to the clipboard.

### Transform card

When every inspected entity has a `Transform`, the Transform card appears first:

- Switch between **Local** and **World** space with the segmented toggle in the header (`transform.set` with `"space": "local"` or `"world"`).
- **Position**, **Rotation** (in Euler degrees) and **Scale** use coloured X/Y/Z drag fields. The Inspector caches your Euler angles per entity so that values such as `180°` yaw or multi-turn angles do not flip when decomposed back from the quaternion.
- Right-click any row label (**Position**, **Rotation** or **Scale**) to reset only that property, or open the card's `⋮` menu to reset the whole Transform (`transform.reset`).

```json
{"method": "transform.set", "params": {"entities": ["$selection"], "space": "local", "position": [0, 2, 0], "euler": [0, 180, 0]}}
```

### Component cards and field widgets

Each reflected Component on the inspected entities is drawn as a collapsible card with its icon, display name and a `⋮` menu (**Reset**, **Copy Component**, **Paste Component Values**, **Remove Component** and **Documentation** when `docsAnchor` is set). Transient runtime Components (`PhysicsBody`, `CharacterBody`, `RenderMesh`, `RenderSky`) are shown read-only.

Field widgets are chosen automatically from the Component's reflection and `FieldMeta`:

| Field / Metadata | Widget |
| --- | --- |
| Numbers (`f32`, `f64`, integers) | Drag field with unit suffix (`m`, `°`, `kg`, `N`, `s`, `Hz`, `dB`, `lm`, `lx`, `cd`). Angles stored in radians with `displayUnit = Unit::Degrees` are edited in degrees and converted automatically. |
| `FieldHint::Slider` / `LogSlider` | Linear or logarithmic slider over `[min, max]` or `[softMin, softMax]`. |
| `Vec2`, `Vec3`, `Vec4`, `Quat` | Multi-axis drag fields (`Quat` is edited as Euler degrees). |
| `FieldHint::Color` / `HdrColor` | Colour swatch and picker; `Color` writes sRGB `#rrggbb` or linear RGB, and `HdrColor` adds a separate intensity drag. |
| `Enum` and `Flags` | Drop-down combo for enumerations; toggle chips for bitmask flags. |
| `FieldHint::Layer` | Collision layer selector (`0..31`). |
| `Entity` | Entity button showing the target's name (clicking selects it), a searchable entity picker popup, an eyedropper that picks the entity in a Scene view (see [Scene Viewport](viewport.md#picking-an-entity-for-a-field)), a clear button, and drop target for Outliner rows. |
| `Asset` (`AssetRef`) | Virtual path field (`assets:/...` or `builtin:/...`), a searchable asset picker filtered by `FieldMeta::assetKind`, a clear button, and drop target for assets. |
| `Optional<T>`, `Array<T>`, `FixedArray<T>`, `Struct` | Check box for `Optional`, element count and `+` / `-` buttons for `Array`, and indented nested rows for structs. |

While dragging any numeric or vector field:

- Hold **Shift** to scrub **10×** faster, or **Alt** to scrub **0.1×** slower.
- The cursor is captured (`CursorMode::Captured`) for **infinite drag**, so you can scrub across the screen edge without running out of room.
- Continuous frames of a drag share a `mergeKey` and collapse into a **single undo step** when you release the mouse button.
- Right-click any property label and choose **Reset** to restore that single field (`component.reset` with `path`).

### Custom drawers and inline validation

Several engine Components tailor their card to the active mode so irrelevant fields stay out of the way:

- **Collider** (`illusion.Collider`) shows only the dimensions that belong to the chosen `shape` (`halfExtents` for `Box`, `radius` for `Sphere`, `radius` and `halfHeight` for `Capsule` and `Cylinder`, `mesh` and `meshIndex` for `ConvexHull` and `Mesh`).
- **Joint** (`illusion.Joint`) hides anchor, axis, limit and motor fields that do not apply to the chosen `JointType`, and shows `minLimit` / `maxLimit` only when `limited` is checked.
- **AudioSource** (`illusion.AudioSource`) hides 3D distance, rolloff, doppler and cone fields when `spatial` is off.

When a Component has a configuration problem, a warning banner appears at the top of its card with a one-click fix-it button:

- A Component whose `requiredComponents` dependency is missing offers **Add** to add the missing Component across the selection.
- A `Collider` with `shape = Mesh` on an entity with a `Dynamic` `RigidBody` warns that triangle meshes are static/kinematic only and offers **Use ConvexHull**.

### Multi-entity editing

When two or more entities are selected:

- The Inspector shows the **intersection** of their Components.
- Fields whose value differs across the selection show `—`. Editing a field (or a single axis of a vector) updates only that field or axis on every selected entity in one `editor.batch` transaction and one undo step.

### Adding, copying and pasting Components

Click **Add Component** at the bottom of the Inspector to search every reflected Component by display name, stable name or category. Pressing **Enter** or clicking an entry adds the Component — together with any missing `requiredComponents` such as `Transform` or `Collider` — in a single undo step.

Use a card's `⋮` menu to **Copy Component** (`component.copy`) from one entity and **Paste Component Values** (`component.paste`) onto any selection; you can also click **Paste Component** at the bottom of the Inspector to add or update the copied Component type on the selection.

```json
{"method": "component.set_field", "params": {"entity": "/Crate", "type": "illusion.Collider", "path": "shape", "value": "ConvexHull"}}
{"method": "component.reset", "params": {"entity": "/Crate", "type": "illusion.Collider", "path": "halfExtents/x"}}
```

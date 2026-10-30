# Transform Tools

The Scene view draws a gizmo on the selected entities. Dragging one of its handles moves, rotates or scales the
selection; the whole drag is one undo step. Every part of it is also an
[Editor command](../../reference/editor-commands.md), so an AI agent chooses the same tool, axes, pivot and
snapping, and moves entities with the `transform.*` commands.

## Tools

The tool buttons are on the left of the toolbar; the keys work while no text field has the keyboard.

| Key | Tool | Handles |
| --- | --- | --- |
| **Q** | Select | No gizmo: a left drag draws a selection box. |
| **W** | Move | Arrows move along an axis, the squares move in a plane, the centre square moves in the view plane. |
| **E** | Rotate | Rings turn about an axis, the outer ring about the view direction, the inside of the rings turns freely (trackball). |
| **R** | Scale | The boxes scale along an axis, the centre box scales uniformly. |
| **T** | Universal | Move arrows and squares, rotation rings and the uniform scale box together. |

The Blender keymap selects with **W** and uses **G**, **R** and **S** for modal transforms instead (see
[Preferences](preferences.md)).

The handle under the cursor is drawn in the theme's hover colour, and the gizmo keeps its size on screen
however far away the selection is. The back halves of the rings are dimmed and cannot be grabbed. An axis that
points at the camera fades out, and so does a plane seen edge on, because a drag along them would be
unstable. The axis colours follow the `colorBlindAxes` preference (the Okabe-Ito colours).

Holding **Alt** gives the camera the left drag, so the gizmo never gets in the way of orbiting. A left drag that
starts away from the handles draws a selection box as with the Select tool.

## Dragging

A drag runs inside a transaction: the selection follows the cursor while the button is held, and releasing it
makes one undo step (labelled Move, Rotate or Scale, from the `gizmo` origin in the History panel). **Esc** or
the right mouse button cancels the drag and puts every entity back exactly where it was.

When the selection contains a parent and its children, only the parent is moved and its children follow it. The
active entity is the one selected last.

## Typed numbers

While a drag or a modal transform runs, typing a number replaces the cursor: the selection moves, turns or scales
by exactly that amount, and the number is shown next to the cursor. Digits and one point make a number, **-**
flips its sign, **Backspace** removes the last character and **Tab** moves to the next number.

| Handle | Numbers |
| --- | --- |
| Move arrow | The distance along the axis. |
| Move square | Two distances, along the plane's axes in x, y, z order. |
| Centre square (view plane) | Three distances, along the gizmo's x, y and z axes. |
| Ring, view ring, trackball | The angle in degrees (the trackball turns about the view direction). |
| Scale box, centre box | The scale factor (at least 0.01). |

**Enter** confirms a drag before the button is released; deleting every digit goes back to the cursor.

## Modal transforms

A modal transform moves, rotates or scales the selection with the cursor without holding a button, as **G**,
**R** and **S** do in Blender. It starts where the cursor is, in the Scene view under the cursor:

| Key (illusion keymap) | Blender keymap | What follows the cursor |
| --- | --- | --- |
| **Shift+W** | **G** | The position, in the view plane. |
| **Shift+E** | **R** | The rotation, about the view direction as the cursor goes round the pivot. |
| **Shift+R** | **S** | The scale, by the cursor's distance from the pivot. |

While it runs, **X**, **Y** or **Z** constrains it to the world axis; pressing the same key again uses the active
entity's own axes, and a third time removes the constraint. **Shift** with the key constrains it to the plane
of the other two axes (a rotation then turns about the key's axis). The constrained axes are drawn across the
view, and the keys are listed next to the cursor. Snapping and typed numbers work as in a drag. **Enter** or a
left click confirms (one undo step); **Esc** or a right click puts everything back.

## Axes

The axes button next to the tools picks the orientation of the Move and Rotate gizmos:

| Axes | Orientation |
| --- | --- |
| World | The scene's x, y and z axes. |
| Local | The active entity's own axes. |
| View | The camera's axes: x to the right, y up, z towards the viewer. |

The Scale gizmo always uses the active entity's own axes, because a scale along other axes would shear the
entity.

## Pivot

The pivot button picks the point the gizmo sits on and that rotations and scaling turn about:

| Pivot | Point |
| --- | --- |
| Median point | The mean position of the selected entities. |
| Active entity | The position of the active entity. |
| Individual origins | Each entity rotates and scales about its own position; the gizmo sits at the median point. |
| Bounding box center | The centre of the box around the selected entities' positions. |

## Snapping

The grid button turns snapping on; the arrow next to it sets the steps. Holding **Ctrl** during a drag inverts
the setting, so a drag snaps when snapping is off and moves freely when it is on.

| Step | Default | Range | What snaps |
| --- | --- | --- | --- |
| Move step | 0.5 | 0.001 to 1,000 world units | The distance moved along each axis of the gizmo. |
| Rotate step | 15° | 0.01° to 180° | The angle of a ring, the view ring or the trackball. |
| Scale step | 0.1 | 0.001 to 10 | The scale factor: 1, 1.1, 1.2 ... |

Move snapping counts from where the drag started. To put entities on the grid itself, use
`transform.snap_to_grid`, which rounds their world positions to the nearest multiple of the step.

## Arranging

The Entity menu and the commands below place entities by the boxes around what they draw (their Meshes and the
Meshes of their children); an entity that draws nothing counts as its position. In an editor without a GPU,
every entity counts as its position.

| Action | Key | What it does |
| --- | --- | --- |
| Drop to ground | **End** | Each selected entity falls (or rises) until the bottom of its box rests on the highest box of another entity below it, or on the ground plane y = 0 when there is none. Hidden and inactive entities are not surfaces. Physics colliders are not used. |
| Align to view | **Ctrl+Shift+F** | Turns the selection to the rotation of the Scene camera, so that it looks where the view looks (along its -z); `move` also puts it at the camera's eye, e.g. to aim a Camera from the current view. |
| Align | | Lines entities up along one axis: their min, centre or max edges at the same edge of the box around all of them, or of the last listed (active) entity, which stays put. |
| Distribute | | Spaces three or more entities evenly along one axis between the first and the last, which stay put: equal distances between their centres, or equal gaps between their boxes. |

Each of them is one undo step.

## Commands

| Command | What it does |
| --- | --- |
| `gizmo.get` | The tool, axes, pivot and snapping. |
| `gizmo.set_tool` | `select`, `translate`, `rotate`, `scale` or `universal`. |
| `gizmo.set_space` | `world`, `local` or `view`. |
| `gizmo.set_pivot` | `median`, `active`, `individual` or `bounds_center`. |
| `gizmo.set_snap` | Snapping on or off, and the `translate`, `rotate` and `scale` steps. |
| `gizmo.modal` | Start a modal `translate`, `rotate` or `scale` for the person at the editor. |
| `transform.snap_to_grid` | Round the world positions of entities to a grid (one undo step). |
| `transform.drop_to_ground` | Drop entities onto the boxes below them (`onto`: `surfaces`) or onto the ground plane (`plane`). |
| `transform.align_to_view` | Turn entities to a viewport's camera; `move` also places them at its eye. |
| `transform.align` | Line entities up along `axis` by their `min`, `center` or `max` edges, `to` the `selection` or the `active` entity. |
| `transform.distribute` | Space entities along `axis` by `centers` or `gaps`. |

The gizmo settings belong to the editor session: they are not saved in the scene and are not undo steps. An
agent that wants to move entities does not drag the gizmo; it calls `transform.translate`, `transform.rotate`
or `transform.scale` with the same pivots:

```json
{"method": "gizmo.set_snap", "params": {"enabled": true, "translate": 0.25}}
{"method": "transform.snap_to_grid", "params": {"entities": ["$selection"]}}
{"method": "transform.rotate", "params": {"entities": ["$selection"], "euler": [0, 90, 0], "pivot": "center"}}
{"method": "transform.drop_to_ground", "params": {"entities": ["$selection"]}}
{"method": "transform.align", "params": {"entities": ["/Crate 1", "/Crate 2"], "axis": "x", "edge": "min"}}
{"method": "transform.distribute", "params": {"entities": ["/Post 1", "/Post 2", "/Post 3"], "axis": "z", "mode": "gaps"}}
```

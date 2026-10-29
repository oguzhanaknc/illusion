# Scene Viewport

The Scene panel shows the open scene through the editor camera, a camera of the editor that is not part of the
scene. It draws the scene with the engine's renderer, adds a ground grid and an outline around the selection,
and selects entities with a click. Everything the panel does is also an [editor command](../../reference/editor-commands.md),
so an AI agent can move the camera, change the shading and pick entities the same way.

A headless editor started with `--no-gpu` has no viewports: the panel shows an empty state and the
`viewport.*` commands fail with code 1400 (`unsupported`).

## Moving the camera

The editor camera orbits around a pivot point. With the default `illusion` navigation the mouse controls
are:

| Input | Action |
| --- | --- |
| **Alt** + left drag | Orbit around the pivot. |
| Middle drag | Pan: the point under the cursor follows it. |
| Wheel | Dolly towards or away from the pivot. |
| **Alt** + right drag | Dolly. |
| Right drag | Look around; while it is held, **W A S D** move and **Q E** go down and up (**Shift** is faster, the wheel changes the speed). |
| **F** | Frame the selection (everything when nothing is selected). |
| **Shift+F** | Frame everything. |

Framing and the standard views move the camera with a short animation; the `reduceMotion` preference moves it
at once. While the camera moves, keyboard shortcuts are paused so that the fly keys do not run actions.

### Navigation presets

The `viewportNavigation` preference (Preferences, Keyboard page) picks the mouse controls of other tools.
`keymap`, the default, follows the keymap preset. The wheel dollies, **F** frames and **Alt** + left drag orbits
in every preset.

| Preset | Left drag | Middle drag | Right drag |
| --- | --- | --- | --- |
| `illusion`, `unity` | Select (box) | Pan | Fly (**Alt**: dolly) |
| `unreal` | Walk: turn, and move forward or back along the ground | Pan | Fly (**Alt**: dolly) |
| `blender` | Select (box) | Orbit (**Shift**: pan, **Ctrl**: dolly) | Nothing |

### View cube and axis navigator

The top right corner of a Scene view shows a cube that turns with the camera and, under it, the three axes.
Clicking a face of the cube moves the camera to that orthographic view (Top, Front, Right...); clicking an edge
or a corner turns the perspective camera to look along it. Dragging the cube orbits around the pivot. Clicking
the end of an axis looks along it: the filled ends are the positive axes, the hollow ones the negative axes.
The axes use the theme's axis colours.

## Game view and more viewports

The **Game** panel shows the scene through its main camera (the first active entity with a `Camera`), the
way the player will see it: without the grid, the outline and editor-only entities. When the scene has no
camera, the panel says so. The editor camera commands and the view menu work only in Scene views.

The **Aspect** menu of every viewport sets the shape of its image: **Free** fills the panel, the presets
(16:9, 16:10, 4:3, 21:9, 1:1) fit the largest image of that shape into the panel, and the fixed resolutions
(640 × 360, 1280 × 720, 1920 × 1080) render exactly that many pixels whatever the panel size. The image is
centred with bars around it.

**New viewport** in the Scene toolbar opens up to three more Scene views (Scene 2, 3, 4), each with its own
editor camera, starting from the camera and settings of the main one. Closing the window closes the viewport.

## Camera bookmarks

**Ctrl+Shift+1** to **Ctrl+Shift+9** save the editor camera of a Scene view in a bookmark; **Ctrl+1** to
**Ctrl+9** move the camera back to it. Bookmarks are shared by the Scene views and kept until the editor
closes.

## Selecting

A click selects the entity under the cursor; **Shift** + click adds it to the selection and **Ctrl** + click adds
or removes it. A click on the background clears the selection. Clicks within 2 pixels of an entity's edge still
hit it. Clicking the same spot again selects the next entity under the cursor, from front to back, so entities
hidden behind others can be selected too. Entities hidden or locked in the Outliner are never picked.

A left drag draws a selection box and selects every entity with a pixel inside it; **Shift** adds them and
**Ctrl** toggles them.

The entity under the cursor gets a thin outline in the hover colour of the theme before it is clicked. Entities
that have no mesh show an icon (billboard) where they are: cameras, directional, point and sky lights, audio
sources and audio listeners. Clicking an icon selects its entity; selected icons are drawn in the selection
colour.

### Picking an entity for a field

The eyedropper button next to an `Entity` field of the Inspector starts picking: the next click on an entity
or an icon in a Scene view writes that entity into the field (as one undo step), and the Scene view shows a
hint while it waits. A click on the background keeps waiting; **Esc** cancels.

### Following the agent

With **Follow agent** on (the AI Activity panel, or the `followAgent` preference), the main Scene view frames
the selection whenever an AI agent changes it, so the person at the editor sees what the agent works on.

## Visualisers

Visualisers draw the shape of Components that have no image of their own, as lines over the scene:

| Visualiser | What it draws |
| --- | --- |
| Lights | The range sphere of point lights; the direction of directional lights. |
| Cameras | The view frustum of every `Camera`, in the aspect of the Game view. |
| Colliders | The box, sphere, capsule, cylinder or bounds of every `Collider`; sensors in another colour. |
| Characters | The capsule of every `CharacterController`. |
| Joints | The anchor and axis of every `Joint`, with its limits and the line to the connected body. |
| Audio | The minimum and maximum distance spheres and the cones of spatial `AudioSource`s. |

By default they draw only the selected entities; **All** in the Visualisers menu draws every entity. Hidden
and inactive entities are never drawn.

When a `Camera` is selected, the bottom right corner of the Scene view shows a small picture of what that
camera sees (**Camera preview**). **Stats** shows the frame time, draw calls, triangles, visible objects, shadow
draw calls and the image size in the top left corner.

## Toolbar

| Menu | Contents |
| --- | --- |
| **Shading** | Lit, Unlit, Albedo, Normals, Roughness, Metallic, Ambient occlusion, Shadow cascades, Motion vectors, and the wireframe overlay. |
| **Overlays** | The grid, the selection outline, hover, icons, the view cube, the camera preview, stats and the **Visualisers** menu (one item per kind, and Selected or All). The Game view has only the outline, hover and stats. |
| **View** | Perspective and the orthographic Top, Bottom, Front, Back, Left and Right views around the pivot. |
| Frame | Frames the selection. |
| **Aspect** | Free, the aspect presets and the fixed resolutions. |
| New viewport | Opens another Scene view (main Scene panel only). |

The grid lies on the ground plane (the plane facing the camera in the orthographic side views). Its spacing
adapts to the distance in steps of ten, and the x and z axes are drawn in the axis colours of the theme (the
colour-blind preset uses the Okabe-Ito colours).

When the panel is resized, the image is stretched until the size has stayed for 150 ms or the mouse button is
released; then the viewport renders at the new size.

## Commands

| Command | What it does |
| --- | --- |
| `viewport.list` | The viewports with their size, camera and settings. |
| `viewport.camera_get` / `viewport.camera_set` | Read or move the editor camera: pivot, distance, yaw, pitch, field of view, projection, or an eye position that looks at the pivot. |
| `viewport.view` | A standard view: `perspective`, `top`, `bottom`, `front`, `back`, `left`, `right`. |
| `viewport.frame` | Frame entities, the selection, or everything. |
| `viewport.set_mode` / `viewport.set_overlays` | Shading and wireframe; the overlays and visualisers. |
| `viewport.project` | The pixel where an entity or a point appears. |
| `viewport.pick` | The entity at a pixel (asynchronous); optionally changes the selection. |
| `viewport.pick_rect` | Every entity with a pixel in a rectangle (asynchronous); optionally changes the selection. |
| `viewport.screenshot` | A PNG of the viewport image (asynchronous), as an MCP image. |
| `viewport.sample` | The average colour of a few pixels as RGBA and hue, saturation, value (asynchronous). |
| `viewport.create` / `viewport.close` | Open or close an extra Scene view (`scene-2` to `scene-4`). |
| `viewport.set_aspect` | Free, an aspect preset, or a fixed `width` and `height`. |
| `camera.bookmark_set` / `camera.bookmark_go` | Save the editor camera in slot 1-9, or move it back. |
| `render.stats` | Frames, visible objects, draw calls and triangles of a viewport's last frame. |
| `render.gpu_info` / `render.diagnostics` | The GPU and driver; whether validation is on and how many errors it reported. |

An agent finds an entity on screen with `viewport.project` and passes the pixel to `viewport.pick`:

```json
{"method": "viewport.project", "params": {"entity": "/Game/Board"}}
{"method": "viewport.pick", "params": {"x": 640, "y": 360, "select": "replace"}}
```

Every command takes a `viewport` id (`scene` by default; `game` is the Game panel). An agent checks what the
player sees by fixing the game view's resolution, taking a screenshot and sampling a colour:

```json
{"method": "viewport.set_aspect", "params": {"viewport": "game", "aspect": "fixed", "width": 640, "height": 360}}
{"method": "viewport.screenshot", "params": {"viewport": "game"}}
{"method": "viewport.sample", "params": {"viewport": "game", "x": 320, "y": 180, "radius": 2}}
```

An agent turns on the collider visualisers for every entity and the stats with:

```json
{"method": "viewport.set_overlays", "params": {"visualisers": {"colliders": true, "audio": false}, "visualiserScope": "all", "stats": true}}
```

Picks, screenshots and samples render the viewport themselves when its panel is hidden, and wait until the
viewport's assets have loaded, so they see the finished image.

Pixels count from the top left corner of the viewport image, with y pointing down; `viewport.list` gives its
size. The camera, the shading and the overlays are editor state: they are not saved in the scene and not part
of the undo history.

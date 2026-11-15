# Game UI Architecture

This page describes how the game UI is built: which library does what, what happens in a frame, and the rules
that keep it fast. How to use it is in the [Game UI manual](../manual/ui.md); the decision and its alternatives
are [ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md).

## Layers

Each layer depends only on the ones above it in this list:

1. **`engine/assets`: `Font`.** A CPU asset. `Font::bake()` turns a `.ttf` or `.otf` file into one
   single-channel signed distance field atlas with glyph advances, bounds and kerning in ems. It is cooked like
   other assets and has no GPU code.
2. **`engine/ui`: geometry, draw list, Components, `UiWorld`, `UiService`.** `UiGeometry.hpp` holds the canvas
   scale and the placement math, `UiDrawList.hpp` the batched quads, `UiComponents.hpp` and `UiWidgets.hpp` the
   saved `illusion.Ui*` Components, `UiWorld.hpp` the System (layout, interaction, events, transitions) and
   `UiService.hpp` the scripting API with its adapter `UiWorldService`. There is no GPU code in this library;
   game modules take its headers with `USES ui`.
3. **`engine/render`: `UiRenderer` and `UiRenderAssets`.** `UiRenderer::render()` draws a `UiDrawData` into a
   colour target. `UiRenderAssets` loads fonts and images through the `AssetStreamer` and registers them with
   the renderer; it implements the `UiAssets` interface that the UI library declares, so the library never
   sees a GPU type.
4. **`engine/runtime`: `GameSession`.** One running game owns a `UiWorld`, runs it in the step order of the
   session, fills its input from the platform and the input actions, and owns the game's `Localization`
   (`assets:/locales`).
5. **The hosts.** The exported player (`engine/player`) and the editor's Game view (`editor/play`,
   `editor/viewport`) set the target size with `GameSession::setUiFrame()`, call `UiRenderer::render()` after the
   scene and show the result.

## A frame

`GameSession::update()` runs the UI after the game modules and the hierarchy step, so a module's changes show
in the same frame:

1. **Layout, only when something changed.** `UiWorld` compares the `World::componentVersion()` of the UI
   Components, the hierarchy and the `Inactive` tags, the language and the target size with those of the last
   layout. When nothing changed it reuses the last result (`ui/layout-reused`). Otherwise it resolves
   placements, stacks and grids into one `UiComputedRect` per element.
2. **Interaction.** With input, the pointer hit test (topmost interactable element, honouring rotation, scale,
   clips and modal canvases), focus and navigation by `ui.navigate`, `ui.submit` and `ui.cancel`, text
   fields, scrolling and transitions run against the layout, and the events of the update are collected for
   `UiService::pollEvents()`.
3. **The draw list.** The System walks the canvases by `order` and the elements in hierarchy order and records
   rectangles, images, nine-slice panels and glyphs as quads into the pre-reserved `UiDrawList`. Consecutive
   quads with the same texture, clip and text effect share a command.
4. **The pass.** The host hands `UiWorld::drawData()` to `UiRenderer::render()`, which draws it over the
   colour target that `SceneRenderer` filled, **after tonemapping and anti-aliasing**, with premultiplied alpha
   blending and no depth. TAA and upscalers never see the UI, and the screenshots include it.

A headless session (tests, the editor's command core) lays out and handles input without drawing. The update
without input, which the Game view uses to preview the edit World, lays out and records but advances no
transition and writes no saved Component (`UiWorldDesc::preview`).

## Why elements are Entities

A canvas is an Entity with `UiCanvas` and its elements are its descendants. Scenes and prefabs save them, the
reflection draws their Inspector, the generic `entity.*` and `component.*` commands and the MCP tools edit them,
undo and hot reload work on them, and the layout is a System over contiguous Components. A UI document of
their own would have duplicated all of that. The price is that a UI Entity has no `Transform` (the engine's
other Systems skip it) and that a long list is many Entities; [ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md)
has the options that were weighed.

## Allocation and performance rules

- **No allocation in a steady frame.** The arrays of the draw list and the event list are sized by
  `UiWorldDesc` when the `UiWorld` is created (16,384 elements, 64 levels of nesting and 256 events per update
  by default). What does not fit is dropped or skipped and counted in `UiWorldStatistics`, never grown. Tests
  count the allocations of a steady frame, with input too.
- **Layout runs only on change.** Writing a UI Component through a reference needs `world.markChanged<T>()`;
  `UiService` does it for modules.
- **Transitions only draw.** They never change the layout; a transition that has arrived costs nothing.
- **The pass allocates nothing of its own.** NVRHI makes a small, constant number of allocations for the two
  buffer uploads per frame; a test asserts that the number does not grow. `UiRenderer` is created with fixed
  capacities for vertices, indices and textures.
- **Batches are broken only by state.** A texture, a clip rectangle or a text outline that differs from the
  previous quad starts a new draw call; a HUD is a handful of them.

The benchmarks and their budgets (nanoseconds per unit, set to roughly 5 to 10 times a desktop result; see
[Profiling](../manual/profiling.md#performance-budgets)):

| Benchmark | Unit | Budget | Measured on the development machine |
| --- | --- | --- | --- |
| `ui/text-layout` | glyph | 1,000 ns | not recorded |
| `ui/draw-list-quads` | quad | 250 ns | not recorded |
| `ui/layout` | element, 1,000 elements | 5,000 ns | 544 ns |
| `ui/layout-reused` | element, 1,000 elements | 3,000 ns | 408 ns |
| `ui/hit-test` | update with 1,000 buttons | 400,000 ns | 58 µs |
| `ui/set-text-key` | call | 20,000 ns | 2.6 µs |
| `ui/scroll-1000` | update | 300,000 ns | 43 µs |
| `ui/transitions-1000` | update with 1,000 running transitions | 500,000 ns | 81 µs |
| `ui/render-hud` (GPU) | `render()` of 195 quads, with submit and wait | 500,000 ns | 67 µs |
| `gameui/pick-and-handles` (editor) | pick and handle hit tests | 20,000 ns | 3.3 µs |

## The editor side

The editor links the same libraries; nothing in a game links the editor (ADR 0030).

- **The Game view** draws the UI of the World being edited with a preview `UiWorld` (`UiWorldDesc::preview`),
  laid out for the size of the view and drawn by a `UiRenderer` after the scene. While a game runs it draws
  the session's own UI. The Scene view draws no UI.
- **Commands.** The category Game UI: `gameui.create`, `gameui.layout`, `gameui.click`, `gameui.focus`,
  `gameui.pick`, `gameui.place` and `gameui.set_anchors` (193 commands in all), each also an MCP tool. They
  change the World only through `WorldEditor`, so every one is undoable; `gameui.click` and `gameui.focus` act
  on the running game's session.
- **Placement math.** `UiPlacementEdit.hpp` in `editor/core` is GPU-free: the 16 anchor presets, placements
  that keep or reach a rectangle, picking in a computed layout, handle hit tests and drags. The window's Game
  view and the commands share it, so a drag with the mouse and a call of `gameui.place` give the same result.
- **Handles and markers** are drawn by the editor's own UI over the picture of the Game view, with the theme
  tokens `canvas.selection`, `canvas.handle` and `canvas.anchor`; the game's draw list never contains editor
  graphics.

## Open items

Recorded in the roadmap and in ADR 0057:

- Right-to-left text and shaping, and CJK with dynamic atlas pages; text selection and a clipboard.
- Multi-channel distance fields, if titles look soft at large sizes.
- World-space UI (a label above a character).
- HDR output of the UI pass, and drawing it after the upscale instead of at the render scale (both with M9).
- Zooming and panning the Game view to frame a small element, and the UI in the Scene view.
- Glyph images instead of labels, and recycled lists.
- A UI-specific function for instantiating a UI prefab (`SceneService::instantiate()` does it today).
- Canvases with different scales share one draw list in the units of the first canvas, and only that canvas
  snaps glyphs to pixels; zero allocations in the pass need persistently mapped buffers.

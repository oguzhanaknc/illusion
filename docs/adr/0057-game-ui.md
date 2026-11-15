# 0057. Build the game UI from ECS elements drawn by a batched pass after tonemapping

- Status: Accepted
- Date: 2026-11-14

## Context

Games have no way to draw a HUD or a menu. The editor's UI is Dear ImGui, which is never linked by games
(ADR 0030), is styled and navigated as a tool, and is immediate-mode: nothing of it can be saved, edited in
the editor or driven by an agent as game content. The constraints:

- **One runtime.** A game runs the same in Play mode and in the exported player (ADR 0042, ADR 0055), from
  saved files, through the scripting API (ADR 0041), with hot reload.
- **Performance.** No allocation in a steady frame; a HUD is a handful of draw calls.
- **Any resolution.** Text and panels stay sharp from 720p to 4K and on ultrawide and notched displays.
- **Input.** Menus work with the mouse, the keyboard and gamepads through `InputActions`, with glyphs per
  device.
- **Dependencies.** Permissive licences only; `stb` is already pinned (ADR 0054 uses `stb_image`).

Options considered for the element model:

- **Ship Dear ImGui in games.** No work on rendering, but it reverses ADR 0030, cannot be saved or skinned
  well, and gives agents nothing to edit.
- **A third-party retained UI (RmlUi, Noesis, a web view).** RmlUi (MIT) brings HTML/CSS, FreeType and its own
  document model, data binding and files: a second scene format beside ours, a large dependency, and nothing
  of the editor (Inspector, undo, prefabs, commands) works on it.
- **An own document tree.** Full control, but it duplicates what the ECS already has: a hierarchy, reflection,
  serialization, prefabs, undo, the Inspector, the Outliner and 170 commands.
- **UI elements are Entities.** A canvas is an Entity with children; elements are Components. Scenes and
  prefabs save them, the reflection edits them, `entity.*`/`component.*` commands and MCP tools reach them with
  no new protocol, and layout is a System over contiguous Components.

Options considered for text:

- **Bitmap atlases per size.** Blurry when scaled; one atlas per size and resolution.
- **Multi-channel distance fields (msdfgen, MIT).** Sharp corners at any size; a new dependency with FreeType
  or its own loader, and a slower bake.
- **Single-channel distance fields from `stb_truetype`.** One atlas per font for every size, no new
  dependency; corners round off when a glyph is drawn several times larger than it was baked.

## Decision

- **Elements are Entities.** A UI is a subtree of a World: a root with `UiCanvas`, descendants with
  `UiElement` (placement) and at most one visual or layout Component each (`UiImage`, `UiText`, `UiStack`,
  `UiGrid`; widgets in UI-2). They are saved Components (`illusion.Ui*`), parented with `Hierarchy.hpp`, and
  drawn in hierarchy order (later siblings on top; canvases by `UiCanvas::order`). A "UI document" is a prefab
  (ADR 0046) whose root has a `UiCanvas`. UI Entities have no `Transform`; engine Systems that need one skip
  them, and `Inactive` hides a subtree as everywhere else (ADR 0034).
- **Libraries.** `engine/ui` (`illusion::ui`, with the header-only `illusion::ui_api` for modules, `USES ui`)
  holds the geometry, the draw list, the Components, layout, interaction and the service; it has no GPU code.
  `Font` is a CPU asset in `engine/assets`. `UiRenderer` in `engine/render` draws a `UiDrawData`. The editor
  links them like any engine library.
- **Units and scaling.** UI coordinates are canvas units, y down, origin at the top left. `UiCanvasScale`
  (`Fit` by default against a 1920x1080 reference, `MatchHeight`, `MatchWidth`, `Pixels`, times a user scale
  for accessibility) gives `canvasMetrics()`: pixels per unit, the canvas size in units and the safe area.
  The host passes the display's safe insets in pixels; a canvas with `useSafeArea` lays out inside them.
- **Placement.** `UiPlacement` is two normalised anchors and two offsets in units:
  `min = parent.min + anchorMin * parent.size + offsetMin`, likewise for `max`. Inside a `UiStack` or `UiGrid`
  the parent places its children and the placement is ignored. Rotation, scale and opacity of `UiElement`
  apply to the element and its descendants about its pivot, and do not affect layout.
- **Text.** `Font::bake()` turns a TrueType or OpenType file into one single-channel signed distance field
  atlas with `stb_truetype` (default 48 px per em, spread 6 px, Latin with Latin Extended-A; ranges are
  import settings), with advances, bounds and pair kerning in ems. It is cooked like other assets
  (`AssetType::Font`). Text is UTF-8, laid out left to right, one glyph per code point, wrapped at spaces and
  line breaks; a missing glyph draws the font's fallback glyph and is counted. The engine ships no font: a
  game brings its own in `assets:`, and the project templates include one. Text of a `UiText` is a literal or
  a string table key formatted by `MessageFormat` in the session's `Localization` (game locales are loaded
  from `assets:/locales`).
- **Drawing.** Each frame the UI System writes a `UiDrawList`: pre-reserved vertices (`position`, `uv`,
  packed colour), 32-bit indices and commands (texture, clip rectangle, index range, SDF spread). Primitives
  are quads: solid rectangles, images, nine-slice panels and glyphs; consecutive primitives with the same
  texture and clip share a command. A full list drops the primitive, returns false and counts it. Colours in
  the API are linear RGBA, straight alpha; vertices store them sRGB-encoded in 8 bits.
- **The pass.** `UiRenderer::render()` draws a `UiDrawData` into the colour target that
  `SceneRenderer::render()` filled, after tonemapping and anti-aliasing, with premultiplied-alpha blending and
  one pipeline for images and distance fields (the fragment shader picks by the command's spread and derives
  the edge width from screen-space derivatives). No depth, no history: TAA and later upscalers never see the
  UI. The player and the editor's Game view call it after the scene; screenshots include it.
- **Session.** `GameSession` owns the UI System and runs it after the modules and the hierarchy step: layout,
  then interaction against `keyboardMouse()` and the actions, then the draw list, which the host reads with
  the session and hands to its `UiRenderer`. A headless session lays out and handles input without drawing.
- **Input.** The pointer hits the topmost interactable element under the cursor. Focus moves by the actions
  `ui.navigate` (a 2D axis), `ui.submit` and `ui.cancel`, which every project's input asset gets with default
  bindings (arrows/WASD, Enter/Space, Escape, the left stick and D-pad, South, East) and which players can
  rebind; navigation picks the nearest focusable element in the direction, or explicit neighbours. A
  `UiGlyph` element shows the current binding of an action for the device used last, from a glyph atlas keyed
  by `glyphId()`. Text fields read `InputState::text()`. While a canvas marked modal is shown, the game's
  other actions see no input.
- **Scripting.** `UiService` (`illusion/ui/UiService.hpp`, group `scripting_api`) lets modules find elements
  by Entity or by name under a canvas, set text (literal or key with arguments), values, visibility and focus,
  instantiate a UI prefab, and poll events (`Clicked`, `ValueChanged`, `Submitted`, `Cancelled`,
  `FocusChanged`) into a caller span. Values, not callbacks, as in ADR 0056: nothing calls into a module, so
  hot reload cannot dangle. The header lands with its implementation (UI-3), because every scripting API
  function must be called by the showcase module.
- **Editor.** UI Entities are edited with the existing commands, Inspector, Outliner, undo and prefabs. UI-3
  adds a 2D canvas view with placement handles and the commands that are not generic (`gameui.*`: create an
  element with defaults, query computed rectangles, click and focus in Play mode); `ui.*` stays the editor's
  own UI.

## Consequences

- The UI gets saving, prefabs, undo, the Inspector, agents' tools and hot reload from the ECS; a list of a
  thousand rows is a thousand Entities, which the archetype storage handles, but virtualised lists are needed
  for very long ones (UI-2).
- Layout is a System over all UI Entities each frame; it must stay allocation-free and has a benchmark. It
  recomputes only when a UI Component, the hierarchy or the canvas size changed (`World::componentVersion()`).
- Single-channel fields round sharp corners at large sizes and carry no hinting; small text (under about
  12 px on screen) is softer than hinted bitmaps. Revisit with multi-channel fields if titles look soft.
- No shaping: Arabic, Hebrew, Indic scripts and ligatures are not supported, and CJK needs ranges too large
  for one static atlas. Right-to-left text, shaping (HarfBuzz) and dynamic atlas pages are follow-ups; the
  `Font` interface (glyph lookup by code point) will change for shaping.
- Outlines, shadows and gradients are not in the first pass; a shadow is the text drawn twice. The distance
  field allows outlines in the shader later without new assets.
- The UI is composited in the display's colour space after tonemapping; HDR output (M9) needs a paper-white
  scale in this pass.
- Elements without `Transform` are new to the editor: the viewport's gizmos, picking and framing ignore them
  until the canvas view of UI-3.
- World-space UI (a label above a character) is not covered; it needs a canvas placed by a `Transform` and
  depth, and is its own decision.

## Addendum: UI-3c, transitions and text effects

- A `UiTransition` changes drawing and hit testing only (offset, scale about the pivot, opacity, tint, applied
  to the element and its descendants). Layout and `UiComputedRect` never see it, so a transition cannot
  trigger a layout pass.
- Its progress lives in a runtime Component, `UiTransitionState`, and only the update with input advances it
  by `UiInput::deltaSeconds`. The update without input (the editor's preview) moves nothing, and fixed-step
  runs stay deterministic. Settled transitions cost nothing.
- Outlines use the distance field of the font with per-draw-command push constants (width and colour), so
  text with a different outline splits a batch; the width is limited to the font's spread minus one texel so
  that it stays inside the glyph quads. No new assets are needed.
- A shadow is a second draw of the same text, offset and drawn first; the shadow of outlined text includes
  its outline.

## Addendum: UI-3d, canvas editing

- UI Entities are edited in the Game view, where the UI is drawn at the size the game will have, not in the
  Scene view: a placement only means something for a target size, and the Game view already lays the edit
  World out for its image. The handles are drawn by the editor's own UI over that image; the game UI's draw
  list never contains editor graphics.
- The math is GPU-free in `editor/core` (`UiPlacementEdit.hpp`): anchor presets, placements that keep or
  reach a rectangle, picking in a `UiLayout`, handle hit tests and drags. The shell and the commands share it.
- The Game view and agents use the same commands: `gameui.pick`, `gameui.place` and `gameui.set_anchors`.
  They take the target size, as `gameui.layout` does, because offsets depend on the parent's size unless
  the anchors coincide. A drag is one transaction of `gameui.place` steps, as a gizmo drag is.
- The viewport host reports the layout its Game view last drew (`ViewportHost::gameUiLayout()`), so the
  shell does not lay the World out a second time.
- A child of a `UiStack` or `UiGrid` is placed by its parent: it has no move or anchor handles, and
  resizing writes `UiElement::size`.
- Not in this package: zooming and panning the Game view to frame a small element (the UI would have to be
  drawn at another scale than the game's), and drawing the UI in the Scene view.

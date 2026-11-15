# Game UI

The UI of a game is made of Entities. A **canvas** is an Entity with `UiCanvas`; the Entities below it with
`UiElement` are its elements, placed by anchors and offsets, by a `UiStack` or by a `UiGrid`, and drawn as
images (`UiImage`) and text (`UiText`). `UiWorld` lays the elements out and records one draw list per frame
inside a `GameSession`; the exported player and the editor's Game view draw it over the scene with
`UiRenderer` (ADR 0057). The UI Components have no `Transform`, and they are saved in scenes and prefabs like
every other Component.

Headers: `<illusion/ui/UiComponents.hpp>`, `<illusion/ui/UiGeometry.hpp>`, `<illusion/ui/UiWorld.hpp>`,
`<illusion/ui/UiWidgets.hpp>`

Library: `illusion::ui` (game modules take the headers with `USES ui`)

All sizes are **canvas units**: the origin is the top left corner and y points down. A canvas maps its units
to the pixels of the target, so a UI looks the same at every resolution (see below).

## Canvases and scaling

A canvas is the root of a UI. Its `scale` (`UiCanvasScale`) says how canvas units map to the target:

| `UiScaleMode` | One canvas unit is |
| --- | --- |
| `Fit` (default) | The smaller of target width / `referenceSize.x` and target height / `referenceSize.y` pixels: the whole reference size always fits |
| `MatchHeight` | Target height / `referenceSize.y` pixels: the canvas is always `referenceSize.y` units tall, its width follows the aspect ratio |
| `MatchWidth` | Target width / `referenceSize.x` pixels: the canvas is always `referenceSize.x` units wide |
| `Pixels` | One pixel |

`referenceSize` is the size the UI was designed for (default 1920 x 1080). `userScale` is an extra factor the
player can choose for accessibility, clamped to 0.5 to 3; it multiplies every mode, `Pixels` included.
`canvasMetrics()` returns the result for a target: `pixelsPerUnit`, the canvas `size` in units and the
`safeArea`.

| Field of `UiCanvas` | Meaning |
| --- | --- |
| `scale` | The `UiCanvasScale` above |
| `order` | Canvases with a higher `order` are drawn on top; equal orders keep the order of their Entities |
| `useSafeArea` | On by default: the canvas is laid out inside the display's safe insets (a TV's overscan, a phone's notch) instead of the whole target |
| `modal` | For a pause menu: while it shows an element, only it and the canvases above it take input and the game's other input actions see none (see [Modal canvases](#modal-canvases)) |

The host passes the target size and the unsafe borders in pixels with `GameSession::setUiFrame()` (the
player and the Game view do). Entities with the `Inactive` tag, and their descendants, are not laid out or
drawn, so disabling a canvas hides the whole UI.

## Placement

`UiElement::placement` puts an element inside the rectangle of its parent (the canvas's area for a child of
the canvas) with two anchors and two offsets:

| Field | Meaning |
| --- | --- |
| `anchorMin` | Where the top left corner is anchored in the parent: (0, 0) is its top left, (1, 1) its bottom right |
| `anchorMax` | The same for the bottom right corner |
| `offsetMin` | The top left corner's distance from its anchor, in canvas units |
| `offsetMax` | The bottom right corner's distance from its anchor |

Each corner is `parent.min + anchor * parent.size + offset`. With equal anchors the element has a fixed size
at a point of the parent; with different anchors it stretches with the parent. `resolvePlacement()` computes
the rectangle; its `max` is never before its `min`.

**Example 1: a score in the top right corner**, 296 x 64 units, 24 units from the top and the right edge:

```cpp
illusion::UiPlacement topRight;
topRight.anchorMin = topRight.anchorMax = {1.0f, 0.0f};
topRight.offsetMin = {-320.0f, 24.0f};
topRight.offsetMax = {-24.0f, 88.0f};
const illusion::UiRect rect = illusion::resolvePlacement(topRight, {{0.0f, 0.0f}, {1920.0f, 1080.0f}});
// rect: min (1600, 24), max (1896, 88)
```

**Example 2: a bar along the bottom** that is 72 units tall, 24 units above the bottom edge, and 24 units
from the left and right edges whatever the canvas width:

```cpp
illusion::UiPlacement bottomBar;
bottomBar.anchorMin = {0.0f, 1.0f};
bottomBar.anchorMax = {1.0f, 1.0f};
bottomBar.offsetMin = {24.0f, -96.0f};
bottomBar.offsetMax = {-24.0f, -24.0f};
const illusion::UiRect rect = illusion::resolvePlacement(bottomBar, {{0.0f, 0.0f}, {1920.0f, 1080.0f}});
// rect: min (24, 984), max (1896, 1056)
```

A child of a `UiStack` or `UiGrid` is placed by that parent and ignores `placement`; it uses only `size`.
`size` is the element's size in canvas units there; a zero axis takes the size of the content (a `UiText` or
an image) or stretches.

## Images

`UiImage` draws a Texture over the element's rectangle, or a solid colour when `image` is empty.

| Field | Meaning |
| --- | --- |
| `image` | The Texture Asset (`assets:/textures/panel.png`); empty draws `tint` as a solid colour |
| `tint` | Linear RGBA multiplied with the image; its alpha and the element's `opacity` fade it |
| `border` | Nine-slice borders in image pixels (`left`, `top`, `right`, `bottom`); all zero stretches the whole image |
| `borderScale` | Canvas units per image pixel of the borders: 2 draws an 8 pixel border 16 units wide |
| `preserveAspect` | Fits the image inside the element keeping its aspect ratio; borders are ignored |

A nine-slice panel keeps its corners and edges crisp at any size: the four corners are drawn at
`border * borderScale` units, the edges are stretched along one axis and the centre along both.

An image that is still loading draws nothing until it arrives.

## Text

`UiText` draws text in the element's rectangle with a [font Asset](assets.md#fonts), a `.ttf` or `.otf` file
in the project's assets. The default character ranges cover Basic Latin, Latin-1 and Latin Extended-A, so
Turkish and most European languages work.

| Field | Meaning |
| --- | --- |
| `text` | The text, UTF-8, shown when `key` is empty |
| `key` | A string table key of the game's Localization; its message is shown instead of `text` |
| `font` | The font Asset (`assets:/fonts/Roboto-Regular.ttf`); without one nothing is drawn |
| `size` | Em size in canvas units |
| `color` | Linear RGBA |
| `align` | Horizontal: `Start`, `Center` or `End` |
| `verticalAlign` | Vertical in the element: `Top`, `Middle` or `Bottom` |
| `wrap` | On by default: lines break at the element's width; off keeps one line |
| `lineSpacing` | Multiplier of the font's line height (1 is the font's own) |
| `letterSpacing` | Extra advance per glyph, in ems |

New projects made from an editor template contain `assets/fonts/Roboto-Regular.ttf` (Apache-2.0, with its
licence next to it), so `UiText` works at once; any other `.ttf` works the same way.

### Literal and localized text

A literal `text` is shown as written. A `key` is looked up in the game's Localization, and its message is
drawn instead (`GameSession::localization()`; it follows the active language, and the layout updates when
the language changes). The session reads the project's `assets:/locales/*.json` when it is created: the
English text of `en.json` is the base, and every other file adds a language. A key the tables lack shows the
key itself, which is counted in `UiWorldStatistics::missingKeys`.

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "number": {"decimal": ".", "group": ","},
  "strings": {
    "showcase.hud.title": "Showcase HUD"
  }
}
```

`assets/locales/tr.json` has the same keys with the translations (see [Localization](localization.md)):

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "tr",
  "displayName": "Türkçe",
  "plural": "tr",
  "number": {"decimal": ",", "group": "."},
  "fallback": "en",
  "strings": {
    "showcase.hud.title": "Vitrin Arayüzü"
  }
}
```

A `UiText` with `key` `showcase.hud.title` then shows "Showcase HUD", or "Vitrin Arayüzü" when the active
language is `tr`.

## Stacks and grids

`UiStack` on an element places its children one after another:

| Field | Meaning |
| --- | --- |
| `axis` | `Vertical` (default, top to bottom) or `Horizontal` (left to right) |
| `spacing` | Gap between children in canvas units |
| `padding` | Space inside the element's edges (`left`, `top`, `right`, `bottom`) |
| `crossAlign` | Placement across the direction: `Start`, `Center`, `End` or `Stretch` (default: fills the stack's inner size) |

Along the axis a child takes its `size`, else its content's size (a text wraps at the stack's inner width),
else it shares the space that is left with the other stretching children. Across the axis it follows
`crossAlign`.

`UiGrid` places the children in rows of equal cells, left to right and then down: `columns` cells per row
(at least 1), `cellSize` for each cell, `spacing` between the columns and the rows, and `padding`. A child
with a `size` is centred in its cell at that size, else it fills the cell. A stack or a grid can contain
other elements with their own stacks and grids.

## Rotation, scale, opacity and clipping

These fields of `UiElement` change how an element and its descendants are drawn, not where the layout puts
them:

| Field | Meaning |
| --- | --- |
| `pivot` | Centre of rotation and scale in the element's rectangle: (0.5, 0.5) is the middle |
| `rotation` | Clockwise rotation about the pivot, in radians (shown in degrees in the Inspector) |
| `scale` | Visual scale about the pivot; the layout does not see it |
| `opacity` | 0 to 1, multiplies the alpha of the element and every descendant |
| `clipChildren` | The descendants are not drawn outside the element's rectangle |

Rotation, scale and opacity accumulate down the hierarchy. A clip is a rectangle: use it for scrolling areas
and bars that fill up, not for rounded shapes.

## Draw order

Canvases are drawn in ascending `order` (equal orders by Entity index). Inside a canvas the elements are
drawn in hierarchy order: a parent first, then its children, and later siblings over earlier ones. To bring
an element to the front, move it after its siblings with `reorder()` from `<illusion/ecs/Hierarchy.hpp>`.

## Building a HUD in the editor

The **Entity > Game UI** menu (also in the command palette) creates a canvas or an element with sensible
defaults as one undo step. Each kind creates an Entity with these Components, and the children it needs:

| Kind | Entity and Components | Children |
| --- | --- | --- |
| Canvas | `UiCanvas` | none |
| Panel | `UiElement` (400 x 300), `UiImage` (translucent dark) | none |
| Text | `UiElement` (320 x 48), `UiText` (size 32) | none |
| Button | `UiElement` (240 x 64), `UiImage`, `UiInteractable`, `UiButton` | `Label`: `UiElement` filling the button, `UiText` centred |
| Toggle | `UiElement` (48 x 48), `UiImage`, `UiInteractable`, `UiToggle` | `Check`: `UiElement` inset by 10, `UiImage`; shown while the toggle is on |
| Slider | `UiElement` (320 x 24), `UiImage`, `UiInteractable`, `UiSlider` | `Fill`: `UiElement` filling the slider, `UiImage`; `Handle`: `UiElement` (16 wide, 12 taller than the slider), `UiImage` |
| Text Field | `UiElement` (320 x 48), `UiImage`, `UiInteractable`, `UiText`, `UiTextField` | none |
| Scroll View | `UiElement` (400 x 300), `UiImage`, `UiScrollView` | `Content`: `UiElement`, `UiStack` (spacing 8, padding 8) |
| Stack | `UiElement` (320 x 400), `UiStack` (spacing 8) | none |
| Grid | `UiElement` (440 x 440), `UiGrid` | none |

An element is placed 24 canvas units from the top left of its parent. Its parent is the selected canvas or UI
element, else the first canvas of the scene; when the scene has no canvas, one named `Canvas` is created
first. A text, a button's label and a text field's placeholder use the project's first font Asset.

1. Choose **Entity > Game UI > Canvas**, or any element: it goes into a canvas that is created for you.
2. Select a canvas or an element before you create the next one to put it inside.
3. Set the fields in the Inspector: the placement anchors and offsets, the text, the font and texture Assets
   (drag them from the Asset Browser onto the fields), the colours and sizes. The canvas has no placement
   handles yet, so placement is edited in the Inspector for now.
4. Look at the result in the **Game view**: it draws the scene's UI also while no game runs. The Scene view does
   not draw UI. The `viewport.screenshot` command with `"viewport": "game"` takes a picture of it.

The `samples/showcase` project contains a HUD with every UI Component and every field; the scene is built
by `build.jsonl` (see [Showcase Project](editor/showcase.md)).

## Building a UI with an agent {#building-a-ui-with-an-agent}

An agent builds and checks a UI with four [Editor commands](../reference/editor-commands.md) of the category
Game UI. The menu items above run `gameui.create`.

**`gameui.create`** makes a canvas or an element with its children in one undo step. `kind` is `canvas`,
`panel`, `text`, `button`, `toggle`, `slider`, `text_field`, `scroll_view`, `stack` or `grid`. `parent`
defaults to the selected canvas or UI element, else the first canvas; `name` defaults to the kind's name; `text`
is the text of a text, a button's label or a text field's placeholder. The result has the new `entity`, its
`parts` (the children with their roles, such as `label`) and `canvas` when one had to be created.

```json
{"call": "gameui.create", "params": {"kind": "button", "parent": "/HUD", "name": "Start", "text": "Start"}}
```

**`gameui.layout`** returns where the UI places its elements, without changing the scene: per element its
rectangle in canvas units (`min`, `max`) and in target pixels (`pixelMin`, `pixelMax`), `opacity`, `visible`,
`interactable` and `focused`, and per canvas its `pixelsPerUnit` and `size`. The edit World is laid out for
`width` x `height` pixels (default 1920 x 1080); while a game runs it reads the play World, unless `world` is
`"edit"`. `entity` limits it to one canvas or element and its subtree.

```json
{"call": "gameui.layout", "params": {"entity": "/HUD", "width": 1280, "height": 720}}
```

**`gameui.click`** clicks an element of the running game at its centre, as the mouse would. It runs two updates
of the game, paused or not (the pointer held there, then released), so buttons report `clicked`, toggles flip and
sliders move, and it returns the UI `events` of those updates with the click's `pixels`. The game's own input
actions do not see the click.

```json
{"call": "gameui.click", "params": {"entity": "/Menu/Start"}}
```

**`gameui.focus`** gives an element of the running game the focus of navigation (no event) and returns the
focused element; `"entity": null` clears the focus. An element that cannot take the focus keeps the old one
and the command warns.

```json
{"call": "gameui.focus", "params": {"entity": "/Menu/Start"}}
```

`gameui.click` and `gameui.focus` work only in Play mode: without a running game they fail with a wrong-mode
error (start one with `play.start`). When the element cannot be clicked, `gameui.click` fails and says why: the
element is hidden (opacity 0, or a toggle that is off), has no `UiInteractable`, is disabled, lies below a modal
canvas or outside a clip, is covered at its centre by another element (the message names it), or is not laid
out. A typical session creates the menu, checks it with `gameui.layout`, saves the scene, starts Play mode,
clicks the button and reads the `clicked` event; `tests/acceptance/game-ui/menu.jsonl` is such a transcript.

## Building a HUD from a module

A module creates the same Entities with the Components. Build it with `USES ui` to get the headers:

```cmake
illusion_add_module(hud SOURCES game/Hud.cpp USES ui)
```

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/ui/UiComponents.hpp>

#include <string>
#include <utility>

namespace
{

illusion::Entity g_score;

void start(illusion::ModuleContext& context)
{
    illusion::World& world = context.world();
    const illusion::Entity canvas = world.create(illusion::UiCanvas{});

    illusion::UiElement placement;
    placement.placement.anchorMin = placement.placement.anchorMax = {1.0f, 0.0f};
    placement.placement.offsetMin = {-320.0f, 24.0f};
    placement.placement.offsetMax = {-24.0f, 88.0f};
    illusion::UiText text;
    text.text = "Score 0";
    text.font = illusion::AssetRef{"assets:/fonts/Roboto-Regular.ttf"};
    text.align = illusion::UiTextAlign::End;
    g_score = world.create(placement, std::move(text));
    illusion::setParent(world, g_score, canvas);
}

void showScore(illusion::World& world, int score)
{
    world.get<illusion::UiText>(g_score).text = "Score " + std::to_string(score);
    world.markChanged<illusion::UiText>();
}

} // namespace
```

`UiWorld` recomputes the layout only when a UI Component, the hierarchy, the `Inactive` tags, the language
or the target changed. Code that writes a UI Component through a reference calls `world.markChanged<T>()`
afterwards, as it does for `Hierarchy`; creating an Entity or adding a Component announces itself. To change
the UI that the scene already has, a module uses `UiService` instead (see [Scripting the UI](#scripting-the-ui)).

## Buttons, toggles and sliders

`UiInteractable` with `UiButton`, `UiToggle` or `UiSlider` (`illusion/ui/UiWidgets.hpp`) makes an element
react to the mouse, the keyboard and gamepads through the actions `ui.navigate`, `ui.submit` and `ui.cancel`
(see [Input and focus](#input-and-focus)). A widget is an element with a `UiImage` (its look), a
`UiInteractable` and one of the three widget Components; every one of them needs `UiElement`, and the widget
Components need `UiInteractable`.

### UiInteractable

It makes an element hoverable, pressable and focusable. The tint of its current state is multiplied with the
tint of the element's `UiImage`:

| Field | Meaning |
| --- | --- |
| `enabled` | On by default. A disabled element ignores input, cannot be focused and shows `disabledTint` |
| `focusable` | On by default. Reachable by navigation with the keyboard and gamepads; the pointer can still press it |
| `hoveredTint` | Under the pointer (default 1.15, 1.15, 1.15, 1) |
| `pressedTint` | Held by the pointer or by submit (default 0.8, 0.8, 0.8, 1) |
| `focusedTint` | Focused by navigation (default 1.15, 1.15, 1.15, 1) |
| `disabledTint` | Not enabled (default 0.5, 0.5, 0.5, 0.5) |
| `up`, `down`, `left`, `right` | Explicit neighbours for navigation: the Entity that gets the focus in that direction |

The states have a priority: disabled, then pressed, then focused, then hovered; an element in none of them
is not tinted. A neighbour that is empty, or that cannot take the focus, falls back to the nearest focusable
element in that direction (see [Input and focus](#input-and-focus)). The runtime Component `UiInteraction`
(`hovered`, `pressed`, `focused`) is added to every interactable element and written by the UI each frame.

### UiButton

A tag. A button reports a `Clicked` event when it is activated: the pointer is pressed and released over it,
or `ui.submit` is pressed and released while it has the focus.

### UiToggle

A value, `on`, that activation switches. The toggle's **first child** is shown only while the toggle is on:
make it the check mark. While it is hidden it is also not hit by the pointer. Changing the value reports a
`ValueChanged` event with 1 (on) or 0 (off).

### UiSlider

A value in a range, changed by dragging or by navigation along its axis:

| Field | Meaning |
| --- | --- |
| `value` | The value, kept between `min` and `max` |
| `min` | The value at the left (or top) |
| `max` | The value at the right (or bottom) |
| `step` | Values snap to multiples of it from `min`; 0 (default) is continuous |
| `axis` | `Horizontal` (default) or `Vertical` |
| `navigationStep` | The change of one navigation press as a fraction of the range, used when `step` is 0 (default 0.1) |

The slider places two children. Its **first child** is the fill: it is stretched from the start of the
slider to the value. Its **second child** is the handle: it is centred on the value along the axis, and keeps
the size and the cross-axis position of its own placement. Children after the second are ordinary elements.
Dragging sets the value from the pointer's position along the slider, also when the pointer leaves it while
the button is held.

## Input and focus

The UI reads the pointer and three input actions. `GameSession` fills them every update in Play mode
(`UiInput` in `UiWidgets.hpp`; tests fill it by hand and call `UiWorld::update(frame, input)`).

**The pointer.** The pointer hits the topmost enabled interactable element under it:

- Later canvases (higher `order`), later nodes in the hierarchy and later siblings are on top.
- The hit test follows each element's rotation and scale, and a point outside the rectangle of an ancestor
  with `clipChildren` does not hit anything below it.
- Elements with `opacity` 0, elements under a hidden toggle child and disabled elements are not hit; a
  disabled button lets the pointer reach the element behind it. Elements without `UiInteractable` (a plain
  image) never block the pointer.
- The pointer is absent when the window has no focus or the cursor is outside the target or captured; nothing
  is then hovered.

Pressing the primary button on an element presses it, and gives it the focus if it is focusable
(a `FocusChanged` event with the value 0). Releasing over the same element activates it. A pressed slider
follows the pointer until the button is released.

**The actions.** `GameSession::create` adds these actions with default bindings when the project's input
asset does not have them:

| Action | Type | Default bindings |
| --- | --- | --- |
| `ui.navigate` | `Vector2` | `Keys.Up.Down.Left.Right`, `Keys.W.S.A.D`, `Stick.Left`, `Stick.DPad` |
| `ui.submit` | `Button` | `Key.Enter`, `Key.Space`, `Gamepad.South` |
| `ui.cancel` | `Button` | `Key.Escape`, `Gamepad.East` |

They are ordinary actions: rebind them like any other (see [Input actions](actions.md#rebinding)), or define
them in your project's input asset with other bindings; an action the asset already has is not replaced.
The showcase project's `assets/input/showcase.input` is an example.

**Navigation.** A deflection of `ui.navigate` beyond 0.5 counts as a direction, the larger axis winning.
The focus moves once when the direction starts, then repeats after 0.4 seconds every 0.12 seconds while it
is held. With nothing focused, the first move (or the first `ui.submit`) focuses the first focusable element
in hierarchy order on the topmost canvas that can be focused. Otherwise the focus goes to the explicit
neighbour of the focused element in that direction; with none (or one that cannot take the focus) it goes to
the focusable element whose centre is nearest in that direction. A focused slider takes the directions along
its axis itself: they change its value by `step`, or by `navigationStep` of the range when `step` is 0
(right and down increase it), and the other directions move the focus away.

**Submit and cancel.** `ui.submit` presses the focused element while it is held and activates it when it is
released with the focus still on that element. `ui.cancel` reports a `Cancelled` event on the frame it is
pressed; its Entity is the canvas of the focused element, or the modal canvas when nothing is focused. The
UI does nothing else with it: closing the menu is the game's decision.

`UiWorld::focused()` gives the focused element and `setFocus()` sets it (for example, to focus the first
button of a menu you just opened); a focus on an element that is removed, disabled, hidden or no longer
focusable is dropped.

## Modal canvases

`UiCanvas::modal` is for a pause menu or a dialog. While a modal canvas shows at least one element (it is
active and an element is laid out on it):

- Only that canvas and the canvases above it (a higher `order`) can be hit by the pointer or take the focus;
  the game's own HUD below it is inert.
- The session masks the input actions of the game: it calls `InputActions::setExclusivePrefix("ui.")`, so
  that only the actions whose names start with `ui.` get input and all others read as released. Modules read
  no movement, no jump and no fire while the menu is open.

`UiWorld::modalActive()` tells whether the last update laid out such an element. The mask follows the last
UI update, so it starts one update after the canvas appears and ends one update after it is hidden or
disabled. A modal canvas with no element does nothing.

## Reading events

`events()` of the `UiWorld` lists what happened in the last update, in order:

| `UiEventType` | When | `value` |
| --- | --- | --- |
| `Clicked` | A `UiButton` was activated | 0 |
| `ValueChanged` | A `UiToggle` or `UiSlider` changed | The new value (0 or 1 for a toggle) |
| `FocusChanged` | An element got the focus | 1 when navigation moved it, 0 for the pointer |
| `Cancelled` | `ui.cancel` was pressed | 0 (`entity` is a canvas, or null) |
| `TextChanged` | The player edited a `UiTextField` | 0 |
| `Submitted` | `ui.submit` on a focused `UiTextField` | 0 |

Poll them from host code after the session's update. The list is replaced by the next update, so read it
once per update:

```cpp
(*session)->update(frameSeconds);
for (const illusion::UiEvent& event : (*session)->ui().events())
{
    if (event.type == illusion::UiEventType::Clicked && event.entity == startButton)
    {
        startGame();
    }
    else if (event.type == illusion::UiEventType::ValueChanged && event.entity == volumeSlider)
    {
        setVolume(event.value);
    }
}
```

Events beyond `UiWorldDesc::maxEvents` in one update are dropped and counted in
`UiWorldStatistics::droppedEvents`.

Game modules read the same list through `UiService::pollEvents()` (see [Scripting the UI](#scripting-the-ui)).
They can also read the state from their World: `UiToggle::on` and `UiSlider::value` are written in place when
they change, and the runtime Component `UiInteraction` says whether an element is hovered, pressed or focused.

```cpp
const illusion::UiSlider& slider = world.get<illusion::UiSlider>(volumeSlider);
const bool musicOn = world.get<illusion::UiToggle>(musicToggle).on;
const bool held = world.get<illusion::UiInteraction>(startButton).pressed;
```

## Scripting the UI

A game module changes and reads the UI through `UiService` (`illusion/ui/UiService.hpp`, `USES ui`; ADR 0057).
The host implements it with `UiWorldService` over the session's `UiWorld`, and the module finds it like any
other service, checking the pointer:

```cpp
illusion::UiService* ui = context.service<illusion::UiService>();
```

Every function takes the Entity of an element and returns false (or nothing) for an Entity that is dead or
lacks the Component the function needs. Nothing calls into the module: it asks, and it polls events.

| Function | Purpose |
| --- | --- |
| `find` | The first Entity with a given `Name` below a root (or on any canvas, in draw order) |
| `setText`, `setTextKey` | The text of a `UiText`, literal or by string table key |
| `setVisible`, `isVisible`, `setInteractable` | Show or hide an element and its descendants, ask whether it shows, enable or disable it |
| `setToggle`, `toggle`, `setSliderValue`, `sliderValue` | The values of a `UiToggle` and a `UiSlider` |
| `setFocus`, `focused` | The focused element |
| `rect` | The rectangle of the last layout, in canvas units |
| `invalidateLayout` | Tells the UI that Components were written directly |
| `pollEvents` | The events of the last UI update |
| `setLanguage`, `language` | The game's language |

### Finding elements

`find(root, name)` returns the first Entity whose `Name` Component is `name` among `root` and its
descendants, in hierarchy order, hidden ones included. With a null `root` it searches every canvas, the one
with the lowest `order` first. It returns a null Entity when nothing matches. Find the elements once, in
`start()`, and keep the Entities in a Component, or find them again in each update: a name costs a walk of
the hierarchy, an Entity costs nothing.

### Setting text

`setText(entity, "Score 120")` shows the literal text and clears the element's `key`. `setTextKey()` shows a
message of the string table in the game's current language, formatted with named arguments, and keeps
following the language when it changes. An argument is `UiTextArgument::number(name, value)`, to which the
plural rules and number formats of the language apply, or `UiTextArgument::string(name, text)`. At most 8
arguments are allowed, each text at most 255 bytes; `setTextKey()` returns false beyond that.

```json
"showcase.hud.frames": "{frames, plural, one {# frame} other {# frames}}"
```

```cpp
const illusion::Entity counter = ui->find({}, "Counter");
const illusion::UiTextArgument frames[] = {illusion::UiTextArgument::number("frames", 120.0)};
ui->setTextKey(counter, "showcase.hud.frames", frames); // "120 frames"; 1 shows "1 frame"
```

The Turkish file `tr.json` has the same key with its own message, `{frames, plural, one {# kare} other {# kare}}`
(see [Localization](localization.md) for the message syntax). The arguments are kept in the runtime Component
`UiTextArguments` of the element.

### Visibility, interaction and values

`setVisible(entity, false)` hides an element and everything below it (it sets the `Inactive` tag, as
`setEnabled()` of `Hierarchy.hpp` does); `isVisible()` is false when the element or an ancestor is hidden.
`setInteractable()` writes `UiInteractable::enabled`. `setToggle()` and `setSliderValue()` set the value of a
widget (a slider value is clamped and snapped like input) and report no event: the module that sets a value
does not need to hear about it. `toggle()` and `sliderValue()` read the values back.

### Focus and layout

`setFocus(entity)` moves the focus, for example to the first button of a menu you just opened; it returns
false when the element cannot be focused now, and `setFocus({})` clears the focus. `rect(entity)` is the
rectangle the last layout gave the element, in canvas units.

`UiService` makes its changes through the host, so they reach the layout without `world.markChanged()`. A
module that writes UI Components directly through its `World` calls `world.markChanged<T>()` as described
above, or `invalidateLayout()` once after a batch of writes.

### Polling events

`pollEvents(events)` copies the events of the UI's last update into a span you provide and returns how many
there were. The UI runs after the modules in the session's step order, so these are the events of the previous
frame. The returned total can be larger than the span: read at most `events.size()` of them, and size the
span for the most you expect in one frame. The events are those of [Reading events](#reading-events), and
`ui.cancel` arrives as `Cancelled`.

### Language

`setLanguage("tr")` switches the game to a locale for which the project has a file in `assets:/locales`; it
returns false when there is none. Every `UiText` with a key follows. `language()` is the current BCP 47 tag,
valid until the next `setLanguage()`.

### Example: a pause menu

The scene has a modal canvas named `PauseMenu`, hidden at the start, with two buttons, `Resume` and `Quit`.
The module shows the menu when `ui.cancel` is pressed, hides it again with Resume, and ends the game with
Quit by asking its host through `SceneService::requestExit()`: the player closes after that update, and the
editor's Play mode stops.

```cmake
illusion_add_module(pause SOURCES game/Pause.cpp USES runtime ui)
```

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/ui/UiService.hpp>

#include <algorithm>
#include <array>
#include <span>

namespace
{

bool loadModule(illusion::ModuleContext&)
{
    return true;
}

void updateModule(illusion::ModuleContext& context, illusion::f64)
{
    illusion::UiService* ui = context.service<illusion::UiService>();
    illusion::SceneService* scenes = context.service<illusion::SceneService>();
    if (ui == nullptr || scenes == nullptr)
    {
        return; // this World was attached without them
    }
    const illusion::Entity menu = ui->find({}, "PauseMenu");
    const illusion::Entity resume = ui->find(menu, "Resume");
    const illusion::Entity quit = ui->find(menu, "Quit");

    std::array<illusion::UiEvent, 16> events{};
    const illusion::u32 count =
        std::min<illusion::u32>(ui->pollEvents(events), static_cast<illusion::u32>(events.size()));
    for (const illusion::UiEvent& event : std::span(events).first(count))
    {
        if (event.type == illusion::UiEventType::Cancelled && !ui->isVisible(menu))
        {
            ui->setVisible(menu, true);
            ui->setFocus(resume);
        }
        else if (event.type == illusion::UiEventType::Clicked && event.entity == resume)
        {
            ui->setVisible(menu, false);
        }
        else if (event.type == illusion::UiEventType::Clicked && event.entity == quit)
        {
            scenes->requestExit();
        }
    }
}

constexpr illusion::ModuleApi kPause =
    illusion::makeModuleApi("pause", "1.0.0", {.load = &loadModule, .update = &updateModule});

} // namespace

ILL_MODULE(pause, kPause)
```

While the menu shows, its canvas is modal: the game's other actions read as released, so the module needs no
check of its own for movement keys ([Modal canvases](#modal-canvases)).

## Text fields, scrolling and glyphs

`UiTextField`, `UiScrollView` and `UiGlyph` (`illusion/ui/UiWidgets.hpp`) are saved Components like the other
widgets: each needs `UiElement`.

### UiTextField

A line of text the player edits. The Entity needs `UiElement`, `UiInteractable` and a `UiText`, which shows
`text`, or `placeholder` (dimmed) while `text` is empty; an `UiImage` gives it a background.

| Field | Meaning |
| --- | --- |
| `text` | The value, UTF-8 |
| `placeholder` | Shown while `text` is empty; a literal, not a string table key |
| `maxLength` | Most code points the player can enter, 1 to 1024 (default 64) |
| `password` | Shows a bullet for every character (default off) |
| `caretColor` | Linear RGBA of the caret (default white) |

While the field has the focus, it takes the characters typed that frame. Left, Right, Home and End move the
caret, Backspace and Delete remove a character, and `ui.submit` reports a `Submitted` event. Navigation left
and right (the gamepad stick or D-pad) moves the caret instead of the focus; up and down move the focus. The
caret goes to the end of the text when the field gets the focus or its text is set from outside. A text wider
than the element scrolls so that the caret stays visible, clipped to the element.

An edit writes `UiTextField::text` and reports a `TextChanged` event with the field's Entity. A module reads
the value from the Component field and writes it the same way, then calls `UiService::invalidateLayout()`:

```cpp
// In update(): read the value on Submitted, then clear the field.
std::array<illusion::UiEvent, 16> events{};
const illusion::u32 count = std::min<illusion::u32>(ui->pollEvents(events), static_cast<illusion::u32>(events.size()));
for (const illusion::UiEvent& event : std::span(events).first(count))
{
    if (event.type == illusion::UiEventType::Submitted)
    {
        illusion::UiTextField& field = context.world().get<illusion::UiTextField>(event.entity);
        const std::string submitted = field.text;
        field.text.clear();
        ui->invalidateLayout();
    }
}
```

**Keys while a field has the focus.** The keyboard edits the field, so the game's other actions do not see
it:

- Typed characters, Left, Right, Backspace and Delete (repeating after 0.4 s, then every 0.05 s while held),
  Home and End go to the field.
- The keyboard bindings of `ui.navigate` and `ui.submit` are ignored, so letters bound to them (W, A, S, D,
  Space) are typed. Up and Down still move the focus and Enter submits.
- Only the gamepad bindings of `ui.navigate` and `ui.submit` count: gamepad left and right move the caret.
- `ui.cancel` keeps all its bindings.
- The game's other actions read as released, as under a modal canvas, until the field loses the focus.

### UiScrollView

It clips its children and scrolls its content: the first child, placed at the top left with its own size
(`UiElement::size`, or the content of its `UiStack`) and moved by `offset`.

| Field | Meaning |
| --- | --- |
| `offset` | Distance scrolled in canvas units, kept within the content |
| `horizontal` | May scroll horizontally (default off) |
| `vertical` | May scroll vertically (default on) |
| `wheelStep` | Canvas units per wheel notch (default 60) |

The wheel scrolls the topmost view under the pointer, vertically when it may. Pressing inside a view and
moving the pointer more than 8 pixels drags the content; that takes the press from an interactable element
under the pointer. Moving the focus to an element inside the view scrolls it into view.

A view with a long list keeps the draw cost bounded: descendants that lie wholly outside the view are neither
hit nor drawn. Their Entities still exist and are laid out whenever the layout runs, so there is no recycling
of rows; a list of many thousands of rows is not a good fit.

### UiGlyph

It shows how an input action is bound on the device used last. The Entity needs a `UiText`; `action` names the
input action (`ui.submit`, `jump`). The `UiText` shows the label of the first binding of that action for the
device used last (`Enter`, `A`, `Cross`) without being written, and the label follows the player when they
rebind the action or switch between the keyboard and a gamepad. The labels are English unless the project's
locale files translate the `input.*` keys. For an unknown action, or one without a binding for that device,
the `UiText` keeps its own text.

## What is not there yet

The UI is drawn, reacts to input, can be scripted by game modules and is built from the Entity menu or with
`gameui.*` commands. Later packages add the canvas editor in the Scene view (placement handles, anchor presets
and picking elements with the mouse), and transitions and animation. Until then placement is edited in the
Inspector. Text selection, glyph images (a button picture instead of a label) and recycled lists are not there
either. The limits of the first version
([ADR 0057](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0057-game-ui.md)):

- Text is drawn left to right without shaping: Arabic, Hebrew, Indic scripts and ligatures are
  not supported, right-to-left text is not either, and CJK needs character ranges that are too large for the
  font atlas.
- Outlines, shadows and gradients on text are not there yet.
- Canvases cover the screen; a UI placed in the world (a label above a character) is not covered.

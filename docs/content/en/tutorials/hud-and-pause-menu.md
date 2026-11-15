# A HUD and a pause menu

This tutorial gives a game a score in the corner, a hint that shows the key (or button) that opens the menu,
and a pause menu with Resume, a volume slider and Quit. You build the UI in the editor, once by hand and once
as the commands an agent would send, and write a small C++ game module that counts points and drives the menu.
The tutorial follows `samples/techdemo`, whose HUD and pause menu are the full version of this one.

You need a Release build of illusion and the C++ compiler the editor was built with. The
[Game UI manual](../manual/ui.md) explains every Component used here.

## 1. What you build

- A canvas **HUD** with the text **Score** in the top right corner, drawn with an outline, and a hint
  (**Esc** Menu) in the top left. The key label is a `UiGlyph`: it shows the first binding of `ui.cancel` for
  the device the player used last, and follows a rebinding.
- A canvas **Pause Menu**, hidden at the start and **modal**: while it shows, the game's other input actions read
  as released. Its panel slides in with a `Shown` transition. It holds a **Resume** button, a **Volume** slider
  with its label and a **Quit** button, and works with the mouse, the keyboard and a gamepad.
- English and Turkish texts, found by string table keys, and a module that counts points, opens the menu on
  Escape (`ui.cancel`), sets the master volume and ends the game.

## 2. Create the project, a font and the texts

Start the editor and create a project named **Hud** in the folder `hud` from the **3D Starter** template (it has
a camera, so the Game view shows a scene behind the UI):

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

Every template ships `assets/fonts/Roboto-Regular.ttf`, so `UiText` works at once; any other `.ttf` or `.otf`
file in `assets/` is a font Asset as well. Its default character ranges cover Basic Latin, Latin-1 and Latin
Extended-A, which is enough for English and Turkish.

Texts that players read belong in string tables. Create `assets/locales/en.json` in your code editor; the
English file is the base:

```json
{
  "format": "illusion.locale",
  "version": 1,
  "locale": "en",
  "displayName": "English",
  "plural": "en",
  "number": {"decimal": ".", "group": ","},
  "strings": {
    "hud.score": "Score {points}",
    "hud.menuHint": "Menu",
    "hud.menu.title": "Paused",
    "hud.menu.resume": "Resume",
    "hud.menu.volume": "Volume {percent}%",
    "hud.menu.quit": "Quit"
  }
}
```

and `assets/locales/tr.json` with the same keys:

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
    "hud.score": "Puan {points}",
    "hud.menuHint": "Menü",
    "hud.menu.title": "Duraklatıldı",
    "hud.menu.resume": "Devam et",
    "hud.menu.volume": "Ses %{percent}",
    "hud.menu.quit": "Çık"
  }
}
```

`{points}` and `{percent}` are arguments that the module fills in; the number formats and plural rules of the
active language apply to them (see [Localization](../manual/localization.md)). The game reads every file of
`assets:/locales` when it starts.

## 3. The HUD

Select nothing and choose **Entity > Game UI > Canvas**. Rename it **HUD** with **F2**. The menu runs the
command `gameui.create`, one undo step per item.

**The score.** Select **HUD** and choose **Entity > Game UI > Text**: the text goes inside the selected canvas,
24 canvas units from its top left, and uses the project's first font. Rename it **Score**, then in the
[Inspector](../manual/editor/scene-editing.md):

1. Open the **anchors** button of the `UiElement` placement and choose the top right preset; the element keeps
   its rectangle and moves its anchors to the corner.
2. Set the placement so that the text is 296 x 64 units, 24 units from the top and the right edge: offset min
   (-320, 24) and offset max (-24, 88).
3. In `UiText`, clear **Text**, set **Key** to `hud.score`, **Size** to 48, **Align** to `End`, **Wrap** off,
   and **Outline Width** to 2.

You can also drag the element and its handles in the **Game view**, which draws the UI at the size the game will
have; the anchors are drawn as markers. Hold Ctrl to snap drags to 8 units. See
[Editing in the Game view](../manual/ui.md#editing-in-the-game-view).

**The hint.** Create a **Stack** inside **HUD** and rename it **Hint**: anchor it at the top left (offset min
(24, 24), offset max (600, 72)) and set the stack's **Axis** to `Horizontal`, **Spacing** to 12 and
**Cross Align** to `Center`. Create two **Text** elements inside it. Rename the first **Menu Glyph**: set its
Size to (0, 44) (the width follows the text), **Text** to `Esc`, and add the Component **UI glyph** with
**Action** `ui.cancel`. Rename the second **Menu Hint** and give it the **Key** `hud.menuHint` and the same
size. Elements inside a stack are placed by the stack, so they have no anchors.

The glyph shows `Esc`, or the label of the gamepad's East button once a gamepad was used last; the text `Esc`
you typed stays for an action without a binding. Glyphs are labels, not pictures.

!!! note "With an agent"
    An agent creates the same HUD with `gameui.create`, `gameui.set_anchors`, `gameui.place`,
    `component.set` and `entity.create` (the [Editor commands](../reference/editor-commands.md) of the
    categories Game UI, Components and Entities). Send them as separate calls or in an `editor.batch`:

    ```json
    {"call": "gameui.create", "params": {"kind": "canvas", "name": "HUD"}}
    {"call": "gameui.create", "params": {"kind": "text", "parent": "/HUD", "name": "Score"}}
    {"call": "gameui.set_anchors", "params": {"entity": "/HUD/Score", "preset": "top_right"}}
    {"call": "gameui.place", "params": {"entity": "/HUD/Score", "min": [1600, 24], "max": [1896, 88]}}
    {"call": "component.set", "params": {"entity": "/HUD/Score", "type": "illusion.UiText", "value": {"key": "hud.score", "size": 48, "align": "End", "wrap": false, "outlineWidth": 2.0, "outlineColor": [0.01, 0.012, 0.015, 1.0]}}}
    {"call": "entity.create", "params": {"name": "Hint", "parent": "/HUD", "components": {"illusion.UiElement": {"placement": {"anchorMin": [0, 0], "anchorMax": [0, 0], "offsetMin": [24, 24], "offsetMax": [600, 72]}}, "illusion.UiStack": {"axis": "Horizontal", "spacing": 12, "crossAlign": "Center"}}}}
    {"call": "entity.create", "params": {"name": "Menu Glyph", "parent": "/HUD/Hint", "components": {"illusion.UiElement": {"size": [0, 44]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "text": "Esc", "size": 32, "wrap": false, "outlineWidth": 2.0}, "illusion.UiGlyph": {"action": "ui.cancel"}}}}
    {"call": "entity.create", "params": {"name": "Menu Hint", "parent": "/HUD/Hint", "components": {"illusion.UiElement": {"size": [0, 44]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "key": "hud.menuHint", "size": 32, "wrap": false, "outlineWidth": 2.0}}}}
    ```

    `gameui.layout` returns the rectangle of every element in canvas units and in pixels, so the agent can
    check the result without a picture (`{"call": "gameui.layout", "params": {"entity": "/HUD"}}`);
    `viewport.screenshot` with `"viewport": "game"` takes one.

## 4. The pause menu

Create a second canvas and rename it **Pause Menu**. In the Inspector set the `UiCanvas` **Order** to 10 (it is
drawn over the HUD) and turn **Modal** on. Then build it as follows; every element goes inside the one above it
where the list says so.

1. **Dim**: a **Panel** inside the canvas, anchored with the stretch preset so that it fills the canvas, with
   all four offsets 0 and the colour of its `UiImage` a dark translucent one.
2. **Panel**: a **Stack** inside the canvas, anchored in the middle, with offset min (-320, -300) and offset max
   (320, 300). Give it a `UiImage` (a dark colour) and set the stack's **Spacing** to 24, its **Padding** to
   (48, 40, 48, 40) and **Cross Align** to `Center`. Add the Component **UI transition** with the trigger
   `Shown`, **Duration** 0.25, **Offset** (0, 48), **Scale** (0.96, 0.96) and **Opacity** 0: when the menu shows,
   the panel starts moved, smaller and transparent, and eases to rest.
3. **Heading**: a **Text** inside **Panel**, size (0, 80), `UiText` key `hud.menu.title`, size 60, centred.
4. **Resume**: a **Button** inside **Panel**, size (440, 72), with the label's key `hud.menu.resume`, centred.
   A button's label is its child **Label**.
5. **Volume Label**: a **Text**, size (440, 40), key `hud.menu.volume`, centred.
6. **Volume**: a **Slider** inside **Panel**, size (440, 24), **Value** 1, **Min** 0, **Max** 1 and **Step** 0.05.
   Its first child **Fill** is stretched to the value and its second child **Handle** sits on it.
7. **Quit**: a **Button**, size (440, 72), the label's key `hud.menu.quit`.

Finally select **Pause Menu** and turn its **enabled** check box off (`entity.set_enabled`): the menu is hidden
at the start. Save the scene with **Ctrl+S**. The elements inside the panel have no placement of their own: the
stack places them one below the other, with the `Cross Align` of `Center`.

To look at the menu while you work, turn the canvas on again for a moment: the Game view draws it with every
element at rest, because only a running game advances a transition.

!!! note "With an agent"
    The menu is the same set of calls, with the font and the keys; `gameui.create` makes the button and the
    slider with their children, and `component.set` changes their Components:

    ```json
    {"call": "gameui.create", "params": {"kind": "canvas", "name": "Pause Menu"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu", "type": "illusion.UiCanvas", "value": {"order": 10, "modal": true}}}
    {"call": "entity.create", "params": {"name": "Dim", "parent": "/Pause Menu", "components": {"illusion.UiElement": {"placement": {"anchorMin": [0, 0], "anchorMax": [1, 1], "offsetMin": [0, 0], "offsetMax": [0, 0]}}, "illusion.UiImage": {"tint": [0.0, 0.01, 0.02, 0.55]}}}}
    {"call": "entity.create", "params": {"name": "Panel", "parent": "/Pause Menu", "components": {"illusion.UiElement": {"placement": {"anchorMin": [0.5, 0.5], "anchorMax": [0.5, 0.5], "offsetMin": [-320, -300], "offsetMax": [320, 300]}}, "illusion.UiImage": {"tint": [0.03, 0.045, 0.04, 0.92]}, "illusion.UiStack": {"spacing": 24, "padding": {"left": 48, "top": 40, "right": 48, "bottom": 40}, "crossAlign": "Center"}, "illusion.UiTransition": {"trigger": "Shown", "duration": 0.25, "ease": "EaseOut", "offset": [0, 48], "scale": [0.96, 0.96], "opacity": 0.0}}}}
    {"call": "entity.create", "params": {"name": "Heading", "parent": "/Pause Menu/Panel", "components": {"illusion.UiElement": {"size": [0, 80]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "key": "hud.menu.title", "size": 60, "align": "Center", "wrap": false}}}}
    {"call": "gameui.create", "params": {"kind": "button", "parent": "/Pause Menu/Panel", "name": "Resume"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Resume", "type": "illusion.UiElement", "value": {"size": [440, 72]}}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Resume/Label", "type": "illusion.UiText", "value": {"key": "hud.menu.resume", "size": 36, "wrap": false}}}
    {"call": "entity.create", "params": {"name": "Volume Label", "parent": "/Pause Menu/Panel", "components": {"illusion.UiElement": {"size": [440, 40]}, "illusion.UiText": {"font": "assets:/fonts/Roboto-Regular.ttf", "key": "hud.menu.volume", "size": 30, "align": "Center", "wrap": false}}}}
    {"call": "gameui.create", "params": {"kind": "slider", "parent": "/Pause Menu/Panel", "name": "Volume"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Volume", "type": "illusion.UiElement", "value": {"size": [440, 24]}}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Volume", "type": "illusion.UiSlider", "value": {"value": 1.0, "min": 0.0, "max": 1.0, "step": 0.05}}}
    {"call": "gameui.create", "params": {"kind": "button", "parent": "/Pause Menu/Panel", "name": "Quit"}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Quit", "type": "illusion.UiElement", "value": {"size": [440, 72]}}}
    {"call": "component.set", "params": {"entity": "/Pause Menu/Panel/Quit/Label", "type": "illusion.UiText", "value": {"key": "hud.menu.quit", "size": 36, "wrap": false}}}
    {"call": "entity.set_enabled", "params": {"entity": "/Pause Menu", "enabled": false}}
    {"call": "scene.save", "params": {}}
    ```

    `tests/acceptance/game-ui/menu.jsonl` is a complete transcript of an agent that builds a menu, plays it
    and clicks a button with `gameui.click`.

## 5. The game module

Run **New module** from the command palette with the name `hud`. The module reads the UI through `UiService`,
sets the master volume through `AudioService` and ends the game through `SceneService`, so it uses `ui`,
`audio` and `runtime`:

```cmake
illusion_add_module(hud SHARED SOURCES hud.cpp USES audio runtime ui)
```

Open `modules/hud/hud.cpp`. The state of the module is one runtime Component with the elements it drives. It is
not saved in the scene, and it survives a hot reload:

```cpp
#include <illusion/audio/AudioService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/runtime/SceneService.hpp>
#include <illusion/ui/UiService.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace hud
{

using namespace illusion;

struct State
{
    Entity score{};
    Entity menu{};
    Entity resume{};
    Entity volume{};
    Entity volumeLabel{};
    Entity quit{};
    f64 seconds = 0.0;
    u32 points = 0;
    bool scoreShown = false;
};

} // namespace hud
```

`start` finds the elements once by their names and stores them. A name costs a walk of the hierarchy and an
Entity costs nothing, so the module does not call `find` in `update`. The elements of a hidden canvas are found
too:

```cpp
namespace
{

using namespace illusion;
using hud::State;

constexpr f64 kPointSeconds = 1.0;
constexpr u32 kPointsPerStep = 10;

bool loadHud(ModuleContext&)
{
    reflectRuntime<State>("hud.State", {.displayName = "HUD state"});
    return true;
}

void startHud(ModuleContext& context)
{
    UiService* ui = context.service<UiService>();
    if (ui == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    State state;
    state.score = ui->find({}, "Score");
    state.menu = ui->find({}, "Pause Menu");
    state.resume = ui->find(state.menu, "Resume");
    state.volume = ui->find(state.menu, "Volume");
    state.volumeLabel = ui->find(state.menu, "Volume Label");
    state.quit = ui->find(state.menu, "Quit");
    context.world().create(Name{"HUD State"}, state);
}
```

The texts are string table keys with arguments, so the module never builds a language-dependent string. The
menu shows with the current master volume and moves the focus to Resume, so that a gamepad or the keyboard can
use it at once:

```cpp
void showScore(UiService& ui, const State& state)
{
    const std::array points{UiTextArgument::number("points", static_cast<f64>(state.points))};
    static_cast<void>(ui.setTextKey(state.score, "hud.score", points));
}

void showVolume(UiService& ui, const State& state, f32 volume)
{
    const std::array percent{UiTextArgument::number("percent", std::round(static_cast<f64>(volume) * 100.0))};
    static_cast<void>(ui.setTextKey(state.volumeLabel, "hud.menu.volume", percent));
}

void showMenu(UiService& ui, const AudioService* audio, const State& state, bool show)
{
    static_cast<void>(ui.setVisible(state.menu, show));
    if (show)
    {
        const f32 volume = audio != nullptr ? audio->masterVolume() : 1.0f;
        static_cast<void>(ui.setSliderValue(state.volume, volume));
        showVolume(ui, state, volume);
        static_cast<void>(ui.setFocus(state.resume));
    }
}
```

`update` polls the events of the last UI update: `Cancelled` is `ui.cancel` (Escape or the gamepad's East
button), `Clicked` a button, `ValueChanged` the slider. The score grows while the menu is closed, which stands
in for your game:

```cpp
void updateHud(ModuleContext& context, f64 deltaSeconds)
{
    UiService* ui = context.service<UiService>();
    if (ui == nullptr)
    {
        return;
    }
    World& world = context.world();
    Entity self{};
    world.query<const State>().each([&self](Entity entity, const State&) { self = entity; });
    if (!world.isAlive(self))
    {
        return;
    }
    // A copy: the UI changes the structure of the World, which would invalidate a reference.
    State state = world.get<State>(self);
    AudioService* audio = context.service<AudioService>();
    SceneService* scenes = context.service<SceneService>();

    std::array<UiEvent, 16> events{};
    const u32 count = std::min<u32>(ui->pollEvents(events), static_cast<u32>(events.size()));
    for (const UiEvent& event : std::span(events).first(count))
    {
        if (event.type == UiEventType::Cancelled)
        {
            showMenu(*ui, audio, state, !ui->isVisible(state.menu));
        }
        else if (event.type == UiEventType::Clicked && event.entity == state.resume)
        {
            showMenu(*ui, audio, state, false);
        }
        else if (event.type == UiEventType::Clicked && event.entity == state.quit && scenes != nullptr)
        {
            scenes->requestExit();
        }
        else if (event.type == UiEventType::ValueChanged && event.entity == state.volume)
        {
            if (audio != nullptr)
            {
                audio->setMasterVolume(event.value);
            }
            showVolume(*ui, state, event.value);
        }
    }

    const bool open = ui->isVisible(state.menu);
    // A menu shown in this update is laid out at its end; the focus can go to Resume only afterwards.
    if (open && ui->focused().isNull())
    {
        static_cast<void>(ui->setFocus(state.resume));
    }
    bool changed = !state.scoreShown;
    if (!open)
    {
        state.seconds += std::max(deltaSeconds, 0.0);
        while (state.seconds >= kPointSeconds)
        {
            state.seconds -= kPointSeconds;
            state.points += kPointsPerStep;
            changed = true;
        }
    }
    if (changed)
    {
        showScore(*ui, state);
        state.scoreShown = true;
    }
    world.get<State>(self) = state;
}

constexpr ModuleApi kHud =
    makeModuleApi("hud", "1.0.0", {.load = &loadHud, .start = &startHud, .update = &updateHud});

} // namespace

ILL_MODULE(hud, kHud)
```

While the menu shows, its canvas is modal: a module that reads `move` or `jump` through the `InputService`
sees them released, so the game stands still without a check of its own. The module changes the UI only through
`UiService`, so the layout follows without `world.markChanged()` (see
[Scripting the UI](../manual/ui.md#scripting-the-ui)).

Press **Ctrl+B** (**Modules → Build Modules**). When the build succeeds the module is loaded.

## 6. Try it

Press **Play**:

- The score counts up in the top right corner and the hint shows **Esc Menu**.
- Press **Escape**: the menu slides in, Resume has the focus, and the score stops. Move the focus with the arrow
  keys, W and S or the D-pad, and activate a button with Enter, Space or the South button. Click the buttons
  and drag the slider with the mouse.
- Touch a gamepad: the hint changes to the label of its binding of `ui.cancel`.
- Change the language: add a button that calls `ui->setLanguage("tr")` in its `Clicked` branch (the tech demo
  has one), and every text that has a key follows; the volume label keeps its `{percent}` argument.
- **Quit** ends the game: the player closes, and the editor leaves Play mode.

An agent tries the same with `play.start`, `gameui.focus` and `gameui.click` on `/Pause Menu/Resume`, and reads
the `events` the click returns. Change the module while the game runs and press **Ctrl+B**: it is hot-reloaded
with its state.

## 7. The limits

- Text is drawn left to right, one glyph per code point, with no shaping and no right-to-left text: Arabic,
  Hebrew and Indic scripts do not work, and CJK needs character ranges that are too large for the font atlas.
- The player cannot select text or use a clipboard in a text field.
- Glyphs are labels (`Esc`, `A`, `Cross`) and not pictures of keys.
- The UI covers the screen; a label above a character in the world is not possible, and the Scene view does
  not draw the UI.

## Export it

The game runs without the editor once you export it: **File > Export…**, or the command `export.build`, writes
the game's directory with the executable and its packed data, the fonts and locale files included. The
[export manual](../manual/editor/export.md) has the settings, and [Releasing a game](../guides/release.md) the
way to a store.

## Next steps

- Read the [Game UI manual](../manual/ui.md) for layout with stacks and grids, text fields, scroll views and
  transitions.
- The module of `samples/techdemo` adds a language button and a frame-time readout; `tests/acceptance/tetris`
  shows a score HUD and a game-over menu.
- [How the UI is built](../architecture/game-ui.md) explains the layers and the budgets.

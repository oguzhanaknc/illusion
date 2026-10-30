# Input Actions

Game code should ask "did the player jump?" rather than "was Space or the A button pressed?".
`InputActions` maps device inputs to named actions, lets players rebind them, saves the bindings and
tells the UI which button symbol to show.

Header: `#include <illusion/input/InputActions.hpp>`

## Defining actions

```cpp
illusion::InputActions actions;
const illusion::ActionId jump = actions.addAction("Jump", illusion::ActionType::Button);
const illusion::ActionId move = actions.addAction("Move", illusion::ActionType::Vector2);
const illusion::ActionId throttle = actions.addAction("Throttle", illusion::ActionType::Axis);

actions.addBinding(jump, illusion::InputBinding::key(illusion::Key::Space));
actions.addBinding(jump, illusion::InputBinding::gamepadButton(illusion::GamepadButton::South));
actions.addBinding(move, illusion::InputBinding::keyVector(illusion::Key::W, illusion::Key::S,
                                                           illusion::Key::A, illusion::Key::D));
actions.addBinding(move, illusion::InputBinding::gamepadStick(illusion::GamepadStick::Left));
actions.addBinding(throttle, illusion::InputBinding::gamepadAxis(illusion::GamepadAxis::RightTrigger));
actions.addBinding(throttle, illusion::InputBinding::key(illusion::Key::S, -1.0f));
```

| Type | Value | Typical use |
| --- | --- | --- |
| `Button` | on/off with pressed and released edges | Jump, Fire, Interact |
| `Axis` | one value in [-1, 1] | Throttle, Zoom |
| `Vector2` | 2D vector, length at most 1 | Move, Look |

A `scale` on a binding flips or weakens it: `key(Key::S, -1.0f)` pushes an axis negative, and
`gamepadAxis(GamepadAxis::LeftY, -1.0f)` makes a button action trigger when the stick is pulled down.

## Reading actions every frame

```cpp
std::vector<const illusion::GamepadState*> pads;
for (illusion::Gamepad* pad : gamepads->gamepads())
{
    pads.push_back(&pad->state());
}
actions.update(&window->input(), pads);

if (actions.wasPressed(jump))
{
    player.jump();
}
player.move(actions.vector(move));
```

When several bindings are active, the strongest value wins. A tap that starts and ends within one frame
still reports `wasPressed` and `wasReleased`. For local multiplayer, give each player their own
`InputActions` and pass only that player's gamepad.

## Rebinding

`BindingCapture` waits for the next input a player presses. Inputs held when the capture starts (such
as the button that opened the menu) are ignored until released.

```cpp
illusion::BindingCapture capture(illusion::BindingCapture::Devices::Gamepad, illusion::ActionType::Button);
// each frame while "Press a button" is shown:
if (auto binding = capture.poll(&window->input(), pads))
{
    for (illusion::ActionId other : actions.findConflicts(*binding, jump))
    {
        actions.removeBinding(other, *binding); // or ask the player
    }
    actions.addBinding(jump, *binding);
}
```

## Saving bindings

`serialize()` writes readable text and `deserialize()` reads it back. Actions are defined in code;
the file only stores their bindings:

```text
# illusion input bindings v1
Jump = Key.Space, Gamepad.South
Move = Keys.W.S.A.D, Stick.Left
Throttle = Axis.RightTrigger, Key.S*-1
```

Unknown actions and malformed bindings are skipped and reported, so an old or edited file never
prevents the game from starting. Store the text with the [file system](filesystem.md), for example in
`user:/input.cfg`.

## Input actions assets

A game's actions are data too: an input actions asset (a `.input` file) defines each action with its type
and default bindings, so the editor and designers change them without touching code. The binding strings
are those of `serialize()`.

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "Jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]},
    {"name": "Move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "Throttle", "type": "Axis", "bindings": ["Axis.RightTrigger", "Key.S*-1"]}
  ]
}
```

```cpp
#include <illusion/input/InputActionsAsset.hpp>

illusion::InputActions actions;
if (auto loaded = illusion::loadInputActions(files, "assets:/game.input", actions); !loaded)
{
    ILL_LOG_ERROR("Game", "{}", loaded.error()); // "assets:/game.input: '/actions/1/type' must be ..."
}
const std::string text = illusion::writeInputActions(actions); // the same actions give the same text
```

`readInputActions()` replaces every action at once, or on any error none: an invalid name, a name used
twice, an unknown type or binding, a wrong format or a newer version fails with the JSON pointer of the
bad value. Action names must pass `InputActions::isValidActionName()`: not empty, no leading or trailing
spaces, none of `=`, `,`, `#` or a line break. The project's `InputSettings::actions` names the asset that a
[game session](runtime.md) starts with; the player's saved rebindings (`serialize()`) then go on top.

Editors change the set with three more functions:

```cpp
actions.renameAction(*actions.findAction("Throttle"), "Accelerate"); // false if taken or invalid
actions.removeAction(*actions.findAction("Jump")); // later ids move down: look them up again
actions.clear();
```

## Glyphs

`glyphId()` returns an icon identifier and `glyphLabel()` a short label for a binding, following the
controller family of the device the player used last:

| Binding | Xbox | PlayStation | Nintendo | Keyboard |
| --- | --- | --- | --- | --- |
| `Gamepad.South` | `xbox_a` / A | `ps_cross` / Cross | `switch_b` / B | |
| `Axis.LeftTrigger` | `xbox_lt` / LT | `ps_l2` / L2 | `switch_zl` / ZL | |
| `Key.Space` | | | | `key_space` / Space |

```cpp
const illusion::GamepadFamily family = actions.lastDevice() == illusion::InputDevice::Gamepad
                                           ? illusion::gamepadFamily(actions.lastGamepadType())
                                           : illusion::GamepadFamily::Generic;
const std::string icon = illusion::glyphId(binding, family);
```

Map glyph identifiers to your UI art; they never change between engine versions.

`glyphLabel(binding, family)` returns stable English labels. For labels that players read, use the
translatable overload, which formats the label in the active language of a
[Localization](localization.md); `glyphLabelKey()` gives the string table key:

```cpp
illusion::text::Localization localization;
localization.addTable(illusion::input::strings::kTable); // #include <illusion/input/InputStrings.hpp>
const std::string label = illusion::glyphLabel(binding, family, localization);
```

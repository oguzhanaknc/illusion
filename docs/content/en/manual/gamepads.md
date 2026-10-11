# Gamepads

The input module supports Xbox, PlayStation, Nintendo and other standard controllers through SDL3's
gamepad subsystem, including hot-plug, rumble, light bars, motion sensors, touchpads and DualSense
adaptive triggers.

Headers: `#include <illusion/input/GamepadSystem.hpp>`, `<illusion/input/Gamepad.hpp>`

## Setting up

```cpp
illusion::EventBus events;
auto gamepads = illusion::GamepadSystem::create(&events);

while (running)
{
    platform->pollEvents();
    gamepads->update(window->isFocused()); // new input frame, connections, buttons, axes
    events.dispatchQueued();

    for (illusion::Gamepad* pad : gamepads->gamepads())
    {
        const illusion::GamepadState& state = pad->state();
        if (state.wasPressed(illusion::GamepadButton::South))
        {
            jump();
        }
        move(state.stick(illusion::GamepadStick::Left));
    }
}
```

- Create one `GamepadSystem` and use it on the main thread.
- Controllers that are already connected are reported by the first `update()`.
- Pass the window focus to `update()`. While the game has no focus every gamepad reads as released, so
  a controller used in another application does not move the game.
- `Gamepad*` pointers become invalid when a controller disconnects. Keep the `GamepadId` and look the
  gamepad up with `find()` each frame.

## Connections and players

`GamepadConnectedEvent` and `GamepadDisconnectedEvent` are enqueued on the event bus. Each gamepad gets
the lowest free player slot (`playerIndex()`, 0-based); the slot is shown on controllers with player
lights and is reused after a disconnection. `findByPlayer()` returns the gamepad of a slot.

## Buttons, sticks and triggers

Buttons are named by **position**: `South` is A on Xbox, Cross on PlayStation and B on Nintendo
controllers. Use [glyphs](actions.md#glyphs) to show the right symbol.

| Value | Range | Notes |
| --- | --- | --- |
| `stick(GamepadStick::Left)` | length 0-1 | +x right, **+y up**; radial dead zone (default 0.15) |
| `trigger(GamepadTrigger::Right)` | 0-1 | dead zone 0.05 |
| `axis(GamepadAxis::LeftX)` | -1-1 | raw value without dead zone |

`isDown`, `wasPressed` and `wasReleased` work like keyboard keys. Change dead zones per gamepad with
`state().setStickDeadZone()` and `setTriggerDeadZone()`.

## Device features

```cpp
const illusion::GamepadCapabilities& caps = pad->capabilities();
pad->rumble(0.4f, 0.8f, 150);                 // low and high frequency motors, milliseconds
if (caps.triggerRumble)
{
    pad->rumbleTriggers(0.0f, 0.6f, 100);     // Xbox impulse triggers
}
if (caps.led)
{
    pad->setLed(255, 64, 0);                  // DualShock 4 / DualSense light bar
}
if (caps.adaptiveTriggers)
{
    pad->setTriggerEffect(illusion::GamepadTrigger::Right,
                          illusion::TriggerEffect::weapon(0.3f, 0.6f, 1.0f));
}
```

| Feature | Supported on | API |
| --- | --- | --- |
| Rumble | Most controllers | `rumble()` |
| Trigger rumble | Xbox One / Series | `rumbleTriggers()` |
| Light bar | DualShock 4, DualSense | `setLed()` |
| Adaptive triggers | DualSense | `setTriggerEffect()` with `TriggerEffect::resistance`, `weapon`, `vibration` or `off` |
| Gyroscope and accelerometer | DualShock 4, DualSense, Switch Pro | `setMotionSensorsEnabled(true)`, then `state().gyroscope()` (rad/s) and `accelerometer()` (m/s²) |
| Touchpad | DualShock 4, DualSense | `state().finger(0)` and `finger(1)`, normalized positions |

Functions return `false` when the controller lacks the feature. Motion sensors are off by default
because they drain the battery.

## Virtual gamepads

`attachVirtualGamepad()` creates a simulated controller that behaves like real hardware: it connects,
reports the inputs you set and records the outputs (rumble, light, effects) it receives. Tests, input
recordings and AI agents controlling the editor use it.

```cpp
illusion::VirtualGamepadDesc desc;
desc.type = illusion::GamepadType::PS5;
auto virtualPad = gamepads->attachVirtualGamepad(desc);
virtualPad->setButton(illusion::GamepadButton::South, true);
virtualPad->setAxis(illusion::GamepadAxis::LeftY, 1.0f); // stick up
gamepads->update();
```

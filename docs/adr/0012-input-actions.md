# 0012. Action mapping with a text binding format and stable glyph identifiers

- Status: Accepted
- Date: 2026-10-11

## Context

Games must support keyboard/mouse and several controller families, let players rebind every action,
remember those choices, and show the correct button symbols. Hard-coding device checks in gameplay
code makes all of this impossible to add later.

## Decision

- Gameplay reads named actions (`Button`, `Axis`, `Vector2`) from `InputActions`, never devices.
- A binding is a small value type (`InputBinding`): key, mouse button, gamepad button, gamepad axis with a
  signed scale, a whole stick, or a four-key vector. When several bindings are active, the strongest
  value wins; button actions use a 0.5 threshold for analog inputs and also detect sub-frame taps.
- Actions are defined in code; only bindings are persisted, in a human-readable line format
  (`Jump = Key.Space, Gamepad.South`). Unknown actions and malformed bindings are skipped and
  reported instead of failing.
- Rebinding uses `BindingCapture`, which ignores inputs held when capture starts.
- Glyphs are stable string identifiers (`xbox_a`, `ps_cross`, `switch_b`, `key_space`) plus English
  labels, chosen by the controller family of the last used device. UI art is mapped to these ids by the
  game.
- `InputActions` is per player; local multiplayer uses one instance per player with that player's
  gamepad.

## Consequences

- Binding names in saved files depend on `keyName()`, `gamepadButtonName()` and `gamepadAxisName()`,
  which are therefore part of the stable API.
- Localized labels are a UI concern; the engine only supplies identifiers and English fallbacks.
- Action evaluation is linear in bindings per action; fine for the dozens of actions games use.

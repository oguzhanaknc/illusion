# 0011. Gamepads through SDL3, with virtual devices for tests

- Status: Accepted
- Date: 2026-10-11

## Context

The brief requires Xbox and DualSense support with rumble, LED, gyro, touchpad, adaptive triggers and
hot-plug on Windows, macOS and Linux. GLFW's joystick API lacks rumble, sensors, touchpads, effects and
reliable controller identification. Writing HID drivers ourselves would duplicate years of work and
per-controller quirks. CI machines have no controllers attached.

## Decision

- Use SDL3 (static) **only** for gamepads: the joystick, haptic, HIDAPI and sensor subsystems are built;
  video, audio, render, GPU and the rest are disabled. GLFW keeps windows, keyboard and mouse.
- SDL is initialized with background joystick events allowed (SDL owns no window) and without its signal
  handlers.
- The engine API (`GamepadSystem`, `Gamepad`, `GamepadState`) hides SDL. Buttons are named by position
  (South/East/West/North) and axes use engine conventions: sticks in [-1, 1] with +y up, triggers in
  [0, 1], radial stick dead zone.
- DualSense adaptive triggers are sent as SDL's DualSense effect report; the 11-byte trigger blocks are
  produced by the pure, unit-tested `encodeDualSenseTriggerEffect()`.
- `attachVirtualGamepad()` exposes SDL's virtual joysticks as a first-class feature: virtual devices of a
  chosen model connect, report inputs and record outputs. All gamepad behaviour, including rumble, LED,
  trigger effects, touchpad and motion, is tested end to end in CI this way. The same feature serves
  input playback and AI-driven editor control.

## Consequences

- SDL adds build time and binary size; acceptable for the controller coverage it provides.
- Two libraries touch input devices. Only SDL reads gamepads; GLFW's joystick functions must not be used.
- Real-hardware behaviour (actual rumble feel, Bluetooth quirks) can only be verified manually; the
  `input_viewer` sample exists for that.
- Steam Input integration (M8) will sit beside this layer; SDL already recognizes Steam virtual
  controllers.

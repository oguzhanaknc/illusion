# Windows and Input

The platform module opens windows and reads the keyboard and mouse. It is built on GLFW, but GLFW never
appears in public headers.

Headers: `#include <illusion/platform/Platform.hpp>`, `<illusion/platform/Window.hpp>`,
`<illusion/platform/Events.hpp>`

## The main loop

```cpp
illusion::EventBus events;
auto platform = illusion::Platform::create();
if (!platform)
{
    return 1; // no display available; the reason is in the log
}

illusion::WindowDesc desc;
desc.title = "My Game";
auto window = illusion::Window::create(*platform, desc, &events);

illusion::FrameClock clock;
while (!window->shouldClose())
{
    platform->pollEvents();   // new input frame + OS events
    events.dispatchQueued();  // deliver window events
    const illusion::FrameTime& frame = clock.tick(illusion::monotonicSeconds());
    update(frame.deltaSeconds, window->input());
    render();
}
```

Rules:

- Create exactly one `Platform`, and use it and all windows on the main thread (macOS requires this).
- Destroy windows before the `Platform`, and the `EventBus` after the windows that use it.
- `Platform::create()` and `Window::create()` return `nullptr` on failure and log the reason.

### Headless mode

`Platform::create(PlatformDesc{PlatformBackend::Headless})` runs without a display. Windows are
simulated: sizes, titles and cursor modes behave normally, but no OS events arrive. Tests, dedicated
servers and command-line tools use it.

## Window properties

| Function | Description |
| --- | --- |
| `size()` | Client area size in window coordinates. |
| `framebufferSize()` | Size in pixels; differs on high-DPI displays and is (0, 0) while minimized. |
| `contentScale()` | Pixels per window coordinate (for example 2.0 on Retina displays). |
| `setTitle()`, `setSize()` | Change the title (UTF-8) and client size. |
| `setCursorMode()` | `Normal`, `Hidden`, or `Captured` (locked, unbounded motion for camera control). |
| `isFocused()`, `isMinimized()` | Window state. |

## Keyboard and mouse

`window->input()` returns the `InputState` of the current frame:

```cpp
const illusion::InputState& input = window->input();
if (input.wasPressed(illusion::Key::Space))
{
    jump();
}
if (input.isDown(illusion::Key::W))
{
    moveForward(frame.deltaSeconds);
}
if (input.isDown(illusion::MouseButton::Right))
{
    rotateCamera(input.mouseDelta());
}
zoom(input.scrollDelta().y);
```

- `isDown` is true while a key is held; `wasPressed` and `wasReleased` are true only in the frame where
  the key changed. A quick tap within one frame reports both edges.
- Keys are physical positions named after the US layout (`Key::W` is the key left of `E` on every
  layout). Use `text()` for typed characters: it handles keyboard layouts and input methods.
- `keyName()` and `keyFromName()` convert keys to stable names for configuration files.
- When a window loses focus every held key and button is released, and matching release events are
  sent, so keys never get stuck.

## Window events

Windows enqueue events on the `EventBus` passed to `Window::create()`. They are delivered by
`EventBus::dispatchQueued()`:

| Event | Sent when |
| --- | --- |
| `WindowCloseRequestedEvent` | The user asks to close the window. |
| `WindowResizedEvent` | The client area changes size. |
| `FramebufferResizedEvent` | The framebuffer changes size; recreate the swapchain. |
| `WindowFocusEvent` | Focus is gained or lost. |
| `KeyEvent` | A key goes down, repeats or goes up. |
| `MouseButtonEvent`, `MouseMovedEvent`, `MouseScrolledEvent` | Mouse input. |
| `TextInputEvent` | A character is typed. |

```cpp
illusion::Subscription onResize = events.subscribe<illusion::FramebufferResizedEvent>(
    [&](const illusion::FramebufferResizedEvent& e) { renderer.resize(e.size); });
```

Events identify their window with a `WindowId`; compare it with `window->id()`. Ids are never reused,
so an event delivered after its window was destroyed is harmless.

Polling `InputState` suits gameplay; events suit UI and tools that must see every key press in order.

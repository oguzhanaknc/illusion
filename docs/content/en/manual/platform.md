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
| `setCursorShape()` | Arrow, text, resize (all, NS, EW, NESW, NWSE), hand, not-allowed or crosshair cursor. |
| `setCursorPosition()` | Moves the cursor (window coordinates). |
| `position()`, `setPosition()` | The client area's position on the screen. |
| `maximize()`, `restore()`, `iconify()` | Change the window state; `WindowDesc::maximized` starts maximized. |
| `isFocused()`, `isMinimized()`, `isMaximized()` | Window state. |
| `setIcon()` | Sets the window icon from RGBA images of several sizes. |
| `setImeRect()` | Tells the input method where text is typed, so its candidate list opens next to it (Windows). |
| `setClipboardText()`, `clipboardText()` | The system clipboard (UTF-8). |

Some systems do not allow everything: on Wayland, windows do not know their position (`position()`
returns nullopt, `setPosition()` false) and the cursor can only be moved while it is captured; macOS and
Wayland take the icon from the application bundle or desktop file (`setIcon()` returns false). Cursor
shapes a system lacks show the arrow.

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
| `FilesDroppedEvent` | Files or folders are dropped on the window. |
| `ContentScaleChangedEvent` | The pixel scale changes, for example on a monitor with another DPI. |
| `WindowMovedEvent` | The window moves (not on Wayland). |
| `CursorEnteredEvent` | The cursor enters or leaves the client area. |
| `WindowRefreshEvent` | The contents must be drawn again, for example during a resize. |

```cpp
illusion::Subscription onResize = events.subscribe<illusion::FramebufferResizedEvent>(
    [&](const illusion::FramebufferResizedEvent& e) { renderer.resize(e.size); });
```

Events identify their window with a `WindowId`; compare it with `window->id()`. Ids are never reused,
so an event delivered after its window was destroyed is harmless.

Polling `InputState` suits gameplay; events suit UI and tools that must see every key press in order.

### Dropped files

A drop enqueues one `FilesDroppedEvent`; its paths (UTF-8) are read with `droppedPath()` and stay valid
until the next `pollEvents()` or `waitEvents()`:

```cpp
illusion::Subscription onDrop = events.subscribe<illusion::FilesDroppedEvent>(
    [&](const illusion::FilesDroppedEvent& e) {
        for (illusion::u32 i = 0; i < e.count; ++i)
        {
            importAsset(window->droppedPath(e.first + i));
        }
    });
```

`Window::dropFiles()` delivers a drop the same way, for input playback and automation.

### High-DPI displays

`contentScale()` is the number of pixels per window coordinate. Render at `framebufferSize()`, lay out UI
in window coordinates, and rebuild fonts when `ContentScaleChangedEvent` arrives.

## Monitors

`platform->monitors()` lists the connected displays, the primary one first, with their name, position,
size, work area (without task bars and docks) and content scale. The headless backend reports one
simulated monitor.

## Waking the event loop

Tools that only redraw on input sleep in `waitEvents()`. `wake()` ends the wait from any thread:

```cpp
// main thread: sleep until input arrives or a background task finishes
platform->waitEvents(1.0);

// worker thread, when a result is ready:
platform->wake();
```

## Key names

Key codes name physical positions. `keyDisplayName()` returns the character a key types in the active
keyboard layout, so that shortcuts can be shown the way the user's keyboard prints them:

```cpp
const std::optional<std::string> character = platform->keyDisplayName(illusion::Key::Y);
// "y" on a US layout, "z" on a German one; nullopt for keys like Space or Shift
```

Keys that type no character return nullopt; show their translated label instead (`glyphLabelKey()`, see
[Localization](localization.md)).

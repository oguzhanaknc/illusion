# Events and Time

Header: `#include <illusion/core/EventBus.hpp>`, `<illusion/core/Time.hpp>`

## Event bus

The `EventBus` connects systems without making them depend on each other. An event is a plain struct;
subscribers register a handler for its type.

```cpp
struct PlayerDied
{
    illusion::Entity player;
    illusion::u32 score;
};

illusion::EventBus bus;
illusion::Subscription sub = bus.subscribe<PlayerDied>([](const PlayerDied& e) {
    showGameOver(e.score);
});

bus.publish(PlayerDied{player, 1200}); // handlers run now
bus.enqueue(PlayerDied{player, 1200}); // handlers run in dispatchQueued()
bus.dispatchQueued();
```

### Immediate and queued delivery

- `publish()` calls every handler before it returns, in subscription order.
- `enqueue()` stores a copy; `dispatchQueued()` delivers all stored events in the order they were
  enqueued. Events enqueued by handlers during `dispatchQueued()` wait for the next call. Queued events
  must be trivially copyable.

Use queued delivery to process events at a defined point in the frame (windows do this), and
immediate delivery when the sender needs the reaction to have happened.

### Subscriptions

`subscribe()` returns a `Subscription`. The handler stays registered while the `Subscription` is alive;
destroy it or call `reset()` to unsubscribe. Store subscriptions as members of the object whose handler
they register, so they cannot outlive it.

Handlers may publish, enqueue, subscribe and unsubscribe while running. A handler subscribed during a
dispatch receives events from the next publish on; a handler unsubscribed during a dispatch is not
called again.

### Rules

- Use the bus from one thread (normally the main thread).
- Handler captures are limited to 56 bytes; capture a pointer to larger state.
- Destroy all subscriptions before the bus.
- Publishing does not allocate once each event type has been seen.

## Time

`monotonicSeconds()` returns a high-resolution, monotonic time stamp. `Stopwatch` measures intervals.

`FrameClock` turns time stamps into per-frame timing. Long stalls (a breakpoint, a window drag) are
clamped so the game does not jump:

```cpp
illusion::FrameClock clock(0.25); // at most 250 ms per frame
const illusion::FrameTime& frame = clock.tick(illusion::monotonicSeconds());
// frame.deltaSeconds, frame.totalSeconds, frame.frameIndex
```

`FixedTimestep` runs simulation at a fixed rate independent of the frame rate:

```cpp
illusion::FixedTimestep physics(1.0 / 60.0);
const illusion::u32 steps = physics.advance(frame.deltaSeconds);
for (illusion::u32 i = 0; i < steps; ++i)
{
    simulate(physics.stepSeconds());
}
renderInterpolated(physics.alpha());
```

At most `maxStepsPerFrame` steps (default 8) run per frame; if the simulation falls further behind, the
extra time is dropped instead of making every following frame slower.

Both classes take time as a parameter instead of reading the clock, so tests and replays are
deterministic.

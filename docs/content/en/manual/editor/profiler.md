# Profiler and Diagnostics

The **Profiler** panel (Window → Profiler, or the `profiling` layout) shows where the editor's frame time
goes, how long each viewport's render passes take on the GPU, and the problems the editor knows about. Every
part of it is also an [Editor command](../../reference/editor-commands.md), so an AI agent can read the same
numbers and act on the same problems.

The Profiler measures the editor itself. For a detailed timeline of a game or the engine, build with the
`ILLUSION_PROFILING` option and connect Tracy (see [Profiling](../profiling.md)).

## CPU

The CPU tab draws the time of the last 240 frames as a graph, with the average, the longest frame and the
frames per second above it. The table below lists the *places* of the frame and how long each took:

| Place | What it measures |
| --- | --- |
| `events` | Window and input events (an unfocused editor waits here for input). |
| `wait` | A `--headless` editor waiting for requests (it has no window events). |
| `requests` | Requests of connected agents and tools (`illusion_ctl`, MCP). |
| `ui` | Building the editor's UI; each drawn panel is nested under it, e.g. `panel.outliner`. |
| `render` | Rendering the viewports and presenting the window. |
| `play` | The running game in Play mode. |
| `editor` | The editor's own work at the end of the frame: asset changes, autosave, prefab refreshes. |

For each place the table shows the time in the newest frame, the average per frame, the most in one frame,
and how many times it ran in the newest frame. Pause recording to look at a moment more closely: the recorded
frames stay until you resume or clear them.

The same data comes from `profiler.get`; `profiler.set` pauses, resumes and clears:

```json
{"frames": 60}
```

```json
{"paused": true}
```

The profiler records into a fixed ring of frames, so recording never allocates and costs well under a
microsecond per scope. Engine code can use the same recorder, `FrameProfiler` in
`illusion/core/FrameProfiler.hpp`:

```cpp
FrameProfiler profiler;
profiler.beginFrame();
{
    FrameProfiler::Scope scope(profiler, "update");
    update();
}
profiler.endFrame();
```

## GPU

The GPU tab lists each viewport with the GPU time of its render passes: shadows, the depth prepass, ambient
occlusion, shading and post-processing. The times come from timestamp queries of the newest frame the GPU has
finished, so reading them never waits for the GPU. A hidden viewport, or a GPU without timestamps, shows
*Not measured yet*. `render.stats` returns the same times in its `gpu` object.

## Diagnostics

The Diagnostics tab lists the problems the editor finds, errors first:

| Code | Severity | Meaning |
| --- | --- | --- |
| `render.validation` | error | The renderer caused Vulkan validation errors (the goal is zero). |
| `log.errors` | error | Error messages in the log since it was last cleared. |
| `asset.missing` | error | An asset that scenes, materials or settings refer to does not exist. |
| `render.lights_dropped` | warning | A viewport has more active lights than the renderer draws. |
| `log.warnings` | warning | Warning messages in the log. |
| `prefab.unresolved` | warning | A prefab instance whose prefab could not be loaded; it keeps its record. |

Click an issue to go to it: log messages open the Console, a missing asset opens its folder in the Asset
Browser, and an unresolved instance is selected. Press **Refresh** after you change something.
`diagnostics.get` returns the same list, each issue with its `code`, `subject` (a viewport, an asset path or
an entity id) and `count`.

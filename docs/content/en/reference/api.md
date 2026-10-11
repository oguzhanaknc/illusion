# API Reference

The API reference is generated from the comments in the public headers with Doxygen. Every public
declaration is documented; CI fails if one is not.

## Generating the reference locally

Install [Doxygen](https://www.doxygen.nl/) 1.9 or newer, then run from the repository root:

```bash
doxygen docs/Doxyfile
```

Open `build/api/index.html` in a browser.

## Public headers

| Header | Contents |
| --- | --- |
| `illusion/core/Base.hpp` | Fixed-width types, platform macros, alignment helpers |
| `illusion/core/Assert.hpp` | `ILL_ASSERT`, `ILL_VERIFY`, `ILL_CHECK`, assert handlers |
| `illusion/core/Log.hpp` | `Logger`, `LogSink`, `ILL_LOG_*` macros |
| `illusion/core/Memory.hpp` | `LinearAllocator`, `PoolAllocator`, aligned allocation |
| `illusion/core/JobSystem.hpp` | `JobSystem`, `JobCounter`, `Job` |
| `illusion/core/InplaceFunction.hpp` | Allocation-free type-erased callable |
| `illusion/core/Thread.hpp` | Thread names, thread indices, spin-wait hint |
| `illusion/core/Hash.hpp` | Stable FNV-1a hashing |
| `illusion/core/TypeName.hpp` | Compile-time type names |
| `illusion/core/Profiler.hpp` | Tracy profiling macros |
| `illusion/core/EventBus.hpp` | `EventBus`, `Subscription` |
| `illusion/core/Time.hpp` | `monotonicSeconds`, `Stopwatch`, `FrameClock`, `FixedTimestep` |
| `illusion/core/Math.hpp` | glm-based vector and matrix types |
| `illusion/ecs/World.hpp` | `World`, `Query`, `ChunkView` |
| `illusion/ecs/CommandBuffer.hpp` | Deferred structural changes |
| `illusion/ecs/Component.hpp` | Component registration and `ComponentRegistry` |
| `illusion/ecs/Entity.hpp` | `Entity` handle |
| `illusion/ecs/Archetype.hpp` | Archetype storage (engine internal) |
| `illusion/platform/Platform.hpp` | `Platform`: windowing system lifetime and event pumping |
| `illusion/platform/Window.hpp` | `Window`, `WindowDesc`, `CursorMode` |
| `illusion/platform/Input.hpp` | `InputState`, `Key`, `MouseButton` |
| `illusion/platform/Events.hpp` | Window and input events |
| `illusion/platform/FileSystem.hpp` | `VirtualFileSystem`, platform directories |
| `illusion/input/Gamepad.hpp` | `Gamepad`, `GamepadState`, `TriggerEffect`, gamepad events |
| `illusion/input/GamepadSystem.hpp` | `GamepadSystem`, `VirtualGamepad` |
| `illusion/input/InputActions.hpp` | `InputActions`, `InputBinding`, `BindingCapture`, glyphs |
| `illusion/render/RenderDevice.hpp` | `RenderDevice`, `GpuInfo`: Vulkan device through NVRHI |
| `illusion/render/Swapchain.hpp` | `Swapchain`: presentation to a window |
| `illusion/render/DebugTriangle.hpp` | Diagnostic triangle renderer |

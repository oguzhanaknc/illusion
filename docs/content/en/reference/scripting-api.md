# Scripting API

Game code is C++ in a [game module](../manual/modules.md). A shared module (the editor's hot-reloadable
build) carries its own copy of `illusion::core` but no other engine library: it reaches physics, audio,
input and scenes through four **host service interfaces**. Each is an abstract class without data members;
the host implements it with an adapter over its engine object and hands it to the module, which finds it
with `context.service<T>()`. The design is in ADR 0041; the Doxygen group "Scripting API" of the
[API reference](api.md) documents every function.

| Interface | `USES` | Header | Host adapter |
| --- | --- | --- | --- |
| `PhysicsService` | `physics` | `illusion/physics/PhysicsService.hpp` | `PhysicsWorldService` over `PhysicsWorld` |
| `AudioService` | `audio` | `illusion/audio/AudioService.hpp` | `AudioWorldService` over `AudioWorld` |
| `InputService` | `input` | `illusion/input/InputService.hpp` | `InputActionsService` over `InputActions` |
| `SceneService` | `runtime` | `illusion/runtime/SceneService.hpp` | `SceneLoaderService` (`illusion::runtime`) |

```cmake
illusion_add_module(tetris SOURCES game/Tetris.cpp USES physics audio input runtime)
```

```cpp
void update(illusion::ModuleContext& context, illusion::f64)
{
    illusion::InputService* input = context.service<illusion::InputService>();
    illusion::PhysicsService* physics = context.service<illusion::PhysicsService>();
    if (input == nullptr || physics == nullptr)
    {
        return; // this World was attached without them
    }
    if (const std::optional<illusion::ActionId> jump = input->findAction("Jump"); jump && input->wasPressed(*jump))
    {
        physics->addImpulse(player, illusion::Vec3(0.0f, 5.0f, 0.0f));
    }
}
```

## Rules

- **Check every pointer.** `context.service<T>()` returns null when neither the World nor the host provides
  `T` (an edit or preview World, a test host).
- **Hold handles, not objects.** Geometry (`PhysicsMeshHandle`), clips (`AudioClipHandle`), voices and
  entities are plain values; the objects behind them belong to the host and survive hot reloads. Store
  handles in Components, which keep their data across reloads.
- **Spans and views are borrowed.** `contactEvents()`, `brokenJoints()`, `bindings()`, `actionName()` and
  `lastError()` point into host memory and stay valid until the next update or change; copy what you keep.
- **Outputs go into your buffers.** `overlap()` and `instantiate()` write as many entities as fit into a
  span and return how many there are, so nothing crosses the boundary in a container.
- **Per World or per host.** The host gives physics, audio and scenes to each Simulation World
  (`attachWorld(world, role, services)`), so every play session has its own; input is usually host-wide
  (`ModuleHost::provide()`).
- **Engine code stays in the host.** A non-inline engine function called from a shared module is a link
  error; plain-data helpers (Components, `InputBinding::key()`, ...) are inline and work everywhere.
  Static modules (exported games) link the engine and may use the libraries directly, but the services
  work there too.
- **Cost.** A call is one indirect call; the `services/virtual-call` Benchmark keeps it under 10 ns.
- **Compatibility.** The interfaces are part of the module ABI: adding or changing a function changes
  `kModuleApiVersion`, and modules must be rebuilt.

## PhysicsService

Behaves like the `PhysicsWorld` functions of the same name.

| Function | Purpose |
| --- | --- |
| `setGravity`, `gravity` | World gravity in m/s² |
| `setLayersCollide`, `layersCollide` | Collision filtering between layers |
| `rebuild` | Recreates a body after its RigidBody or Collider changed |
| `setLinearVelocity`, `linearVelocity`, `setAngularVelocity`, `angularVelocity` | Body velocities |
| `addForce`, `addTorque`, `addImpulse` | Forces for the next step, instant impulses |
| `mass`, `isAwake` | Body state |
| `createMesh`, `createConvexHull`, `destroyMesh`, `isMeshValid` | Host-owned geometry for `Collider::geometry` |
| `setCharacterVelocity`, `characterVelocity`, `isCharacterGrounded` | CharacterController movement |
| `jointPosition`, `brokenJoints` | Joint state and breakage |
| `raycast`, `shapeCast`, `overlap` | Queries; pass `0xFFFFFFFFu` and `kNullEntity` for all layers, nothing ignored |
| `contactEvents` | Contacts that began or ended in the last update |

## AudioService

Behaves like the `AudioWorld` functions of the same name; `stopVoice` and `isVoicePlaying` are its
`stop(AudioVoiceHandle)` and `isPlaying(AudioVoiceHandle)`.

| Function | Purpose |
| --- | --- |
| `loadClip`, `requestClip`, `clipState`, `createClip`, `unloadClip`, `clipInfo` | Host-owned clips |
| `play`, `stop`, `isPlaying` | The AudioSource of an Entity |
| `playOneShot`, `stopVoice`, `isVoicePlaying` | Sounds without an Entity |
| `setMasterVolume`, `masterVolume`, `setBusVolume`, `busVolume` | Mix levels |
| `setBusPaused`, `isBusPaused`, `setBusFilter`, `busFilter` | Bus pause and filters |
| `setReverb`, `reverb` | The shared reverb |

## InputService

Behaves like the `InputActions` functions of the same name. `addAction` returns the existing action of the
same name and type, and nullopt if the name is taken by another type.

| Function | Purpose |
| --- | --- |
| `addAction`, `findAction`, `actionCount`, `actionName`, `actionType` | Actions |
| `bindings`, `setBindings` | Rebinding |
| `isDown`, `wasPressed`, `wasReleased`, `value`, `vector` | State in the current frame |
| `lastDevice`, `lastGamepadType` | Which glyphs to show |

## SceneService

| Function | Purpose |
| --- | --- |
| `exists` | True if a file exists at a virtual path |
| `instantiate` | Loads a scene file into the World with new EntityGuids; returns the number of entities, or nullopt |
| `lastError` | Why the last `instantiate` failed |

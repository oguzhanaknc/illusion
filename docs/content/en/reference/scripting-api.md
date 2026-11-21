# Scripting API

Game code is C++ in a [game module](../manual/modules.md). A shared module (the editor's hot-reloadable
build) carries its own copy of `illusion::core` but no other engine library: it reaches physics, audio,
particles, animation, input, scenes, the store, the game UI and saved games through nine **host service interfaces**. Each is an abstract class without data members;
the host implements it with an adapter over its engine object and hands it to the module, which finds it
with `context.service<T>()`. The design is in ADR 0041; the Doxygen group "Scripting API" of the
[API reference](api.md) documents every function.

| Interface | `USES` | Header | Host adapter |
| --- | --- | --- | --- |
| `PhysicsService` | `physics` | `illusion/physics/PhysicsService.hpp` | `PhysicsWorldService` over `PhysicsWorld` |
| `AudioService` | `audio` | `illusion/audio/AudioService.hpp` | `AudioWorldService` over `AudioWorld` |
| `ParticleService` | `particles` | `illusion/particles/ParticleService.hpp` | `ParticleWorldService` over `ParticleWorld` |
| `InputService` | `input` | `illusion/input/InputService.hpp` | `InputActionsService` over `InputActions` |
| `SceneService` | `runtime` | `illusion/runtime/SceneService.hpp` | `SceneLoaderService` (`illusion::runtime`) |
| `StoreService` | `store` | `illusion/store/StoreService.hpp` | `NullStoreService` or `SteamStoreService` (ADR 0056) |
| `UiService` | `ui` | `illusion/ui/UiService.hpp` | `UiWorldService` over `UiWorld` (`illusion::ui`; ADR 0057) |
| `SaveService` | `save` | `illusion/save/SaveService.hpp` | `SaveSystemService` over `SaveSystem` (`illusion::save`; ADR 0058) |
| `AnimationService` | `animation` | `illusion/animation/AnimationService.hpp` | `AnimationWorldService` over `AnimationWorld` (`illusion::animation`; ADR 0060) |

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
  `kModuleApiVersion` (9 since `AnimationService::setLookAtTarget()`, `setIkTarget()` and `setIkWeight()`), and modules must be rebuilt.

## Coverage

The Doxygen group "Scripting API" holds `ModuleContext` and the nine interfaces. The
[showcase module](../manual/editor/showcase.md#the-showcase-module) calls every public function of the group,
and `python tools/check_api_coverage.py` fails in CI when one is not called there, or when a counting proxy
of the module tests (`tests/modules/CountingServices.hpp`) has the wrong number of counters. A new function
therefore needs a real use in `samples/showcase/modules/showcase/showcase.cpp`.

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

## ParticleService

| Function | Purpose |
| --- | --- |
| `play`, `stop`, `clear` | Start an emitter again from the beginning, stop it emitting, remove its live particles |
| `emit` | A burst of particles now, where the emitter stands, playing or not; returns how many fit |
| `aliveCount`, `isPlaying`, `totalAlive` | Live particles of an emitter, whether it emits, live particles of all |

## SceneService

| Function | Purpose |
| --- | --- |
| `exists` | True if a file exists at a virtual path |
| `instantiate` | Loads a scene file into the World with new EntityGuids; returns the number of entities, or nullopt |
| `lastError` | Why the last `instantiate` failed |
| `requestExit` | Asks the host to end the game after this update: the player closes, the editor's Play mode stops |

## StoreService

The store the game runs in, or none; see [Store services](../manual/runtime.md#store-services). Failures are
`Expected<T, StoreError>`; names are checked by `isValidStoreName()`.

| Function | Purpose |
| --- | --- |
| `available`, `storeName`, `userName`, `userId` | Whether a store runs with a signed-in user, which store, who |
| `unlockAchievement`, `clearAchievement`, `isAchievementUnlocked` | Achievements by API name |
| `setAchievementProgress`, `achievementProgress` | Progress as current/max; reaching max unlocks |
| `setStatInt`, `statInt`, `setStatFloat`, `statFloat`, `storeStats` | Stats, sent to the store by `storeStats` |
| `writeCloudFile`, `readCloudFile`, `removeCloudFile` | Cloud files by name; a read fills a caller span and returns the size |
| `listCloudFiles`, `cloudFileExists`, `cloudQuota` | Names into a caller span, existence, size and free space |
| `overlayActive`, `openOverlay` | The overlay's state, polled each frame, and a request to open it |
| `glyphPath` | Path of an image of a gamepad control in the store's style, or nothing |

## UiService

The game UI of the World; see [Scripting the UI](../manual/ui.md#scripting-the-ui). Functions that take an
Entity return false (or nothing) for a dead Entity or one without the Component they need. Changes reach the
layout without `World::markChanged()`.

| Function | Purpose |
| --- | --- |
| `find` | The first Entity with a given `Name` below a root, or on any canvas in draw order |
| `setText`, `setTextKey` | A literal text, or a string table message formatted with `UiTextArgument::number/string` |
| `setVisible`, `isVisible`, `setInteractable` | Show or hide an element and its descendants, whether it shows, `UiInteractable::enabled` |
| `setToggle`, `toggle`, `setSliderValue`, `sliderValue` | Widget values; setting one reports no event |
| `setFocus`, `focused` | Move the focus (null clears it), the focused element |
| `rect` | The rectangle of the last layout, in canvas units |
| `invalidateLayout` | Tells the UI that Components of UI Entities were written directly |
| `playTransition` | Moves an element's `UiTransition` towards its changed state or back to rest, whatever its trigger |
| `transitionProgress` | Progress of an element's `UiTransition`: 0 at rest, 1 in the changed state |
| `pollEvents` | Copies the events of the previous frame's UI update into a span; returns the total, which may be more than fit |
| `setLanguage`, `language` | The game's language, a BCP 47 tag with a locale file in the project |

## SaveService

Saved games of the Entities with the `Saved` tag and their descendants (ADR 0058). Requests are queued in call
order and run on the host's save thread; `save`, `load` and `remove` fail at once only with `InvalidName`,
`InvalidValue` or `Unavailable`, everything later is a `SaveEvent`. A save captures the World after the
Modules step of the same update; a load replaces the saved Entities in a later update, so their `Entity`
handles change: find them again after `Loaded` or `LoadedBackup`.

| Function | Purpose |
| --- | --- |
| `available` | Whether the host has a save directory |
| `save`, `load`, `remove` | Queue writing, applying or removing a slot (1 to 32 letters, digits, `_`, `-`) |
| `busy` | Whether a request has not delivered its event yet |
| `slots`, `slot` | The slot list (name, `SaveMetadata`, `savedAt`, `SaveSlotState`, `screenshot`) into a caller span, or one slot; no file is read |
| `pollEvents` | Copies the events of the last update (`Saved`, `Loaded`, `LoadedBackup`, `Removed`, `Failed` with a `SaveError`) into a span; returns the total |

`SaveSlotInfo::screenshot` is the virtual path of the slot's 320 x 180 PNG (`user:/saves/<slot>.png` in the
player), or empty when the slot has none. With a store whose cloud is remote, slot files are also mirrored to the
cloud and a newer cloud file replaces the local slot when the session starts; this produces no event
([Saved games in the cloud](../manual/runtime.md#saved-games-in-the-cloud)). The manual page
[Saved games](../manual/saved-games.md) explains the `Saved` tag, the metadata, the events and the files.

## AnimationService

The animation graphs and clips of the Animators, their joints, look-at and IK (ADR 0059, ADR 0060, ADR 0061). Parameter writes are used at the next
Animation step; reads return the values the graph uses. A call on an Entity without an `Animator` returns
`NoAnimator`; one that needs a graph returns `NoGraph` while the Animator has none or its graph has not
loaded; unknown names return `UnknownName` and wrong types `TypeMismatch`. Nothing asserts.

| Function | Purpose |
| --- | --- |
| `setFloat`, `setInt`, `setBool` | Set a parameter of the Entity's graph (`InvalidValue` for a value that is not finite, or an Int beyond 2^24) |
| `setTrigger` | Set a trigger until a transition consumes it or the next step ends |
| `parameter` | The value of a Float, Int or Bool parameter (Int and Bool as numbers) |
| `currentState`, `normalizedTime` | The current state of a layer (the target of a running transition) and its normalized time |
| `play` | Cross-fades an Animator without a graph to the clip `model.glb#animation/<name>` over `fadeSeconds`, from time 0; sets `Animator::clip` (`HasGraph` with a graph) |
| `pollEvents` | Copies the state events of the last update (`AnimationEvent`: Entity, event, layer, state) into a span, in update order; returns the total |
| `jointTransform` | The world-space matrix of a joint of the Entity's skeleton from the last update: the Entity's `Transform` times the joint's model-space pose, where a [socket](../manual/animation.md#sockets) would place a child (`UnknownName` for a joint the skeleton lacks, `NoGraph` while the pose loads) |
| `setLookAtTarget` | Sets the world-space point the Entity's [`LookAt`](../manual/animation.md#look-at) looks at and clears its target Entity (`UnknownName` without a `LookAt`) |
| `setIkTarget` | Sets the world-space target of the Animator's [`TwoBoneIk`](../manual/animation.md#two-bone-ik) whose end joint is `joint` (on the Entity or a child) and clears its target Entity (`UnknownName` without one) |
| `setIkWeight` | Sets the weight of that `TwoBoneIk`, clamped to [0, 1]; 0 shows the animated limb |

Every function returns an `Expected<..., AnimationError>`; `animationErrorName()` gives the name:

| Error | When |
| --- | --- |
| `NoAnimator` | The Entity is not alive or has no `Animator` |
| `NoGraph` | The call needs a graph and the Animator has none, or it has not loaded yet; for `jointTransform()`, the pose has not loaded |
| `HasGraph` | `play()` on an Animator whose graph controls it |
| `UnknownName` | No parameter, layer, clip or joint of that name, or no `LookAt` or `TwoBoneIk` for it (also a `play()` path that is not `model.glb#animation/<name>`) |
| `TypeMismatch` | The parameter has another type (`setFloat()` on a Trigger, `setTrigger()` on a Float, ...) |
| `InvalidValue` | A value is not finite, an Int is beyond 2^24, or a fade is negative |

The events of an update are published until the next Animation update: every `pollEvents()` call in between
returns the same events (the total, even beyond the span's size), so size the span for the busiest update
or read it once per update. The names an `AnimationEvent` and `currentState()` return stay valid until the
next update. Parameter writes made while the game is paused are used at the next Animation step.

```cpp
if (illusion::AnimationService* animation = context.service<illusion::AnimationService>())
{
    static_cast<void>(animation->setFloat(hero, "speed", 3.0f));
    std::array<illusion::AnimationEvent, 16> events{};
    const illusion::u32 count = animation->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].name == "footstep")
        {
            playFootstep(events[i].entity);
        }
    }
}
```

The manual page [Animation](../manual/animation.md#animation-graphs) describes graphs and the
[tutorial](../tutorials/animated-character.md) uses them; root motion is applied by the
[game session](../manual/runtime.md).

# Scripting API

Game code is C++ in a [game module](../manual/modules.md). A shared module (the editor's hot-reloadable
build) carries its own copy of `illusion::core` but no other engine library: it reaches physics, audio,
particles, animation, navigation, AI, input, scenes, the store, the game UI and saved games through eleven **host service interfaces**. Each is an abstract class without data members;
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
| `NavService` | `nav` | `illusion/nav/NavService.hpp` | `NavWorldService` over `NavWorld` (`illusion::nav`; ADR 0062) |
| `AiService` | `ai` | `illusion/ai/AiService.hpp` | `AiWorldService` over `AiWorld` (`illusion::ai`; ADR 0062) |
| `CameraService` | `camera` | `illusion/camera/CameraService.hpp` | `CameraWorldService` over `CameraWorld` (`illusion::camera`; ADR 0064) |

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
  `kModuleApiVersion` (14 since `AnimationService::setAimTarget()`; 13 added `CameraService`), and modules must be rebuilt.

## Coverage

The Doxygen group "Scripting API" holds `ModuleContext` and the ten interfaces. The
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

The animation graphs and clips of the Animators, their joints, look-at, IK and ragdolls (ADR 0059, ADR 0060, ADR 0061). Parameter writes are used at the next
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
| `setAimTarget` | Sets the world-space point the Entity's [`Aim`](../manual/animation.md#aim) aims at and clears its target Entity (`UnknownName` without an `Aim`, `InvalidValue` for a point that is not finite) |
| `setAimWeight` | Sets the weight of the `Aim`'s procedural turn, clamped to [0, 1]; 0 leaves only the graph parameters (`UnknownName` without an `Aim`, `InvalidValue` for a weight that is not finite) |
| `aimAngles` | The yaw and pitch the `Aim` aimed at in the last update, smoothed and clamped, in radians (`AimAngles`; zero before the first update; `UnknownName` without an `Aim`) |
| `setRagdoll` | Turns the Entity's [`Ragdoll`](../manual/animation.md#ragdolls) on (it falls from its current pose and velocity) or off (the pose blends back to the animation) by writing `Ragdoll::active` (`UnknownName` without a `Ragdoll`) |

Every function returns an `Expected<..., AnimationError>`; `animationErrorName()` gives the name:

| Error | When |
| --- | --- |
| `NoAnimator` | The Entity is not alive or has no `Animator` |
| `NoGraph` | The call needs a graph and the Animator has none, or it has not loaded yet; for `jointTransform()`, the pose has not loaded |
| `HasGraph` | `play()` on an Animator whose graph controls it |
| `UnknownName` | No parameter, layer, clip or joint of that name, or no `LookAt`, `TwoBoneIk`, `Aim` or `Ragdoll` for it (also a `play()` path that is not `model.glb#animation/<name>`) |
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

## NavService

The navmeshes and the NavAgents of the game (ADR 0062). The queries (`findPath`, `nearestPoint`, `raycast`,
`randomPoint`) take a `NavQueryFilter` (agent type, area mask, search extents) and write corner points into
your span. Destination changes (`setDestination`, `stop`) are saved in `NavAgent::destination` and take effect
at the next Navigation step; `warp` moves at once. A call on an Entity that is not alive or has no `NavAgent`
returns `NoAgent`; nothing asserts.

| Function | Purpose |
| --- | --- |
| `findPath` | Writes the corner points of the shortest path into a span and returns `NavPathResult`: `count` written, `total` of the whole path, `length` in m and `partial` (the path ends at the reachable point nearest the destination) |
| `nearestPoint` | The navmesh point nearest a position within `searchExtents` |
| `raycast` | Walks the navmesh from one point towards another and returns `NavRaycastHit`: `hit`, `point`, the horizontal `normal` of the wall and the `fraction` walked |
| `randomPoint` | A navmesh point within a radius that is reachable from the centre; the same seed gives the same point |
| `setDestination`, `stop` | Send the Entity's agent to a world-space point, or clear its destination |
| `warp` | Moves the agent to the navmesh point nearest a position at once and drops its path |
| `status`, `remainingDistance` | The agent's `NavAgentStatus` and the distance left along its path in m, as of the last update (an agent that has not been updated yet is `Idle`, or `Pending` with a destination, with 0 m left) |
| `pollEvents` | Copies the events of the last update (`NavEvent`: agent, type, link, link kind, position) into a span, in Entity order; returns the total |

Every function except `pollEvents` returns an `Expected<..., NavError>`; `navErrorName()` gives the name:

| Error | When |
| --- | --- |
| `NoAgent` | The Entity is not alive or has no `NavAgent` |
| `NoNavMesh` | No navmesh of that agent type is loaded yet |
| `OffMesh` | No navmesh polygon within `searchExtents` of a point |
| `NoPath` | The points are on the navmesh but nothing reachable is nearer the destination than the start |
| `InvalidValue` | A value is not finite or out of range: an agent type of 8 or more, a negative radius or extent, an empty span |

The events of an update are published until the next Navigation update: every `pollEvents()` call in between
returns the same events (the total, even beyond the span's size), so size the span for the busiest update or
read it once per update.

```cpp
if (illusion::NavService* nav = context.service<illusion::NavService>())
{
    const illusion::NavQueryFilter filter{
        .agentType = 0, .areaMask = 0xFFFFFFFFu, .searchExtents = illusion::Vec3(2.0f, 4.0f, 2.0f)};
    if (auto point = nav->randomPoint(home, 10.0f, seed, filter))
    {
        static_cast<void>(nav->setDestination(creature, *point));
    }
    if (nav->status(creature).valueOr(illusion::NavAgentStatus::Idle) == illusion::NavAgentStatus::Moving)
    {
        const illusion::f32 left = nav->remainingDistance(creature).valueOr(0.0f);
        showDistance(left);
    }
    std::array<illusion::NavEvent, 16> events{};
    const illusion::u32 count = nav->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].type == illusion::NavEventType::LinkEntered)
        {
            playJump(events[i].agent);
        }
    }
}
```

The manual page [Navigation](../manual/navigation.md#from-a-module) describes surfaces, agents and the
crowd.

## AiService

The behaviour trees of the game (ADR 0062). A tree's **blackboard** holds the typed keys its nodes read and
write; the setters (`setBool`, `setInt`, `setFloat`, `setVector`, `setEntity`) write a key of the Entity's tree,
and the next AI step sees the value. Game actions of a tree (`{"action": "bite"}`) are handed to your code:
`pollActions` lists the ones the last update started or aborted, and `finishAction` ends a running one. A call
on an Entity that is not alive or has no `BehaviorAgent` or `AiPerception` returns `NoAgent`; nothing asserts.

| Function | Purpose |
| --- | --- |
| `setBool`, `setInt`, `setFloat`, `setVector`, `setEntity` | Write a key of the type in the name; a null Entity clears an Entity key |
| `clearValue` | Clears a Vector3 or Entity key |
| `value` | Reads a key as a `BlackboardValue` (its `type`, whether it is `set` and the field of that type), as of the last write or AI step |
| `pollActions` | Writes the `AiActionRequest`s of the last update (agent, action id, the node's `name` and the phase `Started` or `Aborted`) into a span, in agent order; returns the total |
| `finishAction` | Ends a running action by its id: its node succeeds or fails at the next AI step; an `Aborted` action is not finished |
| `reportNoise` | Reports an `AiNoise` (position, radius, instigator, team) that perceivers hear in the next AI step |
| `perceived` | Writes what an `AiPerception` senses or remembers (`AiPerceived`: Entity, sense, last known position, age, whether it is current) into a span, nearest first; returns the count |
| `runningNode` | The name of the deepest running node of the tree, or an empty name when none runs |
| `restart` | Starts the tree again from its root with its starting blackboard; running actions abort |

Every function except `pollActions` returns an `Expected<..., AiError>`; `aiErrorName()` gives the name:

| Error | When |
| --- | --- |
| `NoAgent` | The Entity is not alive or has neither a `BehaviorAgent` nor an `AiPerception` |
| `NoTree` | The agent's tree has not loaded yet, or failed to load |
| `UnknownKey` | The tree has no key of that name |
| `TypeMismatch` | The key has another type (`clearValue` on a Bool, Int or Float key too) |
| `UnknownAction` | The Entity has no running action of that id |
| `InvalidValue` | A value is not finite or out of range: a negative noise radius, a team of 32 or more |
| `Full` | The noises of this update are full (`AiWorldDesc::maxNoisesPerUpdate`) |

The actions of an update are published until the next AI update: every `pollActions()` call in between
returns the same actions (the total, even beyond the span's size), so size the span for the busiest update or
read it once per update. The `name` of an action and the string `runningNode` returns stay valid until the
next update.

```cpp
if (illusion::AiService* ai = context.service<illusion::AiService>())
{
    std::array<illusion::AiActionRequest, 16> actions{};
    const illusion::u32 count = ai->pollActions(actions);
    for (illusion::u32 i = 0; i < count && i < actions.size(); ++i)
    {
        if (actions[i].phase == illusion::AiActionPhase::Started && actions[i].name == "bite")
        {
            static_cast<void>(ai->finishAction(actions[i].entity, actions[i].action, bite(actions[i].entity)));
        }
    }
    static_cast<void>(ai->setEntity(creature, "target", player));
    if (jumped)
    {
        static_cast<void>(ai->reportNoise({.position = playerPosition, .radius = 8.0f, .instigator = player}));
    }
    std::array<illusion::AiPerceived, 8> seen{};
    const illusion::u32 seenCount = ai->perceived(creature, seen).valueOr(0u);
    showAlert(seenCount > 0);
}
```

The manual page [AI](../manual/ai.md#from-a-module) describes the trees, the actions and the perception.

## CameraService

The virtual cameras of the game (ADR 0064). Cameras are Entities with a `VirtualCamera`; the **brain** on the
real `Camera` shows the one with the highest priority and blends when it changes. Your changes are seen by the
next Cameras step. A call on an Entity that is not alive or lacks the Component it needs returns an error;
nothing asserts.

| Function | Purpose |
| --- | --- |
| `activate`, `deactivate` | `activate` raises a `VirtualCamera` above every other one (a disabled camera stays disabled) and remembers its priority; `deactivate` gives the priority back |
| `setPriority` | Sets the priority; this ends an activation |
| `setFollow`, `setLookAt` | Sets the Entity the body follows or the aim looks at; a null Entity clears it |
| `setAngles` | Turns the camera's `CameraOrbit` (or else `CameraSpringArm`) to a yaw and a pitch in radians; the pitch limits apply |
| `swapShoulder` | Swaps the `CameraSpringArm`'s side and returns the new one (-1 left, 1 right); the arm glides over by its `sideDamping` |
| `setFieldOfView` | Sets the vertical field of view in radians, in (0, pi); ease it over frames for an aim zoom |
| `shake` | Starts a `CameraShake` (origin, strength, radius, duration) that every live camera within the radius gets |
| `playImpulse` | Plays the `CameraImpulse` of an Entity once, at its position, whatever its `playOnStart` |
| `liveCamera` | The virtual camera a `CameraBrain` shows; a null Entity before it shows one |
| `blendProgress` | How far the brain's blend has come, from 0 to 1; 1 when none runs |

Every function returns an `Expected<..., CameraError>`; `cameraErrorName()` gives the name:

| Error | When |
| --- | --- |
| `InvalidArgument` | A value is not finite or out of range: a field of view outside (0, pi), a shake with a non-positive radius or duration, a dead target Entity |
| `TooManyShakes` | `CameraWorldDesc::maxShakes` shakes run already |
| `NoCamera` | The Entity is not alive or has no `VirtualCamera` |
| `NoBrain` | The Entity is not alive or has no `CameraBrain` |
| `NoStage` | The camera has no `CameraOrbit` or `CameraSpringArm` (no `CameraSpringArm` for `swapShoulder`) |
| `NoImpulse` | The Entity is not alive or has no `CameraImpulse` |
| `NotActivated` | `deactivate` on a camera that `activate` did not raise |

`activate` and `setPriority` write the saved `priority` of the `VirtualCamera`, and `swapShoulder`,
`setFollow`, `setLookAt` and `setFieldOfView` its other fields, as the Components of the World hold them.

```cpp
if (illusion::CameraService* cameras = context.service<illusion::CameraService>())
{
    static_cast<void>(cameras->activate(cutsceneCamera));
    static_cast<void>(cameras->setLookAt(cutsceneCamera, boss));
    if (swapPressed)
    {
        static_cast<void>(cameras->swapShoulder(shoulderCamera));
    }
    static_cast<void>(cameras->setFieldOfView(shoulderCamera, aiming ? 0.7f : 1.05f));
    if (hit)
    {
        static_cast<void>(cameras->shake({.origin = hitPosition, .strength = 0.8f, .radius = 12.0f}));
    }
    if (cameras->blendProgress(brain).valueOr(1.0f) >= 1.0f)
    {
        static_cast<void>(cameras->deactivate(cutsceneCamera));
    }
}
```

The manual page [Cameras](../manual/cameras.md#from-a-module) describes the shots, the stages and the brain.

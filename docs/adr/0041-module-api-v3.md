# 0041. Module API v3: start and stop per World, a multi-world host and host service interfaces

- Status: Accepted
- Date: 2026-10-30

## Context

ADR 0027 gave modules one World: `ModuleHost(World&, JobSystem&)` passes it to `load`, `unload` and
`update`, and a module creates its game state in `load`. The editor (M7) keeps an edit World and, from M7r
on, a play World per session, plus preview Worlds for assets. Three problems follow:

- **Lifecycle.** A module that spawns its entities in `load` spawns them into whichever World the host was
  built with, and again after every hot reload unless it checks first. Nothing tells a module that a play
  session begins or ends, and nothing stops `update` from running on the edit World.
- **Stripping.** Unloading or reloading a module removes the Components of its types from the host's one
  World before their code goes away. Components of those types in any other World keep lifetime functions
  that point into a closed library and crash on the next structural change.
- **The scripting API.** A shared module contains its own copy of every engine library it links (ADR
  0003). ADR 0027 told modules to `LINK illusion::physics` and to reach the host's `PhysicsWorld` through
  `context.service<PhysicsWorld>()`. That calls the module's copy of the physics code on the host's
  object: Jolt's allocator and factory are installed only in the copy that constructed the `PhysicsWorld`,
  and objects that module-copy code creates (a mesh shape) carry vtables into the module, which dangle
  after a reload. Only `illusion::core` is safe to have twice, because its process-wide state is installed
  from the host (ADR 0027).

Options considered for the scripting API:

- **Engine shared libraries in editor builds.** One copy of everything, but it contradicts the static core
  of the brief and doubles the build matrix (rejected in ADR 0027 already).
- **C function tables per library.** Stable across compilers, but every function needs a hand-written
  thunk, and nothing checks that a table is complete.
- **Abstract C++ interfaces implemented by the host.** A virtual call dispatches through the object's
  vtable, which lives in the binary that created the object: the host. Module code calling the interface
  runs host code on host objects. Both sides are built with the same compiler and settings anyway (the
  build key), so the vtable layout matches. A class that implements a pure interface does not compile
  until it implements all of it, which makes test proxies complete by construction.

## Decision

- **Lifecycle (`kModuleApiVersion = 3`).** `ModuleApi` gains optional `start(ModuleContext&)` and
  `stop(ModuleContext&)`.
  - `load` and `unload` register types and handlers only. They see no World: `ModuleContext::world()`
    asserts there ("use start()"); `hasWorld()` tells.
  - `start` runs once for every Simulation World the module meets: when the World is attached while the
    module is loaded, or when the module loads while the World is attached. `stop` runs once when either
    goes away. A hot reload calls neither: the session's state is in the World's Components.
  - `update` runs once per Simulation World per `ModuleHost::update()`, never on Edit or Preview Worlds.
  - `makeModuleApi(name, version, ModuleCallbacks{...}, dependencies)` builds the description; the
    five-argument form without `start` and `stop` stays.
- **Multi-world host.** `ModuleHost(JobSystem&, ModuleHostDesc)` holds no World of its own.
  `attachWorld(World&, WorldRole, ServiceSet)` and `detachWorld(World&)` add and remove Worlds; the roles
  are `Edit`, `Simulation` and `Preview`. Stripping a module's Components on unload, on a failed load and
  on reload runs over every attached World before the types are orphaned. `update(f64)` steps every
  Simulation World in attach order, modules in load order within each; `update(World&, f64)` steps one
  (the M7r `GameSession` calls this). A World must be detached before it is destroyed, and a detached World
  must not keep Components of a module that unloads later.
- **Services.** A `ServiceSet` maps `typeHash<T>()` to an object. `ModuleContext::service<T>()` looks in the
  set of the World the call is for first, then in the host's set (`ModuleHost::provide()`), so each play
  World gets its own physics while host-wide services stay shared.
- **Host service interfaces (M7q-2).** The scripting API of the engine libraries is a set of abstract
  classes without data members, in header-only targets that link only `illusion::core`: `PhysicsService`
  (`illusion::physics_api`), `AudioService` (`illusion::audio_api`), `InputService` (`illusion::input_api`)
  and `SceneService` (`illusion::runtime_api`). The engine libraries implement them with adapters over
  `PhysicsWorld`, `AudioWorld`, `InputActions` and the scene loader; the host creates the adapters and puts
  them in a World's `ServiceSet`. Everything that allocates or holds engine state is created by host code
  behind the interface; modules hold only plain handles. Adding a function to an interface is an
  incompatible change of the module API.
- **Linking.** `illusion_add_module(<target> ... USES physics audio input render runtime)` links the
  matching `illusion::<name>_api` targets: the public headers of that library, its plain-data Components
  (`render_api` exposes the GPU-free `SceneComponents.hpp`) and, from M7q-2, its service interface, without
  its code. A shared module that names an engine library (`illusion::*` or `illusion_*`) under `LINK`
  fails at configure time; shared modules link only `illusion::core` and third-party code. Static modules
  may `LINK` engine libraries, since they share the game's single copy.

This supersedes ADR 0027's single World per host, its "the host works on one World" consequence and its
guidance to `LINK` engine runtime libraries into shared modules. The rest of ADR 0027 (entry point, build
key, type ownership, shadow copies, hot reload and rollback) stands.

## Consequences

- Modules written for API v2 must move World work from `load` to `start` and rebuild; the host rejects
  version 2 with a clear message.
- The editor can keep module types alive in the edit World and run game code only in play Worlds; M7r
  builds `GameSession` on `attachWorld` and `update(World&, f64)`.
- Calling a non-inline function of an engine library from a shared module is a link error instead of a
  latent crash, because the API targets carry headers only.
- Each service call costs one indirect call; the `services/virtual-call` Benchmark keeps it under 10 ns.
- Service interfaces are part of the module ABI: changing one changes `kModuleApiVersion`.
- Revisit if modules must load across compiler versions; that needs C function tables instead of vtables.

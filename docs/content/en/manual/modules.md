# Game Modules and Hot Reload

Game code lives in modules: libraries with one entry point that the engine loads, runs every frame and,
during development, reloads while the game keeps running. The same module is linked statically into an
exported game, so shipped builds have no loader at all.

Headers: `<illusion/core/Module.hpp>` (the module side), `<illusion/modules/ModuleHost.hpp>` (the host)

Library: `illusion::modules` (the host); shared modules link only `illusion::core` and the API targets
they `USES`

## Writing a module

```cpp
// game/Tetris.cpp
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>

namespace tetris
{
struct Piece
{
    illusion::i32 column = 0;
    illusion::f32 fall = 0.0f;
};
} // namespace tetris

namespace
{

bool load(illusion::ModuleContext&)
{
    // Register every Component type of the module here, so that a reload can take them over.
    illusion::reflect<tetris::Piece>("tetris.Piece").field("column", &tetris::Piece::column);
    return true;
}

void start(illusion::ModuleContext& context)
{
    // A game session begins on this World: spawn what the game needs.
    context.world().create(tetris::Piece{});
}

void update(illusion::ModuleContext& context, illusion::f64 deltaSeconds)
{
    context.world().query<tetris::Piece>().each(
        [&](tetris::Piece& piece) { piece.fall += static_cast<illusion::f32>(deltaSeconds); });
}

constexpr illusion::ModuleApi kTetris =
    illusion::makeModuleApi("tetris", "1.0.0", {.load = &load, .start = &start, .update = &update});

} // namespace

ILL_MODULE(tetris, kTetris)
```

A module is a `ModuleApi`: a name, a semantic version and its functions, given to `makeModuleApi()` as
`ModuleCallbacks`. `makeModuleApi()` fills in the engine version and build settings it is compiled with,
and `ILL_MODULE()` exports it. The form `makeModuleApi(name, version, &load, &unload, &update)` is a
shorthand for modules without `start` and `stop`.

| Function | Called | Typical work |
| --- | --- | --- |
| `load` (required) | After the module is loaded, and after each reload | Register and reflect Component types, subscribe handlers; return `false` to refuse |
| `unload` (optional) | Before the module is unloaded or reloaded | Unsubscribe event handlers, release what points into the module |
| `start` (optional) | Once per Simulation World: when it is attached, or when the module loads while it is attached | Spawn the game's entities |
| `stop` (optional) | Once per Simulation World that started: before it is detached or the module unloads | Save results, release session resources |
| `update` (optional) | Every `ModuleHost::update()`, once per Simulation World | The game logic |

`load` and `unload` see no World: `context.world()` asserts there ("use start()") and `context.hasWorld()`
is `false`. A hot reload calls `unload` and `load` but neither `stop` nor `start`, so a running game keeps
its session.

The `ModuleContext` gives access to the World of the call, the host's `JobSystem` and services:
`context.service<T>()` returns the service of type `T` that the World was attached with, else the one the
host passed to `provide()`, else null.

## Building

```cmake
# Shared in development builds, static when ILLUSION_SHARED_MODULES is OFF (exported games).
illusion_add_module(tetris SOURCES game/Tetris.cpp USES physics render)
```

`illusion_add_module()` builds a shared library while `ILLUSION_SHARED_MODULES` is `ON` (the default),
and a static library otherwise. `SHARED` or `STATIC` forces one. A shared module carries its own copy of
everything it links and exports nothing but its entry point.

`USES` names the engine libraries whose headers and plain-data Components the module needs: `physics`,
`audio`, `input`, `render` (the saved, GPU-free Components of `SceneComponents.hpp`) and `runtime`. Each
links the header-only target `illusion::<name>_api`: the library's headers, its Components and its service
interface (`PhysicsService`, `AudioService`, `InputService`, `SceneService`), never the library's code.

!!! warning "Shared modules do not link engine libraries"
    A copy of an engine library inside a shared module would run on the host's objects with its own
    allocators, factories and vtables, which dangle after a reload (ADR 0041). `LINK` of an engine library
    other than `illusion::core` in a shared module is therefore a configure error. Use `USES` for the
    headers and reach the code through the host's services. `LINK` remains for third-party libraries, and
    for engine libraries in static modules.

## Engine services

A shared module reaches the engine libraries only through the host's service interfaces, the
[Scripting API](../reference/scripting-api.md). Each is an abstract class that the host implements with an
adapter over its own engine object, so every call runs host code on host objects and what it creates
(physics geometry, audio clips, entities from a scene file) stays valid across hot reloads:

```cpp
// Host: one adapter per engine library, given to the play World.
illusion::PhysicsWorldService physicsService(physics);
illusion::AudioWorldService audioService(audio);
illusion::SceneLoaderService sceneService(world, files);
illusion::ServiceSet services;
services.provide<illusion::PhysicsService>(physicsService);
services.provide<illusion::AudioService>(audioService);
services.provide<illusion::SceneService>(sceneService);
modules.attachWorld(world, illusion::WorldRole::Simulation, std::move(services));
modules.provide<illusion::InputService>(inputService); // one InputActions for the whole game

// Module: always check the pointer; a World may come without the service.
if (illusion::PhysicsService* physics = context.service<illusion::PhysicsService>())
{
    physics->addImpulse(player, illusion::Vec3(0.0f, 5.0f, 0.0f));
}
```

Calling a non-inline engine function directly from a shared module is a link error, by design.

## Running modules

```cpp
illusion::ModuleHost modules(jobs);
illusion::ServiceSet services;
services.provide(settings); // modules call context.service<GameSettings>()
modules.attachWorld(world, illusion::WorldRole::Simulation, std::move(services));
if (const std::optional<std::string> error = modules.load("build/game/libtetris.so"))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
while (running)
{
    modules.reloadChanged(); // picks up a rebuilt library
    modules.update(frameSeconds);
}
modules.detachWorld(world);
```

Games rarely wire this by hand: a [game session](runtime.md) attaches its World with the physics, audio,
input and scene services and updates the modules in its step order, in the editor's Play mode and in the
player alike.

`load()` copies the library to a shadow directory and loads the copy, so the build can overwrite the
original. It then checks the module and returns an error message if anything is wrong:

- the module API version (3; modules built for version 2 must move their World work to `start` and be
  rebuilt), the engine version (shared modules must be built against exactly this engine) and the build
  settings (compiler and its version, Debug or Release, asserts);
- a valid, unique name and a semantic version;
- its dependencies.

The host then gives the module its process-wide services (logger, Component registry, asset path table,
assert handler, thread and worker indices), calls `load` and then `start` on every Simulation World.
`unload()` and the destructor stop and unload modules, the last one first.

## Worlds

A host runs modules on any number of Worlds, each attached in a role:

| `WorldRole` | Used for | `start` / `update` / `stop` |
| --- | --- | --- |
| `Edit` | The World an editor edits | Never |
| `Simulation` | A World the game runs in: a play session, the shipped game | Yes |
| `Preview` | A World that shows an asset (a prefab, a scene) without running it | Never |

`attachWorld(world, role, services)` adds a World; on a Simulation World it starts every loaded module in
load order. `detachWorld(world)` stops them, the last loaded first, and leaves the World's entities alone.
`update(deltaSeconds)` steps every Simulation World in attach order, the modules in load order within each;
`update(world, deltaSeconds)` steps one.

Whenever a module unloads, fails to load or drops a type in a reload, the host removes the Components of
those types from every attached World before their code goes away. Detach a World before destroying it,
and do not keep Components of a module in a detached World once that module unloads.

## Hot reload

`reloadChanged()` reloads every shared module whose file changed; `reload(name)` reloads one now:

1. The old version's `unload` runs.
2. The new library loads and its `load` registers the module's Component types again: the registry hands
   them over to the new code, so existing Components keep their data.
3. Components of types that the new version no longer registers are removed from every attached World,
   while the old code is still loaded to destroy them.
4. The old library is closed.

If the new library cannot load, or its `load` fails, the old version takes its types back and keeps
running; the error is logged, and the same file is not tried again until it changes.

!!! warning "What survives a reload"
    Only data in Components survives: keep game state in the World, not in globals of the module. A
    change to the size or alignment of a module's Component type is rejected (the reload is rolled
    back); restart to apply it.

Rules for reloadable modules:

- The host owns every Component type its own code uses, and the `ModuleHost` constructor reflects the core
  Components. Reflect the Components of the other engine libraries the game uses
  (`reflectPhysicsComponents()`, `reflectAudioComponents()`, ...) before loading modules, so that no
  engine type depends on a module's code. Tools see a module's own types through the registry (by stable
  name), not as C++ types.
- Register (or reflect) every Component type of the module in `load`; spawn entities in `start`.
- Undo in `unload` what points into the module: event handlers, callbacks given to the host.
- Jobs the module submits finish before the call that submits them returns.
- Profiling builds (`ILLUSION_PROFILING`) use static modules only.

## Versions and dependencies

```cpp
constexpr std::array kNeeds{illusion::ModuleDependency{"tetris", "1.2.0"}};
constexpr illusion::ModuleApi kScores =
    illusion::makeModuleApi("tetris.scores", "0.3.0", &load, &unload, &update, kNeeds);
```

A module loads only after its dependencies, and only if each loaded dependency is compatible with the
requested version (same major version, not older; see `SemanticVersion`). A module cannot be unloaded
while another depends on it.

## Shipping

In an exported game the module is static: `ILL_MODULE(tetris, ...)` then defines
`illusionModuleEntry_tetris()`, and the game adds it:

```cpp
ILL_DECLARE_STATIC_MODULE(tetris);

illusion::ModuleHost modules(jobs);
modules.attachWorld(world, illusion::WorldRole::Simulation);
if (const auto error = modules.add(*illusionModuleEntry_tetris()))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
```

Static modules share the game's engine, so they need no services and cannot be reloaded.

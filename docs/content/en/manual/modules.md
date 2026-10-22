# Game Modules and Hot Reload

Game code lives in modules: libraries with one entry point that the engine loads, runs every frame and,
during development, reloads while the game keeps running. The same module is linked statically into an
exported game, so shipped builds have no loader at all.

Headers: `<illusion/core/Module.hpp>` (the module side), `<illusion/modules/ModuleHost.hpp>` (the host)

Library: `illusion::modules` (the host); modules only need `illusion::core`

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

bool load(illusion::ModuleContext& context)
{
    // Register every Component type of the module here, so that a reload can take them over.
    illusion::reflect<tetris::Piece>("tetris.Piece").field("column", &tetris::Piece::column);
    return true;
}

void update(illusion::ModuleContext& context, illusion::f64 deltaSeconds)
{
    context.world().query<tetris::Piece>().each(
        [&](tetris::Piece& piece) { piece.fall += static_cast<illusion::f32>(deltaSeconds); });
}

constexpr illusion::ModuleApi kTetris = illusion::makeModuleApi("tetris", "1.0.0", &load, nullptr, &update);

} // namespace

ILL_MODULE(tetris, kTetris)
```

A module is a `ModuleApi`: a name, a semantic version, and `load`, `unload` and `update` functions.
`makeModuleApi()` fills in the engine version and build settings it is compiled with, and `ILL_MODULE()`
exports it.

| Function | Called | Typical work |
| --- | --- | --- |
| `load` (required) | After the module is loaded, and after each reload | Register and reflect Component types; return `false` to refuse |
| `unload` (optional) | Before the module is unloaded or reloaded | Unsubscribe event handlers, release what points into the module |
| `update` (optional) | Every `ModuleHost::update()` | The game logic |

The `ModuleContext` gives access to the host's `World`, `JobSystem` and services: anything the host passes
to `provide()` is available as `context.service<T>()` (null if the host has none).

## Building

```cmake
# Shared in development builds, static when ILLUSION_SHARED_MODULES is OFF (exported games).
illusion_add_module(tetris SOURCES game/Tetris.cpp LINK illusion::physics)
```

`illusion_add_module()` builds a shared library while `ILLUSION_SHARED_MODULES` is `ON` (the default),
and a static library otherwise. `SHARED` or `STATIC` forces one. A shared module carries its own copy of
the engine libraries it links and exports nothing but its entry point.

## Running modules

```cpp
illusion::ModuleHost modules(world, jobs);
modules.provide(physics); // modules call context.service<illusion::PhysicsWorld>()
if (const std::optional<std::string> error = modules.load("build/game/libtetris.so"))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
while (running)
{
    modules.reloadChanged(); // picks up a rebuilt library
    modules.update(frameSeconds);
}
```

`load()` copies the library to a shadow directory and loads the copy, so the build can overwrite the
original. It then checks the module and returns an error message if anything is wrong:

- the module API version, the engine version (shared modules must be built against exactly this engine)
  and the build settings (compiler and its version, Debug or Release, asserts);
- a valid, unique name and a semantic version;
- its dependencies.

The host then gives the module its process-wide services (logger, Component registry, asset path table,
assert handler, thread and worker indices) and calls `load`. `update()` runs the modules in load order; `unload()` and
the destructor unload them, the last one first.

## Hot reload

`reloadChanged()` reloads every shared module whose file changed; `reload(name)` reloads one now:

1. The old version's `unload` runs.
2. The new library loads and its `load` registers the module's Component types again: the registry hands
   them over to the new code, so existing Components keep their data.
3. Components of types that the new version no longer registers are removed from the World, while the old
   code is still loaded to destroy them.
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
- Register (or reflect) every Component type of the module in `load`.
- Do not touch existing Components of the module's own types in `load`; do that in `update`.
- Undo in `unload` what points into the module: event handlers, callbacks given to the host.
- Jobs the module submits finish before its `update` returns.
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

illusion::ModuleHost modules(world, jobs);
if (const auto error = modules.add(*illusionModuleEntry_tetris()))
{
    ILL_LOG_ERROR("Game", "{}", *error);
}
```

Static modules share the game's engine, so they need no services and cannot be reloaded.

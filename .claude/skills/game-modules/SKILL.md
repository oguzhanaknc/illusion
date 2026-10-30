---
name: game-modules
description: Write or change illusion game modules and the scripting API - ILL_MODULE entry points, load/start/update/stop/unload, illusion_add_module USES, host service interfaces (PhysicsService, AudioService, InputService, SceneService) and their adapters, hot reload rules and the module tests. Use whenever code under a module, engine/modules, a *Service.hpp interface or tests/modules changes.
---

# Game modules and the scripting API

Read first: `docs/adr/0027-module-system-and-hot-reload.md`, `docs/adr/0041-module-api-v3.md`,
`docs/content/en/manual/modules.md` and `docs/content/en/reference/scripting-api.md`.

## Writing a module

1. One source with `ILL_MODULE(name, kApi)`; build `kApi` with `makeModuleApi(name, version,
   ModuleCallbacks{.load, .unload, .start, .stop, .update})`.
2. `load`/`unload`: register and unregister types and handlers only. They see no World
   (`context.world()` asserts; `hasWorld()` tells).
3. `start`/`stop`: once per Simulation World; spawn entities in `start`. Hot reloads call neither.
4. `update`: Simulation Worlds only. Keep all game state in Components; globals do not survive a reload.
5. Build with `illusion_add_module(<target> SOURCES ... USES physics audio input render runtime)`. A shared
   module must not `LINK` an engine library (configure error); `LINK` is for third-party code.

## Using services

- Get every service with `context.service<T>()` and **null-check the pointer** before use. GCC's
  `-Wnull-dereference` fails the CI build on an unchecked result.
- Hold handles (`PhysicsMeshHandle`, `AudioClipHandle`, `AudioVoiceHandle`, `Entity`), never pointers into
  host objects. Spans and string views the services return are valid until the next update; copy them.
- A link error for an engine function inside a module is the design working: that code belongs to the
  host. Either call it through a service or, for a trivial plain-data helper, make it `constexpr`/inline
  in its header (as `InputBinding::key()` is).

## Changing a service interface

1. Interfaces are abstract classes without data members, `NonCopyable`, with a protected constructor and
   a protected virtual destructor, documented with `///` and `@ingroup scripting_api`.
2. Arguments and results are plain values, `std::span`, `std::string_view` or `std::optional` of plain
   values. Never pass a standard container to be filled across the boundary; write into a caller span and
   return the total count.
3. Add the function to the interface, its adapter (`PhysicsWorldService`, `AudioWorldService`,
   `InputActionsService`, `SceneLoaderService`; out-of-line in the library's `src/`), the counting proxy in
   `tests/modules/CountingServices.hpp`, the call in `callServices()` of `tests/modules/TestModule.cpp`,
   and a forwarding check in `tests/modules/TestServices.cpp`.
4. Any change to an interface changes the module ABI: bump `kModuleApiVersion` in
   `engine/core/include/illusion/core/Module.hpp` and say so in the commit.
5. Update `reference/scripting-api.md` in English and Turkish.

## Testing

- `tests/modules/TestModules.cpp` loads the variants of `TestModule.cpp`; steer them through
  `test::ModuleControl` (`TestModuleShared.hpp`), which the host provides.
- Reload tests install a new variant over the library file and call `reloadChanged()`.
- Lifetime bugs across reloads show only under ASan (the Linux sanitizer job); write such tests so that
  the old library is closed before the host touches what the module created.
- `services/virtual-call` and `modules/update` in `benchmarks/BenchModules.cpp` guard the call costs.

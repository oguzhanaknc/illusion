# 0027. Game modules: a C entry point, service injection and hot reload that hands Component types over

- Status: Accepted
- Date: 2026-10-17

## Context

The project brief puts game code in a module: a hot-reloadable shared library in the editor, statically
linked in exported games. ADR 0003 made component ids and process-wide services survive the second copy
of the static engine libraries that a shared module contains, and left two things to the module system:
injecting the host's services before module code runs, and re-registering module-owned component types on
reload so that no function pointer into an unloaded module survives.

A survey of the engine's process-wide state found, besides the Logger, the ComponentRegistry and the assert
handler, two more values that a module's copy must not keep for itself: the logical thread index
(`currentThreadIndex()`) and the Job System worker index (`JobSystem::currentWorkerIndex()`), both
thread-local. Archetypes read lifetime functions through pointers into the registry's entries, so changing
an entry changes every World. Hot reload must also cope with builds that are still being written, with
broken builds, and with Windows locking loaded DLLs. Options considered: an engine shared library for
editor builds (contradicts the static core of the brief and doubles the build matrix), or C++ interfaces
across the boundary (vtables tie the layout to both sides anyway); a plain C entry point that returns a
struct of function pointers is the smallest boundary.

## Decision

- **Module side** (`illusion/core/Module.hpp`, in `illusion::core`): a module is a constant `ModuleApi`
  (API layout version, name, semantic version, the engine version and a build key it was compiled with,
  dependencies, and `installServices`, `load`, `unload`, `update` function pointers), built with
  `makeModuleApi()` and exported by `ILL_MODULE(id, api)` as `extern "C" illusionModuleEntry()` in shared
  builds or `illusionModuleEntry_<id>()` in static builds. The build key hashes the compiler and its
  version, Debug or Release, asserts and checked standard library modes: a shared module and its host must
  match, because both contain a copy of the engine.
- **Services.** Before any module code runs, the host calls the module's `installServices` with its
  Logger, ComponentRegistry, a dispatcher to its current assert handler (so later `setAssertHandler()`
  calls in the host reach modules), its thread and worker index functions, and a module id. The module's
  copy forwards `currentThreadIndex()` and `currentWorkerIndex()` to the host's. Static modules are part of
  the host and get nothing installed.
- **Type ownership.** `ComponentInfo::module` records whose code a type's lifetime functions are in (0 for
  the host). The registry can retire a module (its types are taken over by the next registration of the
  same type from another module), restore a retired module or single types (rollback), list a module's
  types, and orphan them (their lifetime functions become fatal errors until someone registers them again).
  A takeover with a different size or alignment is not applied but recorded as a layout conflict.
- **Host** (`illusion::modules`, `ModuleHost`): loads a shared module from a copy in a shadow directory
  (the build can overwrite the original, Windows does not lock it, and a reload never gets the cached image
  back), validates the API version, engine version, build key, name, version and dependencies (semantic
  versions, `SemanticVersion::isCompatibleWith`), installs services and calls `load`. A failed load, a
  layout conflict or an unload removes the module's Components from the World while its code is still
  loaded, then orphans its types and closes it. Modules update in load order; unloading a module that
  another depends on is refused.
- **Hot reload** (`reload()`, or `reloadChanged()` on a new file time): snapshot the old version's types,
  call its `unload`, retire it, open and validate the new library, install services and call its `load`,
  which re-registers the module's types and takes them over. On success the types the new version did not
  register are stripped from the World and orphaned, and the old library closes. On failure (open,
  validation, `load`, layout conflict) the old version gets its types back and loads again; the error is
  logged and the file time remembered, so a broken build is retried only when it changes again.
- **Build**: `illusion_add_module()` (cmake/IllusionModules.cmake) builds a `MODULE` library with hidden
  visibility, exporting only the entry point (`--exclude-libs,ALL` and `-Bsymbolic` on ELF, an export list
  on Mach-O), or a static library; `ILLUSION_SHARED_MODULES` picks the default. Profiling builds use static
  modules, since a second Tracy client cannot run in the process.

## Consequences

- Game state must live in Components: a module's globals do not survive a reload. Modules must register
  their types in `load`, undo in `unload` whatever points into them (event handlers, callbacks), and finish
  their jobs inside `update`.
- Changing the size or alignment of a module's Component type needs a restart; the reload is rolled back
  with a message. A reflection-based migration that rebuilds those Components would lift this (ROADMAP).
- The host works on one World; the editor's play and edit worlds will need the stripping step for each.
- `currentThreadIndex()` and `currentWorkerIndex()` cost one relaxed atomic load more.
- Shared modules work with ASan and TSan builds, and the module tests run in CI on every platform.

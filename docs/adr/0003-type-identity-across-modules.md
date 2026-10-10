# 0003. Type identity and process-wide services across modules

- Status: Accepted
- Date: 2026-10-10

## Context

The core library is static. In the editor, game code is a hot-reloadable shared library that also links
the core library, so the process contains two copies of every global variable and every template static.
Component ids, the Logger and the assert handler must nevertheless be shared, or a Component registered
by the editor and the same Component used by the game module would get different ids.

Per-type counters (a template static incremented on first use) are unique per binary, not per process,
and RTTI names are not guaranteed to match across modules or compilers.

## Decision

1. Component types are identified by `fnv1a64(typeName<T>())`, a hash of the fully qualified type name
   computed at compile time. The process-wide `ComponentRegistry` maps the hash to a dense
   `ComponentId` and stores a copy of the name and the type's lifetime functions.
2. Process-wide services (`Logger`, `ComponentRegistry`, the assert handler) are reached through an
   instance pointer that can be replaced (`setInstance()`, `setAssertHandler()`). The module loader will
   inject the host's instances into a game module before the module runs any code.
3. `componentId<T>()` caches the id in a function-local static, so it is valid per module as long as the
   registry instance is set before first use and never changed afterwards.

## Consequences

- Component types must have unique fully qualified names: no anonymous namespaces, no local classes. Hash
  collisions and layout mismatches are detected at registration and are fatal.
- Template argument spelling differs between compilers, so these names are not used for serialization;
  the reflection system will provide explicit, stable names.
- The registry stores lifetime function pointers from the module that registered a type first. Before
  hot reload ships, the module system must re-register module-owned types on reload so that no pointer
  into an unloaded module survives. This is tracked in ROADMAP.md.

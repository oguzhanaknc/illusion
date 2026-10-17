# 0023. Jolt Physics on the Job System, ECS-driven bodies and a fixed step

- Status: Accepted
- Date: 2026-10-17

## Context

The engine needs rigid-body physics that is fast, multithreaded, deterministic and permissively
licensed, and that fits the ECS: game code and the editor work with Components, not with a separate
physics scene graph. Jolt Physics (MIT) was chosen in the project brief.

## Decision

- **Private dependency:** Jolt is linked privately by `illusion::physics`; no Jolt type appears in a
  public header. It is built as a library only, with the engine's runtime and flags, an SSE4.2 baseline
  on x86-64 (every CPU Steam supports) and without GPU compute, the debug renderer, the profiler or
  object streams.
- **Engine Job System:** a `JPH::JobSystemWithBarrier` adapter submits Jolt jobs to the engine
  `JobSystem` (no second thread pool). The waiting thread executes barrier jobs too; `Job::Execute()`
  runs a job once, so double scheduling is safe. The adapter waits for its outstanding engine jobs
  before it is destroyed.
- **ECS-driven bodies:** `Transform` + `RigidBody` + `Collider` describe a body. `PhysicsWorld::update()`
  creates bodies for new Entities (adding a runtime `PhysicsBody` Component), removes bodies of
  destroyed Entities or Entities that lost a Component, makes static and kinematic bodies follow their
  Transforms and teleports dynamic bodies whose Transform game code changed (detected by comparing
  with the pose last written). Property changes apply on `rebuild()`.
- **Fixed step with interpolation:** `FixedTimestep` (60 Hz, at most 4 steps per update); dynamic bodies
  write Transforms interpolated between the last two steps.
- **Layers:** 16 collision layers with a symmetric collision matrix; object layer = layer × 2 + moving,
  with two broad-phase layers (static, moving).
- **Events and queries:** contact Begin/End events are collected into a preallocated buffer from
  simulation threads (atomic index, no allocation) and sorted after the update. Entity ids are stored
  in body user data and in a per-body-index slot table that stays valid after a body is removed.
  Raycasts skip sensors and filter by a layer mask.

## Consequences

- Results are deterministic for the same inputs regardless of the worker count (tested); they are not
  cross-platform deterministic (that needs Jolt's `CROSS_PLATFORM_DETERMINISTIC`, a later decision).
- Rendering lags the simulation by up to one step because of interpolation.
- Mesh and convex-hull colliders, character controllers, joints and shape casts are not exposed yet;
  they are planned in ROADMAP.md.
- Per-frame synchronization is linear in the number of bodies (no change tracking yet); the physics
  step benchmark keeps the total cost visible.

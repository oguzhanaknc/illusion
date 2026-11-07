# 0047. Particle effects: a saved emitter Component, a deterministic CPU simulation, GPU billboards

- Status: Accepted
- Date: 2026-11-07

## Context

The engine has no particle effects: no smoke, sparks, dust or magic. Games need them, and the editor and AI
agents must be able to create and tune them like any other Component. Particles must follow the engine's
rules:
- no per-frame allocations;
- work on the Job System;
- data-oriented storage;
- deterministic fixed-step play sessions (ADR 0042), whose state hash includes the scene;
- GPU-free saved Components that the scene, prefab and Inspector code handle through reflection
  (ADR 0035).

Options considered:

- **GPU-only simulation in compute shaders.** It scales to millions of particles, but headless and no-GPU
  sessions (CI, agents with `--no-gpu`) would have no particles at all. Tests could not read particle
  state without readbacks, and determinism across GPUs is not guaranteed.
- **A CPU simulation in the renderer.** It is simple, but the editor and the game runtime would need the
  renderer just to advance particles, and the simulation would run once per rendered view instead of once
  per frame.
- **A separate CPU library that simulates, with the renderer only drawing.** It is deterministic, testable
  without a GPU, and runs once per session update. Very large emitters can move to a GPU path later without
  changing the Component.

## Decision

- A new library, `illusion::particles`, depends only on `illusion::core`. It owns:
  - the saved Component `ParticleEmitter` (`illusion.ParticleEmitter`);
  - the runtime Component `ParticlePool` (`illusion.ParticlePool`, transient);
  - `ParticleWorld`, which simulates every emitter of a World.
- `ParticleEmitter` is plain data:
  - capacity, emission rate, burst, looping and duration;
  - lifetime and speed ranges;
  - an emission shape (point, sphere, box) with a spread angle around local +Y;
  - gravity factor and drag;
  - size and colour at birth and death (HDR colours);
  - blending (additive or alpha), simulation space (world or local), a seed and an optional texture
    (`AssetRef`).
- `ParticlePool` is created by `ParticleWorld` on an emitter's first update. It stores the particles as
  structure-of-arrays in one block of `maxParticles` slots, allocated once (when the capacity changes,
  again). Dead particles are swapped with the last live one, so live particles are always contiguous.
- `ParticleWorld::update()` runs the emitters as parallel Jobs, one per Chunk (`Query::parallelEach`), and
  never allocates once the pools exist. Each pool has its own `Random` seeded from `ParticleEmitter::seed`, so the same inputs give
  the same particles, whatever the thread count. Emitters on `Inactive` entities do not update.
- `GameSession` advances particles after physics and the transform hierarchy, with the session's gravity.
- The renderer (PFX-2) draws the live particles of each pool as instanced camera-facing billboards after the
  blended geometry, in the HDR target, so bright particles feed bloom. Colour and size are interpolated from
  each particle's age in the vertex shader, so the pools store no colours.

## Consequences

- Particles work in headless sessions and in tests, and fixed-step sessions repeat exactly.
- The CPU cost grows with the particle count: a few hundred thousand particles per frame. The benchmark
  `particles/update` guards it. A GPU simulation path for very large emitters is planned (PFX-4) behind the
  same Component.
- Uploading the live particles to the GPU each frame costs bandwidth that a GPU simulation would avoid.
  Until PFX-4, this is the price of a single deterministic simulation.
- A new saved Component needs a value for every field in the showcase scene (gate test), editor
  registration and docs, like every Component.

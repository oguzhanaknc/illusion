# Particles

Particle effects are ordinary Entities with a `ParticleEmitter`: sparks, smoke, dust, magic. `ParticleWorld`
emits, moves and retires their particles on the CPU, deterministically and without allocating once an
effect has started, so effects work in headless sessions, in tests and in repeatable fixed-step play
sessions (ADR 0047). `SceneRenderer` draws the live particles (see [Drawing](#drawing)).

Headers: `<illusion/particles/ParticleWorld.hpp>`, `<illusion/particles/ParticleComponents.hpp>`

Library: `illusion::particles`

## Getting started

```cpp
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/particles/ParticleWorld.hpp>

illusion::reflectParticleComponents(); // once, before scenes are loaded

illusion::World world;
illusion::ParticleWorld particles(world);

illusion::ParticleEmitter sparks;
sparks.rate = 60.0f;                                // particles per second
sparks.lifetimeMin = 0.4f;
sparks.lifetimeMax = 0.9f;
sparks.speedMin = 1.5f;
sparks.speedMax = 3.5f;
sparks.spread = 0.6f;                               // radians around local +Y
sparks.gravityFactor = 0.6f;
sparks.colorStart = illusion::Vec4(6.0f, 3.0f, 1.0f, 1.0f); // HDR: glows through bloom
sparks.colorEnd = illusion::Vec4(2.0f, 0.3f, 0.05f, 0.0f);
world.create(illusion::Transform{}, sparks);

particles.update(1.0f / 60.0f); // every frame; pass a JobSystem* to run the emitters in parallel
```

In a game session, `GameSession::particles()` is advanced after physics and the transform hierarchy, with
the physics gravity.

## Emitters

| Field | Meaning |
| --- | --- |
| `maxParticles` | Live particles at most (1 to 65536); changing it starts the emitter's particles afresh. |
| `rate`, `burst` | Particles per second while emitting, and particles emitted at once when it starts. |
| `loop`, `duration` | Emit for ever, or for `duration` seconds. |
| `playOnStart` | Start on the first update; otherwise wait for `ParticleWorld::play()`. |
| `lifetimeMin`, `lifetimeMax` | Seconds a particle lives, chosen at random in the range. |
| `speedMin`, `speedMax` | Initial speed in m/s, chosen at random in the range. |
| `shape`, `radius`, `boxExtents` | Where particles are born: `Point`, `Sphere` (radius) or `Box` (half size). |
| `spread` | Half angle in radians of the initial directions around local +Y (π: every direction). |
| `gravityFactor`, `drag` | Scales the world's gravity (negative rises); fraction of the velocity lost per second. |
| `sizeStart`, `sizeEnd` | Width in m at birth and at death. |
| `colorStart`, `colorEnd` | Linear colour and alpha at birth and at death; values above 1 glow. |
| `blend` | `Additive` (light: fire, sparks) or `Alpha` (covers: smoke, dust). |
| `space` | `World`: particles stay where they were born; `Local`: they move with the emitter. |
| `texture` | Sprite texture; none draws a soft round dot. |
| `seed` | Equal seeds give equal particles. |

Colour and size are interpolated from the particle's age; `particleColor()` and `particleSize()` give the
values for a fraction of the life.

## Playing and stopping

```cpp
particles.play(entity);   // starts again from the beginning: the burst fires, a timed effect runs again
particles.stop(entity);   // stops emitting; the live particles finish their lives
particles.clear(entity);  // removes the live particles at once
const illusion::u32 alive = particles.aliveCount(entity);
```

`play()` replays an effect exactly: every start draws the same random numbers from `seed`. Emitters on
Inactive entities pause.

## Drawing

`SceneRenderer` draws every live particle as a quad facing the camera, after the blended Meshes and into the
HDR image, so colours above 1 glow through bloom:

- `Additive` particles add their light and need no order. `Alpha` particles cover what is behind them: the
  emitters are drawn from the farthest to the nearest, and so are the particles of each.
- The renderer reads the scene depth: a particle behind a surface is not drawn, and one close to it fades
  out over the last 30 cm (soft particles), so smoke does not cut sharp lines into the ground.
- A `texture` is resolved by `RenderAssetResolver` into the runtime Component `RenderParticles`, like the
  Meshes of a `MeshRenderer`; without one, a particle is a soft round dot.
- Colour and size come from the particle's age, computed when the particles are gathered for the frame.

`RenderStats::particles` counts the particles drawn. The GPU benchmark `render/100k-particles` measures a
full frame with 100,000 of them.

## The particle pool

`ParticleWorld` gives each emitter a `ParticlePool` (runtime state, not saved). Particles `[0, count)` are
alive, in structure-of-arrays form: `positions(axis)`, `velocities(axis)`, `ages()` and `lifetimes()`. In
`World` space they are world positions; in `Local` space they are in the emitter's space. The pool is
allocated once per capacity; a dead particle is replaced by the last live one.

## Performance

`ParticleWorld::update()` runs the emitters as parallel Jobs on the Job System and does not allocate once
the pools exist. The benchmark `particles/update` measures the cost per live particle.

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
| `simulation` | `Cpu` (default) or `Gpu`: see [GPU simulation](#gpu-simulation). |

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

## In the editor

The Scene view previews every emitter of the edit World: `EditorContext` advances them each frame, so an
effect plays while you tune it in the Inspector. While a play session runs, the play World's emitters are
the game's and the session advances them. Particles are runtime state: the preview never changes the scene
file or its hash.

The `particles.*` [Editor commands](../reference/editor-commands.md) drive the emitters of the active World
(the play World while playing) for people and agents; they are not undoable:

| Command | What it does |
| --- | --- |
| `particles.play`, `particles.stop`, `particles.clear` | Start an emitter again, stop it emitting, remove its particles |
| `particles.emit` | A burst of `count` particles now; `emitted` says how many fit |
| `particles.stats` | Emitters, those that emit and their live particles (one emitter with `entity`) |

```json
{"method": "particles.emit", "params": {"entity": "/Sparks", "count": 40}}
{"method": "particles.stats", "params": {}}
```

## Game modules

Game code reaches the particles of its World through `ParticleService` (`USES particles`, see the
[scripting API](../reference/scripting-api.md#particleservice)):

```cpp
#include <illusion/particles/ParticleService.hpp>

void onLanding(illusion::ModuleContext& context, illusion::Entity dust)
{
    if (illusion::ParticleService* particles = context.service<illusion::ParticleService>())
    {
        particles->play(dust);          // the dust cloud from the beginning
        if (particles->aliveCount(dust) == 0)
        {
            particles->emit(dust, 20);  // and a puff right now
        }
    }
}
```

## GPU simulation

With `simulation` set to `Gpu`, an emitter's particles live on the GPU: the renderer ages, moves, retires and
spawns them in compute shaders and draws them from their state buffer with an indirect draw, so a frame of a
million particles costs a millisecond or two instead of a CPU pass and an upload (GPU benchmark
`render/1m-gpu-particles`). Each emitter holds up to 65,536 particles.

`ParticleWorld` still keeps the emitter's time: every update publishes one step (its births and seconds,
numbered), and every view that draws the emitter simulates each new step. The step number seeds the births,
so the Scene view and the Game view show the same particles. In exchange:

- The particles are not in `ParticlePool`: code and tests cannot read their positions. `aliveCount()` and
  `particles.stats` report the count a view read back from the GPU a few frames ago.
- They move only where a view draws them, and a view that has not drawn an emitter for a while catches up by
  a few steps, not all of them.
- Alpha particles are drawn in the order they live in, without sorting.

Use the CPU simulation for gameplay effects that code reads or that must repeat exactly in tests; use the GPU
for large, decorative ones: fireflies, snow, rain, embers.

## The particle pool

`ParticleWorld` gives each emitter a `ParticlePool` (runtime state, not saved). Particles `[0, count)` are
alive, in structure-of-arrays form: `positions(axis)`, `velocities(axis)`, `ages()` and `lifetimes()`. In
`World` space they are world positions; in `Local` space they are in the emitter's space. The pool is
allocated once per capacity; a dead particle is replaced by the last live one.

## Performance

`ParticleWorld::update()` runs the emitters as parallel Jobs on the Job System and does not allocate once
the pools exist. The benchmark `particles/update` measures the cost per live particle.

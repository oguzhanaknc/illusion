# 0049. Day and night: an analytic sky drawn per pixel, its sun as the main light, a small cube map redrawn in place

- Status: Accepted
- Date: 2026-11-07

## Context

A day and night cycle needs a sky that changes every frame. The existing procedural sky (`ProceduralSky`)
is an image made on the CPU and baked into cube maps, spherical harmonics and prefiltered reflections: tens
of milliseconds, a new environment per change. The sun's light (a `DirectionalLight`) is separate, so a game
would have to keep the light, the sky and the image-based lighting in step by hand.

Options considered:

- **Rebake the procedural sky when the time changes.** Simple, but too slow for every frame, and every bake
  makes a new environment and binding sets.
- **A physically based sky with precomputed scattering tables (Bruneton, Hillaire).** The best quality,
  but several compute passes and lookup tables, and much more code to keep correct.
- **An analytic single-scattering model** (Rayleigh and Mie along a Kasten-Young air mass, with ozone
  absorption), evaluated per pixel for the background and on the CPU for diffuse light. A few dozen
  instructions; good enough for a stylised but natural sky.

## Decision

- `SkyAtmosphere` (`illusion.SkyAtmosphere`, a saved singleton render Component) holds the time of day, the
  sun's path (azimuth, tilt), the moon's offset, haze, and the sun, moon and star brightness.
- The model lives twice, in `assets/Atmosphere.cpp` (CPU: tests, spherical harmonics, game code) and
  `render/shaders/Atmosphere.slang` (GPU), and the two are kept in step.
- The skybox evaluates the model per pixel and adds the sun disc, the moon with its phase, procedural stars
  and a Milky Way band. The renderer adds the sun (by day) or the moon (by night) as the first, shadowed
  directional light, fading through dusk so the light never jumps.
- Each SceneRenderer owns one environment made by `RenderResources::createAtmosphereEnvironment()`: cube
  maps with binding sets made once, redrawn in place by `updateAtmosphereEnvironment()` (sky, mip chain,
  prefiltered reflections) only when the sky changed. Diffuse light comes from `atmosphereIrradiance()` on
  the CPU (512 directions).

## Consequences

- A moving sky costs a few small compute passes and a little CPU time per frame, and no allocations; a still
  one costs nothing after its first frame.
- No multiple scattering: deep twilight is darker and less colourful than in reality, and haze does not
  brighten the shadowed side of the sky.
- The two copies of the model must change together; the CPU tests describe its behaviour.
- `ProceduralSky` and HDRI SkyLights stay for scenes that do not need a moving sun.

# A dissolving surface

This tutorial writes a material shader that dissolves: noise decides where a crate is cut away as a number
grows, and the edge of the cut glows. You write the shader, make a material that uses it, see it in the Scene
view, change it while the editor runs, and let an agent check it. There is no C++ in it. The
[Material shaders manual](../manual/material-shaders.md) explains every part used here.

You need the editor and a project (the **3D Starter** template is enough).

## 1. What you build

- A shader `assets/shaders/dissolve.shader` with a `progress` parameter between 0 (whole) and 1 (gone), a glowing
  edge colour and a noise scale.
- A material `assets/materials/dissolve.material` that uses it, with `alphaMode` set to `Mask`, because the
  shader cuts pixels away.
- A cube that wears the material.

## 2. Write the shader

Create `assets/shaders/dissolve.shader` with the text below. The Asset Browser lists it as a **Shader**, and the
editor compiles it as soon as the file is saved.

```slang
// A surface that dissolves: noise decides where the material is cut away as progress grows, and the cut
// edge glows. Use it with a material whose alphaMode is "Mask".
struct Parameters
{
    [Default(0.4)] [Range(0, 1)] float progress;
    [Color(1, 0.5, 0.1, 1)] float3 edgeColor;
    [Default(0.06)] float edgeWidth;
    [Default(6)] float scale;
};

SurfaceOutput surface(SurfaceInput input)
{
    const Parameters p = parameters(input);
    const float n = fractalNoise(input.worldPosition * p.scale, 3);
    const float edge = 1.0 - saturate((n - p.progress) / max(p.edgeWidth, 0.001));
    SurfaceOutput output = defaultSurface();
    output.baseColor = float3(0.6, 0.55, 0.5);
    output.roughness = 0.8;
    output.emission = p.edgeColor * edge * 4.0;
    output.opacity = n;
    output.opacityMask = p.progress;
    return output;
}
```

How it works:

- `fractalNoise()` gives a value in [0, 1] for every point of the world, so the cut does not depend on the UVs.
- A pixel is cut away where `opacity` is below `opacityMask`: the shader sets `opacityMask` to `progress`, so
  more of the noise falls below it as `progress` grows.
- `edge` is 1 just above the cut and fades to 0 over `edgeWidth`; it drives the emission, which is why the edge
  glows although nothing else is lit differently.

## 3. Make the material

Create the material with the command, from the palette or an agent:

```json
{"call": "material.create", "params": {"path": "assets:/materials/dissolve.material", "value": {"shader": "assets:/shaders/dissolve.shader", "material": {"name": "Dissolve", "alphaMode": "Mask"}, "parameters": {"progress": 0.45}}}}
```

Select the material in the Asset Browser. The right panel shows the shader and one widget for each parameter:
a slider for `progress` (the `[Range(0, 1)]` of the shader), a colour picker for `edgeColor` and drag fields for
`edgeWidth` and `scale`. Drag the slider: every step is a `material.set`, so Undo takes it back.

## 4. Put it on a cube

Create an Entity with a `MeshRenderer` whose mesh is `builtin:/mesh/cube` and whose material is
`assets:/materials/dissolve.material`. The cube is full of holes with an orange rim. Move `progress` to 0 and it
is whole again; move it to 1 and it is gone.

## 5. Change the shader while the editor runs

Change the colour of `output.baseColor` in the file and save it. The editor compiles it on a background thread
and swaps it into the Scene view when it is ready; no frame waits for the compiler, and the material keeps its
`progress`. Now break it, for example by writing `nothing` in place of `n` in `output.opacity`, and save again:
the Console shows an error with the file, line and column, the cube keeps its last working look, and the
material's panel shows the error. Fix the file and the next compile replaces it.

## 6. Check it from the outside

An agent, or you from `illusion_ctl`, can ask the editor whether everything fits:

```json
{"call": "material.compile", "params": {"path": "assets:/shaders/dissolve.shader"}}
{"call": "material.validate", "params": {"path": "assets:/materials/dissolve.material"}}
```

`material.compile` answers when the background compile is done, with `status`, the diagnostics and the
parameters the shader declares. `material.validate` says whether the material's `parameters` match them: a
misspelt name such as `progres` is an error at `/parameters/progres` that lists the names the shader declares.

## 7. Animate it

The shader reads `input.time`, the seconds of the frame, so it can move without any game code. Replace
`p.progress` with `p.progress + 0.2 * sin(input.time)` in the two places that use it, and the surface breathes
by itself, in the editor and in the exported game.

## Where to go next

- [Material shaders](../manual/material-shaders.md): every field of the surface API, the shading models and the
  export.
- The showcase project's `assets/shaders/pulse.shader` declares one parameter of every type.

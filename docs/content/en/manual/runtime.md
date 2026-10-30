# Game Runtime

A `GameSession` is one running game: the World it plays in, with the physics, audio, input actions, asset
resolvers and [game modules](modules.md) that run it. The editor's Play mode and the exported player create
the same session, so a game steps through the same systems in the same order wherever it runs (ADR 0042).

Header: `#include <illusion/runtime/GameSession.hpp>` (library `illusion::runtime`)

## Running a game

```cpp
illusion::reflectRuntimeStructs();
illusion::ProjectSettings settings;
if (auto loaded = illusion::loadProjectSettings(files, settings); !loaded)
{
    return fail(loaded.error());
}

illusion::GameSessionDesc desc;
desc.jobs = &jobs;
desc.files = &files;
desc.settings = settings;
desc.modules = &modules;           // null runs no game code
desc.streamer = &streamer;         // background loads of clips and physics meshes
desc.renderAssets = &renderAssets; // null when nothing renders (servers, tests)
desc.gamepads = gamepads.get();
auto session = illusion::GameSession::create(desc);
if (!session)
{
    return fail(session.error()); // for example, the input actions asset cannot be read
}

illusion::loadScene((*session)->world(), sceneJson);
(*session)->start(); // modules start() on the World
while (running)
{
    streamer.update();
    gamepads->update(window.isFocused());
    (*session)->keyboardMouse() = window.input();
    (*session)->setInputFocus(window.isFocused());
    (*session)->update(frameSeconds);
    renderer.render((*session)->world());
}
(*session)->stop(); // the destructor stops it too
```

`create()` builds an empty World and its systems from the project settings and loads the input actions
named by `InputSettings::actions`. Load the scene into `world()`, then `start()` attaches the World to the
`ModuleHost` as a Simulation World with four services, which modules reach with `context.service<T>()`:
`PhysicsService`, `AudioService`, `InputService` and `SceneService` (see the
[scripting API](../reference/scripting-api.md)).

The host keeps what it shares with other views: it updates the `AssetStreamer` and the `GamepadSystem`
once per frame, and it renders the World.

## Step order

Every `update(deltaSeconds)` runs:

| Step | What happens |
| --- | --- |
| 1. Input | The `InputActions` read `keyboardMouse()` and the gamepads |
| 2. Modules | `ModuleHost::update()` on the World; then Entities created without an `EntityGuid` get one |
| 3. Physics | `PhysicsWorld::update()`: bodies sync with the World, step and write their Transforms |
| 4. Hierarchy | `updateTransformHierarchy()`: children follow their parents, `Disabled` becomes `Inactive` |
| 5. Audio | `AudioWorld::update()`: sources follow their Entities, the listener moves |
| 6. Resolvers | Asset references become runtime Components (`Collider` meshes, `MeshRenderer` → `RenderMesh`) |

Afterwards the pressed and released edges of `keyboardMouse()` are cleared, so the host writes the next
frame's input on top of the held keys. `updateCount()` and `time()` count the updates and the game time.

## Modes and determinism

| Setting | Effect |
| --- | --- |
| `GameSessionMode::Play` | The whole game |
| `GameSessionMode::Simulate` | No input and no modules: physics moves bodies, no game code runs |
| `fixedStep = true` | Each `update()` advances exactly one physics step, whatever time passed |
| `seed` | With `fixedStep`, Entities created while running get EntityGuids from this seed |

With `fixedStep`, the same scene, input and seed give the same World: tests hash the saved scene after 600
steps and compare. This holds on one build and machine; results across platforms may differ.

## Input and focus

The game sees the keyboard and mouse that the host writes into `keyboardMouse()`: the window's input in the
player, a viewport-local copy in the editor's Game view, or injected keys in a test. `setInputFocus(false)`
hides both the keyboard and the gamepads from the actions, so held actions are released; with
`InputSettings::gamepadsInBackground` gamepads still reach the game without the focus.

## Project settings

The settings are [reflected](reflection.md) structs, each in its own settings file under `settings:/`. A file that does
not exist keeps the defaults, so a new project needs none; `loadProjectSettings()` fails, naming the file,
on a file that cannot be read or holds invalid values.

| File | Struct | Fields |
| --- | --- | --- |
| `settings:/physics.json` | `PhysicsSettings` | `gravity`, `stepSeconds`, `maxStepsPerUpdate`, `layerNames`, `collisionMatrix` |
| `settings:/audio.json` | `AudioSettings` | `masterVolume`, `busVolumes` (Sfx, Music, Ambience, Dialogue, Ui), `maxVoices`, `speedOfSound` |
| `settings:/input.json` | `InputSettings` | `actions` (an [input actions asset](actions.md#input-actions-assets)), `gamepadsInBackground` |
| `settings:/render.json` | `RenderSettings` | `vsync`, `maxFramesPerSecond`, `renderScale` |

```json
{
  "format": "illusion.settings",
  "type": "illusion.PhysicsSettings",
  "version": 1,
  "value": {"gravity": [0, -20, 0], "stepSeconds": 0.01}
}
```

`collisionMatrix` holds one bit mask per layer: layers *a* and *b* collide only when bit *b* of entry *a*
and bit *a* of entry *b* are both set, so clearing either side separates them. The session applies the
settings when it is created; a change takes effect in the next session. `RenderSettings` belong to the host
that presents the game.

The settings file format is not tied to the runtime: `writeSettingsFile()` and `readSettingsFile()`
(`illusion/ecs/SettingsFile.hpp`) save any reflected struct the same way.

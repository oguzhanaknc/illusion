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
desc.uiAssets = &uiAssets;         // fonts and images of the UI; null draws solid colours only
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
    (*session)->setUiFrame({window.framebufferSize()});
    (*session)->update(frameSeconds);
    renderer.render((*session)->world());
}
(*session)->stop(); // the destructor stops it too
```

`create()` builds an empty World and its systems from the project settings and loads the input actions
named by `InputSettings::actions` and the locale files. Load the scene into `world()`, then `start()` attaches the World to the
`ModuleHost` as a Simulation World with five services, which modules reach with `context.service<T>()`:
`PhysicsService`, `AudioService`, `ParticleService`, `InputService` and `SceneService` (see the
[scripting API](../reference/scripting-api.md)).

The host keeps what it shares with other views: it updates the `AssetStreamer` and the `GamepadSystem`
once per frame, and it renders the World.

## Step order

Every `update(deltaSeconds)` runs:

| Step | What happens |
| --- | --- |
| 1. Input | The `InputActions` read `keyboardMouse()` and the gamepads; while the last update showed a modal canvas, only the `ui.*` actions get input |
| 2. Modules | `ModuleHost::update()` on the World; then Entities created without an `EntityGuid` get one |
| 3. Physics | `PhysicsWorld::update()`: bodies sync with the World, step and write their Transforms |
| 4. Hierarchy | `updateTransformHierarchy()`: children follow their parents, `Disabled` becomes `Inactive` |
| 5. UI | `UiWorld` lays out the UI Entities for the frame of `setUiFrame()`, handles the pointer and the actions `ui.navigate`, `ui.submit` and `ui.cancel` (Play mode), and records the draw list (`ui()`), see [Game UI](ui.md) |
| 6. Particles and audio | `ParticleWorld::update()` and `AudioWorld::update()`: sources follow their Entities, the listener moves |
| 7. Resolvers | Asset references become runtime Components (`Collider` meshes, `MeshRenderer` → `RenderMesh`) |

The session's `localization()` holds the string tables of the project's `assets:/locales/*.json`, which
`create()` reads: the keys of every file, with the English text of `en.json`. `UiText::key` is looked up
there ([Game UI](ui.md#literal-and-localized-text)); `ui()` gives the draw list that the host draws over the
scene.

Afterwards the pressed and released edges of `keyboardMouse()` are cleared, so the host writes the next
frame's input on top of the held keys. `updateCount()` and `time()` count the updates and the game time.

## Modes and determinism

| Setting | Effect |
| --- | --- |
| `GameSessionMode::Play` | The whole game |
| `GameSessionMode::Simulate` | No input and no modules: physics moves bodies, no game code runs |
| `fixedStep = true` | Each `update()` advances exactly one physics step, whatever time passed |
| `seed` | With `fixedStep`, Entities created while running get EntityGuids from this seed, including the scenes and prefabs game code instantiates through `SceneService` |

With `fixedStep`, the same scene, input and seed give the same World: tests hash the saved scene after 600
steps and compare. This holds on one build and machine; results across platforms may differ.

## Input and focus

The game sees the keyboard and mouse that the host writes into `keyboardMouse()`: the window's input in the
player, a viewport-local copy in the editor's Game view, or injected keys in a test. `setInputFocus(false)`
hides both the keyboard and the gamepads from the actions, so held actions are released; with
`InputSettings::gamepadsInBackground` gamepads still reach the game without the focus.

The UI reads the same input ([Input and focus](ui.md#input-and-focus)). `create()` adds the actions `ui.navigate`,
`ui.submit` and `ui.cancel` with default bindings when the project's input asset lacks them. While the last
update showed an element on a modal canvas ([Modal canvases](ui.md#modal-canvases)), the session calls
`InputActions::setExclusivePrefix("ui.")` so that every other action reads as released, and clears it when
the canvas is gone.

The pointer is the cursor of `keyboardMouse()` times the `pointerScale` of `setUiFrame(frame, pointerScale)`,
in the pixels of the frame. The player passes the frame's size over the window's size; the editor's Game view
writes the cursor in the pixels of its image and passes 1. Without the input focus the UI sees no pointer.

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
| `settings:/window.json` | `WindowSettings` | `width`, `height` (pixels), `mode` (`Windowed`, `Borderless` or `Fullscreen`), `resizable` |

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
that presents the game. `WindowSettings` belong to the [player](#the-player): `width` and `height` size a
`Windowed` or `Fullscreen` window, `Borderless` covers the primary monitor, and `resizable` lets the player resize
a `Windowed` window. The editor's Game view ignores them.

The settings file format is not tied to the runtime: `writeSettingsFile()` and `readSettingsFile()`
(`illusion/ecs/SettingsFile.hpp`) save any reflected struct the same way.

## The player

`runPlayer()` (`illusion/player/Player.hpp`, library `illusion::player`) is the executable side of an exported
game ([ADR 0055](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0055-exported-games.md)). Call it once from `main()` with a `PlayerDesc`: the game's
`name` (window title and user data directory), `organization`, `version` (written to the log), `startupScene`
and its static `modules`. It then

1. mounts `assets`, `settings` and `cache` from the archives `assets.illpak`, `settings.illpak` and
   `cache.illpak` of the data directory, which is `PlayerDesc::dataDirectory` (default `data`) relative to the
   executable (to `Contents/Resources` inside a macOS bundle), and mounts `user` read-write on the per-user
   directory of the game, `userDataDirectory(organization, name)`;
2. writes the log to `user:/player.log`;
3. loads the [project settings](#project-settings), opens the window of `WindowSettings` and a Vulkan device,
   adds the static modules and creates a `GameSession`;
4. loads the startup scene and runs the game until the window closes.

The process exit code is a `PlayerExit` value: `Ok` (0), `StartFailed` (1: no window, no Vulkan device, or a
module or the startup scene failed to load), `DataMissing` (2: an archive of the data directory is missing or
damaged) or `BadArguments` (3: the command line could not be read). When the game cannot start, the player
logs the reason in English and shows a message box with a text from its string table, unless `--no-dialogs`
is given.

Command line:

| Option | Effect |
| --- | --- |
| `--windowed`, `--borderless`, `--fullscreen` | Override `WindowSettings::mode`. |
| `--size <width>x<height>` | Overrides the window's size. |
| `--data <directory>` | Overrides `PlayerDesc::dataDirectory` (relative to the executable likewise). |
| `--user-data <directory>` | Mounts `user` there instead of the user data directory, for tests and portable installs. |
| `--fixed-step` | Runs the session deterministically. |
| `--frames <count>` | Ends the game after that many frames. |
| `--screenshot <file.png>` | Writes the last frame as a PNG when the game ends. |
| `--no-dialogs` | Keeps failures to the log and stderr. |

```bash
showcase_game --windowed --size 1280x720
showcase_game --fixed-step --frames 300 --screenshot shot.png --no-dialogs
```

`illusion_add_game()` (`cmake/IllusionGame.cmake`) builds the executable of a game: it generates the `main()`
that fills the `PlayerDesc`, links `illusion::player` and the game's static modules, and with `PROJECT` packs the
project's data next to the executable with `illusion_pack`. The showcase project is built this way:

```cmake
illusion_add_module(showcase_game_module STATIC ID showcase
    SOURCES showcase/modules/showcase/showcase.cpp
    USES physics audio particles input render runtime store)
illusion_add_game(showcase_game
    MODULES showcase_game_module
    NAME "Showcase"
    ORGANIZATION "illusion"
    VERSION "0.1.0"
    STARTUP_SCENE "assets:/scenes/everything.scene"
    PROJECT showcase)
```

The CMake package that provides `illusion_add_game()` and `illusion::pack`, and how to install it, is described in [Player SDK](modules.md#player-sdk).

How the editor exports a game with it is described in [Exporting a game](editor/export.md).

## Store services

A game reaches the store it is sold in through `StoreService` (`illusion/store/StoreService.hpp`,
`USES store`; [ADR 0056](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0056-store-service-and-steamworks.md)): achievements and
their progress, integer and float stats, cloud files, the overlay and gamepad glyphs. The same calls work
with every store and with none, so the game code does not change between Play mode, tests and Steam.

```cpp
void update(illusion::ModuleContext& context, illusion::f64)
{
    illusion::StoreService* store = context.service<illusion::StoreService>();
    if (store == nullptr || store->overlayActive())
    {
        return; // no store service, or the overlay covers the game
    }
    if (won(context.world()))
    {
        static_cast<void>(store->unlockAchievement("FIRST_WIN"));
        static_cast<void>(store->setStatInt("wins", wins(context.world())));
        static_cast<void>(store->storeStats());
        const std::array<std::byte, 4> save = saveGame(context.world());
        if (const auto written = store->writeCloudFile("slot1.save", save); !written)
        {
            ILL_LOG_WARN("Game", "not saved: {}", illusion::storeErrorName(written.error()));
        }
    }
}
```

- **No callbacks.** Every result is returned or polled. `overlayActive()` is a value the game reads each frame
  (games usually pause while the overlay is open); nothing calls back into a module, so hot reload stays safe.
- **Errors** are `Expected<T, StoreError>`: `Unavailable`, `InvalidName`, `InvalidValue`, `NotFound`,
  `TypeMismatch`, `TooLarge` or `QuotaExceeded`, `IoError`. Names of achievements, stats and cloud files are
  1 to 64 ASCII letters, digits, `_`, `-` and `.`, neither starting nor ending with `.`; a cloud file holds at
  most 100 MiB.
- **Spans, not containers.** `readCloudFile()` fills a buffer of the game and returns the file's size (an empty
  buffer asks for the size only); `listCloudFiles()` fills a span of names and returns how many there are.

Which store a game gets:

| Where | Store | Cloud files |
| --- | --- | --- |
| Play mode in the editor | `NullStoreService`, new for each session | `cache:/store/cloud` of the project |
| Exported game without a store | `NullStoreService` | `user:/cloud` |
| Exported game with `store.kind` `steam` | `SteamStoreService` when Steam runs, else `NullStoreService` | Steam Cloud, or `user:/cloud` |

`NullStoreService` reports `available()` false and `storeName()` "none". It keeps achievements, progress and
stats in memory for the session, so game logic can be tested, and stores cloud files as files (100 MiB in
all), so saves persist across runs without a store. `openOverlay()` returns false and `glyphPath()` nothing.

`SteamStoreService` (library `illusion::steam`) is built only when the CMake variable `ILLUSION_STEAMWORKS_SDK`
names a Steamworks SDK, which you download from Valve; illusion never ships or downloads it. It maps
achievements, stats, Remote Storage and the overlay; it returns no glyphs yet. The player starts it before the
window opens, so that the overlay can hook the graphics device, and calls its `update()` once per frame. How to
export a Steam game is described in [Exporting a game](editor/export.md#store).

!!! warning "Not tested here"
    The Steam store cannot be built without the SDK, so neither CI nor the engine's tests build or run it.
    Test a Steam export on a machine with the SDK and Steam before you ship.

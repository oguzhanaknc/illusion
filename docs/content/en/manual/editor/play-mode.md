# Play Mode

Play mode runs the game inside the editor. **Play** copies the edited scene into a separate play World and
runs a [GameSession](../runtime.md) on it: game modules, input, physics and audio. **Stop** throws the play
World away, so the edited scene is exactly as it was before Play, whatever the game did (ADR 0042). Every part
of it is an [Editor command](../../reference/editor-commands.md), so an AI agent starts, steps and inspects
the game with the same API.

## Starting and stopping

| Key | Command | What it does |
| --- | --- | --- |
| **F5** | `play.start` | Copies the scene and starts the game. |
| **Shift+F5** | `play.start` with `"mode": "simulate"` | Physics only: bodies fall and collide, no game code or input runs. |
| **F6** | `play.pause` | Pauses; a paused game advances only through steps. |
| **F7** | `play.step` | Advances by fixed physics steps (default one). |
| | `play.stop` | Destroys the play World and returns to editing. |
| | `play.status` | Says whether a session runs: `stopped`, `playing` or `paused`, with its steps and game time. |

The copy is made in memory, without saving the scene, and keeps the entity ids. The game reads the project
settings (`settings:/physics.json`, `settings:/input.json`, ...) when it starts; edit them with
`project.settings_set`. `play.set_time_scale` slows the game down (0.5) or freezes it (0); it stays set for
later sessions and does not change `play.step`.

Pass `"fixedStep": true` to `play.start` for repeatable runs: every frame then advances exactly one physics
step, and entities that the game creates get ids from `seed`, so the same scene, input and seed always give
the same World.

```json
{"method": "play.start", "params": {"mode": "play", "fixedStep": true, "seed": 7}}
```

The toolbar's Play controls follow the session: while it runs, **Play** turns into **Stop** (F5 and Shift+F5
stop it too), **Pause** stays pressed while paused and resumes on the next click, and **Pause** and **Step**
are off while nothing plays. The status bar says **Playing** or **Paused**, and the panels take the theme's
play tint (the `playModeTint` preference) so a running game is never mistaken for the edited scene; changes
made now disappear with Stop.

Before the copy, `play.start` writes an [autosave](projects.md) of an unsaved scene (when autosave is on), so
game code that crashes the editor loses nothing.

## The Game view

While a session runs, every viewport shows the play World: the Game view through the scene's main Camera, the
Scene views through their editor cameras. `viewport.pick`, `viewport.sample`, `viewport.screenshot` and the
other viewport commands read what the game shows, and the entities they report are the play World's (a
viewport command with `"world": "edit"` fails while playing).

Click the Game view to give the game the keyboard and mouse: the cursor hides and moves without bounds, keys
and buttons reach the game's input actions, and editor shortcuts wait. **Shift+F1** takes them back (the game
sees every held key released); Pause and Stop do too. Agents send input with `input.inject` whether or not
the window has captured it.

With **Maximize on Play** (the button in the Game view's toolbar, or the `maximizeOnPlay` preference) the Game
view fills the window while playing and the other panels come back with Stop.

## Editing while playing

While a session runs, the entity, component, transform, hierarchy and clipboard commands act on the play
World. Their changes apply at once (a changed `RigidBody` or `Collider` rebuilds the body, a new `AudioSource`
clip restarts the sound) but they are not undoable and disappear with Stop. Add `"world": "edit"` to the params
to change the edited scene instead, as an undoable step:

```json
{"method": "entity.rename", "params": {"entity": "/Light", "name": "Sun", "world": "edit"}}
```

Commands that only make sense on the edited scene (`scene.save`, `history.undo`, asset and material commands)
always act on it. The selection holds entity ids, so it names the same entities in both Worlds.

To keep values found while playing, copy them to the edited scene with `play.keep_changes` before Stop. It
copies the saved Components of the named entities (or only the listed ones) as one undo step; entities that
the game created are skipped with a warning.

```json
{"method": "play.keep_changes", "params": {"entities": ["/Player"], "components": ["illusion.Transform"]}}
```

## Testing a game from an agent

An agent drives the running game with three commands:

| Command | What it does |
| --- | --- |
| `input.inject` | Presses or releases keys and mouse buttons, or moves the cursor, as the player would. The game sees it on its next update. |
| `input.read` | Returns the state of the game's input actions: held, pressed, released and value. |
| `play.run_until` | Steps the game until an entity exists, is gone, or a Component value equals a given value, or until `maxSteps`. |

```json
{"method": "input.inject", "params": {"keys": [{"key": "Space", "down": true}]}}
{"method": "play.step", "params": {"steps": 1}}
{"method": "play.run_until", "params": {"maxSteps": 600, "entity": "Board", "component": "game.Score", "field": "/lines", "equals": 1}}
```

## Input actions

The input actions asset (a `.input` file, see [Input actions](../actions.md)) lists the game's actions and
their default bindings. The `input.*` asset commands edit it as undoable file changes:

| Command | What it does |
| --- | --- |
| `input.actions` | Lists the actions with their types and bindings. |
| `input.action_define` | Adds an action (creating the file) or replaces its type and bindings. |
| `input.action_remove` | Removes an action. |
| `input.action_rename` | Renames an action. |
| `input.bind`, `input.unbind` | Adds or removes one binding. |

Point the project at the asset with `project.settings_set` on `illusion.InputSettings` (`actions`).

## Game modules

`module.load` loads a game module from its shared library and starts it on a running session, `module.unload`
stops and unloads it, and `module.list` lists the loaded modules. A module that cannot load fails with code
1302 and the loader's reason (see [Modules](../modules.md)).

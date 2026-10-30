# 0042. Play mode through scene snapshots and a shared GameSession runtime

- Status: Accepted
- Date: 2026-10-31

## Context

The editor edits one World (ADR 0036). Pressing Play must run the game, with its modules (ADR 0041),
physics, audio and input, and pressing Stop must bring back exactly the scene that was being edited, since
a play session moves bodies, spawns and destroys entities and changes any value. Agents drive Play too: the
Tetris acceptance run (ADR 0038) presses Play, injects input and checks the outcome without a human.

The exported player (M8) runs the same game. Until now every sample wired the systems by hand, each in its
own order: `InputActions::update`, `PhysicsWorld::update`, `AudioWorld::update`, the asset resolvers and
the hierarchy pass. A game that behaves differently in the editor and in the player, because the two call
the systems in a different order, is the kind of bug that ships.

Options considered for play mode:

- **Play in the edit World and undo afterwards.** No copy, but every write of the simulation (thousands per
  frame) would need an undo record, runtime spawns would leave holes in the undo history, and a crash in
  game code would corrupt the scene being edited.
- **Copy the World in memory, Component by Component.** Fast, but it needs a copy function for every
  Component type, which modules would have to provide, and it copies runtime state (physics handles, GPU
  meshes) that must not be shared between two Worlds.
- **Serialize the edit World to a scene and load it into a fresh World.** It goes through the code that
  already saves and loads every reflected Component, keeps EntityGuids, drops runtime-only state by
  construction, and is exactly what the player does with a scene file. It costs a serialization per Play.

Options considered for the runtime:

- **The editor wires its own play loop.** Simplest now, but the player would duplicate it, and the two would
  drift.
- **A runtime library that owns one running game.** Both the editor and the player create it, so the order
  of the systems, the project settings and the module services are defined once.

## Decision

- **`GameSession`** (`illusion::runtime`) is one running game. It owns the World and, for it, a
  `PhysicsWorld`, an `AudioWorld`, `InputActions` loaded from the project's input actions asset, the
  physics asset resolver (with an `AssetStreamer`), the render asset resolver (with a `RenderAssetCache`;
  headless hosts pass none), the hierarchy index, gamepad routing, and the module attachment with the four
  host services (`PhysicsService`, `AudioService`, `InputService`, `SceneService`) as a Simulation World.
- **Fixed step order.** Every `update()` runs input, modules, physics, hierarchy, audio and resolvers, in
  that order, then clears the input edges. Entities created by game code get EntityGuids after the modules
  run, so selection, commands and saves can refer to them.
- **Deterministic mode.** With `fixedStep`, each `update()` advances exactly one physics step whatever time
  passed, and new EntityGuids come from a generator seeded with `seed`. The same scene, input and seed give
  the same World, which tests check by hashing the saved scene after 600 steps.
- **Simulate mode** runs the session without input and modules: physics moves bodies, no game code runs.
- **Input** comes from the host: it writes the window's (or synthetic) keyboard and mouse state into the
  session and tells it whether the game has the focus; gamepads reach the game only with the focus, unless
  the project allows them in the background. The host owns and updates the `GamepadSystem` and the
  `AssetStreamer`, which the editor shares with other views.
- **Project settings** are reflected structs (`PhysicsSettings`, `AudioSettings`, `InputSettings`,
  `RenderSettings`) in settings files under `settings:/`, read through struct reflection; a missing file
  keeps the defaults. The settings file format moves from the editor to core (`SettingsFile.hpp`), since the
  runtime reads it too.
- **Input actions asset.** A `.input` JSON file defines the actions, their types and default bindings, so
  they are data that the editor and designers change; `InputActions` gains `removeAction`, `renameAction`
  and `clear` for editing them.
- **Play mode** (the editor, `illusion::editor_play`) takes an in-memory scene snapshot of the edit World,
  ids kept, loads it into a fresh `GameSession` World, and destroys that World on Stop; the edit World is
  never written while playing, so after Stop its scene is byte-identical to before Play. While playing,
  commands address the play World unless they ask for the edit World; play-world edits are not undoable,
  and `keep_changes` copies chosen values back to the edit World as one undoable step. Pause, Step N and a
  time scale act on how the editor calls `update()`; they do not change the session.

## Consequences

- The editor and the player run the same code in the same order; a difference between them is a bug in one
  host's input or rendering, not in the game loop.
- Play costs one scene serialization and load, proportional to the scene (budget: 5 µs per entity); scenes
  far beyond that need a faster path, such as a binary snapshot, without changing the model.
- Only reflected, saved state survives into play. Runtime-only Components are rebuilt by the resolvers and
  systems, as they are when the player loads a scene file. A Component that is not reflected for scenes
  does not exist in the play World, which is also what the player would see.
- Determinism holds for the same build and machine. Cross-platform determinism would need Jolt's
  cross-platform deterministic mode and is not promised.
- `illusion::runtime` now depends on the physics, audio, input, render and modules libraries; the
  header-only `illusion::runtime_api` that modules use is unchanged.
- The project settings that apply at session creation need a new session to take effect: changing gravity
  in the settings changes the next Play, not the running one.

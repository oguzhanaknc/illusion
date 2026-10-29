# Tetris acceptance contract

This file is the contract of the agent acceptance test (ADR 0038). CI replays `scripted.jsonl` against a
headless editor and runs `checks.jsonl`; in M7x a live agent receives this file as its prompt and must pass
the same checks through the editor alone. The full plan is §9 of `docs/plans/m7-editor.md`.

Run it locally (Release build):

```bash
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --deterministic --editor-arg --no-gpu \
    replay tests/acceptance/tetris/scripted.jsonl --checks tests/acceptance/tetris/checks.jsonl --temp-workspace
```

## The game

1. Create the project with `project.create {"directory": "tetris", "name": "Tetris", "template": "empty"}`
   in the editor's workspace.
2. Write a module `tetris` (version 1.x) in `modules:/tetris` using `illusion::core` with `USES input
   runtime`, and build it with `module.build`. Its reflected Components:

   | Component | Fields |
   | --- | --- |
   | `tetris.Board` | `width` U32 = 10, `height` U32 = 20, `gravitySeconds` F32 = 0.8, `origin` Vector3 |
   | `tetris.Cell` | `x` I32, `y` I32, `kind` Enum `tetris.Piece` (I, O, T, S, Z, J, L) |
   | `tetris.ActivePiece` | `kind`, `rotation` U32 (0–3), `x` I32, `y` I32; four child entities tagged `tetris.ActiveBlock` |
   | `tetris.PieceQueue` | `seed` U32, `forced` String: its characters are dealt first, then a 7-bag from `illusion::Random` seeded with `seed` |
   | `tetris.Score` | `lines`, `points`, `level` U32, `gameOver` Bool |

3. Rules: cell (x, y) sits at `origin + (x, y, 0)` with size 1 and y up. A piece spawns with its bounding box
   at x = 3 and its top row at y = height − 1. Rotation follows SRS with the standard wall kicks. Gravity moves
   one row per `gravitySeconds · 0.85^(level−1)`; `soft_drop` moves one row per press; `hard_drop` drops and
   locks. Line clears score 100, 300, 500 or 800 × level; level = 1 + lines / 10. The game is over when a new
   piece overlaps a Cell. Board state is derived from `tetris.Cell` entities on every update.
4. Input actions in `assets:/input/tetris.input` (all Button): `tetris.move_left` (`Key.Left`,
   `Gamepad.DPadLeft`), `tetris.move_right` (`Key.Right`, `Gamepad.DPadRight`), `tetris.rotate_cw` (`Key.Up`,
   `Gamepad.South`), `tetris.soft_drop` (`Key.Down`, `Gamepad.DPadDown`), `tetris.hard_drop` (`Key.Space`,
   `Gamepad.North`).
5. Content: materials `assets:/materials/{i,o,t,s,z,j,l}.material` (cyan, yellow, purple, green, red, blue,
   orange) and `frame.material`; the startup scene `assets:/scenes/main.scene` with a camera framing the board
   (orthographic once cameras have a projection, M7n), a DirectionalLight, a SkyLight using
   `builtin:/sky/procedural`, walls and floor of `MeshRenderer {builtin:/mesh/cube, frame}` under `/Board`,
   and one Board, one PieceQueue and one Score. Locked cells are children of the Board.

## What the transcript covers today

The scripted transcript grows with the editor. M7j covers the **board scenario**: the project, the eight
materials and the static board scene (camera, sun, sky, three walls), saved and reopened after a restart.
M7o-2 adds the **rendering checks** (C6) in the game view; the golden image is rewritten with
`ILLUSION_UPDATE_GOLDENS=1`, and CI uploads the captured image (`ui-captures`) when it differs. Module Components, input assets, play mode, rendering checks and hot reload join the transcript in the
milestones that add those commands (M7n, M7r, M7s, M7t, M7u).

## Checks

| Check | Verifies | Since |
| --- | --- | --- |
| C1 Project | The manifest, `assets:/scenes/main.scene` and the eight materials exist (later: the `.input` asset and `modules:/tetris`). | M7j |
| C2 Module | `tetris` is loaded with semver 1.x and no error diagnostics; `component.schema` matches the contract. | M7s |
| C3 Scene | Exactly one camera; a DirectionalLight; a SkyLight with `builtin:/sky/procedural`; three MeshRenderers under `/Board` whose material resolves (later: one Board 10x20, one PieceQueue, one Score, an orthographic camera, `scene.validate`). | M7j |
| C4 Persistence | After a restart the `scene.info` hash is unchanged, the ids survive, the MeshRenderer and SkyLight references survive and a re-save is byte-identical. | M7j |
| C5 Behaviour | Spawn, walls, kicks, drops, clears, gravity and game over through `input.inject` and `play.step`. | M7r |
| C6 Rendering | The 640x360 game view through the scene camera matches `golden/board.png`; `render.stats` sees the three board objects; `viewport.project` and `viewport.pick` find each wall and the floor; `viewport.sample` reads the grey frame; `render.diagnostics` reports no validation errors. Steps carry `"requires": "gpu"`: without a GPU they must fail with 1400. | M7o-2 |
| C7 Hot reload | A Component layout change during play keeps the values. | M7t |
| C8 Hygiene | No error log entries and no asserts; every transaction has an `rpc:` origin. | M7x |

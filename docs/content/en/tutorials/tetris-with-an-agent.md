# Tetris with an Agent

In this tutorial Claude Code builds a complete Tetris game through the editor's MCP bridge: a game module
written and compiled by the agent, materials, a scene, input actions and a game that it plays to test. You
watch it happen in the editor window and keep the right to say no. The game is the one of the agent
acceptance contract, `tests/acceptance/tetris/SPEC.md`, which CI checks against a scripted session.

You need a Release build of illusion (`illusion_editor`, `illusion_mcp` and `illusion_ctl` in
`build/bin/Release`), a C++ compiler that the editor's module SDK can use (the one the editor was built with)
and Claude Code. Do [Your First Agent Session](first-agent-session.md) first if you have not used the bridge
before.

## 1. Create the project

Create an empty project; `project.create` also writes the `.mcp.json` that Claude Code reads:

```bash
mkdir -p ~/games
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --workspace --editor-arg ~/games \
    call project.create '{"directory": "tetris", "name": "Tetris", "template": "empty"}'
cp tests/acceptance/tetris/SPEC.md ~/games/tetris/SPEC.md
```

## 2. Open the editor window

Start the editor yourself with a socket. The bridge in `.mcp.json` runs in `auto` mode and attaches to this
editor instead of launching a headless one, so every change of the agent appears in your window:

```bash
build/bin/Release/illusion_editor --project ~/games/tetris --rpc-listen
```

Choose **Window → Layouts → Agent**: the AI Activity panel lists each call of the agent with its arguments and
how long it took. Its permission mode decides what the agent may do: in **Ask**, the default of the window, the
agent edits the scene freely but file, build and destructive commands wait until you allow them, such as
writing the module source or building it. Allow a kind of command for the rest of the session to stop being
asked. See [Permission modes](../manual/editor/ai-agents.md#permission-modes).

## 3. Give the agent the contract

Start Claude Code in the project and approve the `illusion` server:

```bash
cd ~/games/tetris
claude
```

Then ask:

> Read illusion://guide, then build the game described in SPEC.md in this project with the editor tools.
> Write the module, build it with module_build until it compiles without errors, create the materials, the
> scene and the input actions, save the scene, and test the rules in Play mode with fixed steps before you
> report back.

## 4. What the agent does

The order varies from session to session, but a successful session goes through these steps:

| Step | Tools |
| --- | --- |
| Reads the guide, the contract and the tools it needs | `illusion://guide`, `editor_describe` |
| Creates the module and writes its source | `module_create`, `project_file_write` |
| Compiles it, reads the diagnostics and fixes them | `module_build` |
| Checks the Components the module reflects | `component_schema` |
| Creates the eight materials | `material_create` |
| Builds the scene: camera, sun, sky, walls and floor, the Board, PieceQueue and Score | `editor_batch`, `entity_create`, `component_add` |
| Defines the five actions and points the project at them | `input_action_define`, `project_settings_set` |
| Saves the scene | `scene_save` |
| Plays: starts Play mode, presses keys, steps and reads the pieces back | `play_start`, `input_inject`, `play_step`, `component_get` |

A failed compile is not a tool error: `module_build` returns `"succeeded": false` with the diagnostics, and
the agent edits the source and builds again. While it plays, the Game view of your window shows the pieces
fall.

## 5. Check the result yourself

- Press **Play**, then use the arrow keys and Space (or a gamepad's D-pad, South and North). **Stop** puts the
  scene back exactly as it was.
- Open `modules/tetris/tetris.cpp` in your code editor (`editor_open_source` opens it at a line) and read what
  the agent wrote.
- Undo the agent's last step with `history_undo` and `{"scope": "mine"}`, or with **Edit → Undo** in your
  window: agent changes are ordinary undo steps.
- Compare the game with the rules in SPEC.md: spawn position, SRS kicks, gravity per level, line scores and
  game over.

## 6. Record a session

Add `--record session.jsonl` to the bridge's `args` in `.mcp.json` to keep every MCP message of a session,
for a bug report or to compare two sessions. The [AI Agents manual](../manual/editor/ai-agents.md) lists the
other bridge options.

## Next steps

- [Tetris by Hand](tetris-by-hand.md) builds the same game in the editor window, step by step.
- The [Automation manual](../manual/editor/automation.md) shows how the scripted acceptance transcript drives
  the same commands without an agent.

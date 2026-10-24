# Your First Agent Session

In this tutorial Claude Code builds a small scene in a new project through the editor's MCP bridge. You need
a Release build of illusion (`illusion_editor` and `illusion_mcp` in `build/bin/Release`) and Claude Code.
The [AI agents manual](../manual/editor/ai-agents.md) explains everything used here.

## 1. Create a project

Create the project with the editor itself; `project.create` also writes the `.mcp.json` that Claude Code
reads:

```bash
mkdir -p ~/games
illusion_ctl --launch build/bin/Release/illusion_editor --editor-arg --workspace --editor-arg ~/games \
    call project.create '{"directory": "tetris", "name": "Tetris", "template": "3d-starter"}'
cat ~/games/tetris/.mcp.json
```

## 2. Start Claude Code in the project

```bash
cd ~/games/tetris
claude
```

Claude Code asks whether to use the `illusion` server from `.mcp.json`; approve it. `/mcp` lists the server
and its tools (`entity_create`, `scene_tree`, ...). The bridge launched a headless editor for the project in
the background.

## 3. Ask for something

Try a prompt such as:

> Read illusion://guide. Then build a Tetris board: an entity "Board" with ten columns and twenty rows of
> cubes named "Cell x,y" under it, one metre apart, starting at the origin. Save the scene as
> assets:/scenes/board.scene and check the result with scene_tree.

The agent discovers the commands, creates the board with `editor_batch` (one undo step for all cells),
saves it and checks it. Each tool call shows its arguments and result in Claude Code.

## 4. Look at what happened

Ask the agent to call `illusion_call` with `{"method": "editor.activity"}`, or run it yourself against the
same editor if it was started with `--rpc-listen`:

```bash
illusion_ctl --project ~/games/tetris call editor.activity '{"limit": 20}'
```

Every change has the origin `rpc:illusion_mcp`. Ask the agent to undo its last step
(`history_undo` with `{"scope": "mine"}`) and to check the scene again.

## 5. Use the editor you already have open

When you start the editor yourself with a socket, the bridge attaches to it instead of launching another
one:

```bash
illusion_editor --headless --project ~/games/tetris --rpc-listen
```

The `.mcp.json` from step 1 runs the bridge in `auto` mode, which finds that editor through
`.illusion/run/editor-endpoint.json`.

## Next steps

- Keep the agent's context small with `--tools core` in `.mcp.json`'s `args`.
- Record a session with `--record session.jsonl` to see every message.
- The [automation manual](../manual/editor/automation.md) shows the same commands from scripts.

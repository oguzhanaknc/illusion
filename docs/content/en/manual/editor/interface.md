# Editor Window

`illusion_editor` opens the editor window: a menu bar, a toolbar, docked panels and a status bar. Everything
the window does runs an [editor command](../../reference/editor-commands.md), so a person and an AI agent
use one API: a menu item, a toolbar button and a shortcut are *UI actions* that run a command with fixed
params, and agents can drive the window itself with the `ui.*` commands.

```bash
illusion_editor --project games/tetris
```

Without `--project` the Welcome window opens. See [Getting Started](../../getting-started.md) for building
and launching the editor, and [Preferences](preferences.md) for themes, language and shortcuts.

## Layout of the window

| Part | What it shows |
| --- | --- |
| Menu bar | File, Edit, View, Entity, Assets, Play, Modules, Tools, Window and Help. Items whose command arrives in a later milestone are disabled; their tooltip names the milestone. |
| Toolbar | [Transform tools](transform-tools.md) with their axes, pivot and snapping on the left, Play, Pause, Step and Simulate in the centre, and the layout, the agents chip, notifications and search on the right. |
| Panels | [Outliner and Inspector](scene-editing.md), [Scene](viewport.md), Game, Assets, Console, History, AI Activity, Profiler, Rendering and Project Settings, docked in the window. Panels of later milestones show what they will hold. |
| Status bar | Selection, scene revision and saved state, frame time, language, connected agents (and approvals waiting) and the play state. |

Close a panel with its tab's close button and open it again from the Window menu (`ui.show_panel`).

## Layouts

A layout arranges the panels. Five layouts are built in:

| Layout | Use |
| --- | --- |
| `default` | Outliner on the left, Inspector tabs on the right, tool panels along the bottom. |
| `level-design` | Outliner above Assets on the left, a large Scene view, Console and History below. |
| `debug` | Console and Profiler large along the bottom, History next to the Inspector. |
| `profiling` | The Profiler across the bottom half, Rendering settings on the right. |
| `agent` | AI Activity and History on the right, for watching an agent work. |

Choose one from the toolbar's layout button or Window > Layouts. **Save layout** stores the current
arrangement for your user under a name; **Reset layout** restores the built-in arrangement of the active
layout. The arrangement is saved when the editor closes and comes back at the next start; a project can ship
a default in `settings/editor_layout.ini`, used when you have no arrangement of your own.

```json
{"method": "ui.layout_apply", "params": {"name": "agent"}}
```

## Command palette

Press **Ctrl+Shift+P** (or **Ctrl+K**) to search every command and menu action by its title, in your
language or in English. Start the search with `@` to list entities, or with `>` to list preference pages;
**Ctrl+P** (Go to Anything) opens it with entities. **Up** and **Down** choose, **Enter** runs, **Escape**
closes.

A command that needs params the action does not give opens a form generated from its params schema: text
fields for strings, number fields, check boxes for flags, lists for enumerations, and a JSON field for
anything else. Agents open the palette with `ui.palette`.

## Console

The Console shows the editor's log. Filter by level (Debug, Info, Warnings, Errors), search the text,
collapse repeated messages into one row with a count, and show timestamps. A message's source link copies
`file:line` to the clipboard. **Clear** runs `log.clear`.

The line at the bottom runs a command: its name, then its params as JSON.

```text
entity.create {"name": "Board"}
```

## History

History lists the undo steps; click a row to jump there (`history.jump`). A badge shows who made each step:
**You** for steps made in the window, the client's name for steps of an agent. **Undo *client*'s changes**
undoes every step of that agent, even when your own steps came later, unless they touch the same things.

## AI Activity

The AI Activity panel shows the agents connected to the editor: their client and agent names, the MCP
protocol era and the number of requests. **Pause** holds an agent's requests back until you resume it;
**Disconnect** closes its connection. The feed below lists every call with its params, duration and result,
newest first; **Undo** next to an agent's latest change undoes it.

The permission mode at the top applies to every agent:

| Mode | Agents may |
| --- | --- |
| Read only | only read. |
| Ask | read and edit; destructive, file, build and system commands wait for your approval. |
| Automatic | run everything. |

In the Ask mode a request that needs approval shows a prompt in the top right corner and a toast; the panel
lists every waiting request. **Allow once** runs it, **Allow for session** also allows that kind of command
for the agent's connection from now on, and **Deny** answers it with `permission_denied`. While a request
waits, the agent's later requests wait behind it; other agents go on. A batch (`editor.batch`) asks once for
all of its steps. Approvals are deliberately not commands: no client can approve its own requests.

## Notifications

Toasts report results and failures for a few seconds; the bell in the toolbar opens the notification centre
with the last 64. Agents can post one with `ui.notify`.

## Projects

The Welcome window lists recent projects, creates a project from a template (`project.create`), and browses
the folders of the workspace to open one (`project.open`). Projects outside the workspace are listed but
cannot be opened from it; start the editor with `--workspace` to choose another one. Help > About shows the
version, and Help > Third-party licenses shows the licences of the libraries and fonts the editor uses.

## Screenshots

`editor.screenshot` returns the window as a PNG (to MCP clients as image content). A headless editor with a
GPU renders the UI offscreen at the requested size, 1920x1080 by default (`--ui-size`).

```json
{"method": "editor.screenshot", "params": {"width": 1280, "height": 720}}
```

## Not yet

Gamepad navigation of the window, following an agent's selection in the viewport, External Tools that open
source files, and the `#` asset search of the palette arrive with later milestones (see the
[roadmap](https://github.com/oguzhanaknc/illusion/blob/main/ROADMAP.md)).

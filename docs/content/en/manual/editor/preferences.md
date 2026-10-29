# Preferences

File > Preferences (**Ctrl+,**) changes how the editor looks and behaves for your user. Every change runs
`preferences.set`, so agents can read and change preferences too (`preferences.get`). Preferences are saved
as `preferences.json` in the user directory:

| System | User directory |
| --- | --- |
| Windows | `%APPDATA%\illusion\editor` |
| macOS | `~/Library/Application Support/illusion/editor` |
| Linux | `$XDG_DATA_HOME/illusion/editor` (default `~/.local/share/illusion/editor`) |

Saved layouts live next to it in `layouts/`. Start the editor with `--user-dir <dir>` to use another
directory; a headless or `--deterministic` editor keeps its preferences in memory unless it gets one.

```json
{"method": "preferences.set", "params": {"value": {"theme": "light", "uiScale": 1.25}}}
```

The value is a merge patch: fields it does not mention keep their values (`"replace": true` resets them,
except the recent projects). Invalid values fail with the field's JSON Pointer, e.g. `/value/uiScale`.

## Pages

| Page | Preferences |
| --- | --- |
| Appearance | `theme` (`dark`, `light`, `high-contrast`), `accent` (`azure`, `violet`, `teal`, `amber`), `uiScale` (0.75 to 2, times the monitor's scale), `reduceMotion`, `colorBlindAxes` (Okabe-Ito colours for the X, Y and Z axes). |
| Language | `locale`: English, the pseudo-locale `en-XA` (accented, longer text for testing layouts) and every locale file in `editor-data/locales`. The language changes at the next frame. |
| Keyboard | `keymap` preset and `shortcuts`, see below; `viewportNavigation` (`keymap`, `illusion`, `unity`, `unreal`, `blender`), the mouse controls of the Scene view (see [Scene Viewport](viewport.md#navigation-presets)). |
| AI Agents | `agentPermissions` (`read-only`, `ask`, `auto`; `ask` by default), `approvalPopups` and `followAgent` (the Scene view frames what an agent selects). See [Editor Window](interface.md#ai-activity). |
| Autosave | `autosaveInterval` (seconds, 0 for never, at most 3,600) and `autosaveCopies` (1 to 20). |
| External Tools | `codeEditor`: the command that opens source files (used by the module workflow of a later milestone). |

The file also keeps `recentProjects` (the last ten projects opened) and `layout` (the layout used last).

## Shortcuts

Every menu item and toolbar button is a UI action with a stable id such as `edit.undo`. A keymap preset
gives the actions their chords:

| Preset | Differences from `illusion` |
| --- | --- |
| `illusion` | The defaults: Ctrl+Z, Ctrl+Y, Ctrl+D, Delete, F2, Ctrl+Shift+P, Ctrl+P, Q/W/E/R/T for tools, F5 to play. |
| `unity` | Ctrl+P plays, Ctrl+Shift+P pauses, Ctrl+Alt+P steps, Ctrl+K is Go to Anything. |
| `unreal` | Alt+P plays, Alt+S simulates, Pause pauses, the backtick opens the Console. |
| `blender` | Shift+D duplicates, X deletes, G/R/S move, rotate and scale, F3 searches, Shift+A adds an entity. |

On the Keyboard page, click an action's chord and press the new keys to rebind it; **Escape** cancels.
**Reset** goes back to the preset's chord and **Remove** leaves the action without one. Chords bound to two
actions are listed as conflicts and marked in the table. Chords are written as modifiers and a key, the key
named as in [Windows and Input](../platform.md):

```json
{"value": {"keymap": "blender", "shortcuts": [{"action": "edit.redo", "chord": "Ctrl+Shift+Z"}]}}
```

# Projects

Header: `#include <illusion/editor/core/Project.hpp>`

A project is a directory that holds a game's content, settings and module sources. The editor opens one
project at a time and reaches its files only through mounts.

## Layout

```text
MyGame/
  project.illusion      the manifest
  assets/               content: scenes, materials, models, input, audio      assets:
  settings/             project settings                                       settings:
  modules/              game module sources (editor only)                      modules:
  .illusion/            per-machine state, git-ignored
    cache/              derived data                                           cache:
    journal/            crash journals of running and crashed sessions
    autosave/           rotating autosaves of dirty scenes
    trash/              contents of replaced and deleted files, for undo
  .gitignore
```

Opening a project creates the standard directories that are missing.

## The manifest

`project.illusion` is JSON:

```json
{
  "format": "illusion.project",
  "name": "Tetris",
  "version": "0.1.0",
  "engineVersion": "0.7.0",
  "startupScene": "assets:/scenes/main.scene",
  "modules": ["tetris"]
}
```

`name` is required; `version` is a semantic version (default `0.1.0`); `startupScene` opens with the project.
`parseProjectManifest()` and `writeProjectManifest()` read and write it.

## Mounts

| Mount | Points at | Access |
| --- | --- | --- |
| `assets:` | `assets/` | Read-write |
| `settings:` | `settings/` | Read-write |
| `modules:` | `modules/` | Read-write |
| `cache:` | `.illusion/cache/` | Read-write |
| `builtin:` | Built-in assets (no files) | Read-only |
| `editor:` | `editor-data/` next to the editor | Read-only |

File commands (`project.file_write`, `file_move`, `file_delete`, `file_mkdir`) work only on the writable
mounts and are undoable: replaced and deleted contents are kept in `.illusion/trash` until the session ends.

## Templates

New projects are copies of a template in `editor-data/templates/`:

| Template | Contents |
| --- | --- |
| `empty` | An empty startup scene |
| `3d-starter` | A camera, a sun, a procedural sky, a ground plane and a cube |

```cpp
auto project = illusion::editor::Project::create("C:/Games/Tetris", "Tetris",
                                                 illusion::executableDirectory() / "editor-data/templates/empty");
if (!project)
{
    ILL_LOG_ERROR("Editor", "{}", project.error());
}
```

`Project::create()` refuses a directory that is not empty; it writes the name and the engine version into
the manifest. `EditorContext::openProject()` then mounts the project, starts the crash journal and opens
the startup scene.

## Settings files

Each project settings type registered in `EditorContext::settings()` has its own file under `settings:`,
written by `project.settings_set` (see [the editor core](index.md#settings)):

```json
{
  "format": "illusion.settings",
  "type": "game.Controls",
  "version": 1,
  "value": {"invertY": true, "sensitivity": 1.5}
}
```

`version` is the data version of the struct, so files saved by an older version migrate when they are read
(`readSettingsFile()`); fields the file does not mention keep their defaults.

## Recovering a crashed session

When the editor opens a project, journals of other sessions in `.illusion/journal/` are sessions that
ended without a clean shutdown. `EditorContext::leftoverJournals()` lists them, newest first, and
`project.recover` replays the newest (or a named one) on top of the scene it started from, as one undo step.
The journal is deleted afterwards.

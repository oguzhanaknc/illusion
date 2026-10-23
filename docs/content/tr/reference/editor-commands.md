# Editör Komutları

Komut kaydından üretilen tüm editör komutları. İşaretler arasındaki tabloyu
`illusion_editor_command_reference --write docs/content` yazar; tablo güncel değilse
`editor.command_reference` CTest'i başarısız olur. `editor.describe` bir komutun tam JSON Schema'larını
döndürür; [editör çekirdeği kılavuzu](../manual/editor/index.md) Entity referanslarını, değerleri,
seçenekleri ve hataları anlatır. Özetler, ajanlar için yazıldıklarından İngilizcedir.

## Bayraklar ve izinler

| Bayrak | Anlamı |
| --- | --- |
| R | Salt okunur: hiçbir şeyi değiştirmez |
| U | Geri alınabilir: bir undo adımı olan bir transaction içinde çalışır |
| D | Yıkıcı: veriyi siler veya üzerine yazar (geri alınabilse bile) |
| I | Idempotent: iki kez çalıştırmak bir kez çalıştırmakla aynı etkiyi verir |

İzin sınıfı (`read`, `edit`, `destructive`, `files`, `build`, `system`), bir izin modunun bir ajanın neyi
çalıştırmasına izin vereceğini belirler (M7j'den itibaren).

## Komutlar

<!-- BEGIN GENERATED: editor commands -->

| Command | Category | Flags | Permission | Shortcut | Summary |
| --- | --- | --- | --- | --- | --- |
| `component.add` | Component | U | edit | - | Adds a Component (and the ones it requires) with default values, then the given fields. Example: `{"entity": "/Game/Board", "type": "illusion.LocalTransform", "value": {"position": [0, 1, 0]}}` |
| `component.get` | Component | RI | read | - | Returns a Component's value, or one field with path. Example: `{"entity": "/Game/Board", "type": "illusion.Transform", "path": "position/1"}` |
| `component.remove` | Component | UD | edit | - | Removes a Component unless another one requires it. Example: `{"entity": "/Game/Board", "type": "illusion.Disabled"}` |
| `component.reset` | Component | UI | edit | - | Gives a Component its default value. Example: `{"entity": "/Game/Board", "type": "illusion.Transform"}` |
| `component.schema` | Component | RI | read | - | Returns the JSON Schema of a Component's value (fields, types, units as ranges). Example: `{"type": "illusion.Transform"}` |
| `component.set` | Component | UI | edit | - | Merges the given fields into a Component; angles take radians or {"deg": n}, colours linear arrays or "#rrggbb". Use mergeKey for continuous edits. Example: `{"entity": "/Game/Board", "type": "illusion.Transform", "value": {"scale": [2, 2, 2]}}` |
| `component.set_field` | Component | UI | edit | - | Sets one field by path (fields and array indices joined by '/'). Example: `{"entity": "/Game/Board", "type": "illusion.Transform", "path": "position", "value": [0, 2, 0]}` |
| `component.types` | Component | RI | read | - | Lists the reflected Component types with display names, categories, icons and whether they are tags or runtime state. Example: `{"category": "Physics"}` |
| `editor.describe` | Editor | RI | read | - | Returns a command's description and the JSON Schemas (2020-12) of its params and result. Example: `{"command": "entity.create"}` |
| `editor.list_commands` | Editor | RI | read | - | Lists every command with its title, summary, category, permission class and flags. Example: `{"query": "create"}` |
| `entity.create` | Entity | U | edit | Ctrl+Shift+N | Creates an entity with a new id, an optional name, parent and position, and Components given as {stable name: value}. Example: `{"name": "Board", "parent": "/Game", "components": {"illusion.Transform": {"position": [4.5, 9.5, 0]}}}` |
| `entity.destroy` | Entity | UD | edit | Delete | Deletes entities with their descendants; entity fields elsewhere that point at them become null (undo restores both). Example: `{"entities": ["$selection"]}` |
| `entity.duplicate` | Entity | U | edit | Ctrl+D | Copies entities with their descendants and new ids next to the originals, and selects the copies. Example: `{"entities": ["/Game/Board/Cell 3"]}` |
| `entity.find` | Entity | RI | read | - | Finds entities whose name matches a * and ? pattern, ordered by id. Example: `{"name": "Cell *", "limit": 20}` |
| `entity.get` | Entity | RI | read | - | Returns an entity's id, name, path, parent, child count and Component values (optionally only some). Example: `{"entity": "/Game/Board", "components": ["illusion.Transform"]}` |
| `entity.rename` | Entity | UI | edit | F2 | Sets an entity's name (no '/', at most 256 bytes; "" removes it). Example: `{"entity": "name:Board", "name": "Playfield"}` |
| `entity.set_enabled` | Entity | UI | edit | - | Adds or removes the Disabled tag, which switches the entity and its descendants off. Example: `{"entity": "/Game/Ghost", "enabled": false}` |
| `hierarchy.children` | Hierarchy | RI | read | - | Lists an entity's children in order, or the roots without an entity. Example: `{"entity": "/Game"}` |
| `hierarchy.reorder` | Hierarchy | UI | edit | - | Moves an entity to an index among its siblings. Example: `{"entity": "/Game/Board", "index": 0}` |
| `hierarchy.set_parent` | Hierarchy | UI | edit | - | Moves an entity under a parent (absent or null: to the roots) at an index, keeping its world transform unless keepWorld is false. Example: `{"entity": "/Cell", "parent": "/Game/Board", "index": 0}` |
| `history.clear` | History | DI | destructive | - | Forgets every undo step (the scene is unchanged). Example: `{}` |
| `history.jump` | History | I | edit | - | Undoes or redoes until the given number of steps is applied (0: before the first). Example: `{"position": 3}` |
| `history.list` | History | RI | read | - | Lists the undo steps with labels and origins, the position, and the memory used. Example: `{}` |
| `history.redo` | History | - | edit | Ctrl+Y | Redoes undone steps (default 1). Example: `{}` |
| `history.undo` | History | - | edit | Ctrl+Z | Undoes the last steps (default 1) and restores the selection before them. Example: `{"steps": 2}` |
| `project.file_delete` | Project | UD | files | - | Deletes a file or a directory with its contents; undo restores them from the trash. Example: `{"path": "assets:/old"}` |
| `project.file_list` | Project | RI | read | - | Lists a directory of a project mount (assets:, settings:, modules:, cache:). Example: `{"path": "assets:/scenes", "recursive": true}` |
| `project.file_mkdir` | Project | U | files | - | Creates a directory and its missing parents. Example: `{"path": "assets:/levels"}` |
| `project.file_move` | Project | U | files | - | Moves or renames a file or directory; the target must not exist. Example: `{"from": "assets:/a.txt", "to": "assets:/docs/a.txt"}` |
| `project.file_read` | Project | RI | read | - | Reads a UTF-8 text file. Example: `{"path": "assets:/materials/frame.material"}` |
| `project.file_stat` | Project | RI | read | - | Tells whether a path exists, whether it is a directory and its size. Example: `{"path": "assets:/scenes/main.scene"}` |
| `project.file_write` | Project | UDI | files | - | Creates or replaces a UTF-8 text file; undo restores the previous contents. Example: `{"path": "assets:/notes.txt", "text": "hello"}` |
| `project.info` | Project | RI | read | - | Returns the project's name, version, root directory, startup scene, modules and mounts. Example: `{}` |
| `project.recover` | Project | - | edit | - | Replays the crash journal of a session that ended without saving, as one undo step on top of the scene it started from. Example: `{}` (the newest journal). |
| `scene.info` | Scene | RI | read | - | Returns the scene's file, revision, unsaved state and entity counts. Example: `{}` |
| `scene.new` | Scene | D | edit | Ctrl+N | Replaces the open scene with an empty, untitled one; refuses when there are unsaved changes unless discardChanges is true. Clears the undo history. Example: `{}` |
| `scene.open` | Scene | D | edit | Ctrl+O | Opens a scene file in place of the open scene (unsaved changes need discardChanges). Example: `{"path": "assets:/scenes/main.scene"}` |
| `scene.query` | Scene | RI | read | - | Finds entities that have all of 'has', none of 'without', a name matching a * and ? pattern and lie under an entity; pages with limit and cursor. Example: `{"has": ["illusion.Transform"], "name": "Cell*", "limit": 50}` |
| `scene.save` | Scene | I | files | Ctrl+S | Saves the scene to its file (scene format v2); untitled scenes need scene.save_as. Example: `{}` |
| `scene.save_as` | Scene | - | files | Ctrl+Shift+S | Saves the scene to a new file, which becomes the scene's file. Example: `{"path": "assets:/scenes/level1.scene", "overwrite": false}` |
| `scene.tree` | Scene | RI | read | - | Lists entities in pre-order with depth and child counts, for the whole scene or one subtree. Example: `{"root": "/Game", "depth": 1}` |
| `selection.add` | Selection | I | edit | - | Adds entities to the selection; the last one becomes the primary. Example: `{"entities": ["9f2c41aa0b7d3e15"]}` |
| `selection.clear` | Selection | I | edit | - | Deselects everything. Example: `{}` |
| `selection.get` | Selection | RI | read | - | Returns the selected entities, the primary (most recent) one last. Example: `{}` |
| `selection.remove` | Selection | I | edit | - | Removes entities from the selection. Example: `{"entities": ["/Game/Board"]}` |
| `selection.set` | Selection | I | edit | - | Replaces the selection (not an undo step; undo restores selections with history). Example: `{"entities": ["/Game/Board", "name:Player"]}` |

<!-- END GENERATED: editor commands -->

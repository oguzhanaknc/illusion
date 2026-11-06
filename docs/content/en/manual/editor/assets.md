# Asset Browser

The Asset Browser shows the project's `assets:` folder as a navigable grid or list, with thumbnails, search,
sorting and a kind filter. Every file operation runs an [Editor command](../../reference/editor-commands.md), so
people and AI agents rename, move, delete and reimport assets through the same API (ADR 0045).

## Navigating

The left side lists every **folder** of the project (indented by depth) and the **favourites**. Click a folder
to open it; a favourite folder opens, and a favourite file opens its folder and selects it. The toolbar's
back, forward and up buttons move through the history, and the breadcrumbs jump straight to an ancestor
folder. The current folder is the one the breadcrumbs end at.

**Refresh** rescans the mount. The editor usually keeps the catalog up to date through file watching, so this
is only for folders a watcher cannot see (for example a network drive).

## Grid and list

The **grid view** shows one tile per asset: a thumbnail where one exists, else the kind's icon, with the name
under it. The **list view** shows a table with the name, kind and size. Use the toolbar to switch, to sort by
name, kind, size or modification time, and to filter by kind. The search field filters the current folder by
name.

Thumbnails are the PNG files `asset.thumbnail` writes to the `thumbnails:` mount; the browser asks for the
visible assets' thumbnails a few at a time, so an asset without one first shows its icon and the picture
appears once it is made. A changed asset gets a new thumbnail automatically (its key includes its contents and
import settings).

Click an asset to select it (Ctrl-click selects several). Double-click opens it: a folder opens, a scene opens
in the editor, and another file opens in the code editor of the [External Tools](preferences.md).
Drag an asset onto an asset field of the Inspector (a mesh, material, clip...) to assign it there. Drag a
prefab into the viewport or the Outliner to place it, and an entity from the Outliner into the browser to make
it a [prefab](prefabs.md).

## File operations

Right-click an asset for its menu, or use the toolbar:

| Operation | What it does |
| --- | --- |
| **Open** | Opens the asset as a double-click does. |
| **Reimport** | Reimports a texture or model (see [import settings](#import-settings)). |
| **Rename** (F2) | Renames it and updates every reference to it. |
| **Move to folder…** | Moves it and updates every reference to it. |
| **Delete** | Deletes it; undo brings the file back. If something refers to it, **Delete anyway** removes it and leaves the references dangling. |
| **Reveal in the file manager** | Shows it in the system's file manager (`editor.reveal`). |
| **Copy path** | Copies its virtual path to the clipboard. |
| **New folder** | Creates a folder in the current one. |

Rename and move are one `asset.move`; deleting is `asset.delete`. Every one is a single undo step, and files
the operation changes are reported so you see what a move fixed up.

## Import settings

Select a texture or a model to edit how it is imported. A sidecar `<file>.import.json` next to the asset holds
the settings that differ from their defaults; **Apply** writes it (and deletes it when every setting is back to
default), **Revert** reloads what is on disk.

- **Textures** (`.png`, `.jpg`, `.jpeg`, `.tga`, `.bmp`, `.hdr`): the color space (automatic, sRGB or linear),
  whether mipmaps are generated, and the maximum size in pixels.
- **Models** (`.gltf`, `.glb`): a uniform scale applied to positions and node translations.

The import settings inspector also says how many files refer to the selected asset.

## Finding assets from the command palette

Type `#` in the command palette (or open it with `"mode": "assets"`) to search every asset by path. Choosing
one opens the Asset Browser on its folder with it selected.

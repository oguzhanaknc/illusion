# Editor Architecture

The editor is a separate part of the repository that games never link. This page describes how it is
organised, how the boundary to the engine is enforced, and the UI foundation it is built on. The editor is
under construction (milestone M7); this page grows with it.

## Tree and targets

Editor code lives in `editor/`:

| Path | Target | Contents |
| --- | --- | --- |
| `editor/thirdparty` | `illusion::imgui`, `illusion::implot` | Dear ImGui (docking) and ImPlot with the editor's configuration header |
| `editor/strings` | `illusion::editor_strings` | The editor's English text (`editor/data/locales/en.json`) as a string table |
| `editor/core` | `illusion::editor_core` | The headless editor core: projects, the edited scene, typed commands, transactions, undo, journals (no GPU, Dear ImGui or sockets) |
| `editor/ui` | `illusion::editor_ui` | Backends, theme, fonts, icons, per-frame text, widget kit, `UiDriver` |
| `editor/tools/ui_gallery` | `illusion_ui_gallery` | The widget catalogue used for design reviews and golden-image tests |
| `editor/tools/command_reference` | `illusion_editor_command_reference` | Writes and checks the generated [command reference](../reference/editor-commands.md) |
| `editor/data` | `editor-data/` | Themes, locales and project templates, copied next to the editor executables with the fonts |
| `editor/tests`, `editor/benchmarks` | | Editor tests and benchmarks |

`ILLUSION_BUILD_EDITOR` adds the tree; it is on when illusion is the top-level project. Editor executables
are written to `<build>/bin/<Config>/` with `editor-data/` beside them, which the editor mounts read-only as
`editor:`.

## The export boundary

Games must never contain editor code ([ADR 0030](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0030-editor-tree-and-export-boundary.md)).
Three checks enforce it:

- **At configure time.** Editor targets are created with `illusion_add_editor_library()` and
  `illusion_add_editor_executable()`. After all targets exist, CMake walks the link dependencies of every
  other target and fails with the path to any editor target it reaches:

```text
Games and the engine must not depend on editor code (see ADR 0030):
  'my_engine_lib' links the editor-only target 'illusion_editor_ui' (my_engine_lib -> helper -> illusion_editor_ui)
```

- **In CI.** `python tools/check_layering.py` rejects editor and Dear ImGui includes and editor target names
  outside `editor/`, and a **Runtime-only build** job builds and tests the engine with the editor off.
- **By design.** Engine code never checks for the editor. Panels only read editor state and submit
  commands; humans and AI agents use the same commands.

## UI stack

The editor UI uses Dear ImGui with its docking branch and our own backends
([ADR 0031](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0031-editor-ui-stack.md)):

| Piece | Role |
| --- | --- |
| `UiContext` | Dear ImGui and ImPlot contexts of one window; settings kept in memory; counts Dear ImGui allocations |
| `UiPlatformBackend` | Window events from the EventBus into Dear ImGui; cursor, clipboard and IME requests back to the window |
| `UiRenderer` | Draws Dear ImGui with NVRHI; dynamic font atlas; textures the editor registers (viewports) |
| `Theme` | Colour tokens and design metrics turned into a Dear ImGui style |
| `FontManager` | Roboto, Roboto Mono and Material Symbols, rasterised on demand at the size in use |
| `FrameText` | Localized labels with stable ids, built per frame in an arena |
| `Widgets.hpp` | The widget kit: icon buttons, toggles, segmented controls, search fields, vector fields, component cards, property tables, asset tiles, toasts |
| `UiDriver` | Finds items by stable id and clicks, drags and types, for tests and automation |

A frame of the editor UI:

```cpp
backend.newFrame(deltaSeconds);
ImGui::NewFrame();
text.reset();
drawPanels(kit);            // widgets read state and submit commands
ImGui::Render();

commands->open();
commands->clearTextureFloat(uiTarget, nvrhi::AllSubresources, nvrhi::Color(0.0f));
renderer->render(commands, uiFramebuffer, ImGui::GetDrawData());
renderer->present(commands, uiTarget, swapchain->currentFramebuffer());
commands->close();
```

### Colour pipeline

Dear ImGui's colours and blending assume gamma space, so the UI is drawn into an `RGBA8_UNORM` UI target
that holds sRGB-encoded values. `UiRenderer::present()` copies the target to the swapchain: for `_SRGB`
swapchains it decodes the values first, so that the hardware's encoding writes them back unchanged, and for
UNORM swapchains it copies them as they are. Both show identical colours, which a GPU test checks.
Screenshots read the UI target directly.

### Stable ids

Every interactive item has a label of the form `"<localized text>###<stable id>"`. Dear ImGui hashes only
the part after `###`, so an item keeps its id, its saved state and its docking position in every language,
and tests and agents address it by the same id:

```cpp
if (ImGui::Button(text.label(strings::kCommonSave, "inspector.save")))   // "Save###inspector.save"
{
    save();
}
ui::vec3Field(kit, "inspector.transform.position", position);            // items ".x", ".y", ".z"
```

Ids are lowercase and dot-separated, from the panel down: `outliner.search`, `inspector.transform.position.x`.

## Theme

A theme is a JSON file with 28 colour tokens; every theme defines every token:

```json
{"format": "illusion.theme", "version": 1, "name": "illusion Dark", "highContrast": false,
 "tokens": {"bg.base": "#0F1115", "bg.panel": "#16191F", "text.primary": "#E3E6EB", "accent": "#4C8DFF"}}
```

| Group | Tokens |
| --- | --- |
| Surfaces | `bg.base`, `bg.panel`, `bg.raised`, `bg.input`, `bg.hover`, `bg.active`, `border`, `separator` |
| Text | `text.primary`, `text.secondary`, `text.disabled` |
| Accent | `accent`, `accent.fill`, `accent.text`, `accent.subtle` |
| Status | `success`, `warning`, `error`, `info`, `play.tint` |
| Viewport | `viewport.selection`, `viewport.hover`, `axis.x`, `axis.y`, `axis.z`, `axis.cb.x`, `axis.cb.y`, `axis.cb.z` |

The editor ships **illusion Dark** (default), **illusion Light** and **High Contrast**. Tests enforce the
WCAG contrast ratios: text at least 4.5:1 on every surface (7:1 in High Contrast) and graphics at least 3:1.
Accent presets (Azure, Violet, Teal, Amber) replace only the accent tokens.

The metrics follow a 4 px grid: window padding 10, frame padding 8×5, item spacing 8×6, rounding 6 for
windows and popups, 4 for frames and 5 for tabs, 1 px separators instead of boxes, and a 2 px accent line on
the active tab. `Theme::apply()` rebuilds the whole style from the tokens and the metrics times the scale
(user UI scale times the monitor's content scale) whenever one of them changes.

## Fonts and icons

| Role | Font | Size |
| --- | --- | --- |
| Body | Roboto Regular | 15 px |
| Caption | Roboto Regular | 13 px |
| Strong (headers, tabs, buttons) | Roboto Medium | 15 px |
| Title | Roboto Bold | 17 px |
| Mono (console, code, numbers) | Roboto Mono | 14 px |
| Icon (toolbars) | Material Symbols Rounded | 20 px |

Material Symbols are merged into every text font, so labels can mix icons and text. Fonts are rasterised
on demand at the size in use; UI scale and DPI only change `ImGuiStyle::FontScaleDpi`.

Icon constants are generated from the pinned codepoints file, so an unknown icon is a compile error:

```cpp
#include <illusion/editor/ui/Icons.hpp>

ui::iconButton(kit, icons::kPlayArrow, "toolbar.play", text.format(strings::kToolPlay), "Ctrl+P");
const std::optional<ui::Icon> icon = ui::findIcon(componentMeta.icon);   // names known at run time
```

## The UI gallery

`illusion_ui_gallery` shows every widget, the typography and icons, and an editor-style layout with sample
data. It is the reference for design reviews:

```bash
illusion_ui_gallery --theme light --scale 1.5
illusion_ui_gallery --headless --page editor --locale en-XA --screenshot editor.png
```

In a window, F2 cycles the theme, F3 the page and F4 the locale (English or the en-XA pseudo-locale). The
editor GPU tests compare gallery captures with the golden images in `editor/tests/data/golden`, and CI
publishes every capture as the `ui-captures` artifact.

## Editor core

`illusion::editor_core` is the editor without a window. `EditorContext` owns the project's
`VirtualFileSystem`, one World with the open scene, the `EntityIndex` (ids, paths and names), the
`Selection`, the `UndoStack`, the `ChangeJournal`, the `EventHub` and the `CommandRegistry`
([ADR 0036](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0036-editor-command-core.md)). Every
change goes through a command:

```text
UI widget / shortcut / palette / RPC / MCP
        |  execute<Result>(name, params)  or  executeJson(name, json)
        v
EditorContext::run()  -- validate params (reflected structs, JSON Pointers, suggestions)
        |  begin transaction, run the handler
        v
WorldEditor           -- validated primitives, each recording reversible operations
        |  commit: UndoStep (merge keys, budget), crash journal line, change journal revision, events
        v
World + project files
```

Operations are Component values before and after (scene v2 JSON), entity snapshots that appear or
disappear, values of scene settings, and file changes backed by blobs in `.illusion/trash`. Undo, redo,
rollback of failed commands and dry runs, and crash recovery all replay them, so the scene after undoing
everything is byte for byte the scene before. After each change the editor derives the world Transforms of
children again; it writes only those derived values, which scene files do not keep, so replaying operations
stays exact.

A transaction can also span commands (`transaction.begin`, `editor.batch`): each command still commits and
journals its own operations and ends a revision, but they collect in the open transaction instead of the
history and become one undo step at the commit. The transaction belongs to a caller, so an RPC connection
owns its own and other callers wait (`busy`) until it ends. `history.undo {"scope": "mine"}` undoes one
origin's step past later independent steps of others by moving it to the top first, which is exact because
independent steps commute. Async commands are tasks that `RunningCommand::update()` advances once per frame;
the RPC server advances them in `pump()` and reports `rpc.progress`.

Two more parts sit beside the World: the `SettingsCatalog` (the reflected structs that scenes and the
project keep as settings) and the `ReferenceIndex`, which knows the asset references of every scene and
material file under `assets:/` (found through reflection, parsed again only when a file's contents change)
so that `asset.move` can rewrite them and `asset.delete` can refuse. The manual describes
[using it](../manual/editor/index.md).

## The MCP bridge

`illusion_mcp` is a separate process between an MCP client and the editor
([ADR 0039](https://github.com/oguzhanaknc/illusion/blob/main/docs/adr/0039-mcp-bridge.md)):

```text
MCP client --stdio--> StdioTransport --> McpServer (illusion::mcp_protocol) --> EditorToolProvider --> RpcClient --> editor
                                            |  era, tools, results, resources       |  launch/attach, crash relaunch,
                                            |  progress, cancellation, prompts      |  rpc.progress, rpc.cancel, log ring
```

`illusion::mcp_protocol` is pure (core Json only): `McpServer` takes one line at a time and writes through an
`McpTransport`; a `ToolProvider` runs the commands. It generates the tools from the OpenRPC document of
`rpc.discover`, so it never needs to know a command. `illusion::editor_client` holds the editor side
(`EditorToolProvider`) and an MCP client of the bridge (`McpClient`) that the acceptance runner uses for
`--via-mcp`. A tool call runs inside `McpServer::handle()`; while it waits, the provider calls
`CallControl::service()`, which takes cancellations and pings at once and keeps other requests for later.
The [AI agents manual](../manual/editor/ai-agents.md) describes the result.

## Performance

- Idle UI frames do not allocate: labels are built in a per-frame arena, renderer buffers only grow, and a
  test counts both global and Dear ImGui allocations over gallery frames.
- The benchmark `ui/gallery-frame` measures the CPU cost of a frame of the densest gallery page (about 40 µs
  on a desktop; budget 400 µs).
- The editor core's benchmarks: `editor/component-set` (one merged `component.set`), `editor/undo-capture-delete`
  (destroying and restoring a subtree, per entity), `editor/scene-query` (per entity searched),
  `editor/transform-translate` (one merged `transform.translate` four levels deep, like a gizmo drag) and
  `editor/reference-refresh` (checking an unchanged scene file for the reference index), `editor/batch-step`
  (one step of an `editor.batch` with a `$` reference, undone with the batch) and `editor/undo-own-check`
  (checking one later step before an undo of one's own steps).
- `mcp/translate` measures one MCP `tools/call` through the bridge's server with an immediate provider: parsing,
  finding the tool, writing the result (about 1.6 µs on a desktop; budget 10 µs).

## Accessibility

The editor supports full keyboard navigation, a High Contrast theme, a colour-blind axis preset (axis
handles also carry letters), UI scale from 75% to 200% and reduced motion. Dear ImGui does not expose the UI
to screen readers.

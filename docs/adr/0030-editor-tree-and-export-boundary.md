# 0030. A separate editor tree, a command-first editor and an enforced export boundary

- Status: Accepted
- Date: 2026-10-20

## Context

M7 adds an editor that humans and AI agents control. Games exported from illusion must not contain editor
code: it would bloat the player, ship third-party code that is not licensed or audited for games (Dear
ImGui, ImPlot, editor fonts), and tempt engine code to grow `#ifdef EDITOR` branches that make the shipped
runtime differ from the tested one. At the same time the editor must reuse the engine as it is, and agents
must be able to do everything a human can.

Options considered:

- **Editor code inside the engine libraries behind a define.** Simple to start, but every engine target
  then has two variants, the boundary is a convention that nothing checks, and an exported game can link
  editor code by accident.
- **A separate repository for the editor.** A hard boundary, but engine and editor change together during
  M7, and two repositories make every cross-cutting change two pull requests and two CI runs.
- **A separate tree in this repository with a checked boundary.** Engine and editor evolve together; the
  build enforces the direction of dependencies.

For control, the editor can expose its UI to agents (agents click widgets) or expose commands that the
UI and agents both call. Driving widgets is brittle (layouts change, text is translated) and slow.

## Decision

- **Tree.** Editor code lives in `editor/` (`thirdparty/`, `strings/`, `ui/`, later `core/`, `rpc/`,
  `app/`, ...), with its own tests (`editor/tests`) and benchmarks (`editor/benchmarks`) so that a build
  without the editor contains no editor test either. `ILLUSION_BUILD_EDITOR` (default: on when illusion
  is the top-level project) adds the tree.
- **Editor-only targets.** Every editor target is created with `illusion_add_editor_library()` or
  `illusion_add_editor_executable()` (`cmake/IllusionEditor.cmake`), which set the target property
  `ILLUSION_EDITOR_ONLY`. Editor executables are written to `<build>/bin/<Config>/` next to
  `editor-data/` (themes, locales, fonts and their licences), which the editor mounts read-only as
  `editor:`.
- **Boundary check at configure time.** `illusion_check_editor_boundary()` runs once after every target
  exists (`cmake_language(DEFER)`). It walks `LINK_LIBRARIES` and `INTERFACE_LINK_LIBRARIES` (including
  `$<LINK_ONLY:...>` and aliases) from every target that is not editor-only and fails the configure with
  each path that reaches an editor-only target. A fixture project (`tests/cmake/editor_boundary`) proves
  that the check fires (CTest `editor.boundary`).
- **Layering check in CI.** `tools/check_layering.py` (Format job) rejects editor and Dear ImGui includes
  and editor target names in `engine/`, `samples/`, `tools/`, `tests/` and `benchmarks/`, also in code
  that the current configuration does not build, and relative includes from the editor into engine
  sources.
- **Runtime-only build in CI.** A Linux GCC job configures with `-DILLUSION_BUILD_EDITOR=OFF`, verifies
  that no editor target exists, and builds and tests the engine.
- **No editor define in the engine.** Engine code never branches on the editor. Features the editor needs
  from the engine (entity ids, debug drawing, readbacks) are opt-in runtime features that games may use
  too.
- **Command-first editor.** Every change the editor makes goes through typed commands (M7i) that the UI,
  the JSON-RPC server and the MCP bridge all call. Panels read `const` views of editor state and submit
  commands; agents never need to drive widgets. Widgets still carry stable `###ids` so that tests and
  agents can find them (`UiDriver`), independent of the language.

## Consequences

- Linking editor code from engine code is a configure error with the offending path, and including
  editor headers is a CI error, so the boundary holds without review discipline.
- Games and exports never contain Dear ImGui, ImPlot or the editor fonts; their licences are listed with
  "Shipped: No".
- Editor tests live outside `tests/` and need `editor-data/`, so they are editor executables in
  `bin/<Config>/`; shared test helpers from `tests/` are reused by path.
- The command layer must be designed before panels that change state (M7i before M7l); panels written
  earlier (the gallery) only demonstrate widgets.
- A symbol scan of the exported player (M8) will add a check after linking.

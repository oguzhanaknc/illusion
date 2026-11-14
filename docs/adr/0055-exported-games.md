# 0055. Export games by building a player from the SDK

- Status: Accepted
- Date: 2026-11-14

## Context

A project runs only inside the editor so far. The brief asks for an "Export" that produces a clean runtime
build: the game's modules linked statically, packed assets, no editor code, and packages for Windows
(signing-ready), macOS (`.app`, notarization-ready) and Linux (Steam Runtime "sniper" compatible).

The pieces exist: `GameSession` runs a game the same way in the editor and outside it (ADR 0042), modules
build as static libraries from the same sources (ADR 0041), `cookAssets()` and `.illpak` archives ship
cooked assets without their sources (ADR 0019), and the editor already builds a project's modules by running
CMake against a package of the engine (ADR 0043). What is missing is the executable, the way it is built on
the machine of someone who has the editor but not the engine's build tree, and the packaging.

Options considered for the executable:

- **A prebuilt generic player that loads the modules as shared libraries.** No compiler needed to export,
  but it contradicts the brief, ships the hot-reload boundary (one copy of `illusion::core` per module,
  services behind interfaces) and leaves loose libraries to sign and to break.
- **Build the engine from source in every export.** Always consistent, but needs the engine's sources and
  its dependencies on the exporting machine and costs minutes per project.
- **Link prebuilt engine libraries.** The SDK grows a second component with the static libraries of the
  engine; an export compiles only the project's modules and a generated `main.cpp`, then links.

Options considered for where the game's description lives (name, startup scene, modules):

- **A loose file next to the executable.** Editable after export, but it can be lost or changed, and the
  module list is fixed at link time anyway.
- **Compiled into the executable** by the generated `main.cpp`.

Options considered for packaging:

- **Own code per format** in the editor: three more formats to write and test.
- **CMake's install rules and CPack** in the generated project: the bundle layout, archives and installers
  are CMake's, and the editor already runs CMake as a child process.

## Decision

- **Player library.** `engine/player` builds `illusion::player`, a static library with one entry point,
  `runPlayer()` (`illusion/player/Player.hpp`). It owns what the samples wired by hand: the window, the
  render device and swapchain, the Job System, the asset streamer, gamepads, a `ModuleHost` with the game's
  static modules, a `GameSession` and a `SceneRenderer`. It mounts `assets`, `settings` and `cache` from the
  archives of the data directory (`cache` without source verification) and `user` on
  `userDataDirectory(organization, name)`, loads the project settings and the startup scene, and runs until
  the window closes or a module asks to quit. A start that fails (no Vulkan device, missing data) is logged
  to `user:/player.log` and shown in a native message box.
- **Window settings.** `WindowSettings` joins the project settings (`settings:/window.json`, type
  "illusion.WindowSettings"): `width`, `height`, `mode` (Windowed, Borderless, Fullscreen), `resizable`.
  The player applies it; the editor's Game view ignores it.
- **The game's description is compiled in.** The generated `main.cpp` passes a `PlayerDesc` (name,
  organization, version, startup scene, static module entries) to `runPlayer()`. Nothing next to the
  executable is needed to start except the data directory.
- **Player SDK.** The CMake package gets the component `player`: `find_package(illusion CONFIG COMPONENTS
  player)` imports `illusion::player` with every engine and third-party static library it links, and defines
  `illusion_add_game(<target> MODULES <names...>)`, which builds the modules of the project as static
  libraries and the executable. `illusion-sdk.json` becomes version 2 with `"player": true` when the
  component exists for that configuration. A build tree exports it in place; `cmake --install` writes a
  relocatable copy (headers, libraries per configuration, the package) that the installed editor points at.
  The package holds no editor target, so an exported game cannot link editor code; a test scans the
  exported executable for editor and Dear ImGui symbols (ADR 0030).
- **Export settings.** `settings:/export.json` (type "illusion.ExportSettings", reflected in the editor, not
  shipped): product name, organization, bundle identifier, copyright, icon (a PNG asset) and exclude
  patterns. The version is the project's.
- **Export is an async editor command.** `export.build` (class Build) runs, in order: validate (the SDK has
  the player for the configuration, the startup scene and modules exist); generate
  `.illusion/export/<platform>/` (CMakeLists.txt, `main.cpp`, icon and version resources); cook every asset
  under `assets:/` into a fresh cache and pack `assets.illpak` (without the sources the cache replaces and
  the excluded files), `settings.illpak` (without `export.json`) and `cache.illpak`; configure and build with
  the SDK's toolchain, reporting diagnostics as `module.build` does; install into the output directory; and
  optionally package. It returns a report with every shipped file and its size. The same command works in
  `illusion_editor --headless`, which is how CI and build machines export.
- **Layout.** Windows and Linux: `<Name>/<name>[.exe]` and `<Name>/data/*.illpak`. macOS: `<Name>.app` with
  the executable in `Contents/MacOS`, the archives in `Contents/Resources/data`, MoltenVK in
  `Contents/Frameworks` and a generated `Info.plist`.
- **Packages come from CPack** in the generated project: ZIP everywhere, NSIS on Windows, a DMG on macOS,
  a `.tar.gz` on Linux. A missing packaging tool is an error that names it.
- **Signing is a hook, never a secret in the project.** Windows: a command line with `{file}` (for example
  `signtool sign ...`) from the command's params or `ILLUSION_SIGN_COMMAND`, run on the executable in the
  build tree and on the installer. macOS: a `codesign` identity (`ILLUSION_CODESIGN_IDENTITY`; `-` signs ad
  hoc) with the hardened runtime and generated entitlements, and a `notarytool` keychain profile
  (`ILLUSION_NOTARY_PROFILE`) that submits and staples the DMG. Signing happens before staging, so the
  output directory and every package hold the same signed files. Without the settings the export is
  unsigned and the report says so.
- **Linux and the Steam Runtime.** An export is as portable as the machine that links it. For Steam, the
  export runs headless inside the Steam Runtime "sniper" SDK container with the SDK built there; the
  `linuxRuntime: "sniper"` param then checks the executable's glibc symbol versions and needed libraries
  against the runtime and fails the export when it exceeds them. Runtime libraries are found through
  `$ORIGIN`.
- **No cross-compilation.** An export targets the platform and architecture of the SDK that builds it.
- **Configurations.** `export.build` takes a configuration of the SDK (default `Release`); the modules are
  compiled with that configuration's flags, so the build key matches (ADR 0027).
- **Releases of the engine itself** use the same install rules: a tagged build packages the editor, its
  tools, the data files and the SDK per platform, and CI exports the showcase and the acceptance game from
  that installed package and runs them.

## Consequences

- Exporting needs a C++ compiler and CMake, as building modules already does; in exchange a shipped game is
  one executable with no module boundary, and link-time dead-code removal applies to the whole game.
- The first export of a project costs a configure and a link (tens of seconds); later ones rebuild only what
  changed, and cooking reuses nothing from the project's cache so that a stale cache cannot ship.
- Every engine library and third-party target becomes part of an installed CMake package; a dependency that
  cannot be exported needs a wrapper target. Renaming an exported target is an incompatible change.
- Changing the name, version or startup scene means exporting again; there is no loose file to edit.
- All files under `assets:/` ship unless excluded. Shipping only what scenes reach needs a dependency walk
  that also covers scenes loaded by name; revisit when a project's size asks for it.
- Windows installers need NSIS and Linux Steam builds need the sniper container on the exporting machine;
  both are documented in the release guide and present in CI.
- Signing and notarization run only with a certificate, which CI does not have: CI covers the hooks with a
  recording command on Windows and an ad hoc identity on macOS, and the real path is a documented manual
  check before a release.
- The Steamworks module is a separate decision (its own ADR in M8-6); it links through `illusion_add_game()`
  like any other optional library.

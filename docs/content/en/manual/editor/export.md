# Exporting a game

An export turns a project into a game that runs without the editor: one executable with the project's
[game modules](../modules.md) linked in, and a `data/` directory with the cooked and packed assets (ADR 0055).
`export.build` does it, from the editor's menu, from an AI agent or from `illusion_ctl`; the same command works
in a [headless editor](automation.md), which is how a build machine exports.

## Prerequisites

- A C++ compiler and CMake, the same ones [Project Modules](project-modules.md) uses. The first export of a
  project configures and links, which takes minutes; later ones rebuild only what changed.
- An SDK with the [player](../runtime.md#the-player) for the configuration you export. A build tree of the
  engine provides it (`<build>/sdk/<Config>/illusion-sdk.json` with `"player": true`), and so does an
  installed engine. See [Player SDK](../modules.md#player-sdk).
- A project with a startup scene (`project.illusion`) under `assets:/`.

An export targets the platform and architecture of the machine that builds it; there is no cross-compilation.

## Export settings

`settings:/export.json` (type `illusion.ExportSettings`) describes the game. It is a
[project setting](../runtime.md#project-settings) you edit with `project.settings_get` and
`project.settings_set`, and it is never part of an exported game.

| Field | Meaning |
| --- | --- |
| `productName` | Name of the game and of its directory. Empty uses the project's name. The executable is this name in lower case with other characters joined into underscores, so it must start with a letter. |
| `organization` | Publisher; parent of the game's user data directory. Empty uses the product name. |
| `identifier` | Reverse-DNS identifier such as `com.example.game`, at least two parts. |
| `copyright` | Copyright line of the game. |
| `icon` | Square PNG asset of at least 256 pixels. |
| `exclude` | Files under `assets:/` that are not shipped. |

The version of the game is the project's `version`. The next section says what each platform makes of
`identifier`, `copyright` and `icon`.

### Icon, identifier and copyright

The icon is a square PNG of at least 256 pixels under `assets:/`; without one the game gets the engine's icon.
The export makes every size a platform needs from it with `illusion::icon`.

| Platform | Icon | `identifier` | `copyright` |
| --- | --- | --- | --- |
| Windows | An `.ico` with every size from 16 to 256 pixels, embedded in the executable and used by the installer. | Not used. | `LegalCopyright` of the executable's version resource, next to the product name, the version and the organization (`CompanyName`). |
| macOS | An `.icns` in the bundle's `Contents/Resources`. | The bundle identifier in `Info.plist`. Empty: `com.<organization>.<name>`, made of the organization and the product name. | `NSHumanReadableCopyright`. |
| Linux | `<executable>.png` (512 pixels) next to the executable. | Not used. | Not used. |

The copyright line is also the second line of the license text of the Windows installer. The export checks the settings before it writes anything: an icon that is not a PNG, not square or smaller than 256
pixels, an `identifier` that is not reverse-DNS and a `copyright` of more than one line each fail the command.

### Exclude patterns

A pattern is a path relative to `assets:/`, with `/` separators. `*` matches a run of characters inside one
directory, `**` matches any run including directories, and `**/` also matches no directory at all:

```json
{"exclude": ["raw/**", "*.psd", "**/notes.txt"]}
```

An excluded file is neither packed nor cooked. An asset that needs it, for example a material that names an
excluded texture, makes the export's cook fail, and so does excluding the startup scene. The report lists
what was left out in `skipped`.

## The Export window

**File > Export…** (also in the command palette) opens the Export window. It edits the same
`settings:/export.json` and calls the same `export.build`, so what it does an agent can do.

- **Settings**: Product name, Organization, Identifier, Copyright, Icon (an asset path) and Excluded files (one
  pattern per line), as in [Export settings](#export-settings). The hints show what an empty field means, for
  example the project's name for the product name. Version is read-only: set it in `project.illusion`. An error
  of `project.settings_set` appears under the field it belongs to.
- **Target**: Platform is the one the editor runs on; export on each platform you ship. Package offers the kinds
  of this platform: Folder only, Folder and ZIP archive, Folder and installer (NSIS) on Windows, Folder and disk
  image (DMG) on macOS, Folder and .tar.gz archive on Linux. Output folder and Configuration are the `output` and
  `configuration` params; Clean build is `clean`.
- **Signing**: Sign command on Windows; Codesign identity and Notary profile on macOS. An empty field uses its
  environment variable, which the hint names. Linux shows "Linux exports are not signed."
- **Export** starts the command. While it runs the window shows the stage (for example "Cooking assets") with a
  progress bar and **Cancel**, and the fields are disabled.
- **Result** shows Exported or the failed stage, the duration, the Game folder, the Package file, the Size, the
  files left out by the exclude patterns and the Signed and Notarized badges. Diagnostics list errors and
  warnings; a click on one with a source file selects it in the Asset Browser. Tool output holds the last lines
  of the build tools, and **Show in folder** opens the game's folder in the file manager.

Problems that `export.build` reports in one message (product name, identifier, organization, exclude patterns)
appear in the result, not next to their field.

## Running an export

An agent calls the command; the output path is absolute or relative to the project:

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "configuration": "Release", "clean": true}}
```

From a shell, `illusion_ctl` calls it on the project's editor, as described in [Automation](automation.md):

```bash
illusion_ctl --project ~/games/tetris call export.build '{"output": "/tmp/tetris-out", "configuration": "Release"}'
```

| Param | Meaning |
| --- | --- |
| `output` | Directory that receives the game. Default `.illusion/export/<platform>/out` in the project. It must not lie inside the project's `assets`, `settings` or `modules` directories. |
| `configuration` | Configuration of the SDK to build with. Default `Release`, else the editor's own. The modules compile with its flags. |
| `clean` | Discards the export's build tree and configures again. |
| `package` | `none` (default), `archive` or `installer`; see [Packages](#packages). |
| `signCommand` | Windows: the command line that signs a file; see [Signing](#signing). |
| `codesignIdentity` | macOS: the `codesign` identity. |
| `notaryProfile` | macOS: the `notarytool` keychain profile that notarizes the DMG. |
| `linuxRuntime` | Reserved for the Steam Runtime check; leave it empty. |

`export.build` is [asynchronous](automation.md): it reports its stage as progress and returns when the export
is done. Only one module build or export runs at a time; a second one fails with a retryable `Busy`.

## Stages

1. **validate**: the project, its settings, its modules and the SDK are checked; every problem is listed in one
   error, and nothing is written.
2. **generate**: writes the game's build under `.illusion/export/<platform>/` (`CMakeLists.txt`, the file lists).
3. **cook**: imports every asset under `assets:/` into a fresh cache; a stale cache cannot ship.
4. **pack**: writes `assets.illpak`, `settings.illpak` and `cache.illpak`.
5. **configure** and **build**: `cmake` configures and builds the executable from the modules and the SDK's
   player, then signs what it built (see [Signing](#signing)).
6. **install**: installs into a staging directory and moves it to `<output>/<Product>` only when complete
   (`<output>/<Product>.app` on macOS).
7. **package**: with `package` set, CPack writes the package; the installer is signed and, on macOS, notarized.

## Output

```text
<output>/
  Showcase/
    showcase.exe          the game (no extension on Linux and macOS)
    data/
      assets.illpak       the project's assets, minus excluded files and glTF sources
      settings.illpak     the project settings, without export.json
      cache.illpak        the cooked assets
```

On Linux the icon and the desktop file sit next to the executable (see [Linux](#linux)); on macOS the game is a
bundle (see [The macOS bundle](#the-macos-bundle)).

glTF sources (`.gltf`, `.glb` and the buffers they name) are dropped because the cache replaces them; image
sources stay in `assets.illpak`. The executable contains no editor code.

## Packages

`package` makes one file for distribution next to the game's directory, in `output`. The file is named
`<Product>-<version>-<platform><suffix>`; the version is the project's, with characters that are not letters,
digits, `.`, `-`, `+` or `_` written as `_`.

| `package` | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `none` | Only the game's directory. | Only the bundle. | Only the game's directory. |
| `archive` | `Showcase-1.0.0-windows.zip` with the `Showcase` directory. | `Showcase-1.0.0-macos.zip` with `Showcase.app`. | `Showcase-1.0.0-linux.tar.gz` with the `Showcase` directory. |
| `installer` | `Showcase-1.0.0-windows-setup.exe`, an NSIS installer. | `Showcase-1.0.0-macos.dmg` with the bundle and a link to `/Applications`. | The same `.tar.gz` as `archive`. |

The Windows installer installs into `Program Files\<Product>`, adds a Start menu shortcut, an uninstaller and the
game's icon, and removes an older installation first. The result's `package` is the full path of the file; a
failed export leaves none.

The packages are made by CPack from the SDK's CMake, which must sit next to its `cpack`. The installers need one
more tool, and a missing tool fails the validation with an error that names it:

- Windows: `makensis` from [NSIS](https://nsis.sourceforge.io), on `PATH` or in `Program Files\NSIS`;
- macOS: `hdiutil`, which every macOS has.

## Signing

Signing is optional and never changes what the game does. A setting that is not given in the params comes from
an environment variable of the editor's process, so a build machine keeps its secrets out of commands and
projects; an empty string in the params turns that setting off for this export. Secrets are never written to
project files, and the output shows only the name of the program that `signCommand` runs, never its arguments,
which may hold a password. A setting of the other platform is an error: `signCommand` on macOS and Linux,
`codesignIdentity` and `notaryProfile` on Windows and Linux.

Signing happens before the install, on the files the build made. The game's directory and every package are
copied from those files, so they hold the same signed bytes, and the report's `signedBinaries` is true.

### Windows

`signCommand` (default: the environment variable `ILLUSION_SIGN_COMMAND`) is a command line with a program and
the placeholder `{file}`. Double quotes group a part that contains spaces; backslashes need no escaping. The
export runs it on the executable and, for `package: "installer"`, on the setup program:

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "package": "installer", "signCommand": "signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a {file}"}}
```

The program must be on `PATH`. A command without a program and `{file}` fails the validation.

### macOS

- `codesignIdentity` (default: `ILLUSION_CODESIGN_IDENTITY`) is the identity that `codesign` uses, for example
  `Developer ID Application: Example Ltd (TEAMID)`. The export signs every library in `Contents/Frameworks`, then
  the bundle, with the hardened runtime and a timestamp. A bundle that you ship needs a Developer ID identity.
- The identity `-` signs ad hoc: no certificate and no timestamp, good for a local check that the bundle starts.
  The generated entitlements then disable library validation so that the bundled Vulkan loader loads; a real
  identity needs no entitlement.
- With the `installer` package the DMG is signed too, unless the identity is `-`.
- `notaryProfile` (default: `ILLUSION_NOTARY_PROFILE`) names a keychain profile of `notarytool`. The export
  submits the DMG with `xcrun notarytool submit --wait`, then staples the ticket with `xcrun stapler staple`, and
  the report's `notarized` is true. It needs a Developer ID identity (not none and not `-`), `package:
  "installer"` and `xcrun`.

Create the profile once on the machine that exports; the password is an app-specific password of your Apple ID,
and it stays in the keychain:

```bash
xcrun notarytool store-credentials "illusion-notary" --apple-id you@example.com --team-id TEAMID
```

The profile name `illusion-notary` is what you pass as `notaryProfile`. A failed signing or notarization quotes
the last lines of the tool in the error, and the export fails with no package.

Without any of these settings the export is unsigned: `signedBinaries` and `notarized` are false.

## The macOS bundle

On macOS the game is `<Product>.app`, with the executable named after the product:

```text
Showcase.app/
  Contents/
    Info.plist                   bundle identifier, name, version, copyright, icon, minimum macOS
    MacOS/Showcase               the game
    Resources/
      data/*.illpak              the archives
      *.icns                     the icon
      vulkan/icd.d/MoltenVK_icd.json
    Frameworks/
      libvulkan.1.dylib          the Vulkan loader
      libMoltenVK.dylib          MoltenVK
```

The bundle carries its own Vulkan loader and MoltenVK, so it runs on a Mac without a Vulkan SDK. The export
copies them from the machine that builds: the Vulkan SDK (set `VULKAN_SDK`) or Homebrew
(`brew install vulkan-loader molten-vk`). When neither is found, CMake warns and the bundle needs a Vulkan loader
on the player's Mac.

## Linux

The game's directory holds, besides the executable and `data/`, `<executable>.png` (the icon, 512 pixels) and
`<executable>.desktop`, a desktop entry with the product name, the executable as `Exec`, the icon and the
category `Game`. Copy the three into a desktop's application directories to list the game in menus. The
`.tar.gz` package holds them too.

## The report

The result has `succeeded`, the `stage` it reached, `error`, `platform`, `configuration`, `directory` (the game's
directory), `files` (every shipped file with its size, relative to `output`), `totalBytes`, `skipped`, `output`
(the last lines of tool output) and the counts `errors` and `warnings`. `package` (the full path of the package file, empty
without one), `signedBinaries` (the executable or bundle was signed) and `notarized` (the DMG was notarized and
stapled).

## When an export fails

A validation problem fails the command with a message that names each cause. A failed cook, compile or link does
not: the command succeeds with `succeeded: false`, the `stage` that failed and `diagnostics` with file, line and
message, exactly like `module.build`. The staging directory is removed, so the output never holds a partial game.
Read `diagnostics` first and `output` for the raw tool lines.

## Not there yet

A non-empty `linuxRuntime` is rejected with "not supported yet": the check of the executable against the Steam
Runtime comes with the Steam work (M8-6). The [release guide](../../guides/release.md) says what you can do
until then.

## Limits

- Everything under `assets:/` ships unless excluded; the export does not walk what scenes reach.
- Changing the name, version or startup scene means exporting again.
- The first export configures CMake and takes minutes.
- Exclude patterns are the only way to leave files out, and an asset that depends on an excluded file fails
  the cook.
- Image sources stay in `assets.illpak` next to their cooked form; only glTF sources are dropped.
- Signing with a real certificate and notarization are not tested in CI, which has no certificate: check a
  signed package by hand before every release (`codesign --verify`, `spctl`, a start on a clean machine).
- A failed Package stage keeps the finished game directory.

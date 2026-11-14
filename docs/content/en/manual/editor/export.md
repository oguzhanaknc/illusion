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

The version of the game is the project's `version`. `identifier`, `copyright` and `icon` are checked and kept in
the settings, but the executable does not use them yet; they take effect with the packages of M8-4.

### Exclude patterns

A pattern is a path relative to `assets:/`, with `/` separators. `*` matches a run of characters inside one
directory, `**` matches any run including directories, and `**/` also matches no directory at all:

```json
{"exclude": ["raw/**", "*.psd", "**/notes.txt"]}
```

An excluded file is neither packed nor cooked. An asset that needs it, for example a material that names an
excluded texture, makes the export's cook fail, and so does excluding the startup scene. The report lists
what was left out in `skipped`.

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

`export.build` is [asynchronous](automation.md): it reports its stage as progress and returns when the export
is done. Only one module build or export runs at a time; a second one fails with a retryable `Busy`.

## Stages

1. **validate**: the project, its settings, its modules and the SDK are checked; every problem is listed in one
   error, and nothing is written.
2. **generate**: writes the game's build under `.illusion/export/<platform>/` (`CMakeLists.txt`, the file lists).
3. **cook**: imports every asset under `assets:/` into a fresh cache; a stale cache cannot ship.
4. **pack**: writes `assets.illpak`, `settings.illpak` and `cache.illpak`.
5. **configure** and **build**: `cmake` configures and builds the executable from the modules and the SDK's
   player.
6. **install**: installs into a staging directory and moves it to `<output>/<Product>` only when complete.

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

glTF sources (`.gltf`, `.glb` and the buffers they name) are dropped because the cache replaces them; image
sources stay in `assets.illpak`. The executable contains no editor code.

## The report

The result has `succeeded`, the `stage` it reached, `error`, `platform`, `configuration`, `directory` (the game's
directory), `files` (every shipped file with its size, relative to `output`), `totalBytes`, `skipped`, `output`
(the last lines of tool output) and the counts `errors` and `warnings`. `package`, `signedBinaries` and
`notarized` are empty or false until packages and signing exist.

## When an export fails

A validation problem fails the command with a message that names each cause. A failed cook, compile or link does
not: the command succeeds with `succeeded: false`, the `stage` that failed and `diagnostics` with file, line and
message, exactly like `module.build`. The staging directory is removed, so the output never holds a partial game.
Read `diagnostics` first and `output` for the raw tool lines.

## Not there yet

The following parameters are rejected with "not supported yet" until M8-4:

- packages (`package`: `archive`, `installer`);
- signing and notarization (`signCommand`, `codesignIdentity`, `notaryProfile`);
- the Linux Steam Runtime check (`linuxRuntime`);
- the macOS `.app` bundle.

## Limits

- Everything under `assets:/` ships unless excluded; the export does not walk what scenes reach.
- Changing the name, version or startup scene means exporting again.
- The first export configures CMake and takes minutes.
- Exclude patterns are the only way to leave files out, and an asset that depends on an excluded file fails
  the cook.

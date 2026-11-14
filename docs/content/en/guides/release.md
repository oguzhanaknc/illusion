# Releasing a game

This guide takes a finished project to files you can upload to a store. Every step uses
[`export.build`](../manual/editor/export.md); read that page for the details of each setting. What the project
has not verified is listed in [Known limits](#known-limits): read it before you rely on a step.

Export on the platform you ship for: an export targets the platform and architecture of the machine that
builds it, so a Windows build comes from Windows, a macOS build from macOS and a Linux build from Linux.

## Checklist

1. Fill in the export settings and set the version.
2. Export a Release build with a package.
3. Sign it (Windows), or sign and notarize it (macOS); on Linux, check the distribution you build on.
4. For Steam: supply the SDK, set the `store` setting and export again.
5. Check the result as a player would.
6. Upload.

## 1. Prepare the export settings

Edit `settings:/export.json` with `project.settings_set`:

```json
{"call": "project.settings_set", "params": {"type": "illusion.ExportSettings", "value": {"productName": "Showcase", "organization": "Example Ltd", "identifier": "com.example.showcase", "copyright": "Copyright 2026 Example Ltd", "icon": "assets:/icons/showcase.png"}}}
```

- `productName` names the game's directory, the window and the package; `organization` is the publisher and the
  parent of the user data directory.
- `icon` is a square PNG of at least 256 pixels. Without one the game shows the engine's icon.
- `identifier` is the macOS bundle identifier. Pick it once: it names the app to macOS, and a different one is a
  different app.
- `copyright` ends up in the Windows file properties and in the macOS bundle.
- `exclude` leaves files out of the game, for example `raw/**` for source files. An asset that needs an excluded
  file fails the export's cook, so run one export to see.

The game's version is the project's `version` in `project.illusion`. It names the package
(`Showcase-1.0.0-windows-setup.exe`) and is written to the Windows version resource and to the macOS bundle, so
change it before the export, not after.

## 2. Export a Release build

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "configuration": "Release", "package": "installer", "clean": true}}
```

- `configuration: "Release"` builds the game and its modules with optimizations. It needs an SDK with the player
  for that configuration.
- `clean: true` discards the export's build tree, so nothing of an earlier build reaches the release.
- `package: "installer"` writes an NSIS `setup.exe` on Windows (needs `makensis`) and a DMG on macOS (needs
  `hdiutil`). `archive` writes a ZIP, and a `.tar.gz` on Linux, where `installer` makes the same file.

The first export of a project configures and links, which takes minutes. The same export also runs from the
editor: **File > Export…** opens the [Export window](../manual/editor/export.md#the-export-window).

## 3. Signing on Windows

An unsigned game starts, but Windows SmartScreen warns about it. Install a signing tool such as `signtool`, make
the certificate available to it, and give the export a command line with `{file}` where the file goes. The
editor's environment holds it, so that it is in no command and no project file:

```bash
set ILLUSION_SIGN_COMMAND=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a {file}
illusion_ctl --project C:/games/showcase call export.build "{\"package\": \"installer\"}"
```

The `signCommand` param of `export.build` does the same for one export. The program must be on `PATH`; double
quotes group a part with spaces.

The export signs two things, in this order:

1. The game's executable, right after it is built and before it is installed. The game's directory, the ZIP and
   the installer's contents are copied from that file, so all of them hold the same signed bytes.
2. The setup program, when `package` is `installer`.

It signs no other file: a DLL that you ship next to the executable keeps the signature it came with, or none.
The output shows only the program's name, never its arguments, because they may hold a password.

## 4. Signing and notarizing on macOS

1. Install a Developer ID Application certificate in your keychain. The identity is its name, for example
   `Developer ID Application: Example Ltd (TEAMID)`.
2. Store a notarization profile once on the machine that exports (the password is an app-specific password of
   your Apple ID, and it stays in the keychain):

   ```bash
   xcrun notarytool store-credentials "illusion-notary" --apple-id you@example.com --team-id TEAMID
   ```

3. Export with the identity and the profile:

   ```bash
   export ILLUSION_CODESIGN_IDENTITY="Developer ID Application: Example Ltd (TEAMID)"
   export ILLUSION_NOTARY_PROFILE="illusion-notary"
   illusion_ctl --project ~/games/showcase call export.build '{"package": "installer"}'
   ```

What the export automates: it signs the libraries in `Contents/Frameworks`, then the bundle, with the hardened
runtime and a timestamp; signs the DMG; submits the DMG with `notarytool submit --wait`; and staples the ticket.
The wait takes as long as Apple takes, often a few minutes. Notarization needs a Developer ID identity and
`package: "installer"`. The result's `signedBinaries` and `notarized` tell what happened.

What you check by hand, on the finished DMG and preferably on a Mac that never built the game:

```bash
codesign --verify --deep --strict --verbose=2 Showcase.app
spctl -a -vv Showcase.app
xcrun stapler validate Showcase-1.0.0-macos.dmg
```

`spctl` reports `accepted` with `source=Notarized Developer ID` for a notarized app. Then open the app from the
mounted DMG and play through the startup scene.

The identity `-` signs ad hoc, which is good to try the bundle on your own Mac. Such a bundle is not for
distribution.

## 5. Linux and the Steam Runtime

A Linux export is a directory (and a `.tar.gz`) with the executable, `data/`, the icon and a `.desktop` file;
nothing is signed. The executable links against the system libraries of the machine that built it, so it runs
on that distribution and on newer ones, not on older ones.

Steam runs Linux games in the Steam Runtime "sniper" container. The export does not check the executable against
it yet: `linuxRuntime` is rejected with "not supported yet", and the project has not built or run an export in the
sniper containers. Until it does, build on the oldest distribution you support, and test the Steam build through
the Steam client on a Linux machine before you ship.

## 6. Steam

### Supply the SDK

The Steamworks SDK is Valve's, proprietary, and never part of illusion or of a project. Download it from Valve
and set the environment variable `ILLUSION_STEAMWORKS_SDK` of the editor to its directory, the one with
`public/steam/steam_api.h`:

```bash
export ILLUSION_STEAMWORKS_SDK=~/sdk/steamworks
```

The export checks the header and the platform's redistributable library (`steam_api64.dll`, `libsteam_api.so`,
`libsteam_api.dylib`) before it starts, and fails with a message that names the variable when either is missing.
The engine's release package does not contain the SDK: the game's build compiles `illusion::steam` against yours.

### Set the store and the app id

Your app id comes from Steamworks, where you create the app. Set it in `settings:/export.json`; the Export window
does not show the store yet:

```json
{"call": "project.settings_set", "params": {"type": "illusion.ExportSettings", "value": {"productName": "Showcase", "store": {"kind": "steam", "steamAppId": 480}}}}
```

`project.settings_set` replaces the whole value, so send the other fields of step 1 with it. The id 480 is
Valve's test app, good for trying the build.

An export in a development configuration (anything but `Release` and `MinSizeRel`) also writes `steam_appid.txt`
next to the executable, so that the game starts outside Steam while you test. A Release export does not write it,
and it never reaches a package. Do not add it to a depot: a game started through Steam does not need it.

### Depots

A depot holds the files of one platform. Export on each platform, then upload what the table names, as the
export wrote it; the output layout is described in [Output](../manual/editor/export.md#output).

| Platform | Depot content | Launch option |
| --- | --- | --- |
| Windows | `<output>/<Product>/`: the executable, `data/` and `steam_api64.dll`. | The executable, `showcase.exe`. |
| macOS | `<output>/<Product>.app`, with `libsteam_api.dylib` in `Contents/MacOS`. | The `.app`. |
| Linux | `<output>/<Product>/`: the executable, `data/`, `libsteam_api.so`, the icon and the `.desktop` file. | The executable, `showcase`. |

Upload the directory, not the setup program or the DMG: Steam installs the files itself, and the installer or the
disk image adds nothing. Do not change a file after the export; a changed file loses its signature. Create the
depots and upload them with Valve's own tools (the Steamworks partner site and `steamcmd`).

### Cloud saves and achievements

A game module reaches the store through `StoreService`. The same calls work in Play mode, in tests and on Steam;
without a store the null implementation keeps achievements and stats in memory and cloud files in `user:/cloud`.
Names are 1 to 64 ASCII letters, digits, `_`, `-` and `.`, and an achievement's name is the API name you give it
in Steamworks. This is the showcase module's code:

```cpp
// Count this run in a cloud file.
std::array<std::byte, sizeof(u32)> save{};
const Expected<u64, StoreError> read = store.readCloudFile("showcase.save", save);
if (read && *read == save.size())
{
    std::memcpy(&runtime.runs, save.data(), save.size());
}
++runtime.runs;

// Later: an achievement, a stat, and the cloud file again.
static_cast<void>(store.unlockAchievement("SHOWCASE_STARTED"));
static_cast<void>(store.setAchievementProgress("SHOWCASE_WALKER", {frame / 60, 4}));
static_cast<void>(store.setStatInt("showcase_frames", static_cast<i32>(stats.frames)));
static_cast<void>(store.storeStats());
std::memcpy(save.data(), &runtime.runs, save.size());
static_cast<void>(store.writeCloudFile("showcase.save", save));
```

Define the achievements, the stats and the Steam Cloud quota in Steamworks before you upload; the game cannot
create them. Games poll `overlayActive()` each frame and usually pause while the Steam overlay covers them. See
the [scripting API](../reference/scripting-api.md) and `samples/showcase/modules/showcase/showcase.cpp`.

## 7. Before you ship

- [ ] The version is the one you mean, in `project.illusion`, and the export's package name shows it.
- [ ] You exported with `configuration: "Release"` and `clean: true`; `succeeded` is true and `errors` is 0.
- [ ] Windows: `signedBinaries` is true and `signtool verify /pa /v` passes for the executable and the setup
      program.
- [ ] macOS: `signedBinaries` and `notarized` are true, and `codesign`, `spctl` and `stapler` pass as in step 4.
- [ ] You installed or unpacked the package on a machine with neither the engine nor an SDK and played through
      the startup scene.
- [ ] Steam: the build came from a Release export, holds no `steam_appid.txt`, and you started it through the
      Steam client, signed in, and saw achievements, stats and the cloud save work.
- [ ] Linux: you tested on the oldest distribution you support.
- [ ] You uploaded the files of the export unchanged.

## Known limits

The project's own tests cannot check these; do them by hand.

- **Signing and notarization with real certificates have no CI coverage.** CI tests the Windows hook with a
  recording command and macOS with an ad hoc identity. A real certificate, a real notarization and a
  Gatekeeper check on a clean machine are manual, before every release.
- **The Steam backend has never been built or tested.** `SteamStoreService` and the `steam` package component
  need the Steamworks SDK, which the development machine and CI do not have. Only the null store is built and
  tested. Expect to find faults on your first Steam build, and test achievements, stats and cloud saves in the
  Steam client.
- **Steam Input glyphs are open.** On Steam `glyphPath()` returns nothing: draw your own button images.
- **A game that must be started through Steam is not enforced.** `SteamAPI_RestartAppIfNecessary()` is not
  called, so a Steam game started outside Steam runs with the null store.
- **The sniper container check is open.** `linuxRuntime` is rejected, and no CI job exports inside or runs in the
  Steam Runtime containers.
- **Not checked by the project:** that the Steam library inside the macOS bundle is covered by the export's
  signing, and that Steam accepts the depot layouts in the table above. Verify both with your first upload.
- The Export window does not show the `store` setting yet.
- A failed Package stage keeps the finished game directory.

# Releasing a game

This guide takes a project from the editor to files you can upload to a store. Every step uses
[`export.build`](../manual/editor/export.md); read that page for the details of each setting.

Export on the platform you ship for: an export targets the platform and architecture of the machine that
builds it, so a Windows build comes from Windows and a macOS build from macOS.

## Checklist

1. Fill in the export settings.
2. Set the version.
3. Export a Release build with a package.
4. Sign it (Windows), or sign and notarize it (macOS).
5. Check the result.
6. Upload.

## 1. Export settings

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

## 2. Version

The game's version is the project's `version` in `project.illusion`. It names the package
(`Showcase-1.0.0-windows-setup.exe`) and is written to the Windows version resource and to the macOS bundle, so
change it before the export, not after.

## 3. Export a Release build

```json
{"call": "export.build", "params": {"output": "C:/Games/out", "configuration": "Release", "package": "installer", "clean": true}}
```

- `configuration: "Release"` builds the game and its modules with optimizations. It needs an SDK with the player
  for that configuration.
- `clean: true` discards the export's build tree, so nothing of an earlier build reaches the release.
- `package: "installer"` writes an NSIS `setup.exe` on Windows (needs `makensis`) and a DMG on macOS (needs
  `hdiutil`). `archive` writes a ZIP, and a `.tar.gz` on Linux, where `installer` makes the same file.

The first export of a project configures and links, which takes minutes.

## 4. Sign

An unsigned game starts, but Windows SmartScreen warns about it and macOS Gatekeeper blocks it. Give the secrets to
the editor process through its environment, so that they are in no command and no project file.

### Windows

Install a signing tool such as `signtool`, make the certificate available to it, and set the command line with
`{file}` where the file goes:

```bash
set ILLUSION_SIGN_COMMAND=signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a {file}
illusion_ctl --project C:/games/showcase call export.build "{\"package\": \"installer\"}"
```

The export signs the executable before it installs, so the game's directory, the ZIP and the installer hold the
same signed file, and then it signs the installer.

### macOS

1. Install a Developer ID Application certificate in your keychain.
2. Store a notarization profile once (the password is an app-specific password of your Apple ID):

   ```bash
   xcrun notarytool store-credentials "illusion-notary" --apple-id you@example.com --team-id TEAMID
   ```

3. Export with the identity and the profile:

   ```bash
   export ILLUSION_CODESIGN_IDENTITY="Developer ID Application: Example Ltd (TEAMID)"
   export ILLUSION_NOTARY_PROFILE="illusion-notary"
   illusion_ctl --project ~/games/showcase call export.build '{"package": "installer"}'
   ```

The export signs the libraries in the bundle, then the bundle, with the hardened runtime; signs the DMG; submits
it to Apple and waits for the verdict; and staples the ticket. It takes as long as Apple takes, often a few
minutes. Notarization needs `package: "installer"`.

To try the bundle on your own Mac without a certificate, use the identity `-` (ad hoc). Such a bundle is not for
distribution.

## 5. Check the result

The result of `export.build` has `succeeded: true`, `errors: 0`, the full path in `package`, and `signedBinaries`
and (on macOS with a profile) `notarized` set to true. Then check the files as a player would:

- Install or unpack the package on a machine that has neither the engine nor the SDK, run the game and play
  through the startup scene.
- Windows: `signtool verify /pa /v showcase.exe` for the executable (and for the setup program), and the
  Details tab of the file's properties for the version and the copyright.
- macOS: verify the signature, ask Gatekeeper about the app, and check the ticket on the DMG:

  ```bash
  codesign --verify --deep --strict --verbose=2 Showcase.app
  spctl -a -vv Showcase.app
  xcrun stapler validate Showcase-1.0.0-macos.dmg
  ```

  `spctl` reports `accepted` with `source=Notarized Developer ID` for a notarized app.

## 6. Upload

Upload the package file named in `package`: the setup program, the DMG or the archive. Do not change a file after
the export; a changed file loses its signature.

### Steam

What is true today: the game's directory (`<output>/<Product>` on Windows and Linux, `<Product>.app` on macOS) is a
complete game and holds everything it needs, so you can upload it as a depot with Steam's own tools. The
Steamworks module (achievements, cloud saves, Steam Input) and the check of a Linux export against the Steam
Runtime "sniper" are planned in M8-6 and are not there yet: `linuxRuntime` is rejected, and an export is as
portable as the Linux machine that links it. Until then, test a Linux build on the oldest distribution you support.

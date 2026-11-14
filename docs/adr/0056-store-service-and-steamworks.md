# 0056. Reach stores through StoreService, with Steamworks as an optional library

- Status: Accepted
- Date: 2026-11-14

## Context

The brief asks for an optional Steamworks module: achievements, cloud saves, Steam Input glyphs and the
overlay. Games are written against the scripting API (ADR 0041) and run the same in the editor's Play mode and
in the exported player (ADR 0042, ADR 0055). Three constraints shape the design:

- **The Steamworks SDK is proprietary.** Valve's Steamworks SDK Access Agreement does not allow it to be
  committed or redistributed with the engine, and its redistributable library (`steam_api64.dll`,
  `libsteam_api.so`, `libsteam_api.dylib`) is not under a permissive licence. Every other dependency is
  pinned and downloaded by `cmake/Dependencies.cmake` under a permissive licence (AGENTS.md).
- **Most builds have no SDK.** CI, the engine's releases and most developers build without it, so a game must
  build, run and be tested without a store, and Play mode must not need Steam.
- **Hot reload.** Game modules are reloaded while the host keeps running; a callback that the host stores into
  a module's code dangles after a reload.

Options considered:

- **Call Steamworks from game modules directly.** Simple, but ties every game to Steam, cannot run in Play mode
  or CI, and puts proprietary headers into project code.
- **One Steam-shaped service.** Fewer types, but the shape (callbacks, `CSteamID`, `EInputActionOrigin`) leaks
  into games and other stores would have to imitate it.
- **A vendor-neutral service with a null implementation and an optional Steam one.** Games call the same
  functions everywhere; the null implementation keeps game logic testable; the Steam library is built only
  where the SDK is.

## Decision

- **Interface.** `StoreService` (`illusion/store/StoreService.hpp`, group `scripting_api`) is a host service like
  the others (ADR 0041): modules find it with `context.service<StoreService>()` and build with
  `illusion_add_module(... USES store)`, which links the header-only target `illusion::store_api`. It covers:
  - the store: `available()`, `storeName()` ("none", "steam"), the player's `userName()` and `userId()`;
  - achievements by API name: `unlockAchievement()`, `clearAchievement()`, `isAchievementUnlocked()`,
    `setAchievementProgress()` and `achievementProgress()` as `AchievementProgress{current, max}` (reaching
    `max` unlocks);
  - stats: `setStatInt()`, `statInt()`, `setStatFloat()`, `statFloat()` and an explicit `storeStats()` that
    sends changed stats and achievements;
  - cloud files by name: `writeCloudFile()`, `readCloudFile()` (into a caller span, returning the file's size),
    `removeCloudFile()`, `listCloudFiles()` (names into a caller span, returning the count),
    `cloudFileExists()` and `cloudQuota()`;
  - the overlay: `overlayActive()`, a value the game polls each frame, and `openOverlay(StoreOverlayPage)`;
  - glyphs: `glyphPath(GamepadOrigin)` returns the path of an image of a gamepad control in the store's style,
    or nothing.
- **Values, not callbacks.** Every result is returned or polled; nothing calls into a module. Failures are
  `Expected<T, StoreError>` (`Unavailable`, `InvalidName`, `InvalidValue`, `NotFound`, `TypeMismatch`,
  `TooLarge`, `QuotaExceeded`, `IoError`). Names of achievements, stats and cloud files are 1 to 64 ASCII
  letters, digits, `_`, `-` and `.`, not starting with `.` (`isValidStoreName()`); cloud files are at most
  100 MiB. String views and span contents a store returns are valid until its next `update()` or the next call
  of the same function.
- **Host side.** `StoreBackend` adds `update()`, which the host calls once per frame; it does not allocate, nor
  do `overlayActive()` and the other per-frame queries. `GameSessionDesc::store` hands a store to the session's
  modules; a session without one provides no `StoreService`.
- **Null store.** `NullStoreService` (library `illusion::store`, always built) is the default everywhere:
  `available()` is false and `storeName()` is "none"; achievements, progress and stats are kept in memory, so
  game logic can be tested; cloud files are files of a VFS directory (default `user:/cloud`, quota 100 MiB),
  so saves persist across runs and a game works the same without a store; `openOverlay()` returns false and
  `glyphPath()` nothing. The editor's Play mode always uses it, with its cloud directory in the project's
  cache (`cache:/store/cloud`), because the editor has no `user` mount.
- **Steam store.** `SteamStoreService` lives in the optional library `engine/steam` (`illusion::steam`), which
  is configured only when the CMake variable `ILLUSION_STEAMWORKS_SDK` names a Steamworks SDK directory. The
  configure step checks `public/steam/steam_api.h` and the platform's redistributable library and fails,
  naming the variable, when the path is set but wrong. Steam headers are included only in
  `engine/steam/src/*.cpp`. It maps achievements and progress (`IndicateAchievementProgress`), stats, Remote
  Storage and the overlay (`GameOverlayActivated_t`, `ActivateGameOverlay`); it requires an SDK that requests
  the current stats during `SteamAPI_Init()` (1.61 or newer). It returns no glyphs: Steam Input glyphs need
  `ISteamInput`, which would take over the controllers that SDL reads (see Consequences).
- **The SDK is never committed or downloaded.** Nothing about it is in `cmake/Dependencies.cmake`. This is the
  one exception to the permissive-licence rule: the SDK is user-supplied, proprietary and optional, and
  `THIRD_PARTY_NOTICES.md` says that illusion does not distribute it. The engine's package (ADR 0055) ships the
  sources of `engine/steam` with `cmake/IllusionSteam.cmake`; `find_package(illusion COMPONENTS player steam)`
  builds `illusion::steam` from them against the `ILLUSION_STEAMWORKS_SDK` of the game's build, so an engine
  release built without the SDK still exports Steam games.
- **Player.** `PlayerDesc::store` is a factory that the generated `main.cpp` of a Steam game sets to
  `createSteamStore` (with `PlayerDesc::storeAppId`). `runPlayer()` calls it before it opens the window (the
  overlay hooks the graphics device), keeps the store it returns, or a `NullStoreService` on `user:/cloud` when
  there is no factory or the store does not start (Steam not running), calls `update()` once per frame and
  passes the store to the `GameSession`.
- **Export.** `ExportSettings::store` (`settings:/export.json`) has `kind` (`"none"`, the default, or
  `"steam"`) and `steamAppId`. With `"steam"` the app id must be positive, `ILLUSION_STEAMWORKS_SDK` (from the
  editor's environment) must name an SDK directory, else the export fails naming the variable; the generated
  project asks for the `steam` component, passes the SDK path and calls `illusion_add_game(... STORE steam
  STEAM_APP_ID <id>)`, which links `illusion::steam`, copies the redistributable next to the executable and
  installs it with the game. `steam_appid.txt` is written into the game's directory only for development
  exports (configurations other than Release and MinSizeRel), after the install, so it never reaches a package.

## Consequences

- Games use one API in Play mode, in tests and on Steam; the null store makes achievements, stats and saves
  testable without a store, and cloud saves are plain files under `user:` when no store runs.
- `SteamStoreService` cannot be built in this repository's CI or on machines without the SDK: it is unbuilt and
  untested here, and is kept thin so that review covers it. A CI job with the SDK (a secret, never committed) is
  needed before it is trusted.
- Steam Input glyphs are open: they need `ISteamInput` initialised next to SDL's gamepads (or Steam Input as the
  gamepad backend), which is its own decision.
- Achievement progress on Steam is shown with `IndicateAchievementProgress`; `achievementProgress()` returns the
  last progress the game set in this run (or `max` of an unlocked achievement), not a stat bound in Steamworks.
- `SteamAPI_RestartAppIfNecessary()` is not called: a Steam game started outside Steam runs with the null store.
  A game that must be started through Steam needs it; revisit with the release guide.
- Adding another store (Epic, GOG) is another library implementing `StoreBackend` and another `kind`.

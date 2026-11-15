# 0058. Save games as checksummed slot files of tagged entities, through SaveService

- Status: Proposed
- Date: 2026-11-15

## Context

The brief asks for a commercial-quality engine; a shipped game must keep the player's progress across runs,
survive a crash or a power cut in the middle of a write, keep loading saves made by older versions of the
game, and sync them through the store when there is one (ADR 0056). Games are written against the scripting
API (ADR 0041) and run the same in the editor's Play mode and in the exported player (ADR 0042, ADR 0055).

What exists and shapes the design:

- **Scene serialization.** `saveScene()` and `loadScene()` write reflected Components by stable type and field
  name, with a version per type and migration steps (ADR 0032), entity ids (ADR 0033), preserved unknown types
  and prefab links (ADR 0046). Runtime state (`reflectRuntime`) is never written.
- **Module state lives in Components.** Hot reload (ADR 0041, ADR 0044) already makes modules keep their state
  in Components of the World rather than in globals.
- **Files.** The player mounts `user:` read-write; the editor has no `user` mount and keeps Play mode data in
  the project's cache (ADR 0056). `VirtualFileSystem` has `writeFile()` and `rename()`, but no write that
  survives an interruption.
- **Threads.** Jobs must not block, so file writes cannot run on the Job System. The World is not thread-safe.
- **Values, not callbacks.** Nothing calls into a module (ADR 0056, ADR 0057); results are returned or polled.

Options considered for **what is saved**:

- **The whole World.** Nothing to mark, but every save repeats the static level, a patched level never reaches
  old saves, and derived runtime state would have to be filtered out one type at a time.
- **A blob the module writes itself.** Smallest engine surface, but every game reinvents versioning, entity
  references and corruption handling, and the reflection the engine already has is not used.
- **Entities the game marks.** A save holds only what changes while playing; the level comes from its scene
  file, the save is applied on top. Costs a rule for what happens to marked entities that the save lacks.

Options considered for **the data format**: a new binary format (smaller, faster, but a second serializer with
its own migrations to keep correct) or the scene JSON inside a binary container (larger, but one serializer,
one migration path, readable in a bug report).

Options considered for **applying a save**: updating live entities in place (keeps `Entity` handles, but needs
a second code path that adds, removes and overwrites Components and re-triggers every resolver) or destroying
and recreating them through `loadScene()` (handles change, but it is the path every scene load already takes).

## Decision

- **What is saved.** The tag Component `Saved` (`illusion.Saved`, library `engine/save`, registered by
  `registerSaveComponents()`) marks an entity; a save contains every entity with the tag **and all its
  descendants**, with every Component that a scene file would contain. Game state that must be saved is a
  Component reflected with `reflect<T>()` on such an entity; state reflected with `reflectRuntime<T>()` is
  not saved. Nothing else of the World is written.
- **World section.** The saved entities are written by `saveScene()` with `PrefabSaveMode::Expanded` and
  `SceneOrder::Hierarchy`, after `assignMissingGuids()`. Two additions to the scene serializer make a subset
  usable as a delta:
  - `SceneSaveOptions::externalReferences`: Entity fields that refer to entities outside the saved ones are
    written as their id instead of null (a saved child keeps its unsaved parent).
  - `SceneLoadOptions::replace` and `SceneLoadOptions::resolveExternal`: after the whole file has been
    validated, and only then, the entities of `replace` are destroyed and the file's entities are created
    with their ids (`IdPolicy::Keep`); ids the file does not define are looked up in the World, and become
    null when no entity has them. A file that fails validation changes nothing.
- **Applying a save.** `applySave()` replaces the World's saved set (tagged entities and descendants) with
  the file's entities in one `loadScene()` call. So: an entity in both is recreated with the saved values, an
  entity only in the World is destroyed (it was destroyed in the saved game), an entity only in the file is
  created (it was spawned in the saved game). `Entity` handles of saved entities change: code that caches
  them finds them again after a `Loaded` event. Runtime Components are derived again by the resolvers, as
  after any scene load. The game loads the right level before it applies a save; which level that is, is the
  game's own data (see `custom` below).
- **Slot file.** One slot is one file `<directory>/<slot>.save`: a fixed little-endian header (magic `ILSV`,
  container version `kSaveFormatVersion` = 1, payload size, `xxhash64` of the payload) and a payload of two
  sections, the metadata as JSON and the World section as scene JSON. A file with a wrong magic, size or
  checksum is `Corrupt`; a container version above the program's is `NewerVersion`; neither is ever applied.
  Per-type versions and migrations are those of the reflection (ADR 0032), so a renamed field or a changed
  meaning is migrated exactly as in scene files, and Components of types the game no longer has are dropped
  with a warning in the log. Slot names are 1 to 32 ASCII letters, digits, `_` and `-`
  (`isValidSaveSlotName()`). A payload is at most 64 MiB.
- **Metadata.** `title` (text for the player, at most 128 bytes), `playSeconds`, `gameVersion` (a number the
  game chooses and gets back, for its own fix-ups after a load), `custom` (at most 1024 bytes the game
  defines, for example the level to load) and, written by the host, `savedAt` (seconds since the Unix epoch,
  UTC) and the engine version. `SaveService::slots()` returns them without reading World sections.
- **Atomic writes with one backup.** `VirtualFileSystem::writeFileAtomic()` writes a temporary file in the
  same directory, flushes it to the device and renames it over the target (`MoveFileExW` with
  `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`, `fsync` + `rename` elsewhere). Before a slot is
  replaced, its current valid file becomes `<slot>.save.bak`. Reading a slot whose file is missing or
  `Corrupt` falls back to the backup and reports `SaveEventType::LoadedBackup`. An interrupted write
  therefore leaves the previous save, never a half-written one.
- **Threads.** Capturing (`saveScene()`) and applying (`loadScene()`) run on the session's thread, at one
  fixed point of `GameSession::update()`: after the Modules step and before Physics. Encoding, checksums and
  all file reads and writes run on one save thread owned by `SaveSystem`, started on first use, fed by a
  queue in request order. `SaveSystem::waitIdle()` lets hosts and tests wait for it without sleeping; the
  player calls it before it exits, so a save requested in the last frame is written.
- **Scripting API.** `SaveService` (`illusion/save/SaveService.hpp`, group `scripting_api`,
  `illusion_add_module(... USES save)`, header-only target `illusion::save_api`); adding it makes the module
  API version 6. Requests are queued and their results polled:

  ```cpp
  class SaveService : NonCopyable
  {
  public:
      [[nodiscard]] virtual bool available() const = 0;
      virtual Expected<void, SaveError> save(std::string_view slot, const SaveMetadata& metadata) = 0;
      virtual Expected<void, SaveError> load(std::string_view slot) = 0;
      virtual Expected<void, SaveError> remove(std::string_view slot) = 0;
      [[nodiscard]] virtual bool busy() const = 0;
      virtual u32 slots(std::span<SaveSlotInfo> out) = 0;
      [[nodiscard]] virtual std::optional<SaveSlotInfo> slot(std::string_view slot) const = 0;
      virtual u32 pollEvents(std::span<SaveEvent> out) = 0;
  };
  ```

  `save()`, `load()` and `remove()` fail at once with `InvalidName`, `InvalidValue` (metadata too long) or
  `Unavailable`; everything later arrives as a `SaveEvent{type, slot, error}` with `Saved`, `Loaded`,
  `LoadedBackup`, `Removed` or `Failed` (`NotFound`, `Corrupt`, `NewerVersion`, `TooLarge`, `IoError`,
  `InvalidScene`). `SaveSlotInfo` has the name, the metadata, `savedAt`, a `SaveSlotState` (`Ok`, `Backup`,
  `Corrupt`, `NewerVersion`) and `screenshot`, the virtual path of the slot's image or empty. `slots()` reads
  a list that the save thread keeps current (one directory scan at start, then updated by each operation), so
  it does not touch the disk; string views are valid until the next update.
- **Host side.** `GameSessionDesc::saves` is a `SaveSystem*`; a session without one provides no
  `SaveService`. `SaveSystemDesc` names the writable `VirtualFileSystem` and the directory: `user:/saves` in
  the player, `cache:/saves` in the editor's Play mode. `captureSave()`, `applySave()`, `encodeSaveFile()` and
  `decodeSaveFile()` are plain functions of `engine/save`, usable and tested without a thread.
- **Screenshots.** A slot's image is the sidecar `<slot>.png`, written after the slot file; a missing image
  is not an error. The host supplies the pixels (`SaveSystemDesc::thumbnail`, a function that copies the last
  presented frame scaled to 320 x 180), because `GameSession` has no GPU code; without it slots have no image.
- **Cloud.** With `SaveSettings::cloud` (`settings:/save.json`, default on) and a `StoreService` that is
  `available()`, each written slot file is mirrored with `writeCloudFile("<slot>.save")` and each removed one
  with `removeCloudFile()`, on the session's thread; when the session starts, a cloud file whose `savedAt` is
  newer than the local slot's replaces it, the local file becoming the backup. With the null store nothing is
  mirrored: its cloud files are already local files.
- **Editor.** Commands `saves.list`, `saves.write`, `saves.load` and `saves.remove` act on the running Play
  session, so agents and tests reach saved games through the same API as the UI.

## Consequences

- Games get saving with one tag and one service; versioning, corruption handling and interrupted writes are
  the engine's and are tested once (every byte of a file flipped, every truncation length, a write stopped
  after each step).
- One serializer and one migration path cover scenes, prefabs and saves. The price is size and time: JSON is
  several times larger than a packed format and capture runs on the session's thread. `save/capture-1000` and
  `save/apply-1000` benchmarks set the budget; if a game outgrows it, a binary World section is a new
  container version, not a new API. Compression is open for the same reason.
- `Entity` handles of saved entities do not survive a load. This is the same rule as for a scene load, but it
  is new for code that never reloads its scene; the manual and the tutorial say so, and `Loaded` is the hook.
- State outside saved Components is not captured: velocities of dynamic bodies, live particles, playing
  sounds and running UI transitions restart from what the Components say. Saving body velocities needs them
  mirrored into a saved Component; recorded as open in ROADMAP.
- A save is a delta over a level: a patch that removes an untagged entity that a save's entity was parented to
  leaves that entity a root (`resolveExternal` gives null). Games that restructure levels use `gameVersion`.
- The cloud mirror is exercised only against test stores here: `SteamStoreService` is unbuilt in CI (ADR
  0056). Conflict handling is "newest `savedAt` wins, the other is the backup"; a game that needs the player
  to choose reads both through `StoreService` itself.
- The sidecar image is not part of the atomic write: after a crash a slot can show an older image. It is
  cosmetic and is replaced by the next save.
- Revisit if games need streaming worlds (several level deltas in one slot) or saves larger than the limit.

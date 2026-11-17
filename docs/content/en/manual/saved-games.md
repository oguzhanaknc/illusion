# Saved games

A saved game keeps what changed while the player played and puts it back on top of the level the next time
(ADR 0058). You mark the Entities that must persist with the `Saved` tag, and a game module writes, loads and
removes slots through `SaveService`. Slot files are checksummed, written so that a crash never leaves half a
file, keep one backup, can carry an image and are mirrored to the store's cloud when there is one.

Headers: `#include <illusion/save/SaveService.hpp>` and `#include <illusion/save/SaveComponents.hpp>` (library
`illusion::save`; modules use `USES save`)

```cmake
illusion_add_module(mygame SHARED SOURCES mygame.cpp USES runtime save)
```

## The Saved tag

Add the Component `Saved` (`illusion.Saved`, an empty tag) to an Entity in the Inspector, or in code, and a save
contains that Entity **and all its descendants**:

```cpp
illusion::registerSaveComponents(); // once, with the other Components
world.emplace<illusion::Saved>(player);
```

A child of a saved Entity needs no tag of its own. An Entity outside every saved subtree is never written:
the level comes from its scene file, and a load applies the save on top of it.

## What is and is not saved

A save contains every Component that a scene file would contain, of every saved Entity: `Transform`,
physics bodies, lights and your own Components that are reflected with `reflect<T>("mygame.Name")`. Game state
that must survive a load is therefore a reflected Component on a saved Entity:

```cpp
struct Health
{
    float current = 100.0f;
};

illusion::reflect<Health>("mygame.Health").field("current", &Health::current);
```

What a save does not contain:

- Entities outside the saved subtrees, and the level itself.
- Runtime state: Components reflected with `reflectRuntime<T>()`, the runtime Components of the renderer, the
  physics and the audio (they are derived again after a load), and the state of your module's variables.
- Velocities of dynamic bodies, live particles, playing sounds and running UI transitions. They restart from
  what the saved Components say.

A load replaces the saved set: an Entity that exists in the World and in the slot gets the saved values, one
only in the World is destroyed (it was destroyed in the saved game), one only in the slot is created (it was
spawned in the saved game). The game loads the right level first; which level that is belongs in the
metadata's `custom` text.

Saved Components are versioned and migrated like scene files ([Reflection](reflection.md#versions-and-migrations)):
a renamed field loads, and a Component of a type that the game no longer has is dropped with a warning in the
log.

## Slots and metadata

A slot has a name of 1 to 32 ASCII letters, digits, `_` and `-` (`isValidSaveSlotName()`), and metadata that
you give when you save and read back without loading the slot:

| Field | Meaning |
| --- | --- |
| `title` | Text for the player, UTF-8, at most 128 bytes |
| `playSeconds` | Time played, not negative |
| `gameVersion` | A number you choose and get back, for your own fix-ups after a load |
| `custom` | At most 1024 bytes of UTF-8 that you define, for example the level to load |

The host adds `savedAt` (seconds since the Unix epoch, UTC) and the engine's version. `SaveService::slots()`
returns the list sorted by name from memory, so it can run every frame; each entry has a `SaveSlotState`:

| State | Meaning |
| --- | --- |
| `Ok` | The slot file is valid |
| `Backup` | The file is missing or corrupt and its backup is valid; a load reads the backup |
| `Corrupt` | Neither the file nor a backup can be read |
| `NewerVersion` | The file was written by a newer version of the container and cannot be loaded |

## Saving and loading

`save()`, `load()` and `remove()` queue a request and return at once. They fail at once only with
`InvalidName`, `InvalidValue` (metadata too long or not UTF-8) or `Unavailable` (no save directory). A `save()`
captures the World at the end of the update that calls it; the file is encoded, checked and written on the
host's save thread, so a game never waits for the disk.

```cpp
if (illusion::SaveService* saves = context.service<illusion::SaveService>())
{
    if (savePressed)
    {
        static_cast<void>(saves->save("slot1", {.title = "Chapter 2", .playSeconds = played, .gameVersion = 3}));
    }
    if (loadPressed && saves->slot("slot1").has_value())
    {
        static_cast<void>(saves->load("slot1"));
    }
}
```

Requests run in call order, so a `save()` followed by a `load()` of the same slot reads what the first one
wrote.

## Events

The result of a request arrives as a `SaveEvent` in a later update. `pollEvents()` returns the events of the last
update; every call in an update returns the same ones, so several parts of a module can read them:

```cpp
std::array<illusion::SaveEvent, 8> events{};
const illusion::u32 count = std::min<illusion::u32>(saves->pollEvents(events), events.size());
for (const illusion::SaveEvent& event : std::span(events).first(count))
{
    switch (event.type)
    {
    case illusion::SaveEventType::Saved:
        break;
    case illusion::SaveEventType::Loaded:
    case illusion::SaveEventType::LoadedBackup:
        findMyEntitiesAgain();
        break;
    case illusion::SaveEventType::Removed:
        break;
    case illusion::SaveEventType::Failed:
        ILL_LOG_WARN("Game", "slot {} failed: {}", event.slot, illusion::saveErrorName(event.error));
        break;
    }
}
```

`Failed` carries a `SaveError`: `NotFound` (no slot of that name), `Corrupt`, `NewerVersion`, `TooLarge` (a
payload above 64 MiB), `IoError` or `InvalidScene`. A failed load changes nothing in the World. `busy()` is true
while a request has not delivered its event.

## Entity handles change on load

A load destroys the saved Entities and creates them again, as any scene load does, so an `Entity` handle that
your module kept in a Component or a variable is dead after the `Loaded` or `LoadedBackup` event. Find the
Entities again at that point, by name or by a tag Component. Handles to Entities outside the saved subtrees stay
valid. An Entity outside them whose `Entity` field points at a saved Entity keeps a dead handle too, so keep
such references inside the saved subtrees.

## Backups and corrupt files

Slot files are written to a temporary file in the same directory, flushed to the device and renamed over the
slot, so a crash or a power cut leaves the previous save and never half a file. Before a slot is replaced its
valid file becomes `<slot>.save.bak`. A file has a checksum; one with a wrong magic number, size or checksum is
`Corrupt` and is never applied. Loading a slot whose file is missing or corrupt reads the backup instead and
reports `LoadedBackup`, so a game can tell the player. A file from a newer container version is `NewerVersion`;
it is kept, never replaced by a backup and never loaded.

Stale `*.tmp-*` files of a write that a crash interrupted are ignored and not deleted yet.

## Slot images

A saved slot gets an image: the last presented frame, cropped to 16:9 and scaled to 320 x 180, written as
`<slot>.png` next to the slot file. `SaveSlotInfo::screenshot` is its virtual path, or empty when the slot has
none, and a `UiImage` can show it. The player reads its view back and Play mode the Game view; only a save reads
a frame back. The image is not part of the atomic write: after a crash a slot can show the image of its
previous save. A slot that came from the cloud has no image until it is saved again.

!!! note "Image cache"
    The UI caches an image by its path, so a `UiImage` that shows `SaveSlotInfo::screenshot` keeps the first
    image of a slot after the slot is saved again.

## Where the files live

| Host | Directory |
| --- | --- |
| The exported player | `user:/saves` (see [File System](filesystem.md); `%APPDATA%\<organization>\<product>\saves` on Windows) |
| Play mode in the editor | `cache:/saves` of the project (`.illusion/cache/saves`); every project has its own slots, and an export does not contain them |

A slot is `<slot>.save`, with `<slot>.save.bak` and `<slot>.png` next to it. The game does not choose the
directory; it only names slots.

## The cloud mirror

When the game has a store whose cloud files are remote (Steam; the null store keeps them as local files already)
and `SaveSettings::cloud` is on, `SaveService` mirrors the slot files: each written slot is copied to the cloud
as `<slot>.save` and each removed one is removed there. When the session starts, a cloud file whose `savedAt`
is newer than the local slot's replaces it and the local file becomes the backup; an older or equally old cloud
file changes nothing. Cloud failures are only logged and never fail a save. The setting is the file
`settings:/save.json`:

```json
{
  "format": "illusion.settings",
  "type": "illusion.SaveSettings",
  "version": 1,
  "value": {"cloud": false}
}
```

More about stores in [Game Runtime](runtime.md#saved-games-in-the-cloud). A local slot that is newer than its
cloud file, or absent from the cloud, is not uploaded when the session starts; it goes up with the next save.

## Editor commands

While Play mode runs a game with a save directory, the [Editor commands](../reference/editor-commands.md)
`saves.list`, `saves.write`, `saves.load` and `saves.remove` reach the same service, so an agent and a test
use saved games like a game module does. They fail with `wrong_mode` when no session runs, and with
`precondition_failed` in a `simulate` session. `saves.list` returns every slot with its title, play time,
game version, custom text, `savedAt`, state and image path. The other three are asynchronous: they queue the
request and answer with the event that finished it (`Saved`, `Loaded`, `LoadedBackup` or `Removed`), or fail
with the name of the `SaveError`. The game sends requests when it updates, so a paused game answers only after
`play.step`.

```json
{"method": "saves.write", "params": {"slot": "slot1", "title": "Chapter 2", "playSeconds": 3600, "gameVersion": 3}}
{"method": "saves.load", "params": {"slot": "slot1"}}
{"method": "saves.list", "params": {}}
```

Stopping the session discards the play World, so a load changes the game that runs, never the open scene. The
slots stay in the project's cache until you remove them.

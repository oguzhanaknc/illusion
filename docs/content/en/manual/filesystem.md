# File System

Header: `#include <illusion/platform/FileSystem.hpp>`

## Virtual paths

Engine and game code never use real paths. They use virtual paths of the form `mount:/relative/path`:

```cpp
illusion::VirtualFileSystem vfs;
vfs.mount("game", illusion::executableDirectory() / "data");
vfs.mount("user", illusion::userDataDirectory("MyStudio", "MyGame"), illusion::MountAccess::ReadWrite);

std::string config;
if (vfs.readText("game:/config/defaults.json", config) != illusion::FileError::None)
{
    // handle the missing file
}
vfs.writeText("user:/settings.json", settingsJson);
```

The same code then works in the editor (loose files), in exported games (packaged data) and with
per-user save locations, and a path can never escape its mount:

- Mount names use lowercase letters, digits, `_` and `-`.
- The relative part uses `/` separators. Empty components, `.`, `..`, backslashes and drive letters are
  rejected with `FileError::InvalidPath`.
- Paths are UTF-8 on every platform.

File formats often refer to other files relatively (`../textures/a.png`). Combine such a reference
with the referring file's directory, then call `normalizePath()`, which resolves `.` and `..` but never
lets the path leave its mount:

```cpp
const auto path = illusion::VirtualFileSystem::normalizePath("game:/models/../textures/./a.png");
// path == "game:/textures/a.png"; "game:/../secret" gives std::nullopt
```

Real paths that come from UTF-8 text (configuration, command lines on macOS and Linux) should be built
with `utf8Path()`. Constructing `std::filesystem::path` from a narrow string uses the system code page
on Windows and garbles non-ASCII names:

```cpp
vfs.mount("mods", illusion::utf8Path(modDirectoryFromConfig));
```

## Reading and writing

| Function | Description |
| --- | --- |
| `readFile()`, `readText()` | Read a whole file into a byte vector or string. |
| `writeFile()`, `writeText()` | Write a whole file; only on `ReadWrite` mounts. |
| `exists()` | Check whether a file or directory exists. |
| `list()` | List a directory, sorted by name. |
| `listRecursive()` | List everything below a directory; names are relative paths such as `sub/a.png`. |
| `stat()` | Tell a file from a directory and get its size and modification stamp (`FileStat`). |
| `createDirectory()` | Create a directory and its missing parents. |
| `remove()` | Remove a file, or a directory (with its contents only when `recursive` is true). |
| `rename()` | Move or rename a file or directory, also between writable mounts. |
| `copy()` | Copy a file or a directory tree; the source may be read-only or an archive. |
| `resolve()` | Get the real path, for example to pass to a third-party library. |

Every operation returns a `FileError` instead of throwing. `fileErrorName()` turns it into text for
logs. The file management functions never replace anything: a target that exists gives
`FileError::AlreadyExists`, removing a directory that has entries without `recursive` gives
`FileError::NotEmpty`, and the root of a mount cannot be removed, renamed or replaced
(`FileError::InvalidPath`). `FileStat::modified` is only for telling whether a file changed; it is not a
calendar time.

```cpp
vfs.createDirectory("user:/saves/slot2");
vfs.copy("game:/defaults/profile.json", "user:/saves/slot2/profile.json");
illusion::FileStat stat;
if (vfs.stat("user:/saves/slot1", stat) == illusion::FileError::None && stat.isDirectory)
{
    vfs.remove("user:/saves/slot1", true);
}
```

Writes are atomic: the data is written to a temporary file that then replaces the target, so a crash
or power loss never leaves a half-written save file. Parent directories are created automatically.

## Archives

An archive (`.illpak`) holds a whole directory tree in one file. Mount it with `mountArchive()` and read
it exactly like a mounted directory:

Header: `<illusion/platform/Archive.hpp>`

```cpp
illusion::VirtualFileSystem files;
files.mountArchive("game", illusion::executableDirectory() / "data.illpak");

std::string config;
files.readText("game:/config/defaults.json", config); // same path as with a directory mount
```

- Archive mounts are read-only: writes return `FileError::ReadOnly`, and `resolve()` returns
  `FileError::Unsupported` because the files have no path of their own.
- Every file's content hash is checked when it is read; damaged data gives `FileError::IoError`.
- The archive is memory-mapped, so reads are fast and safe from several threads.
- Create archives with the `illusion_pack` tool, or from code with `writeArchive()` and
  `packDirectory()`:

```bash
illusion_pack data/ build/data.illpak
```

## Watching for changes

`FileWatcher` reports what changes below a directory: files and directories created, modified or removed,
by the operating system's notifications (ReadDirectoryChangesW on Windows, inotify on Linux, FSEvents on
macOS) or, with `FileWatchBackend::Polling`, by comparing directory listings.

Header: `<illusion/platform/FileWatcher.hpp>`

```cpp
#include <illusion/platform/FileWatcher.hpp>

auto watcher = illusion::FileWatcher::watch(projectRoot / "assets", illusion::FileWatchBackend::Native);
std::vector<illusion::FileEvent> events;
watcher->poll(events); // once per frame
for (const illusion::FileEvent& event : events)
{
    // event.kind: Created, Modified, Removed or Overflow (rescan); event.path: "textures/rock.png"
}
```

- Nothing is delivered behind your back: changes collect until `poll()` and arrive sorted by path, at most
  one per path. A file created and written is one `Created`; created and deleted again between two polls is
  nothing; deleted and created again is `Modified`.
- A rename is a `Removed` of the old path and a `Created` of the new one. A directory that appears or
  disappears is one change, and its files are not always reported one by one: treat it as a change of
  everything below it. Directories never report `Modified`.
- `Overflow` means the system dropped changes: rescan the whole tree.
- Where native notifications are unavailable (network drives, an exhausted inotify watch limit), a `Native`
  watcher falls back to polling and logs a warning; `backend()` tells which one runs. Polling walks the tree
  on every `poll()`, so poll it less often.
- `list()` and `listRecursive()` also report each entry's modification stamp (`DirectoryEntry::modified`),
  so a listing is enough to see which files changed.

## Platform directories

| Function | Windows | macOS | Linux |
| --- | --- | --- | --- |
| `userDataDirectory(org, app)` | `%APPDATA%\org\app` | `~/Library/Application Support/org/app` | `$XDG_DATA_HOME/org/app` or `~/.local/share/org/app` |
| `executableDirectory()` | Folder of the `.exe` | Folder of the executable | Folder of the executable |

`userDataDirectory()` does not create the directory; mounting it requires that it exists, so create it
with `std::filesystem::create_directories()` first.

## Threading

Set up mounts on one thread before use. After that, reads and writes may run on any thread. File
operations block, so do not call them from Jobs (see the [Job System rules](jobs.md#rules)); run them on
a dedicated I/O thread. Asynchronous asset streaming will provide this in a later milestone.

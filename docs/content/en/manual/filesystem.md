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
| `resolve()` | Get the real path, for example to pass to a third-party library. |

Every operation returns a `FileError` instead of throwing. `fileErrorName()` turns it into text for
logs.

Writes are atomic: the data is written to a temporary file that then replaces the target, so a crash
or power loss never leaves a half-written save file. Parent directories are created automatically.

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

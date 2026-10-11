# 0008. Virtual file system with mount points

- Status: Accepted
- Date: 2026-10-11

## Context

Game data lives in different places in the editor (loose project files), in exported games (packaged
data next to the executable, later archives) and for user data (per-user save directories that differ
per OS). Code that builds real paths by hand breaks across these setups, mishandles Unicode on Windows,
and can be tricked into reading or writing outside the intended directories.

## Decision

- All engine and game file access uses virtual paths `mount:/relative/path` resolved by
  `VirtualFileSystem`.
- Mount names are `[a-z0-9_-]+`. Relative parts use `/` and reject empty, `.` and `..` components,
  backslashes, colons and control characters, so a virtual path can never escape its mount.
- Paths are UTF-8 strings; conversion to native paths goes through `char8_t` so Windows never uses the
  ANSI code page.
- Mounts are read-only unless created with `MountAccess::ReadWrite`. Writes go to a temporary file that
  is renamed over the target (atomic replace), creating parent directories as needed.
- Errors are returned as `FileError`; the file system never throws.
- `userDataDirectory()` and `executableDirectory()` provide the platform locations; Steam Cloud and
  packaged archives will plug in as additional mount backends.

## Consequences

- Moving from loose files to packaged data only changes mounts, not game code.
- Every file API is synchronous and blocking; it must not be called from Jobs. Asynchronous streaming
  (M4) will run file reads on dedicated I/O threads on top of this layer.
- The mount table is not synchronized; mounts are configured at start-up before other threads read.

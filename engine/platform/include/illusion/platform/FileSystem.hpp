#pragma once

/// @file
/// Virtual file system and platform directories.
///
/// Engine and game code address files through virtual paths of the form `mount:/relative/path`, for
/// example `game:/levels/intro.json` or `user:/settings.json`. Mounts map a name to a real directory, so
/// the same code works in the editor (loose files), in exported builds (packaged directories) and with
/// per-user save locations. Relative parts use forward slashes and may not contain `.` or `..`
/// components, backslashes or drive letters, so a virtual path can never escape its mount.

#include <illusion/core/Base.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// Result of a file operation.
enum class FileError : u8
{
    None,          ///< Success.
    InvalidPath,   ///< The virtual path is malformed or tries to escape its mount.
    UnknownMount,  ///< The mount name is not registered.
    NotFound,      ///< The file or directory does not exist.
    ReadOnly,      ///< Writing to a read-only mount.
    AccessDenied,  ///< The operating system refused access.
    IoError,       ///< Any other I/O failure (including damaged archive data).
    Unsupported,   ///< The mount cannot do this (e.g. resolve() on an archive mount).
    AlreadyExists, ///< The target of a copy, move or new directory exists.
    NotEmpty,      ///< Removing a directory that has entries without asking for a recursive removal.
};

/// Human-readable name of @p error, e.g. "NotFound".
[[nodiscard]] std::string_view fileErrorName(FileError error) noexcept;

/// Whether a mount accepts writes.
enum class MountAccess : u8
{
    ReadOnly,  ///< Reads only (game data).
    ReadWrite, ///< Reads and writes (saves, settings, caches).
};

/// What VirtualFileSystem::stat() reports about a file or directory.
struct FileStat
{
    bool isDirectory = false; ///< True for directories.
    u64 size = 0;             ///< Size in bytes (0 for directories).
    /// Last modification in the file system's clock, in nanoseconds (0 in archives). Only for comparing
    /// two stamps of the same file system ("has it changed?"); it is not a calendar time.
    i64 modified = 0;
};

/// One entry returned by VirtualFileSystem::list().
struct DirectoryEntry
{
    std::string name;         ///< File or directory name (no path).
    bool isDirectory = false; ///< True for directories.
    u64 size = 0;             ///< Size in bytes (0 for directories).
};

/// Maps virtual paths to real files. Not thread-safe for mount()/unmount(); concurrent reads are safe
/// once mounts are set up.
class VirtualFileSystem : NonCopyable
{
public:
    VirtualFileSystem();
    ~VirtualFileSystem();

    /// Mounts directory @p root as @p name. Names use lowercase letters, digits, '_' and '-'. Returns
    /// NotFound if @p root is not an existing directory and InvalidPath for bad names and for "builtin",
    /// which is reserved for the engine's built-in assets (AssetId.hpp). Mounting an existing name replaces
    /// it.
    FileError mount(std::string_view name, const std::filesystem::path& root,
                    MountAccess access = MountAccess::ReadOnly);

    /// Mounts the archive file @p archive (see Archive.hpp) as @p name, read-only. Paths inside it work
    /// exactly like paths in a mounted directory. Returns FileError::NotFound if the file is missing,
    /// FileError::IoError if it is not a valid archive and FileError::InvalidPath for names mount() rejects.
    FileError mountArchive(std::string_view name, const std::filesystem::path& archive);

    /// Removes a mount. Returns false if it did not exist.
    bool unmount(std::string_view name);

    /// Returns true if @p name is mounted.
    [[nodiscard]] bool isMounted(std::string_view name) const;

    /// Converts @p virtualPath to a real path in @p out. Does not check that the file exists.
    FileError resolve(std::string_view virtualPath, std::filesystem::path& out) const;

    /// Reads a whole file into @p out (replacing its contents).
    FileError readFile(std::string_view virtualPath, std::vector<std::byte>& out) const;

    /// Reads a whole file as text into @p out (bytes are copied unchanged; no newline conversion).
    FileError readText(std::string_view virtualPath, std::string& out) const;

    /// Writes @p data to a file on a ReadWrite mount, creating parent directories. The file is written to
    /// a temporary name first and then renamed, so readers never see a half-written file.
    FileError writeFile(std::string_view virtualPath, std::span<const std::byte> data);

    /// Writes @p text to a file. See writeFile().
    FileError writeText(std::string_view virtualPath, std::string_view text);

    /// Returns true if the virtual path names an existing file or directory.
    [[nodiscard]] bool exists(std::string_view virtualPath) const;

    /// Lists a directory's entries in @p out, sorted by name.
    FileError list(std::string_view virtualPath, std::vector<DirectoryEntry>& out) const;

    /// Lists every file and directory below a directory in @p out. Names are paths relative to it with '/'
    /// separators ("textures/rock.png"), sorted so that a directory comes before its contents.
    FileError listRecursive(std::string_view virtualPath, std::vector<DirectoryEntry>& out) const;

    /// Reports whether @p virtualPath is a file or a directory, its size and modification stamp.
    FileError stat(std::string_view virtualPath, FileStat& out) const;

    /// Creates a directory and any missing parents. Returns AlreadyExists if a file or directory is there.
    FileError createDirectory(std::string_view virtualPath);

    /// Removes a file or a directory. A directory with entries is removed only when @p recursive is true
    /// (NotEmpty otherwise). The root of a mount cannot be removed (InvalidPath).
    FileError remove(std::string_view virtualPath, bool recursive = false);

    /// Moves or renames a file or directory. Both paths must be on writable directory mounts; the target
    /// must not exist (AlreadyExists) and a directory cannot move into itself (InvalidPath). Missing
    /// parent directories of the target are created.
    FileError rename(std::string_view from, std::string_view to);

    /// Copies a file, or a directory with everything in it. The source may be on any mount, archives
    /// included; the target must be on a writable directory mount and must not exist (AlreadyExists).
    FileError copy(std::string_view from, std::string_view to);

    /// Resolves dot components and repeated slashes in a virtual path.
    ///
    /// For example `game:/models/../textures/./a.png` becomes `game:/textures/a.png`, so paths built by
    /// combining a base path with a relative reference (as file formats like glTF do) can be used.
    /// Returns std::nullopt if the path is malformed or a parent reference would leave the mount.
    [[nodiscard]] static std::optional<std::string> normalizePath(std::string_view virtualPath);

private:
    struct Mount;
    FileError resolveMount(std::string_view virtualPath, const Mount*& mount,
                           std::filesystem::path& out) const;
    FileError writablePath(std::string_view virtualPath, std::filesystem::path& out) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Converts UTF-8 text to a filesystem path. Use this instead of `std::filesystem::path(const char*)`,
/// which interprets narrow strings in the system code page on Windows and garbles non-ASCII characters.
[[nodiscard]] std::filesystem::path utf8Path(std::string_view utf8);

/// Directory containing the running executable.
[[nodiscard]] std::filesystem::path executableDirectory();

/// Per-user directory for saves and settings: `%APPDATA%\<organization>\<application>` on Windows,
/// `~/Library/Application Support/<organization>/<application>` on macOS and
/// `$XDG_DATA_HOME/<organization>/<application>` (default `~/.local/share/...`) on Linux. The directory
/// is not created.
[[nodiscard]] std::filesystem::path userDataDirectory(std::string_view organization,
                                                      std::string_view application);

} // namespace illusion

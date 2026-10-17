#pragma once

/// @file
/// Packed archives (`.illpak`): many files in one read-only file with a checksummed table of contents
/// and a content hash per file. Mount one with VirtualFileSystem::mountArchive() to read it through
/// virtual paths, exactly like a directory.

#include <illusion/core/Base.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// One file in an archive.
struct ArchiveEntry
{
    std::string path;    ///< Relative path with '/' separators, e.g. "textures/rock.png".
    u64 offset = 0;      ///< Position of the data in the archive file.
    u64 size = 0;        ///< Size in bytes.
    u64 contentHash = 0; ///< xxhash64() of the data, checked on every read.
};

/// A file to put into an archive.
struct ArchiveInput
{
    std::string path;            ///< Relative path with '/' separators.
    std::vector<std::byte> data; ///< Contents.
};

/// Reads an archive. Thread-safe: reads may run concurrently.
class ArchiveReader : NonCopyable
{
public:
    /// Opens and validates @p path (header, table of contents and its checksum, entry bounds). Returns
    /// nullptr and sets @p error on failure.
    [[nodiscard]] static std::unique_ptr<ArchiveReader> open(const std::filesystem::path& path,
                                                             std::string* error = nullptr);
    ~ArchiveReader();

    /// The entry for @p relativePath, or nullptr.
    [[nodiscard]] const ArchiveEntry* find(std::string_view relativePath) const noexcept;

    /// Reads @p entry into @p out. Returns FileError::IoError if the data is damaged (hash mismatch).
    FileError read(const ArchiveEntry& entry, std::vector<std::byte>& out) const;

    /// True if @p relativePath is a directory (a prefix of some entry); "" is the root.
    [[nodiscard]] bool isDirectory(std::string_view relativePath) const noexcept;

    /// Lists the files and subdirectories directly inside @p relativePath ("" for the root), sorted by
    /// name. Returns FileError::NotFound if it is not a directory.
    FileError list(std::string_view relativePath, std::vector<DirectoryEntry>& out) const;

    /// All entries, sorted by path.
    [[nodiscard]] std::span<const ArchiveEntry> entries() const noexcept;

private:
    ArchiveReader();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Writes an archive containing @p files to @p path (atomically: a temporary file is renamed). The
/// output is deterministic: entries are sorted by path, so the order of @p files does not matter.
/// Paths must be valid relative paths and unique.
FileError writeArchive(const std::filesystem::path& path, std::span<const ArchiveInput> files);

/// Writes every file under @p directory (recursively) into an archive at @p archive.
FileError packDirectory(const std::filesystem::path& directory, const std::filesystem::path& archive);

} // namespace illusion

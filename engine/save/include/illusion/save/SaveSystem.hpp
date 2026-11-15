#pragma once

/// @file
/// The host side of saved games (docs/adr/0058-saved-games.md): SaveSystem owns the save thread that encodes,
/// checks, reads and writes slot files in request order and keeps the slot list. GameSession talks to it
/// through SaveSystemService; tests and tools may use it directly.
///
/// @code
/// illusion::SaveSystem saves({.files = &files, .directory = "user:/saves"});
/// illusion::GameSessionDesc desc;
/// desc.saves = &saves;
/// // ... run the game ...
/// session.reset();
/// saves.waitIdle(); // a save requested in the last frame is written
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/save/SaveFile.hpp>
#include <illusion/save/SaveService.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// The file operations of a SaveSystem, called only on its save thread (after construction); paths are
/// virtual paths in the save directory. The default, VfsSaveStorage, uses a VirtualFileSystem; tests put a
/// layer in between that fails.
class SaveStorage : NonCopyable
{
public:
    /// Destroys the storage.
    virtual ~SaveStorage() = default;

    /// Creates the save directory @p path and its parents; AlreadyExists when it exists.
    virtual FileError createDirectory(std::string_view path) = 0;
    /// Lists the entries of the directory @p path.
    virtual FileError list(std::string_view path, std::vector<DirectoryEntry>& out) = 0;
    /// Reads the file @p path into @p out.
    virtual FileError read(std::string_view path, std::vector<std::byte>& out) = 0;
    /// Replaces the file @p path with @p data atomically and durably
    /// (VirtualFileSystem::writeFileAtomic()): after a failure or a crash it holds its old or its new bytes.
    virtual FileError writeAtomic(std::string_view path, std::span<const std::byte> data) = 0;
    /// Removes the file @p path.
    virtual FileError remove(std::string_view path) = 0;

protected:
    SaveStorage() = default;
};

/// SaveStorage over a VirtualFileSystem, whose mounts must not change while it is used.
class VfsSaveStorage final : public SaveStorage
{
public:
    /// Uses @p files, which must outlive it.
    explicit VfsSaveStorage(VirtualFileSystem& files) noexcept
        : m_files(files)
    {
    }

    /// @name SaveStorage
    /// @{
    FileError createDirectory(std::string_view path) override;
    FileError list(std::string_view path, std::vector<DirectoryEntry>& out) override;
    FileError read(std::string_view path, std::vector<std::byte>& out) override;
    FileError writeAtomic(std::string_view path, std::span<const std::byte> data) override;
    FileError remove(std::string_view path) override;
    /// @}

private:
    VirtualFileSystem& m_files;
};

/// Creation parameters of a SaveSystem.
struct SaveSystemDesc
{
    /// The file system with the writable save directory; ignored with storage. One of both is required.
    VirtualFileSystem* files = nullptr;
    /// Virtual path of the directory of the slot files: `user:/saves` in the player, `cache:/saves` in the
    /// editor's Play mode.
    std::string directory = "user:/saves";
    /// File operations used instead of files (tests); must outlive the SaveSystem.
    SaveStorage* storage = nullptr;
    /// Seconds since the Unix epoch (UTC) written as savedAt; empty uses the system clock.
    std::function<i64()> clock{};
};

/// A request that the save thread finished, read with SaveSystem::takeCompleted().
struct SaveCompletion
{
    u64 ticket = 0;                             ///< What the request call returned.
    SaveEventType type = SaveEventType::Failed; ///< Saved, Loaded, LoadedBackup, Removed or Failed.
    SaveError error = SaveError::IoError;       ///< Why, when type is Failed.
    std::string slot;                           ///< The slot.
    std::string world;                          ///< Loaded and LoadedBackup: the World section to apply.
};

/// One slot of the slot list kept by the save thread (see SaveSlotInfo).
struct SaveSlotRecord
{
    std::string name;                        ///< The slot name.
    SaveFileMetadata metadata;               ///< The metadata of the file that a load reads.
    SaveSlotState state = SaveSlotState::Ok; ///< Whether it can be loaded.
    std::string screenshot;                  ///< Virtual path of the slot's image, or empty.

    /// The record as SaveSlotInfo, viewing its strings.
    [[nodiscard]] SaveSlotInfo info() const noexcept
    {
        return SaveSlotInfo{name, metadata.metadata(), metadata.savedAt, state, screenshot};
    }
};

/// Saved games in one directory, written and read on one save thread in request order (ADR 0058). The thread
/// starts on first use; its first job scans the directory once for the slot list, which each request then
/// keeps current, so reading the list touches no file. Before a slot is replaced its current valid file
/// becomes `<slot>.save.bak`; reading a slot whose file is missing or corrupt reads the backup instead.
/// Requests and results are plain data: capturing and applying the World is the caller's job (see
/// SaveSystemService). One GameSession at a time uses a SaveSystem. Thread-safe.
class SaveSystem : NonCopyable
{
public:
    /// Creates the save directory (on the calling thread) unless it exists; available() tells whether that
    /// worked. Does not start the thread.
    explicit SaveSystem(const SaveSystemDesc& desc);

    /// Finishes the queued writes and removals (queued loads are dropped), then joins the thread.
    ~SaveSystem();

    /// True when the save directory exists.
    [[nodiscard]] bool available() const noexcept;

    /// The directory of the slot files.
    [[nodiscard]] std::string_view directory() const noexcept;

    /// The current time of the desc's clock, in seconds since the Unix epoch (UTC).
    [[nodiscard]] i64 now() const;

    /// Queues writing @p world (scene JSON from captureSave()) with @p metadata (copied) and @p savedAt as
    /// the slot @p slot. Returns the request's ticket, or InvalidName, InvalidValue or Unavailable.
    Expected<u64, SaveError> write(std::string_view slot, const SaveMetadata& metadata, i64 savedAt,
                                   std::string world);

    /// Queues reading the slot @p slot; its completion carries the World section. Returns the ticket, or
    /// InvalidName or Unavailable.
    Expected<u64, SaveError> load(std::string_view slot);

    /// Queues removing the slot @p slot and its backup. Returns the ticket, or InvalidName or Unavailable.
    Expected<u64, SaveError> remove(std::string_view slot);

    /// The ticket that the next request gets.
    [[nodiscard]] u64 nextTicket() const;

    /// True while a request (or the first scan) is queued or running.
    [[nodiscard]] bool busy() const;

    /// Blocks until no request is queued or running; starts the thread, so the slot list is scanned after it.
    void waitIdle();

    /// Moves the finished requests, in request order, to the end of @p out.
    void takeCompleted(std::vector<SaveCompletion>& out);

    /// Version of the slot list; it changes whenever the list does.
    [[nodiscard]] u64 slotsVersion() const;

    /// Replaces @p out with the slot list, sorted by name, and returns its version.
    u64 copySlots(std::vector<SaveSlotRecord>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace illusion

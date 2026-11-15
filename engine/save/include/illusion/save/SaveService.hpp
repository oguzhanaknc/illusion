#pragma once

/// @file
/// The saved-game part of the scripting API (docs/adr/0058-saved-games.md): game modules write, load and
/// remove slots of their Saved entities and read the slot list. Requests are queued; their results arrive as
/// events in a later update.
///
/// @code
/// if (illusion::SaveService* saves = context.service<illusion::SaveService>())
/// {
///     if (pausePressed)
///     {
///         static_cast<void>(saves->save("slot1", {.title = "Chapter 2", .playSeconds = played}));
///     }
///     std::array<illusion::SaveEvent, 8> events{};
///     const illusion::u32 count = saves->pollEvents(events);
///     for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
///     {
///         if (events[i].type == illusion::SaveEventType::Loaded)
///         {
///             findPlayerAgain(); // Entity handles of saved entities changed
///         }
///     }
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/save/SaveFile.hpp>

#include <optional>
#include <span>
#include <string_view>

namespace illusion
{

/// What happened to a request of SaveService.
enum class SaveEventType : u8
{
    Saved,        ///< The slot was written.
    Loaded,       ///< The slot was read and applied to the World.
    LoadedBackup, ///< The slot's file was missing or corrupt; its backup was read and applied instead.
    Removed,      ///< The slot was removed.
    Failed,       ///< The request failed; SaveEvent::error says why.
};

/// Stable name of @p type, e.g. "LoadedBackup".
[[nodiscard]] constexpr std::string_view saveEventTypeName(SaveEventType type) noexcept
{
    switch (type)
    {
    case SaveEventType::Saved:
        return "Saved";
    case SaveEventType::Loaded:
        return "Loaded";
    case SaveEventType::LoadedBackup:
        return "LoadedBackup";
    case SaveEventType::Removed:
        return "Removed";
    case SaveEventType::Failed:
        return "Failed";
    }
    return "Failed";
}

/// The result of one save(), load() or remove() of SaveService.
struct SaveEvent
{
    SaveEventType type = SaveEventType::Failed; ///< What happened.
    std::string_view slot{};                    ///< The slot of the request.
    SaveError error = SaveError::IoError;       ///< Why, when type is Failed.
};

/// What the slot list knows about a slot's file.
enum class SaveSlotState : u8
{
    Ok,           ///< The slot file is valid.
    Backup,       ///< The slot file is missing or corrupt, its backup is valid; loading reads the backup.
    Corrupt,      ///< Neither the slot file nor a backup can be read.
    NewerVersion, ///< The slot file was written by a newer version of the container; it cannot be loaded.
};

/// Stable name of @p state, e.g. "NewerVersion".
[[nodiscard]] constexpr std::string_view saveSlotStateName(SaveSlotState state) noexcept
{
    switch (state)
    {
    case SaveSlotState::Ok:
        return "Ok";
    case SaveSlotState::Backup:
        return "Backup";
    case SaveSlotState::Corrupt:
        return "Corrupt";
    case SaveSlotState::NewerVersion:
        return "NewerVersion";
    }
    return "Corrupt";
}

/// One slot of the slot list; empty metadata and savedAt 0 when the state is Corrupt or NewerVersion.
struct SaveSlotInfo
{
    std::string_view name{};                 ///< The slot name.
    SaveMetadata metadata{};                 ///< What the game wrote with save().
    i64 savedAt = 0;                         ///< When it was written, in seconds since the Unix epoch (UTC).
    SaveSlotState state = SaveSlotState::Ok; ///< Whether it can be loaded.
    std::string_view screenshot{};           ///< Virtual path of the slot's image, or empty.
};

/// Saved games of the running game (ADR 0058), implemented by the host (SaveSystemService over a SaveSystem)
/// and found by modules with `context.service<SaveService>()`. save() writes the Saved entities and their
/// descendants; load() replaces them with a slot's. Both are queued in call order with remove(): a request
/// that cannot be queued fails at once, everything later arrives as a SaveEvent that pollEvents() returns in
/// a later update. Entity handles of saved entities change when a load is applied; find them again after the
/// Loaded or LoadedBackup event. String views it returns stay valid until the next update. Adding a function
/// changes the module API version.
/// @ingroup scripting_api
class SaveService : NonCopyable
{
public:
    /// True when saved games can be written here (a save directory exists).
    [[nodiscard]] virtual bool available() const = 0;

    /// Queues writing the saved entities to @p slot with @p metadata (copied); the World is captured in this
    /// update. Fails at once with InvalidName, InvalidValue (isValidSaveMetadata()) or Unavailable.
    virtual Expected<void, SaveError> save(std::string_view slot, const SaveMetadata& metadata) = 0;

    /// Queues reading @p slot and applying it to the World in a later update. Fails at once with InvalidName
    /// or Unavailable; NotFound, Corrupt and NewerVersion arrive as Failed events.
    virtual Expected<void, SaveError> load(std::string_view slot) = 0;

    /// Queues removing @p slot and its backup. Fails at once with InvalidName or Unavailable.
    virtual Expected<void, SaveError> remove(std::string_view slot) = 0;

    /// True while a request has not delivered its event yet.
    [[nodiscard]] virtual bool busy() const = 0;

    /// Writes the slots, sorted by name, into @p out and returns how many there are; reads no file.
    virtual u32 slots(std::span<SaveSlotInfo> out) = 0;

    /// The slot @p slot of the slot list, or nothing.
    [[nodiscard]] virtual std::optional<SaveSlotInfo> slot(std::string_view slot) const = 0;

    /// Writes the events of the last update into @p out and returns how many there are; every call in an
    /// update returns the same events.
    virtual u32 pollEvents(std::span<SaveEvent> out) = 0;

protected:
    SaveService() = default;
    virtual ~SaveService() = default;
};

} // namespace illusion

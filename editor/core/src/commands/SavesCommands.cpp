// The saves.* commands (ADR 0058): the saved games of the game that Play mode runs. They go through the
// SaveService of the PlayHost; write, load and remove are async commands that finish with the request's
// event.

#include "CommandSupport.hpp"

#include <algorithm>
#include <format>
#include <memory>

namespace illusion::editor::detail
{
namespace
{

enum class RequestKind : u8
{
    Write,
    Load,
    Remove,
};

constexpr std::string_view kSlotRule = "must be 1 to 32 letters, digits, '_' or '-'";

Expected<SaveService*, CommandError> requireSaves(CommandContext& context)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    if (play->status().mode != PlayMode::Play)
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed,
            "saved games need a session in mode \"play\"; stop it and start one with play.start"));
    }
    SaveService* saves = play->saves();
    if (saves == nullptr || !saves->available())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "saved games are not available: the game has no writable save "
                                             "directory (cache:/saves of the project)"));
    }
    return saves;
}

Expected<void, CommandError> checkSlot(const std::string& slot)
{
    if (!isValidSaveSlotName(slot))
    {
        return unexpected(CommandError::invalid("/slot", std::string(kSlotRule)));
    }
    return {};
}

std::string_view verbOf(RequestKind kind) noexcept
{
    switch (kind)
    {
    case RequestKind::Write:
        return "writing";
    case RequestKind::Load:
        return "loading";
    case RequestKind::Remove:
        return "removing";
    }
    return "writing";
}

std::string_view hintOf(SaveError error) noexcept
{
    switch (error)
    {
    case SaveError::NotFound:
        return "no slot has that name";
    case SaveError::Corrupt:
        return "the slot file and its backup cannot be read";
    case SaveError::NewerVersion:
        return "the slot was written by a newer version of the engine";
    case SaveError::Unavailable:
        return "saved games are not available";
    default:
        return "see the editor log";
    }
}

CommandError failure(RequestKind kind, std::string_view slot, SaveError error)
{
    ErrorKind errorKind = ErrorKind::PreconditionFailed;
    switch (error)
    {
    case SaveError::NotFound:
        errorKind = ErrorKind::NotFound;
        break;
    case SaveError::Unavailable:
        errorKind = ErrorKind::Unsupported;
        break;
    case SaveError::InvalidName:
    case SaveError::InvalidValue:
        errorKind = ErrorKind::ValidationFailed;
        break;
    case SaveError::IoError:
        errorKind = ErrorKind::IoError;
        break;
    default:
        break;
    }
    return CommandError::make(errorKind, std::format("{} slot '{}' failed with {}: {}", verbOf(kind), slot,
                                                     saveErrorName(error), hintOf(error)));
}

bool finishes(RequestKind kind, SaveEventType type) noexcept
{
    if (type == SaveEventType::Failed)
    {
        return true;
    }
    switch (kind)
    {
    case RequestKind::Write:
        return type == SaveEventType::Saved;
    case RequestKind::Load:
        return type == SaveEventType::Loaded || type == SaveEventType::LoadedBackup;
    case RequestKind::Remove:
        return type == SaveEventType::Removed;
    }
    return false;
}

// Queues one request in its first update and waits for the event that finishes it.
class SaveRequestTask final : public CommandTask<SavesRequestResult>
{
public:
    SaveRequestTask(RequestKind kind, std::string slot, SavesWriteParams metadata)
        : m_kind(kind)
        , m_slot(std::move(slot))
        , m_metadata(std::move(metadata))
    {
    }

    Expected<std::optional<SavesRequestResult>, CommandError> update(CommandContext& context,
                                                                     CommandProgress&) override
    {
        PlayHost* play = context.editor().play();
        SaveService* saves = play != nullptr ? play->saves() : nullptr;
        if (saves == nullptr)
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "the play session ended before the request finished"));
        }
        if (!m_sent)
        {
            m_first = play->saveEventCount();
            if (Expected<void, SaveError> sent = send(*saves); !sent)
            {
                return unexpected(failure(m_kind, m_slot, sent.error()));
            }
            m_sent = true;
        }
        m_events.clear();
        play->saveEvents(m_first, m_events);
        for (const PlaySaveEvent& event : m_events)
        {
            if (event.slot != m_slot || !finishes(m_kind, event.type))
            {
                continue;
            }
            if (event.type == SaveEventType::Failed)
            {
                return unexpected(failure(m_kind, m_slot, event.error));
            }
            return std::optional<SavesRequestResult>(
                SavesRequestResult{m_slot, std::string(saveEventTypeName(event.type))});
        }
        return std::optional<SavesRequestResult>{};
    }

private:
    Expected<void, SaveError> send(SaveService& saves) const
    {
        switch (m_kind)
        {
        case RequestKind::Write:
            return saves.save(m_slot, SaveMetadata{.title = m_metadata.title.value_or(std::string()),
                                                   .playSeconds = m_metadata.playSeconds.value_or(0.0),
                                                   .gameVersion = m_metadata.gameVersion.value_or(0),
                                                   .custom = m_metadata.custom.value_or(std::string())});
        case RequestKind::Load:
            return saves.load(m_slot);
        case RequestKind::Remove:
            return saves.remove(m_slot);
        }
        return {};
    }

    RequestKind m_kind;
    std::string m_slot;
    SavesWriteParams m_metadata;
    bool m_sent = false;
    u64 m_first = 0;
    std::vector<PlaySaveEvent> m_events;
};

using SaveTask = std::unique_ptr<CommandTask<SavesRequestResult>>;

// Warns when nothing will answer until the game advances.
void warnIfPaused(CommandContext& context)
{
    if (context.editor().play()->status().state == PlayState::Paused)
    {
        context.warn(
            "the game is paused: the request finishes when it advances (play.pause with paused false, "
            "or play.step)");
    }
}

Expected<SavesListResult, CommandError> savesList(CommandContext& context, const NoParams&)
{
    Expected<SaveService*, CommandError> saves = requireSaves(context);
    if (!saves)
    {
        return unexpected(std::move(saves).error());
    }
    std::vector<SaveSlotInfo> slots((*saves)->slots({}));
    slots.resize((*saves)->slots(slots));
    SavesListResult result;
    result.busy = (*saves)->busy();
    for (const SaveSlotInfo& slot : slots)
    {
        result.slots.push_back({.name = std::string(slot.name),
                                .title = std::string(slot.metadata.title),
                                .playSeconds = slot.metadata.playSeconds,
                                .gameVersion = slot.metadata.gameVersion,
                                .custom = std::string(slot.metadata.custom),
                                .savedAt = slot.savedAt,
                                .state = std::string(saveSlotStateName(slot.state)),
                                .screenshot = std::string(slot.screenshot)});
    }
    return result;
}

Expected<SaveTask, CommandError> savesWrite(CommandContext& context, const SavesWriteParams& params)
{
    if (Expected<void, CommandError> valid = checkSlot(params.slot); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (params.title && !isValidSaveMetadata({.title = *params.title}))
    {
        return unexpected(CommandError::invalid("/title", "must be valid UTF-8 of at most 128 bytes"));
    }
    if (params.custom && !isValidSaveMetadata({.custom = *params.custom}))
    {
        return unexpected(CommandError::invalid("/custom", "must be valid UTF-8 of at most 1024 bytes"));
    }
    if (params.playSeconds && !isValidSaveMetadata({.playSeconds = *params.playSeconds}))
    {
        return unexpected(CommandError::invalid("/playSeconds", "must be a finite number, 0 or more"));
    }
    if (Expected<SaveService*, CommandError> saves = requireSaves(context); !saves)
    {
        return unexpected(std::move(saves).error());
    }
    warnIfPaused(context);
    return SaveTask(std::make_unique<SaveRequestTask>(RequestKind::Write, params.slot, params));
}

Expected<SaveTask, CommandError> savesRequest(CommandContext& context, RequestKind kind,
                                              const SavesSlotParams& params)
{
    if (Expected<void, CommandError> valid = checkSlot(params.slot); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    if (Expected<SaveService*, CommandError> saves = requireSaves(context); !saves)
    {
        return unexpected(std::move(saves).error());
    }
    warnIfPaused(context);
    return SaveTask(std::make_unique<SaveRequestTask>(kind, params.slot, SavesWriteParams{}));
}

Expected<SaveTask, CommandError> savesLoad(CommandContext& context, const SavesSlotParams& params)
{
    return savesRequest(context, RequestKind::Load, params);
}

Expected<SaveTask, CommandError> savesRemove(CommandContext& context, const SavesSlotParams& params)
{
    return savesRequest(context, RequestKind::Remove, params);
}

} // namespace

void registerSavesCommands(CommandRegistry& registry)
{
    registry.add<&savesList>({
        .name = "saves.list",
        .title = strings::kCommandSavesListTitle,
        .description = strings::kCommandSavesListDescription,
        .agentSummary =
            "Play mode: lists the slots of the running game's saved games (in cache:/saves of the project), "
            "sorted by name, with title, playSeconds, gameVersion, custom, savedAt (seconds since the Unix "
            "epoch, UTC), state (Ok, Backup, Corrupt or NewerVersion) and the virtual path of the slot's "
            "image, and whether a request is still running. The list is read when the game updates, so it "
            "is empty until the first update after play.start. Example: {}.",
        .category = "Saves",
        .icon = "save",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Read,
    });
    registry.addAsync<&savesWrite>({
        .name = "saves.write",
        .title = strings::kCommandSavesWriteTitle,
        .description = strings::kCommandSavesWriteDescription,
        .agentSummary =
            "Play mode, asynchronous: writes the running game's saved entities (the Saved tag and their "
            "descendants) to a slot and answers with the event \"Saved\", or fails with the SaveError name. "
            "The request is sent when the game updates, so a paused game answers only after play.step or "
            "play.pause with paused false. slot is 1 to 32 letters, digits, '_' or '-'; title (128 bytes), "
            "playSeconds, gameVersion and custom (1024 bytes) are the metadata that saves.list shows. "
            "Writing an existing slot replaces it and keeps the old file as backup. Example: {\"slot\": "
            "\"slot1\", \"title\": \"Chapter 2\", \"playSeconds\": 3600, \"gameVersion\": 3}.",
        .category = "Saves",
        .icon = "save",
        .flags = {.destructive = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
    });
    registry.addAsync<&savesLoad>({
        .name = "saves.load",
        .title = strings::kCommandSavesLoadTitle,
        .description = strings::kCommandSavesLoadDescription,
        .agentSummary =
            "Play mode, asynchronous: replaces the running game's saved entities with those of a slot and "
            "answers with the event \"Loaded\", or \"LoadedBackup\" when the slot file was missing or "
            "corrupt and its backup was read; fails with the SaveError name (NotFound, Corrupt, "
            "NewerVersion, ...). Entity handles and ids of saved entities change, so look them up again "
            "(entity.get). Changes the play World only; stopping the session discards it. Example: "
            "{\"slot\": \"slot1\"}.",
        .category = "Saves",
        .icon = "restore",
        .flags = {.destructive = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Edit,
    });
    registry.addAsync<&savesRemove>({
        .name = "saves.remove",
        .title = strings::kCommandSavesRemoveTitle,
        .description = strings::kCommandSavesRemoveDescription,
        .agentSummary =
            "Play mode, asynchronous: deletes a slot, its backup and its image, and answers with the event "
            "\"Removed\", or fails with NotFound when no slot has that name. Cannot be undone. Example: "
            "{\"slot\": \"slot1\"}.",
        .category = "Saves",
        .icon = "delete",
        .flags = {.destructive = true},
        .mode = CommandMode::Play,
        .permission = PermissionClass::Destructive,
    });
}

} // namespace illusion::editor::detail

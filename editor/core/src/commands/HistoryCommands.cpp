#include "CommandSupport.hpp"

#include <algorithm>

namespace illusion::editor::detail
{
namespace
{

HistoryResult historyResult(EditorContext& editor, u32 steps)
{
    return HistoryResult{steps, static_cast<u32>(editor.history().position()), editor.revision()};
}

Expected<HistoryResult, CommandError> historyUndo(CommandContext& context, const HistoryUndoParams& params)
{
    EditorContext& editor = context.editor();
    const u32 steps = params.steps.value_or(1);
    const std::string scope = params.scope.value_or("all");
    if (scope != "all" && scope != "mine")
    {
        return unexpected(CommandError::invalid("/scope", "must be \"all\" or \"mine\""));
    }
    if (scope == "mine")
    {
        Expected<u32, CommandError> undone = context.dryRun() ? editor.checkUndoOwn(steps, context.origin())
                                                              : editor.undoOwn(steps, context.origin());
        if (!undone)
        {
            return unexpected(std::move(undone).error());
        }
        return historyResult(editor, *undone);
    }
    if (context.dryRun())
    {
        return historyResult(editor, std::min(steps, static_cast<u32>(editor.history().position())));
    }
    Expected<u32, CommandError> undone = editor.undo(steps);
    if (!undone)
    {
        return unexpected(std::move(undone).error());
    }
    return historyResult(editor, *undone);
}

Expected<HistoryResult, CommandError> historyRedo(CommandContext& context, const HistoryStepParams& params)
{
    EditorContext& editor = context.editor();
    const u32 steps = params.steps.value_or(1);
    if (context.dryRun())
    {
        const usize available = editor.history().steps().size() - editor.history().position();
        return historyResult(editor, std::min(steps, static_cast<u32>(available)));
    }
    Expected<u32, CommandError> redone = editor.redo(steps);
    if (!redone)
    {
        return unexpected(std::move(redone).error());
    }
    return historyResult(editor, *redone);
}

Expected<HistoryListResult, CommandError> historyList(CommandContext& context, const NoParams&)
{
    const UndoStack& history = context.editor().history();
    HistoryListResult result;
    const std::span<const UndoStep> steps = history.steps();
    for (usize i = 0; i < steps.size(); ++i)
    {
        result.steps.push_back(
            HistoryEntry{static_cast<u32>(i), steps[i].label, steps[i].origin, i < history.position()});
    }
    result.position = static_cast<u32>(history.position());
    result.bytes = history.bytes();
    result.budget = history.budget();
    return result;
}

Expected<HistoryResult, CommandError> historyJump(CommandContext& context, const HistoryJumpParams& params)
{
    EditorContext& editor = context.editor();
    const usize position = editor.history().position();
    if (params.position > editor.history().steps().size())
    {
        return unexpected(CommandError::invalid(
            "/position", "must be at most " + std::to_string(editor.history().steps().size())));
    }
    const u32 distance = static_cast<u32>(params.position > position ? params.position - position
                                                                     : position - params.position);
    if (context.dryRun())
    {
        return historyResult(editor, distance);
    }
    if (Expected<void, CommandError> jumped = editor.jump(params.position); !jumped)
    {
        return unexpected(std::move(jumped).error());
    }
    return historyResult(editor, distance);
}

Expected<HistoryResult, CommandError> historyClear(CommandContext& context, const NoParams&)
{
    EditorContext& editor = context.editor();
    if (context.dryRun())
    {
        return historyResult(editor, static_cast<u32>(editor.history().steps().size()));
    }
    const usize removed = editor.clearHistory();
    return historyResult(editor, static_cast<u32>(removed));
}

} // namespace

void registerHistoryCommands(CommandRegistry& registry)
{
    registry.add<&historyUndo>({
        .name = "history.undo",
        .title = strings::kCommandHistoryUndoTitle,
        .description = strings::kCommandHistoryUndoDescription,
        .agentSummary =
            "Undoes the last steps (default 1) and restores the selection before them. With scope "
            "\"mine\" it undoes the caller's own last steps even if others edited later, unless "
            "their steps touched the same Components (conflict). Example: {\"steps\": 2, "
            "\"scope\": \"mine\"}.",
        .category = "History",
        .icon = "undo",
        .flags = {.outsideTransactions = true},
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+Z",
    });
    registry.add<&historyRedo>({
        .name = "history.redo",
        .title = strings::kCommandHistoryRedoTitle,
        .description = strings::kCommandHistoryRedoDescription,
        .agentSummary = "Redoes undone steps (default 1). Example: {}.",
        .category = "History",
        .icon = "redo",
        .flags = {.outsideTransactions = true},
        .permission = PermissionClass::Edit,
        .palette = true,
        .defaultShortcut = "Ctrl+Y",
    });
    registry.add<&historyList>({
        .name = "history.list",
        .title = strings::kCommandHistoryListTitle,
        .description = strings::kCommandHistoryListDescription,
        .agentSummary = "Lists the undo steps with labels and origins, the position, and the memory used. "
                        "Example: {}.",
        .category = "History",
        .icon = "history",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&historyJump>({
        .name = "history.jump",
        .title = strings::kCommandHistoryJumpTitle,
        .description = strings::kCommandHistoryJumpDescription,
        .agentSummary = "Undoes or redoes until the given number of steps is applied (0: before the first). "
                        "Example: {\"position\": 3}.",
        .category = "History",
        .icon = "manage_history",
        .flags = {.idempotent = true, .outsideTransactions = true},
        .permission = PermissionClass::Edit,
    });
    registry.add<&historyClear>({
        .name = "history.clear",
        .title = strings::kCommandHistoryClearTitle,
        .description = strings::kCommandHistoryClearDescription,
        .agentSummary = "Forgets every undo step (the scene is unchanged). Example: {}.",
        .category = "History",
        .icon = "delete_history",
        .flags = {.destructive = true, .idempotent = true, .outsideTransactions = true},
        .permission = PermissionClass::Destructive,
    });
}

} // namespace illusion::editor::detail

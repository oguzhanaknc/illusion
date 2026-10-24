#include "CommandSupport.hpp"

namespace illusion::editor::detail
{
namespace
{

Expected<TransactionBeginResult, CommandError> transactionBegin(CommandContext& context,
                                                                const TransactionBeginParams& params)
{
    EditorContext& editor = context.editor();
    const std::string label = params.label.value_or("Transaction");
    if (label.empty() || label.size() > 200)
    {
        return unexpected(CommandError::invalid("/label", "must have 1 to 200 bytes"));
    }
    if (context.dryRun())
    {
        if (const TransactionInfo* open = editor.transaction())
        {
            CommandError error = CommandError::make(
                open->caller == context.options().caller ? ErrorKind::PreconditionFailed : ErrorKind::Busy,
                "transaction '" + open->label + "' is already open");
            error.retryable = open->caller != context.options().caller;
            return unexpected(std::move(error));
        }
        return TransactionBeginResult{label};
    }
    if (Expected<void, CommandError> begun = editor.beginTransaction(label, context.options()); !begun)
    {
        return unexpected(std::move(begun).error());
    }
    return TransactionBeginResult{label};
}

Expected<TransactionEndResult, CommandError> endResult(EditorContext& editor, const TransactionInfo& info)
{
    return TransactionEndResult{info.commands, static_cast<u32>(info.operations),
                                static_cast<u32>(editor.history().position()), editor.revision()};
}

// The open transaction if the caller owns it.
Expected<TransactionInfo, CommandError> ownTransaction(CommandContext& context)
{
    const TransactionInfo* open = context.editor().transaction();
    if (open == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no transaction is open"));
    }
    if (open->caller != context.options().caller)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "transaction '" + open->label + "' belongs to " + open->origin));
    }
    return *open;
}

Expected<TransactionEndResult, CommandError> transactionCommit(CommandContext& context, const NoParams&)
{
    EditorContext& editor = context.editor();
    Expected<TransactionInfo, CommandError> info = ownTransaction(context);
    if (!info)
    {
        return unexpected(std::move(info).error());
    }
    if (context.dryRun())
    {
        return endResult(editor, *info);
    }
    Expected<u64, CommandError> step = editor.commitTransaction(context.options());
    if (!step)
    {
        return unexpected(std::move(step).error());
    }
    context.report().transaction = *step;
    return endResult(editor, *info);
}

Expected<TransactionEndResult, CommandError> transactionCancel(CommandContext& context, const NoParams&)
{
    EditorContext& editor = context.editor();
    Expected<TransactionInfo, CommandError> info = ownTransaction(context);
    if (!info)
    {
        return unexpected(std::move(info).error());
    }
    if (context.dryRun())
    {
        return endResult(editor, *info);
    }
    if (Expected<void, CommandError> cancelled = editor.cancelTransaction(context.options()); !cancelled)
    {
        return unexpected(std::move(cancelled).error());
    }
    return endResult(editor, *info);
}

} // namespace

void registerTransactionCommands(CommandRegistry& registry)
{
    registry.add<&transactionBegin>({
        .name = "transaction.begin",
        .title = strings::kCommandTransactionBeginTitle,
        .description = strings::kCommandTransactionBeginDescription,
        .agentSummary =
            "Begins a transaction of this client: its following changes become one undo step when "
            "transaction.commit runs, or are reverted by transaction.cancel (also when the client "
            "disconnects). Meanwhile other clients cannot change the scene. Example: "
            "{\"label\": \"Build board\"}.",
        .category = "Transaction",
        .icon = "playlist_add",
        .flags = {},
        .permission = PermissionClass::Edit,
    });
    registry.add<&transactionCommit>({
        .name = "transaction.commit",
        .title = strings::kCommandTransactionCommitTitle,
        .description = strings::kCommandTransactionCommitDescription,
        .agentSummary =
            "Ends this client's transaction and keeps its changes as one undo step (none if nothing "
            "changed). _editor.transaction is the step's id. Example: {}.",
        .category = "Transaction",
        .icon = "playlist_add_check",
        .flags = {},
        .permission = PermissionClass::Edit,
    });
    registry.add<&transactionCancel>({
        .name = "transaction.cancel",
        .title = strings::kCommandTransactionCancelTitle,
        .description = strings::kCommandTransactionCancelDescription,
        .agentSummary = "Ends this client's transaction and reverts every change made in it, restoring the "
                        "selection. Example: {}.",
        .category = "Transaction",
        .icon = "playlist_remove",
        .flags = {.destructive = true},
        .permission = PermissionClass::Edit,
    });
}

} // namespace illusion::editor::detail

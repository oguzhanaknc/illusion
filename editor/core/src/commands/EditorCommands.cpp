#include "CommandSupport.hpp"

#include <illusion/editor/core/JsonSchema.hpp>
#include <illusion/text/FuzzyMatcher.hpp>

namespace illusion::editor::detail
{
namespace
{

CommandSummary summarize(EditorContext& editor, const CommandDescriptor& command)
{
    CommandSummary summary;
    summary.name = std::string(command.info.name);
    summary.version = command.info.version;
    summary.title = localized(editor.localization(), command.info.title);
    summary.summary = std::string(command.info.agentSummary);
    summary.category = std::string(command.info.category);
    summary.permission = std::string(permissionClassName(command.info.permission));
    const CommandFlags& flags = command.info.flags;
    const std::pair<bool, const char*> names[] = {
        {flags.readOnly, "read_only"},    {flags.destructive, "destructive"},
        {flags.idempotent, "idempotent"}, {flags.undoable, "undoable"},
        {flags.async, "async"},           {flags.needsGpu, "needs_gpu"},
    };
    for (const auto& [set, name] : names)
    {
        if (set)
        {
            summary.flags.emplace_back(name);
        }
    }
    return summary;
}

JsonText schemaText(JsonMutValue schema)
{
    JsonText text;
    if (!schema.write(text.text, JsonWriteOptions{.pretty = false}))
    {
        text.text = "{}";
    }
    return text;
}

Expected<ListCommandsResult, CommandError> listCommands(CommandContext& context,
                                                        const ListCommandsParams& params)
{
    ListCommandsResult result;
    std::optional<text::FuzzyMatcher> matcher;
    if (params.query && !params.query->empty())
    {
        matcher.emplace(*params.query);
    }
    for (const CommandDescriptor* command : context.editor().commands().commands())
    {
        if (params.category && command->info.category != *params.category)
        {
            continue;
        }
        if (matcher && !matcher->score(command->info.name))
        {
            continue;
        }
        result.commands.push_back(summarize(context.editor(), *command));
    }
    return result;
}

Expected<DescribeResult, CommandError> describe(CommandContext& context, const DescribeParams& params)
{
    const CommandDescriptor* command = context.editor().commands().find(params.command);
    if (command == nullptr)
    {
        CommandError error =
            CommandError::make(ErrorKind::NotFound, "there is no command '" + params.command + "'");
        error.errors.push_back(ValueError{"/command", "is not a command"});
        error.suggestions = context.editor().commands().suggest(params.command);
        return unexpected(std::move(error));
    }
    DescribeResult result;
    result.command = summarize(context.editor(), *command);
    result.description = localized(context.editor().localization(), command->info.description);
    result.icon = std::string(command->info.icon);
    result.mode = std::string(commandModeName(command->info.mode));
    result.shortcut = std::string(command->info.defaultShortcut);
    JsonMutDocument doc;
    result.params = schemaText(writeCommandParamsSchema(doc, *command));
    result.result = schemaText(writeCommandResultSchema(doc, *command));
    return result;
}

} // namespace

void registerEditorCommands(CommandRegistry& registry)
{
    registry.add<&listCommands>({
        .name = "editor.list_commands",
        .title = strings::kCommandEditorListCommandsTitle,
        .description = strings::kCommandEditorListCommandsDescription,
        .agentSummary = "Lists every command with its title, summary, category, permission class and flags. "
                        "Example: {\"query\": \"create\"}.",
        .category = "Editor",
        .icon = "list",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&describe>({
        .name = "editor.describe",
        .title = strings::kCommandEditorDescribeTitle,
        .description = strings::kCommandEditorDescribeDescription,
        .agentSummary = "Returns a command's description and the JSON Schemas (2020-12) of its params and "
                        "result. Example: {\"command\": \"entity.create\"}.",
        .category = "Editor",
        .icon = "help",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail

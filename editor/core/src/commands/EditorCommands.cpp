#include "CommandSupport.hpp"

#include <illusion/editor/core/JsonSchema.hpp>
#include <illusion/text/FuzzyMatcher.hpp>

#include <algorithm>
#include <charconv>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

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
        {flags.readOnly, "read_only"},
        {flags.destructive, "destructive"},
        {flags.idempotent, "idempotent"},
        {flags.undoable, "undoable"},
        {flags.async, "async"},
        {flags.needsGpu, "needs_gpu"},
        {flags.outsideTransactions, "outside_transactions"},
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

constexpr usize kMaxBatchSteps = 1000;
constexpr usize kMaxLabel = 200;
constexpr u32 kMaxWaitFrames = 100000;

// Results of earlier steps by their "as" names.
using NamedResults = std::vector<std::pair<std::string, JsonDocument>>;

bool isStepName(std::string_view name) noexcept
{
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::string pointerToken(std::string_view key)
{
    std::string token;
    for (const char c : key)
    {
        token += c == '~'   ? std::string_view("~0")
                 : c == '/' ? std::string_view("~1")
                            : std::string_view(&c, 1);
    }
    return token;
}

// The value that the string @p text names when it is a reference "$name.path.to.field" to the result of an
// earlier step; std::nullopt when it is not one (other strings, "$$..." and "$selection").
Expected<std::optional<JsonValue>, CommandError>
lookupReference(std::string_view text, const NamedResults& named, const std::string& pointer)
{
    if (!text.starts_with('$') || text.starts_with("$$"))
    {
        return std::optional<JsonValue>{};
    }
    const std::string_view body = text.substr(1);
    const usize dot = body.find('.');
    const std::string_view name = body.substr(0, dot);
    const auto found =
        std::find_if(named.begin(), named.end(), [&](const auto& entry) { return entry.first == name; });
    if (found == named.end())
    {
        if (dot == std::string_view::npos)
        {
            return std::optional<JsonValue>{};
        }
        CommandError error = CommandError::invalid(pointer, "refers to '" + std::string(name) +
                                                                "', which no earlier step names with \"as\"");
        std::vector<std::string_view> names;
        for (const auto& entry : named)
        {
            names.push_back(entry.first);
        }
        error.suggestions = closestMatches(name, names);
        return unexpected(std::move(error));
    }
    JsonValue value = found->second.root();
    std::string_view rest = dot == std::string_view::npos ? std::string_view() : body.substr(dot + 1);
    while (!rest.empty())
    {
        const usize next = rest.find('.');
        const std::string_view segment = rest.substr(0, next);
        rest = next == std::string_view::npos ? std::string_view() : rest.substr(next + 1);
        JsonValue child;
        if (value.isObject())
        {
            child = value.find(segment);
        }
        else if (value.isArray())
        {
            usize index = 0;
            const auto [end, code] = std::from_chars(segment.data(), segment.data() + segment.size(), index);
            if (code == std::errc{} && end == segment.data() + segment.size() && !segment.empty() &&
                index < value.size())
            {
                child = value[index];
            }
        }
        if (child.isAbsent())
        {
            return unexpected(CommandError::invalid(
                pointer, "refers to '" + std::string(text) + "', but the result of step '" +
                             std::string(name) + "' has no '" + std::string(segment) + "'"));
        }
        value = child;
    }
    return std::optional<JsonValue>(value);
}

// A copy of @p value in @p doc with every reference to an earlier result replaced by that value.
Expected<JsonMutValue, CommandError> substitute(JsonMutDocument& doc, JsonValue value,
                                                const NamedResults& named, const std::string& pointer)
{
    if (value.isObject())
    {
        JsonMutValue object = doc.makeObject();
        for (const JsonMember member : value.members())
        {
            Expected<JsonMutValue, CommandError> child =
                substitute(doc, member.value, named, pointer + "/" + pointerToken(member.key));
            if (!child)
            {
                return child;
            }
            object.add(member.key, *child);
        }
        return object;
    }
    if (value.isArray())
    {
        JsonMutValue array = doc.makeArray();
        usize index = 0;
        for (const JsonValue element : value.elements())
        {
            Expected<JsonMutValue, CommandError> child =
                substitute(doc, element, named, pointer + "/" + std::to_string(index++));
            if (!child)
            {
                return child;
            }
            array.push(*child);
        }
        return array;
    }
    if (const std::optional<std::string_view> text = value.asString())
    {
        if (text->starts_with("$$"))
        {
            return doc.makeString(text->substr(1));
        }
        Expected<std::optional<JsonValue>, CommandError> reference = lookupReference(*text, named, pointer);
        if (!reference)
        {
            return unexpected(std::move(reference).error());
        }
        if (reference->has_value())
        {
            return doc.copy(**reference);
        }
    }
    return doc.copy(value);
}

// The error of a failed step as JSON text: {code, message, data}.
JsonText errorText(const CommandError& error)
{
    JsonMutDocument doc;
    doc.setRoot(writeCommandError(doc, error));
    JsonText text;
    if (!doc.write(text.text, JsonWriteOptions{.pretty = false}))
    {
        text.text = "{}";
    }
    return text;
}

// Checks the shape of every step before any runs and parses their params.
Expected<std::vector<JsonDocument>, CommandError> prepareSteps(EditorContext& editor,
                                                               const BatchParams& params)
{
    if (params.steps.empty() || params.steps.size() > kMaxBatchSteps)
    {
        return unexpected(
            CommandError::invalid("/steps", "must have 1 to " + std::to_string(kMaxBatchSteps) + " steps"));
    }
    std::vector<JsonDocument> stepParams;
    std::vector<std::string_view> names;
    for (usize i = 0; i < params.steps.size(); ++i)
    {
        const BatchStep& step = params.steps[i];
        const std::string pointer = "/steps/" + std::to_string(i);
        if (step.as)
        {
            if (!isStepName(*step.as) || *step.as == "selection")
            {
                return unexpected(CommandError::invalid(
                    pointer + "/as", "must be 1 to 64 letters, digits or '_', and not \"selection\""));
            }
            if (std::find(names.begin(), names.end(), *step.as) != names.end())
            {
                return unexpected(CommandError::invalid(pointer + "/as", "is the name of an earlier step"));
            }
            names.push_back(*step.as);
        }
        const CommandDescriptor* command = editor.commands().find(step.method);
        if (command == nullptr)
        {
            CommandError error =
                CommandError::make(ErrorKind::NotFound, "there is no command '" + step.method + "'");
            error.errors.push_back(ValueError{pointer + "/method", "is not a command"});
            error.suggestions = editor.commands().suggest(step.method);
            return unexpected(std::move(error));
        }
        if (command->info.flags.async || step.method == "editor.batch" ||
            step.method.starts_with("transaction."))
        {
            return unexpected(CommandError::invalid(pointer + "/method", "cannot run in a batch"));
        }
        if (!step.params)
        {
            stepParams.emplace_back(std::move(JsonDocument::parse("{}").value()));
            continue;
        }
        Expected<JsonDocument, CommandError> parsed = parseParam(*step.params, pointer + "/params");
        if (!parsed)
        {
            return unexpected(std::move(parsed).error());
        }
        if (!parsed->root().isObject())
        {
            return unexpected(CommandError::invalid(pointer + "/params", "must be an object"));
        }
        stepParams.push_back(std::move(parsed).value());
    }
    return stepParams;
}

Expected<BatchResult, CommandError> batch(CommandContext& context, const BatchParams& params)
{
    EditorContext& editor = context.editor();
    const bool atomic = params.atomic.value_or(true);
    const std::string label = params.label.value_or("Batch");
    if (label.empty() || label.size() > kMaxLabel)
    {
        return unexpected(CommandError::invalid("/label", "must have 1 to 200 bytes"));
    }
    Expected<std::vector<JsonDocument>, CommandError> prepared = prepareSteps(editor, params);
    if (!prepared)
    {
        return unexpected(std::move(prepared).error());
    }

    // The batch is one transaction of its own, or a part of the caller's open one.
    const CommandOptions& options = context.options();
    const bool own = editor.transaction() == nullptr;
    if (own)
    {
        if (Expected<void, CommandError> begun = editor.beginTransaction(label, options); !begun)
        {
            return unexpected(std::move(begun).error());
        }
    }
    else if (const TransactionInfo* open = editor.transaction();
             open != nullptr && open->caller != options.caller)
    {
        CommandError error =
            CommandError::make(ErrorKind::Busy, "transaction '" + open->label + "' of " + open->origin +
                                                    " is open; try again after it ends");
        error.retryable = true;
        return unexpected(std::move(error));
    }
    const TransactionInfo* current = editor.transaction();
    const usize savepoint = current != nullptr ? current->operations : 0;
    const auto revert = [&]() -> Expected<void, CommandError> {
        return own ? editor.cancelTransaction(options) : editor.rollbackTransactionTo(savepoint);
    };

    BatchResult result;
    NamedResults named;
    u64 operations = 0;
    for (usize i = 0; i < params.steps.size(); ++i)
    {
        const BatchStep& step = params.steps[i];
        const std::string pointer = "/steps/" + std::to_string(i);
        const CommandDescriptor& command = *editor.commands().find(step.method);
        Expected<std::string, CommandError> ran = [&]() -> Expected<std::string, CommandError> {
            JsonMutDocument doc;
            Expected<JsonMutValue, CommandError> substituted =
                substitute(doc, (*prepared)[i].root(), named, pointer + "/params");
            if (!substituted)
            {
                return unexpected(std::move(substituted).error());
            }
            doc.setRoot(*substituted);
            std::string text;
            if (!doc.write(text, JsonWriteOptions{.pretty = false}))
            {
                return unexpected(
                    CommandError::make(ErrorKind::Internal, "cannot write the params of a step"));
            }
            const JsonDocument stepParams = std::move(JsonDocument::parse(text).value());
            CommandOptions stepOptions;
            stepOptions.origin = options.origin;
            stepOptions.caller = options.caller;
            stepOptions.gate = options.gate;
            // A dry batch reverts its undoable steps; the others only validate.
            stepOptions.dryRun =
                context.dryRun() && !command.info.flags.undoable && !command.info.flags.readOnly;
            Expected<std::string, CommandError> out =
                editor.executeStep(step.method, stepParams.root(), stepOptions);
            if (!out)
            {
                CommandError error = withPointerPrefix(std::move(out).error(), pointer + "/params");
                error.message =
                    "step " + std::to_string(i) + " (" + step.method + ") failed: " + error.message;
                return unexpected(std::move(error));
            }
            return out;
        }();
        if (!ran)
        {
            if (atomic)
            {
                if (Expected<void, CommandError> reverted = revert(); !reverted)
                {
                    return unexpected(std::move(reverted).error());
                }
                return unexpected(std::move(ran).error());
            }
            result.steps.push_back(BatchStepResult{std::nullopt, errorText(ran.error())});
            ++result.failed;
            continue;
        }
        JsonDocument parsed = std::move(JsonDocument::parse(*ran).value());
        operations += parsed.root().pointer("/_editor/operations").asU64().value_or(0);
        if (step.as)
        {
            named.emplace_back(*step.as, std::move(parsed));
        }
        result.steps.push_back(BatchStepResult{JsonText{std::move(ran).value()}, std::nullopt});
    }

    context.report().operations = static_cast<u32>(operations);
    if (context.dryRun())
    {
        if (Expected<void, CommandError> reverted = revert(); !reverted)
        {
            return unexpected(std::move(reverted).error());
        }
    }
    else if (own)
    {
        Expected<u64, CommandError> committed = editor.commitTransaction(options);
        if (!committed)
        {
            return unexpected(std::move(committed).error());
        }
        context.report().transaction = *committed;
    }
    return result;
}

// Counts frames until the requested number has passed.
class WaitTask final : public CommandTask<WaitResult>
{
public:
    explicit WaitTask(u32 frames) noexcept
        : m_frames(frames)
    {
    }

    Expected<std::optional<WaitResult>, CommandError> update(CommandContext&,
                                                             CommandProgress& progress) override
    {
        ++m_waited;
        progress.done = static_cast<f64>(m_waited);
        progress.total = static_cast<f64>(m_frames);
        if (m_waited < m_frames)
        {
            return std::optional<WaitResult>{};
        }
        return std::optional<WaitResult>(WaitResult{m_waited});
    }

private:
    u32 m_frames;
    u32 m_waited = 0;
};

Expected<std::unique_ptr<CommandTask<WaitResult>>, CommandError> wait(CommandContext&,
                                                                      const WaitParams& params)
{
    if (params.frames == 0 || params.frames > kMaxWaitFrames)
    {
        return unexpected(
            CommandError::invalid("/frames", "must be between 1 and " + std::to_string(kMaxWaitFrames)));
    }
    return std::unique_ptr<CommandTask<WaitResult>>(std::make_unique<WaitTask>(params.frames));
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
    registry.add<&batch>({
        .name = "editor.batch",
        .title = strings::kCommandEditorBatchTitle,
        .description = strings::kCommandEditorBatchDescription,
        .agentSummary =
            "Runs commands in order as one undo step. A step named with \"as\" can be referred to by later "
            "steps: the string \"$name.path.to.field\" in their params becomes that value of its result "
            "(\"$$\" escapes a literal \"$\"). Atomic batches (the default) revert everything when a step "
            "fails. Example: {\"label\": \"Board\", \"steps\": [{\"as\": \"board\", \"method\": "
            "\"entity.create\", "
            "\"params\": {\"name\": \"Board\"}}, {\"method\": \"entity.create\", \"params\": {\"name\": "
            "\"Cell\", "
            "\"parent\": \"$board.entity.id\"}}]}.",
        .category = "Editor",
        .icon = "playlist_play",
        .flags = {},
        .permission = PermissionClass::Edit,
    });
    registry.addAsync<&wait>({
        .name = "editor.wait",
        .title = strings::kCommandEditorWaitTitle,
        .description = strings::kCommandEditorWaitDescription,
        .agentSummary =
            "Asynchronous: answers after the given number of editor frames, reporting rpc.progress "
            "each frame; rpc.cancel ends it early. Example: {\"frames\": 60}.",
        .category = "Editor",
        .icon = "hourglass_empty",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
}

} // namespace illusion::editor::detail

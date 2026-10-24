#include "PathText.hpp"
#include "ReflectedObject.hpp"

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <new>
#include <random>
#include <system_error>
#include <utility>

namespace illusion::editor
{
namespace
{

constexpr std::string_view kReservedParams[] = {"dryRun", "mergeKey", "expectRevision", "clientToken"};

u64 sessionSeed(u64 seed)
{
    if (seed != 0)
    {
        return seed ^ 0x5e55'10b5'eed0'0001ull;
    }
    std::random_device device;
    return (static_cast<u64>(device()) << 32) ^ static_cast<u64>(device());
}

std::string hex16(u64 value)
{
    const auto text = formatGuid(EntityGuid{value});
    return std::string(text.data(), text.size());
}

void reflectOnce()
{
    static const bool reflected = [] {
        reflectCoreComponents();
        reflectAssetStructs(ComponentRegistry::instance());
        reflectCoreCommandTypes(ComponentRegistry::instance().structs());
        return true;
    }();
    (void)reflected;
}

using detail::ReflectedObject;

std::string compact(const JsonMutDocument& doc)
{
    std::string out;
    if (!doc.write(out, JsonWriteOptions{.pretty = false}))
    {
        out.clear();
    }
    return out;
}

std::string fieldName(std::string_view pointer)
{
    return std::string(pointer.substr(1, pointer.find('/', 1) == std::string_view::npos
                                             ? std::string_view::npos
                                             : pointer.find('/', 1) - 1));
}

// The result of a command as compact JSON with its `_editor` member.
Expected<std::string, CommandError> writeResult(const CommandDescriptor& descriptor, const void* result,
                                                const CommandReport& report)
{
    JsonMutDocument doc;
    JsonMutValue root;
    if (descriptor.result != nullptr)
    {
        Expected<JsonMutValue, ValueError> written = writeValue(doc, *descriptor.result, result);
        if (!written)
        {
            return unexpected(CommandError::make(ErrorKind::Internal,
                                                 "cannot write the result: " + written.error().message));
        }
        root = *written;
    }
    else
    {
        root = doc.makeObject();
    }
    JsonMutValue editor = root.addObject("_editor");
    editor.add("revision", doc.makeU64(report.revision));
    editor.add("transaction", doc.makeU64(report.transaction));
    editor.add("dryRun", doc.makeBool(report.dryRun));
    editor.add("operations", doc.makeU64(report.operations));
    JsonMutValue warnings = editor.addArray("warnings");
    for (const std::string& warning : report.warnings)
    {
        warnings.push(doc.makeString(warning));
    }
    doc.setRoot(root);
    return compact(doc);
}

CommandError notInCommand()
{
    return CommandError::make(ErrorKind::Busy, "commands cannot run inside other commands");
}

} // namespace

struct RunningCommand::State
{
    EditorContext* editor = nullptr;
    const CommandDescriptor* descriptor = nullptr;
    std::unique_ptr<CommandTaskBase> task; // nullptr for a dry run
    std::optional<ReflectedObject> result;
    NoResult noResult;
    // Owned copies of the strings the options point to.
    std::string origin;
    std::string mergeKey;
    std::string label;
    std::string clientToken;
    CommandOptions options;
    CommandProgress progress;
    CommandReport report;
    u32 frames = 0;
    bool finished = false;
};

RunningCommand::RunningCommand(std::unique_ptr<State> state) noexcept
    : m_state(std::move(state))
{
}

RunningCommand::~RunningCommand() = default;

const CommandProgress& RunningCommand::progress() const noexcept
{
    return m_state->progress;
}

const CommandDescriptor& RunningCommand::command() const noexcept
{
    return *m_state->descriptor;
}

u32 RunningCommand::frames() const noexcept
{
    return m_state->frames;
}

Expected<std::optional<std::string>, CommandError> RunningCommand::update()
{
    ILL_PROFILE_SCOPE();
    State& state = *m_state;
    ILL_ASSERT(!state.finished, "{} already finished", state.descriptor->info.name);
    EditorContext& editor = *state.editor;
    if (editor.m_running)
    {
        return unexpected(notInCommand());
    }
    void* result = state.result ? state.result->data() : &state.noResult;
    bool done = true;
    if (state.task)
    {
        CommandContext context(editor, state.options, state.report);
        editor.m_running = true;
        Expected<bool, CommandError> advanced = state.task->advance(context, state.progress, result);
        editor.m_running = false;
        ++state.frames;
        if (!advanced)
        {
            state.finished = true;
            state.task.reset();
            return unexpected(std::move(advanced).error());
        }
        done = *advanced;
    }
    else
    {
        state.report.dryRun = true;
    }
    if (!done)
    {
        return std::optional<std::string>{};
    }
    state.finished = true;
    state.task.reset();
    state.report.revision = editor.revision();
    Expected<std::string, CommandError> written = writeResult(*state.descriptor, result, state.report);
    if (!written)
    {
        return unexpected(std::move(written).error());
    }
    return std::optional<std::string>(std::move(written).value());
}

WorldEditor& CommandContext::edit() noexcept
{
    return m_editor->editor();
}

World& CommandContext::world() noexcept
{
    return m_editor->world();
}

Expected<Entity, CommandError> CommandContext::entity(std::string_view reference, std::string_view pointer)
{
    return m_editor->entities().resolve(reference, m_editor->selection(), pointer);
}

Expected<void, CommandError> CommandContext::entities(std::span<const std::string> references,
                                                      std::string_view pointer, std::vector<Entity>& out)
{
    return m_editor->entities().resolveAll(references, m_editor->selection(), pointer, out);
}

EntitySummary CommandContext::summary(Entity entity)
{
    return m_editor->entities().summary(entity);
}

Expected<ComponentId, CommandError> CommandContext::componentType(std::string_view name,
                                                                  std::string_view pointer) const
{
    const ComponentRegistry& registry = m_editor->world().registry();
    const ComponentId type = registry.findByStableName(name);
    if (type != kInvalidComponentId && registry.reflection(type) != nullptr)
    {
        return type;
    }
    std::vector<std::string_view> names;
    for (ComponentId id = 0; id < registry.count(); ++id)
    {
        if (const TypeReflection* reflection = registry.reflection(id))
        {
            names.push_back(reflection->name);
        }
    }
    CommandError error =
        CommandError::make(ErrorKind::NotFound, "there is no Component type '" + std::string(name) + "'");
    error.errors.push_back(ValueError{std::string(pointer), "is not a Component type"});
    error.suggestions = closestMatches(name, names);
    return unexpected(std::move(error));
}

EditorContext::EditorContext(const EditorContextOptions& options)
    : m_options(options)
    , m_guids(options.seed != 0 ? GuidGenerator(options.seed) : GuidGenerator())
    , m_session(hex16(sessionSeed(options.seed)))
    , m_index(m_world)
    , m_editor(m_world, m_index, m_guids, m_changes)
    , m_history(options.undoBudget)
{
    reflectOnce();
    if (options.logCapacity > 0)
    {
        m_log = std::make_shared<LogRingSink>(options.logCapacity);
        Logger::instance().addSink(m_log);
    }
    m_localization.addTable(strings::kTable);
    if (options.registerCoreCommands)
    {
        registerCoreCommands(m_commands);
    }
    m_commandsRevision = m_commands.revision();
    newScene();
}

EditorContext::~EditorContext()
{
    if (m_project)
    {
        m_journal.closeClean();
        // Blobs of this session are only needed by its undo history and journal.
        std::error_code code;
        std::filesystem::remove_all(m_project->stateDirectory() / "trash" / utf8Path(m_session), code);
        m_project->unmount(m_files);
    }
    if (m_log)
    {
        Logger::instance().removeSink(m_log.get());
    }
}

Expected<void, CommandError> EditorContext::openProject(const std::filesystem::path& root)
{
    Expected<Project, std::string> project = Project::open(root);
    if (!project)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, std::move(project).error()));
    }
    if (m_project)
    {
        m_journal.closeClean();
        m_project->unmount(m_files);
        m_project.reset();
        m_editor.setFiles(nullptr, {}, {});
    }
    if (const FileError error = project->mount(m_files); error != FileError::None)
    {
        return unexpected(CommandError::make(
            ErrorKind::IoError, "cannot mount the project (" + std::string(fileErrorName(error)) + ")"));
    }
    m_project = std::move(project).value();
    m_references.clear();
    m_editor.setFiles(&m_files, m_project->stateDirectory() / "trash", m_session);
    if (m_options.crashJournal)
    {
        // A fixed seed gives every run the same session id: never truncate the journal a crashed run left.
        const std::filesystem::path journals = m_project->stateDirectory() / "journal";
        std::filesystem::path journal = journals / utf8Path(m_session + ".jsonl");
        std::error_code code;
        for (u32 attempt = 2; std::filesystem::exists(journal, code); ++attempt)
        {
            journal = journals / utf8Path(m_session + "-" + std::to_string(attempt) + ".jsonl");
        }
        m_journal.open(journal, m_session);
    }
    const std::string& startup = m_project->manifest().startupScene;
    if (!startup.empty() && m_files.exists(startup))
    {
        std::vector<std::string> warnings;
        if (Expected<void, CommandError> opened = openScene(startup, &warnings); !opened)
        {
            ILL_LOG_WARN("Editor", "Cannot open the startup scene {}: {}", startup, opened.error().message);
            newScene();
        }
    }
    else
    {
        newScene();
    }
    return {};
}

void EditorContext::sceneReplaced(std::string_view path)
{
    // The operations of an open transaction belong to the scene that is gone.
    m_transaction.reset();
    m_transactionOperations.clear();
    m_transactionSelection.clear();
    m_index.rebuild();
    m_index.updateTransforms(true);
    m_selection.clear();
    m_history.clear();
    m_changes.reset();
    m_document.markSaved(std::string(path), m_history.stateId());
    m_autosaveTimer = 0.0;
    m_events.post("scene.opened", "{\"revision\":" + std::to_string(revision()) + "}");
}

void EditorContext::newScene()
{
    m_world.clear();
    m_editor.resetSettings({});
    sceneReplaced("");
    m_journal.writeBase("");
}

Expected<bool, CommandError> EditorContext::loadSceneText(std::string_view json,
                                                          std::vector<std::string>* warnings)
{
    SceneLoadOptions options;
    options.guids = &m_guids;
    options.assetExists = [this](std::string_view path) {
        return path.starts_with("builtin:") || m_files.exists(path);
    };
    {
        // Load into a scratch World first, so that a broken file leaves the open scene as it is.
        World scratch;
        const SceneLoadResult trial = loadScene(scratch, json, options);
        if (!trial.ok())
        {
            return unexpected(
                CommandError::make(ErrorKind::ValidationFailed, "cannot read the scene: " + trial.error));
        }
    }
    m_world.clear();
    SceneLoadResult result = loadScene(m_world, json, options);
    m_editor.resetSettings(std::move(result.preservedSettings));
    if (warnings != nullptr)
    {
        warnings->insert(warnings->end(), result.warnings.begin(), result.warnings.end());
        for (const std::string& missing : result.missingAssets)
        {
            warnings->push_back("missing asset " + missing);
        }
    }
    return result.version < 2; // version 1 files have no ids: their entities got new ones
}

Expected<void, CommandError> EditorContext::openScene(std::string_view path,
                                                      std::vector<std::string>* warnings)
{
    ILL_PROFILE_SCOPE();
    std::string json;
    if (const FileError error = m_files.readText(path, json); error != FileError::None)
    {
        return unexpected(
            error == FileError::NotFound
                ? CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist")
                : CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                             std::string(fileErrorName(error)) + ")"));
    }
    Expected<bool, CommandError> loaded = loadSceneText(json, warnings);
    if (!loaded)
    {
        return unexpected(std::move(loaded).error());
    }
    const u32 assigned = assignMissingGuids(m_world, m_guids);
    sceneReplaced(path);
    if (*loaded || assigned > 0)
    {
        if (warnings != nullptr)
        {
            warnings->push_back("entities got new ids; save the scene to keep them");
        }
        Expected<std::string, CommandError> content = sceneJson();
        m_journal.writeBase(path, content ? std::string_view(*content) : std::string_view());
    }
    else
    {
        m_journal.writeBase(path);
    }
    return {};
}

Expected<std::string, CommandError> EditorContext::sceneJson() const
{
    SceneSaveOptions options;
    options.preservedSettings = m_editor.settings();
    const SceneSaveResult saved = illusion::saveScene(m_world, options);
    if (!saved.ok())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot write the scene: " + saved.error));
    }
    return saved.json;
}

Expected<u32, CommandError> EditorContext::saveScene(std::string_view path)
{
    ILL_PROFILE_SCOPE();
    if (m_transaction)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the scene cannot be saved while a transaction is open"));
    }
    const std::string target(path.empty() ? std::string_view(m_document.path()) : path);
    if (target.empty())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the scene has no file yet; use scene.save_as"));
    }
    assignMissingGuids(m_world, m_guids);
    SceneSaveOptions options;
    options.preservedSettings = m_editor.settings();
    const SceneSaveResult saved = illusion::saveScene(m_world, options);
    if (!saved.ok())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot write the scene: " + saved.error));
    }
    if (const FileError error = m_files.writeText(target, saved.json); error != FileError::None)
    {
        return unexpected(error == FileError::ReadOnly || error == FileError::InvalidPath ||
                                  error == FileError::UnknownMount
                              ? CommandError::invalid("/path", "is not a writable project path (" +
                                                                   std::string(fileErrorName(error)) + ")")
                              : CommandError::make(ErrorKind::IoError, "cannot write '" + target + "' (" +
                                                                           std::string(fileErrorName(error)) +
                                                                           ")"));
    }
    m_document.markSaved(target, m_history.stateId());
    m_history.seal();
    m_journal.writeSaved(target);
    m_events.post("scene.saved", "{\"revision\":" + std::to_string(revision()) + "}");
    return saved.entities;
}

const ReferenceIndex& EditorContext::references()
{
    if (m_project)
    {
        m_references.refresh(m_files);
    }
    return m_references;
}

void EditorContext::clearLog()
{
    m_logCleared = m_log ? m_log->latestSequence() : 0;
}

CommandError EditorContext::unknownCommand(std::string_view name) const
{
    CommandError error =
        CommandError::make(ErrorKind::NotFound, "there is no command '" + std::string(name) + "'");
    error.suggestions = m_commands.suggest(name);
    return error;
}

Expected<void, CommandError> EditorContext::readParams(const CommandDescriptor& descriptor, JsonValue json,
                                                       void* params, CommandOptions& options) const
{
    if (json.isAbsent() || json.isNull())
    {
        static const JsonDocument empty = std::move(JsonDocument::parse("{}").value());
        json = empty.root();
    }
    if (!json.isObject())
    {
        return unexpected(CommandError::invalid("", "params must be an object"));
    }
    std::vector<ValueError> errors;
    std::vector<std::string> suggestions;
    if (const JsonValue dryRun = json.find("dryRun"); !dryRun.isAbsent())
    {
        if (!dryRun.isBool())
        {
            errors.push_back(ValueError{"/dryRun", "must be a boolean"});
        }
        options.dryRun = dryRun.asBool().value_or(false);
    }
    if (const JsonValue mergeKey = json.find("mergeKey"); !mergeKey.isAbsent())
    {
        if (!mergeKey.isString())
        {
            errors.push_back(ValueError{"/mergeKey", "must be a string"});
        }
        options.mergeKey = mergeKey.asString().value_or("");
    }
    if (const JsonValue token = json.find("clientToken"); !token.isAbsent())
    {
        if (!token.isString())
        {
            errors.push_back(ValueError{"/clientToken", "must be a string"});
        }
        options.clientToken = token.asString().value_or("");
    }
    if (const JsonValue expect = json.find("expectRevision"); !expect.isAbsent())
    {
        std::optional<u64> revision = expect.asU64();
        if (const std::optional<std::string_view> text = expect.asString())
        {
            u64 parsed = 0;
            const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), parsed);
            if (error == std::errc{} && end == text->data() + text->size() && !text->empty())
            {
                revision = parsed;
            }
        }
        if (!revision)
        {
            errors.push_back(ValueError{"/expectRevision", "must be a revision number"});
        }
        options.expectRevision = revision;
    }
    std::vector<std::string_view> fieldNames;
    if (descriptor.params != nullptr)
    {
        for (const FieldInfo& field : descriptor.params->fields)
        {
            fieldNames.push_back(field.name);
            if (field.type != FieldType::Optional && json.find(field.name).isAbsent())
            {
                errors.push_back(ValueError{"/" + std::string(field.name), "is required"});
            }
        }
        const ValueReadResult read =
            readValue(*descriptor.params, params, json, {.mode = JsonReadMode::Replace});
        errors.insert(errors.end(), read.errors.begin(), read.errors.end());
        for (const std::string& unknown : read.unknownKeys)
        {
            const std::string name = fieldName(unknown);
            if (std::find(std::begin(kReservedParams), std::end(kReservedParams), name) ==
                std::end(kReservedParams))
            {
                errors.push_back(ValueError{unknown, "is not a parameter"});
                if (suggestions.empty())
                {
                    suggestions = closestMatches(name, fieldNames);
                }
            }
        }
    }
    else
    {
        for (const JsonMember member : json.members())
        {
            if (std::find(std::begin(kReservedParams), std::end(kReservedParams), member.key) ==
                std::end(kReservedParams))
            {
                errors.push_back(ValueError{"/" + std::string(member.key), "is not a parameter"});
            }
        }
    }
    if (errors.empty())
    {
        return {};
    }
    CommandError error = CommandError::make(ErrorKind::ValidationFailed,
                                            "'" + errors.front().pointer + "' " + errors.front().message);
    if (errors.size() > 1)
    {
        error.message += " (and " + std::to_string(errors.size() - 1) + " more problems)";
    }
    error.errors = std::move(errors);
    error.suggestions = std::move(suggestions);
    return unexpected(std::move(error));
}

Expected<std::string, CommandError> EditorContext::executeJson(std::string_view name, JsonValue params,
                                                               const CommandOptions& options)
{
    const CommandDescriptor* descriptor = m_commands.find(name);
    if (descriptor == nullptr)
    {
        return unexpected(unknownCommand(name));
    }
    return executeJsonAs(*descriptor, params, options);
}

Expected<std::string, CommandError> EditorContext::executeStep(std::string_view name, JsonValue params,
                                                               const CommandOptions& options)
{
    const bool nested = m_nested;
    m_nested = m_running;
    Expected<std::string, CommandError> result = executeJson(name, params, options);
    m_nested = nested;
    return result;
}

Expected<std::string, CommandError> EditorContext::executeJsonAs(const CommandDescriptor& command,
                                                                 JsonValue params,
                                                                 const CommandOptions& options)
{
    const CommandDescriptor* descriptor = &command;
    CommandOptions effective = options;
    // Commands without params or a result take NoParams and NoResult objects.
    NoParams noParams;
    NoResult noResult;
    std::optional<ReflectedObject> paramObject;
    std::optional<ReflectedObject> resultObject;
    void* paramData = &noParams;
    void* resultData = &noResult;
    if (descriptor->params != nullptr)
    {
        paramData = paramObject.emplace(descriptor->params).data();
    }
    if (descriptor->result != nullptr)
    {
        resultData = resultObject.emplace(descriptor->result).data();
    }
    if (Expected<void, CommandError> read = readParams(*descriptor, params, paramData, effective); !read)
    {
        return unexpected(std::move(read).error());
    }
    CommandReport report;
    if (Expected<void, CommandError> ran = run(*descriptor, paramData, resultData, effective, report); !ran)
    {
        return unexpected(std::move(ran).error());
    }
    return writeResult(*descriptor, resultData, report);
}

Expected<std::unique_ptr<RunningCommand>, CommandError>
EditorContext::startJson(std::string_view name, JsonValue params, const CommandOptions& options)
{
    ILL_PROFILE_SCOPE();
    const CommandDescriptor* descriptor = m_commands.find(name);
    if (descriptor == nullptr)
    {
        return unexpected(unknownCommand(name));
    }
    if (!descriptor->info.flags.async)
    {
        return unexpected(CommandError::make(
            ErrorKind::Unsupported, std::string(name) + " is not asynchronous; run it with executeJson()"));
    }
    if (m_running && !m_nested)
    {
        return unexpected(notInCommand());
    }
    auto state = std::make_unique<RunningCommand::State>();
    state->editor = this;
    state->descriptor = descriptor;
    CommandOptions effective = options;
    NoParams noParams;
    std::optional<ReflectedObject> paramObject;
    void* paramData = &noParams;
    if (descriptor->params != nullptr)
    {
        paramData = paramObject.emplace(descriptor->params).data();
    }
    if (Expected<void, CommandError> read = readParams(*descriptor, params, paramData, effective); !read)
    {
        return unexpected(std::move(read).error());
    }
    if (Expected<void, CommandError> admitted = admit(*descriptor, effective); !admitted)
    {
        return unexpected(std::move(admitted).error());
    }
    state->origin = std::string(effective.origin);
    state->mergeKey = std::string(effective.mergeKey);
    state->label = std::string(effective.label);
    state->clientToken = std::string(effective.clientToken);
    state->options = effective;
    state->options.origin = state->origin;
    state->options.mergeKey = state->mergeKey;
    state->options.label = state->label;
    state->options.clientToken = state->clientToken;
    if (descriptor->result != nullptr)
    {
        state->result.emplace(descriptor->result);
    }
    const bool wasRunning = std::exchange(m_running, true);
    const bool nested = std::exchange(m_nested, false);
    CommandContext context(*this, state->options, state->report);
    Expected<std::unique_ptr<CommandTaskBase>, CommandError> task = descriptor->start(context, paramData);
    m_running = wasRunning;
    m_nested = nested;
    if (!task)
    {
        return unexpected(std::move(task).error());
    }
    if (!effective.dryRun)
    {
        state->task = std::move(task).value();
    }
    return std::unique_ptr<RunningCommand>(new RunningCommand(std::move(state)));
}

Expected<void, CommandError> EditorContext::admit(const CommandDescriptor& descriptor,
                                                  const CommandOptions& options) const
{
    if (options.gate != nullptr)
    {
        if (Expected<void, CommandError> allowed = options.gate->check(descriptor); !allowed)
        {
            return allowed;
        }
    }
    if (options.expectRevision && *options.expectRevision != revision())
    {
        return unexpected(
            CommandError::make(ErrorKind::Conflict, "the scene is at revision " + std::to_string(revision()) +
                                                        ", not " + std::to_string(*options.expectRevision)));
    }
    if (!m_transaction)
    {
        return {};
    }
    const bool owner = m_transaction->caller == options.caller;
    const std::string open =
        "transaction '" + m_transaction->label + "' of " + m_transaction->origin + " is open";
    if (descriptor.info.flags.outsideTransactions)
    {
        CommandError error =
            CommandError::make(owner ? ErrorKind::PreconditionFailed : ErrorKind::Busy,
                               std::string(descriptor.info.name) + " cannot run while " + open);
        error.retryable = !owner;
        return unexpected(std::move(error));
    }
    if (descriptor.info.flags.undoable && !owner)
    {
        CommandError error = CommandError::make(ErrorKind::Busy, "the " + open + "; try again after it ends");
        error.retryable = true;
        return unexpected(std::move(error));
    }
    return {};
}

Expected<void, CommandError> EditorContext::run(const CommandDescriptor& descriptor, const void* params,
                                                void* result, const CommandOptions& options,
                                                CommandReport& report)
{
    ILL_PROFILE_SCOPE();
    if (m_running && !m_nested)
    {
        return unexpected(notInCommand());
    }
    if (descriptor.info.flags.async)
    {
        return unexpected(
            CommandError::make(ErrorKind::Unsupported, std::string(descriptor.info.name) +
                                                           " is asynchronous; start it with startJson()"));
    }
    if (Expected<void, CommandError> admitted = admit(descriptor, options); !admitted)
    {
        return admitted;
    }
    const bool wasRunning = std::exchange(m_running, true);
    const bool nested = std::exchange(m_nested, false);
    CommandContext context(*this, options, report);
    const bool undoable = descriptor.info.flags.undoable;
    const u64 selectionVersion = m_selection.version();
    std::vector<EntityGuid> selectionBefore(m_selection.items().begin(), m_selection.items().end());
    if (undoable)
    {
        m_editor.begin();
    }
    Expected<void, CommandError> ran = descriptor.invoke(context, params, result);
    if (undoable)
    {
        report.operations = static_cast<u32>(m_editor.pendingOperations());
        if (!ran || options.dryRun)
        {
            m_editor.rollback();
            m_changes.discardPending();
            restoreSelection(selectionBefore);
            report.dryRun = options.dryRun && ran.hasValue();
        }
        else if (m_transaction)
        {
            addToTransaction(m_editor.commit(),
                             options.label.empty() ? descriptor.info.title.english() : options.label);
        }
        else
        {
            commitStep(m_editor.commit(),
                       options.label.empty() ? descriptor.info.title.english() : options.label, options,
                       std::move(selectionBefore), report);
        }
    }
    else
    {
        report.dryRun = options.dryRun;
    }
    if (m_selection.version() != selectionVersion)
    {
        m_events.post("selection.changed", "{\"count\":" + std::to_string(m_selection.size()) + "}");
    }
    report.revision = revision();
    m_running = wasRunning;
    m_nested = nested;
    return ran;
}

void EditorContext::addToTransaction(std::vector<Operation> operations, std::string_view label)
{
    if (operations.empty())
    {
        m_changes.discardPending();
        return;
    }
    if (!m_transaction->trial)
    {
        m_journal.writeOperations(label, operations);
    }
    m_transactionOperations.insert(m_transactionOperations.end(), std::make_move_iterator(operations.begin()),
                                   std::make_move_iterator(operations.end()));
    ++m_transaction->commands;
    m_transaction->operations = m_transactionOperations.size();
    transactionChanged(m_transaction->trial);
}

void EditorContext::transactionChanged(bool trial)
{
    if (trial)
    {
        // A trial keeps its changes pending, so the revision stays and nothing is announced.
        m_index.updateTransforms();
    }
    else
    {
        afterChange(false);
    }
}

Expected<void, CommandError> EditorContext::beginTransaction(std::string_view label,
                                                             const CommandOptions& options)
{
    if (m_transaction)
    {
        const bool owner = m_transaction->caller == options.caller;
        CommandError error =
            CommandError::make(owner ? ErrorKind::PreconditionFailed : ErrorKind::Busy,
                               "transaction '" + m_transaction->label + "' of " + m_transaction->origin +
                                   " is already open" + (owner ? "; commit or cancel it first" : ""));
        error.retryable = !owner;
        return unexpected(std::move(error));
    }
    m_transaction = TransactionInfo{
        .label = label.empty() ? std::string("Transaction") : std::string(label),
        .origin = std::string(options.origin),
        .caller = options.caller,
        .commands = 0,
        .operations = 0,
        .trial = options.dryRun,
    };
    m_transactionOperations.clear();
    m_transactionSelection.assign(m_selection.items().begin(), m_selection.items().end());
    return {};
}

Expected<void, CommandError> EditorContext::ownTransaction(const CommandOptions& options) const
{
    if (!m_transaction)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no transaction is open"));
    }
    if (m_transaction->caller != options.caller)
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed, "transaction '" + m_transaction->label +
                                                                  "' belongs to " + m_transaction->origin));
    }
    return {};
}

Expected<u64, CommandError> EditorContext::commitTransaction(const CommandOptions& options)
{
    if (Expected<void, CommandError> owned = ownTransaction(options); !owned)
    {
        return unexpected(std::move(owned).error());
    }
    if (m_transaction->trial)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "a transaction of a dry run can only be cancelled"));
    }
    TransactionInfo info = std::move(*m_transaction);
    m_transaction.reset();
    std::vector<Operation> operations = std::move(m_transactionOperations);
    m_transactionOperations.clear();
    if (operations.empty())
    {
        return u64{0};
    }
    UndoStep step;
    step.label = std::move(info.label);
    step.origin = std::move(info.origin);
    step.operations = std::move(operations);
    step.selectionBefore = std::move(m_transactionSelection);
    step.selectionAfter.assign(m_selection.items().begin(), m_selection.items().end());
    m_transactionSelection.clear();
    const u64 id = pushStep(std::move(step));
    m_events.post("history.changed", "{\"position\":" + std::to_string(m_history.position()) +
                                         ",\"steps\":" + std::to_string(m_history.steps().size()) + "}");
    return id;
}

Expected<void, CommandError> EditorContext::cancelTransaction(const CommandOptions& options)
{
    if (Expected<void, CommandError> owned = ownTransaction(options); !owned)
    {
        return owned;
    }
    // A failed revert puts everything back, and the transaction stays open.
    const bool trial = m_transaction->trial;
    if (Expected<void, CommandError> reverted =
            applyOperations(m_transactionOperations, false, trial ? "" : "Cancel " + m_transaction->label);
        !reverted)
    {
        return reverted;
    }
    const bool changed = !m_transactionOperations.empty();
    const std::vector<EntityGuid> selection = std::move(m_transactionSelection);
    m_transaction.reset();
    m_transactionOperations.clear();
    m_transactionSelection.clear();
    restoreSelection(selection);
    if (trial)
    {
        m_index.updateTransforms();
        m_changes.discardPending();
    }
    else if (changed)
    {
        afterChange(false);
    }
    return {};
}

bool EditorContext::abandonTransaction(u64 caller)
{
    if (!m_transaction || m_transaction->caller != caller)
    {
        return false;
    }
    ILL_LOG_INFO("Editor", "Cancelling transaction '{}' of {}: its client is gone", m_transaction->label,
                 m_transaction->origin);
    CommandOptions options;
    options.caller = caller;
    if (Expected<void, CommandError> cancelled = cancelTransaction(options); !cancelled)
    {
        ILL_LOG_ERROR("Editor", "Cannot cancel the transaction: {}", cancelled.error().message);
    }
    return true;
}

Expected<void, CommandError> EditorContext::rollbackTransactionTo(usize operations)
{
    if (!m_transaction)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no transaction is open"));
    }
    if (operations > m_transactionOperations.size())
    {
        return unexpected(CommandError::invalid(
            "/operations", "must be at most " + std::to_string(m_transactionOperations.size())));
    }
    if (operations == m_transactionOperations.size())
    {
        return {};
    }
    const std::span<const Operation> tail =
        std::span<const Operation>(m_transactionOperations).subspan(operations);
    const bool trial = m_transaction->trial;
    if (Expected<void, CommandError> reverted =
            applyOperations(tail, false, trial ? "" : "Roll back " + m_transaction->label);
        !reverted)
    {
        return reverted;
    }
    m_transactionOperations.resize(operations);
    m_transaction->operations = operations;
    transactionChanged(trial);
    return {};
}

void EditorContext::commitStep(std::vector<Operation> operations, std::string_view label,
                               const CommandOptions& options, std::vector<EntityGuid> selectionBefore,
                               CommandReport& report)
{
    if (operations.empty())
    {
        m_changes.discardPending();
        return;
    }
    m_journal.writeOperations(label, operations);
    UndoStep step;
    step.label = std::string(label);
    step.origin = std::string(options.origin);
    step.mergeKey = std::string(options.mergeKey);
    step.operations = std::move(operations);
    step.selectionBefore = std::move(selectionBefore);
    step.selectionAfter.assign(m_selection.items().begin(), m_selection.items().end());
    report.transaction = pushStep(std::move(step));
    afterChange();
}

u64 EditorContext::pushStep(UndoStep step)
{
    const UndoPushResult pushed = m_history.push(std::move(step));
    if (pushed.trimmed > 0)
    {
        ILL_LOG_INFO("Editor", "The undo history is over its budget: {} oldest steps were dropped",
                     pushed.trimmed);
        m_events.post("history.trimmed", "{\"steps\":" + std::to_string(pushed.trimmed) + "}");
    }
    return m_history.undoStep()->id;
}

void EditorContext::afterChange(bool historyChanged)
{
    m_index.updateTransforms();
    m_changes.endRevision();
    m_events.post("scene.changed", "{\"revision\":" + std::to_string(revision()) + "}");
    if (historyChanged)
    {
        m_events.post("history.changed", "{\"position\":" + std::to_string(m_history.position()) +
                                             ",\"steps\":" + std::to_string(m_history.steps().size()) + "}");
    }
}

void EditorContext::restoreSelection(std::span<const EntityGuid> guids)
{
    std::vector<EntityGuid> alive;
    for (const EntityGuid guid : guids)
    {
        if (!m_index.find(guid).isNull())
        {
            alive.push_back(guid);
        }
    }
    m_selection.set(alive);
}

Expected<void, CommandError> EditorContext::applyOperations(std::span<const Operation> operations,
                                                            bool forward, std::string_view label)
{
    std::vector<Operation> applied;
    const usize count = operations.size();
    for (usize i = 0; i < count; ++i)
    {
        Operation operation = forward ? operations[i] : inverted(operations[count - 1 - i]);
        if (Expected<void, CommandError> done = m_editor.apply(operation); !done)
        {
            // Put back what was changed so far; the caller keeps its state.
            for (auto it = applied.rbegin(); it != applied.rend(); ++it)
            {
                (void)m_editor.apply(inverted(*it));
            }
            m_changes.discardPending();
            ILL_LOG_ERROR("Editor", "{} failed: {}", label, done.error().message);
            return unexpected(CommandError::make(
                ErrorKind::Internal, "'" + std::string(label) + "' failed: " + done.error().message));
        }
        applied.push_back(std::move(operation));
    }
    if (!label.empty())
    {
        m_journal.writeOperations(label, applied);
    }
    return {};
}

Expected<void, CommandError> EditorContext::applyStep(const UndoStep& step, bool forward)
{
    if (Expected<void, CommandError> applied =
            applyOperations(step.operations, forward, (forward ? "Redo " : "Undo ") + step.label);
        !applied)
    {
        return applied;
    }
    restoreSelection(forward ? step.selectionAfter : step.selectionBefore);
    return {};
}

Expected<std::vector<usize>, CommandError> EditorContext::planUndoOwn(u32 steps,
                                                                      std::string_view origin) const
{
    // Undoing a step that is independent of the later ones is the same as moving it to the top first, so
    // each planned step leaves the order of the others as it is.
    const std::span<const UndoStep> history = m_history.steps();
    std::vector<usize> applied(m_history.position());
    for (usize i = 0; i < applied.size(); ++i)
    {
        applied[i] = i;
    }
    std::vector<usize> planned;
    while (planned.size() < steps)
    {
        auto own = std::find_if(applied.rbegin(), applied.rend(),
                                [&](usize index) { return history[index].origin == origin; });
        if (own == applied.rend())
        {
            break;
        }
        const auto position = std::prev(own.base());
        const UndoStep& step = history[*position];
        for (auto later = std::next(position); later != applied.end(); ++later)
        {
            const UndoStep& other = history[*later];
            if (std::optional<std::string> overlap = findOverlap(step.operations, other.operations))
            {
                CommandError error = CommandError::make(
                    ErrorKind::Conflict, "cannot undo '" + step.label + "': the later step '" + other.label +
                                             "' of " + other.origin + " also changed " + *overlap);
                return unexpected(std::move(error));
            }
        }
        planned.push_back(*position);
        applied.erase(position);
    }
    return planned;
}

Expected<u32, CommandError> EditorContext::checkUndoOwn(u32 steps, std::string_view origin) const
{
    Expected<std::vector<usize>, CommandError> planned = planUndoOwn(steps, origin);
    if (!planned)
    {
        return unexpected(std::move(planned).error());
    }
    return static_cast<u32>(planned->size());
}

Expected<u32, CommandError> EditorContext::undoOwn(u32 steps, std::string_view origin)
{
    if (m_transaction)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "undo is not possible while a transaction is open"));
    }
    Expected<std::vector<usize>, CommandError> planned = planUndoOwn(steps, origin);
    if (!planned)
    {
        return unexpected(std::move(planned).error());
    }
    u32 done = 0;
    for (; done < planned->size(); ++done)
    {
        // The plan names indices before any step moved; the last own applied step is always the next one.
        const std::span<const UndoStep> history = m_history.steps();
        usize index = m_history.position();
        while (history[index - 1].origin != origin)
        {
            --index;
        }
        m_history.moveToTop(index - 1);
        const UndoStep* step = m_history.undoStep();
        if (step == nullptr)
        {
            break;
        }
        if (Expected<void, CommandError> applied = applyStep(*step, false); !applied)
        {
            if (done > 0)
            {
                afterChange();
            }
            return unexpected(std::move(applied).error());
        }
        m_history.stepBack();
    }
    if (done > 0)
    {
        afterChange();
    }
    return done;
}

Expected<u32, CommandError> EditorContext::undo(u32 steps)
{
    if (m_transaction)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "undo is not possible while a transaction is open"));
    }
    u32 done = 0;
    for (; done < steps; ++done)
    {
        const UndoStep* step = m_history.undoStep();
        if (step == nullptr)
        {
            break;
        }
        if (Expected<void, CommandError> applied = applyStep(*step, false); !applied)
        {
            if (done > 0)
            {
                afterChange();
            }
            return unexpected(std::move(applied).error());
        }
        m_history.stepBack();
    }
    if (done > 0)
    {
        afterChange();
    }
    return done;
}

Expected<u32, CommandError> EditorContext::redo(u32 steps)
{
    if (m_transaction)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "redo is not possible while a transaction is open"));
    }
    u32 done = 0;
    for (; done < steps; ++done)
    {
        const UndoStep* step = m_history.redoStep();
        if (step == nullptr)
        {
            break;
        }
        if (Expected<void, CommandError> applied = applyStep(*step, true); !applied)
        {
            if (done > 0)
            {
                afterChange();
            }
            return unexpected(std::move(applied).error());
        }
        m_history.stepForward();
    }
    if (done > 0)
    {
        afterChange();
    }
    return done;
}

Expected<void, CommandError> EditorContext::jump(usize position)
{
    if (position > m_history.steps().size())
    {
        return unexpected(CommandError::invalid("/position", "must be at most " +
                                                                 std::to_string(m_history.steps().size())));
    }
    if (position < m_history.position())
    {
        Expected<u32, CommandError> undone = undo(static_cast<u32>(m_history.position() - position));
        if (!undone)
        {
            return unexpected(std::move(undone).error());
        }
    }
    else if (position > m_history.position())
    {
        Expected<u32, CommandError> redone = redo(static_cast<u32>(position - m_history.position()));
        if (!redone)
        {
            return unexpected(std::move(redone).error());
        }
    }
    return {};
}

usize EditorContext::clearHistory()
{
    const bool clean = !dirty();
    const usize removed = m_history.clear();
    if (clean)
    {
        m_document.markSaved(m_document.path(), m_history.stateId());
    }
    else
    {
        // The saved state is gone with the history: the scene stays dirty.
        m_document.markSaved(m_document.path(), ~u64{0});
    }
    m_events.post("history.changed", "{\"position\":0,\"steps\":0}");
    return removed;
}

void EditorContext::endFrame(f64 deltaSeconds)
{
    ILL_PROFILE_SCOPE();
    if (m_commands.revision() != m_commandsRevision)
    {
        m_commandsRevision = m_commands.revision();
        m_events.post("commands.changed",
                      "{\"commands\":" + std::to_string(m_commands.commands().size()) + "}");
    }
    m_events.flush();
    m_journal.flush();
    if (m_project && m_options.autosaveInterval > 0.0 && dirty())
    {
        m_autosaveTimer += deltaSeconds;
        if (m_autosaveTimer >= m_options.autosaveInterval)
        {
            m_autosaveTimer = 0.0;
            autosave();
        }
    }
    else
    {
        m_autosaveTimer = 0.0;
    }
}

void EditorContext::autosave()
{
    Expected<std::string, CommandError> json = sceneJson();
    if (!json)
    {
        ILL_LOG_WARN("Editor", "Autosave failed: {}", json.error().message);
        return;
    }
    const u32 copies = std::max(1u, m_options.autosaveCopies);
    const std::filesystem::path directory = m_project->stateDirectory() / "autosave";
    const std::filesystem::path file =
        directory / utf8Path(std::string(m_document.displayName()) + "." +
                             std::to_string(m_autosaveCounter++ % copies) + ".scene");
    std::error_code code;
    std::filesystem::create_directories(directory, code);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(json->data(), static_cast<std::streamsize>(json->size()));
    if (!out)
    {
        ILL_LOG_WARN("Editor", "Autosave cannot write {}", detail::pathText(file));
        return;
    }
    m_events.post("scene.autosaved", "{\"revision\":" + std::to_string(revision()) + "}");
}

std::vector<std::filesystem::path> EditorContext::leftoverJournals() const
{
    std::vector<std::filesystem::path> journals;
    if (!m_project)
    {
        return journals;
    }
    std::error_code code;
    const std::filesystem::path directory = m_project->stateDirectory() / "journal";
    for (std::filesystem::directory_iterator it(directory, code), end; !code && it != end; it.increment(code))
    {
        if (it->path().extension() == ".jsonl" && it->path() != m_journal.path())
        {
            journals.push_back(it->path());
        }
    }
    std::sort(journals.begin(), journals.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  std::error_code ignored;
                  const auto timeA = std::filesystem::last_write_time(a, ignored);
                  const auto timeB = std::filesystem::last_write_time(b, ignored);
                  return timeA != timeB ? timeA > timeB : a < b;
              });
    return journals;
}

Expected<RecoveryResult, CommandError> EditorContext::recover(const std::filesystem::path& journal)
{
    if (!m_project)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    std::filesystem::path file = journal;
    if (file.empty())
    {
        const std::vector<std::filesystem::path> journals = leftoverJournals();
        if (journals.empty())
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "there is no journal of a crashed session"));
        }
        file = journals.front();
    }
    else if (file.is_relative())
    {
        file = m_project->stateDirectory() / "journal" / file;
    }
    if (file == m_journal.path())
    {
        return unexpected(CommandError::invalid("/journal", "is the journal of this session"));
    }
    Expected<JournalReplay, std::string> replay = readJournal(file);
    if (!replay)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "cannot recover: " + replay.error()));
    }
    if (replay->content)
    {
        if (Expected<bool, CommandError> loaded = loadSceneText(*replay->content, nullptr); !loaded)
        {
            return unexpected(std::move(loaded).error());
        }
        sceneReplaced(replay->scene.value_or(""));
        m_document.markSaved(replay->scene.value_or(""), ~u64{0});
        m_journal.writeBase(replay->scene.value_or(""), *replay->content);
    }
    else if (replay->scene)
    {
        if (Expected<void, CommandError> opened = openScene(*replay->scene); !opened)
        {
            return unexpected(std::move(opened).error());
        }
    }
    else
    {
        newScene();
    }
    m_editor.begin();
    for (const Operation& operation : replay->operations)
    {
        if (Expected<void, CommandError> applied = m_editor.apply(operation); !applied)
        {
            m_editor.rollback();
            m_changes.discardPending();
            return unexpected(CommandError::make(
                ErrorKind::Internal, "the journal does not match the scene: " + applied.error().message));
        }
    }
    CommandOptions options;
    options.origin = "recovery";
    CommandReport report;
    commitStep(m_editor.commit(), "Recover unsaved changes", options, {}, report);
    std::error_code code;
    std::filesystem::remove(file, code);
    RecoveryResult result;
    result.journal = detail::pathText(file.filename());
    result.scene = replay->scene.value_or("");
    result.operations = static_cast<u32>(replay->operations.size());
    return result;
}

} // namespace illusion::editor

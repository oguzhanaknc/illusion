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
#include <new>
#include <random>
#include <system_error>

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

} // namespace

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
        m_journal.open(m_project->stateDirectory() / "journal" / utf8Path(m_session + ".jsonl"), m_session);
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
    JsonMutDocument doc;
    JsonMutValue root;
    if (descriptor->result != nullptr)
    {
        Expected<JsonMutValue, ValueError> written = writeValue(doc, *descriptor->result, resultData);
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

Expected<void, CommandError> EditorContext::run(const CommandDescriptor& descriptor, const void* params,
                                                void* result, const CommandOptions& options,
                                                CommandReport& report)
{
    ILL_PROFILE_SCOPE();
    if (m_running)
    {
        return unexpected(CommandError::make(ErrorKind::Busy, "commands cannot run inside other commands"));
    }
    if (options.expectRevision && *options.expectRevision != revision())
    {
        CommandError error =
            CommandError::make(ErrorKind::Conflict, "the scene is at revision " + std::to_string(revision()) +
                                                        ", not " + std::to_string(*options.expectRevision));
        return unexpected(std::move(error));
    }
    m_running = true;
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
    m_running = false;
    return ran;
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
    const UndoPushResult pushed = m_history.push(std::move(step));
    if (pushed.trimmed > 0)
    {
        ILL_LOG_INFO("Editor", "The undo history is over its budget: {} oldest steps were dropped",
                     pushed.trimmed);
        m_events.post("history.trimmed", "{\"steps\":" + std::to_string(pushed.trimmed) + "}");
    }
    report.transaction = m_history.undoStep()->id;
    afterChange();
}

void EditorContext::afterChange()
{
    m_index.updateTransforms();
    m_changes.endRevision();
    m_events.post("scene.changed", "{\"revision\":" + std::to_string(revision()) + "}");
    m_events.post("history.changed", "{\"position\":" + std::to_string(m_history.position()) +
                                         ",\"steps\":" + std::to_string(m_history.steps().size()) + "}");
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

Expected<void, CommandError> EditorContext::applyStep(const UndoStep& step, bool forward)
{
    std::vector<Operation> applied;
    const usize count = step.operations.size();
    for (usize i = 0; i < count; ++i)
    {
        Operation operation = forward ? step.operations[i] : inverted(step.operations[count - 1 - i]);
        if (Expected<void, CommandError> done = m_editor.apply(operation); !done)
        {
            // Put back what this step changed so far; the history stays where it was.
            for (auto it = applied.rbegin(); it != applied.rend(); ++it)
            {
                (void)m_editor.apply(inverted(*it));
            }
            m_changes.discardPending();
            ILL_LOG_ERROR("Editor", "{} '{}' failed: {}", forward ? "Redo of" : "Undo of", step.label,
                          done.error().message);
            return unexpected(CommandError::make(ErrorKind::Internal,
                                                 std::string(forward ? "redo" : "undo") + " of '" +
                                                     step.label + "' failed: " + done.error().message));
        }
        applied.push_back(std::move(operation));
    }
    m_journal.writeOperations((forward ? "Redo " : "Undo ") + step.label, applied);
    restoreSelection(forward ? step.selectionAfter : step.selectionBefore);
    return {};
}

Expected<u32, CommandError> EditorContext::undo(u32 steps)
{
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

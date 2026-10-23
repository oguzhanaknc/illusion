#pragma once

/// @file
/// The headless editor core: the project, the edited World and scene, the selection, the command registry,
/// transactions, undo, the change and crash journals, and autosave.

#include <illusion/core/Assert.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/ChangeJournal.hpp>
#include <illusion/editor/core/CommandRegistry.hpp>
#include <illusion/editor/core/CrashJournal.hpp>
#include <illusion/editor/core/EntityIndex.hpp>
#include <illusion/editor/core/Project.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/SceneDocument.hpp>
#include <illusion/editor/core/Selection.hpp>
#include <illusion/editor/core/SettingsCatalog.hpp>
#include <illusion/editor/core/UndoStack.hpp>
#include <illusion/editor/core/WorldEditor.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/Localization.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class LogRingSink;
} // namespace illusion

namespace illusion::editor
{

class EditorContext;

/// Settings of an EditorContext.
struct EditorContextOptions
{
    /// Seeds entity ids and the session id; 0 picks a random seed. Tests and `--deterministic` set it.
    u64 seed = 0;
    usize undoBudget = kDefaultUndoBudget; ///< Memory budget of the undo history.
    f64 autosaveInterval = 60.0;           ///< Seconds between autosaves of a dirty scene (0: never).
    u32 autosaveCopies = 3;                ///< Autosave files kept per scene (rotating).
    bool crashJournal = true;              ///< Write `.illusion/journal/<session>.jsonl` in projects.
    usize logCapacity = 1024;              ///< Log messages kept for `log.read` (0: none are kept).
    bool registerCoreCommands = true;      ///< Register the core command modules (registerCoreCommands()).
};

/// How a command is run.
struct CommandOptions
{
    bool dryRun = false;                 ///< Validate and report the footprint without applying.
    std::string_view origin = "ui";      ///< Who runs it: "ui", "shortcut", "inspector", "rpc:<client>", ...
    std::string_view mergeKey{};         ///< Continuous edits with the same key become one undo step.
    std::optional<u64> expectRevision{}; ///< Fail with Conflict unless the scene is at this revision.
    std::string_view label{};            ///< Undo label (default: the command's English title).
};

/// Facts about the last command, returned in the `_editor` member of JSON results.
struct CommandReport
{
    u64 revision = 0;                  ///< Scene revision after the command.
    u64 transaction = 0;               ///< Id of its undo step (0 if it made none).
    bool dryRun = false;               ///< Nothing was applied.
    u32 operations = 0;                ///< Number of primitive operations (also for dry runs).
    std::vector<std::string> warnings; ///< Remarks, e.g. skipped Components or missing assets.
};

/// What a command handler gets: the editor, validated access to entities, and its options.
class CommandContext
{
public:
    /// A context for one command run by @p editor.
    CommandContext(EditorContext& editor, const CommandOptions& options, CommandReport& report) noexcept
        : m_editor(&editor)
        , m_options(&options)
        , m_report(&report)
    {
    }

    /// The editor.
    [[nodiscard]] EditorContext& editor() noexcept { return *m_editor; }

    /// The edits of the open transaction.
    [[nodiscard]] WorldEditor& edit() noexcept;

    /// The edited World (read it; change it only through edit()).
    [[nodiscard]] World& world() noexcept;

    /// True for a dry run. Undoable commands need not check it (their transaction is rolled back); other
    /// commands that change state must validate and return without changing anything.
    [[nodiscard]] bool dryRun() const noexcept { return m_options->dryRun; }

    /// The origin of the call, e.g. "ui".
    [[nodiscard]] std::string_view origin() const noexcept { return m_options->origin; }

    /// Resolves an entity reference at JSON Pointer @p pointer of the params.
    [[nodiscard]] Expected<Entity, CommandError> entity(std::string_view reference, std::string_view pointer);

    /// Resolves a list of entity references (`$selection` expands to every selected entity).
    [[nodiscard]] Expected<void, CommandError> entities(std::span<const std::string> references,
                                                        std::string_view pointer, std::vector<Entity>& out);

    /// Id, name and path of @p entity.
    [[nodiscard]] EntitySummary summary(Entity entity);

    /// The reflected Component named @p name (stable name), with suggestions if there is none.
    [[nodiscard]] Expected<ComponentId, CommandError> componentType(std::string_view name,
                                                                    std::string_view pointer) const;

    /// Adds a warning to the result.
    void warn(std::string message) { m_report->warnings.push_back(std::move(message)); }

private:
    EditorContext* m_editor;
    const CommandOptions* m_options;
    CommandReport* m_report;
};

/// What EditorContext::recover() did.
struct RecoveryResult
{
    std::string journal; ///< The journal file that was replayed.
    std::string scene;   ///< Scene it started from ("" for a new scene).
    u32 operations = 0;  ///< Operations applied.
};

/// The editor without a window: everything humans and agents change goes through its commands.
///
/// It owns a VirtualFileSystem (with the project mounts once a project is open), one World holding the
/// open scene and its settings, the EntityIndex, the Selection, the UndoStack, the journals, the clipboard,
/// the settings types, the asset reference index and a ring of recent log messages. Commands run on the
/// thread that owns the context, at the command point of the frame, never while a Query iterates. After
/// every change the world-space Transforms of children are derived again (EntityIndex::updateTransforms()).
class EditorContext : NonCopyable
{
public:
    /// An editor with no project and an empty, untitled scene.
    explicit EditorContext(const EditorContextOptions& options = {});

    /// Closes the crash journal cleanly (deleting it) and the project.
    ~EditorContext();

    /// Opens the project in @p root: mounts it, starts the crash journal and opens the startup scene if
    /// it has one. Unsaved changes are discarded; journals of crashed sessions are kept for recover().
    [[nodiscard]] Expected<void, CommandError> openProject(const std::filesystem::path& root);

    /// The open project, or nullptr.
    [[nodiscard]] const Project* project() const noexcept { return m_project ? &*m_project : nullptr; }

    /// The file system with the project mounts.
    [[nodiscard]] VirtualFileSystem& files() noexcept { return m_files; }

    /// The edited World.
    [[nodiscard]] World& world() noexcept { return m_world; }

    /// Entities by id, path and name.
    [[nodiscard]] EntityIndex& entities() noexcept { return m_index; }

    /// The selection.
    [[nodiscard]] Selection& selection() noexcept { return m_selection; }

    /// The open scene.
    [[nodiscard]] const SceneDocument& document() const noexcept { return m_document; }

    /// The undo history.
    [[nodiscard]] const UndoStack& history() const noexcept { return m_history; }

    /// Changes by revision.
    [[nodiscard]] const ChangeJournal& changes() const noexcept { return m_changes; }

    /// Editor events (delivered by endFrame()).
    [[nodiscard]] EventHub& events() noexcept { return m_events; }

    /// The commands.
    [[nodiscard]] CommandRegistry& commands() noexcept { return m_commands; }

    /// The edits of the World (use it only inside commands, through CommandContext::edit()).
    [[nodiscard]] WorldEditor& editor() noexcept { return m_editor; }

    /// The editor's localization (titles of commands, messages for the UI).
    [[nodiscard]] text::Localization& localization() noexcept { return m_localization; }

    /// Makes entity ids (seeded by EditorContextOptions::seed).
    [[nodiscard]] GuidGenerator& guids() noexcept { return m_guids; }

    /// The settings of the open scene (the "settings" section of its file), sorted by type.
    [[nodiscard]] std::span<const PreservedValue> sceneSettings() const noexcept
    {
        return m_editor.settings();
    }

    /// The settings types the settings commands accept.
    [[nodiscard]] SettingsCatalog& settings() noexcept { return m_settings; }

    /// The asset references of the project's scene and material files, brought up to date first (empty
    /// without a project).
    [[nodiscard]] const ReferenceIndex& references();

    /// The editor's clipboard: scene v2 JSON of copied entities ("" when empty).
    [[nodiscard]] const std::string& clipboard() const noexcept { return m_clipboard; }

    /// Replaces the clipboard.
    void setClipboard(std::string text) noexcept { m_clipboard = std::move(text); }

    /// Recent log messages of the process (nullptr if EditorContextOptions::logCapacity is 0).
    [[nodiscard]] const LogRingSink* log() const noexcept { return m_log.get(); }

    /// Sequence of the last log message hidden by clearLog() (`log.read` returns newer ones).
    [[nodiscard]] u64 logCleared() const noexcept { return m_logCleared; }

    /// Hides the log messages written so far from `log.read`.
    void clearLog();

    /// The session id: 16 hex digits, also the crash journal's file name.
    [[nodiscard]] const std::string& session() const noexcept { return m_session; }

    /// The scene revision: increases with every change of the scene (commits, undos and redos).
    [[nodiscard]] u64 revision() const noexcept { return m_changes.revision(); }

    /// True if the scene has changes that are not saved.
    [[nodiscard]] bool dirty() const noexcept { return m_document.dirty(m_history.stateId()); }

    /// Replaces the scene with an empty, untitled one.
    void newScene();

    /// Replaces the scene with the scene file at @p path; @p warnings receives missing assets and skipped
    /// data (may be null).
    [[nodiscard]] Expected<void, CommandError> openScene(std::string_view path,
                                                         std::vector<std::string>* warnings = nullptr);

    /// Saves the scene to @p path, or to its own path when @p path is empty. Returns the number of entities.
    [[nodiscard]] Expected<u32, CommandError> saveScene(std::string_view path = {});

    /// The scene as scene v2 JSON (in hierarchy order).
    [[nodiscard]] Expected<std::string, CommandError> sceneJson() const;

    /// Runs command @p name with typed params; the Result type must be the command's.
    template <class R, class P>
    [[nodiscard]] Expected<R, CommandError> execute(std::string_view name, const P& params,
                                                    const CommandOptions& options = {},
                                                    CommandReport* report = nullptr)
    {
        const CommandDescriptor* descriptor = m_commands.find(name);
        if (descriptor == nullptr)
        {
            return unexpected(unknownCommand(name));
        }
        ILL_ASSERT(descriptor->paramsType == typeHash<P>() && descriptor->resultType == typeHash<R>(),
                   "execute<{}, {}>() does not match the types of command {}", typeName<R>(), typeName<P>(),
                   name);
        R result{};
        CommandReport local;
        if (Expected<void, CommandError> ran =
                run(*descriptor, &params, &result, options, report ? *report : local);
            !ran)
        {
            return unexpected(std::move(ran).error());
        }
        return result;
    }

    /// Runs command @p name with JSON params (an object, or absent for none). Returns the result as compact
    /// JSON with an `_editor` member ({revision, transaction, dryRun, operations, warnings}).
    [[nodiscard]] Expected<std::string, CommandError> executeJson(std::string_view name, JsonValue params,
                                                                  const CommandOptions& options = {});

    /// Reads JSON params for @p descriptor into @p params (a default-constructed params struct). The reserved
    /// members `dryRun`, `mergeKey` and `expectRevision` go to @p options.
    [[nodiscard]] Expected<void, CommandError> readParams(const CommandDescriptor& descriptor, JsonValue json,
                                                          void* params, CommandOptions& options) const;

    /// Undoes up to @p steps steps; returns how many were undone.
    [[nodiscard]] Expected<u32, CommandError> undo(u32 steps = 1);

    /// Redoes up to @p steps steps; returns how many were redone.
    [[nodiscard]] Expected<u32, CommandError> redo(u32 steps = 1);

    /// Undoes or redoes until UndoStack::position() is @p position.
    [[nodiscard]] Expected<void, CommandError> jump(usize position);

    /// Forgets the undo history; returns the number of removed steps.
    usize clearHistory();

    /// End of a frame: delivers events, flushes the crash journal and autosaves when it is time.
    void endFrame(f64 deltaSeconds);

    /// Journals of other sessions in `.illusion/journal/` (crashed sessions), newest first.
    [[nodiscard]] std::vector<std::filesystem::path> leftoverJournals() const;

    /// Replays the journal @p journal (default: the newest leftover journal) as one undoable step on top of
    /// the scene it started from, then deletes it.
    [[nodiscard]] Expected<RecoveryResult, CommandError> recover(const std::filesystem::path& journal = {});

private:
    friend class CommandContext;

    [[nodiscard]] Expected<void, CommandError> run(const CommandDescriptor& descriptor, const void* params,
                                                   void* result, const CommandOptions& options,
                                                   CommandReport& report);
    [[nodiscard]] CommandError unknownCommand(std::string_view name) const;
    [[nodiscard]] Expected<bool, CommandError> loadSceneText(std::string_view json,
                                                             std::vector<std::string>* warnings);
    void commitStep(std::vector<Operation> operations, std::string_view label, const CommandOptions& options,
                    std::vector<EntityGuid> selectionBefore, CommandReport& report);
    [[nodiscard]] Expected<void, CommandError> applyStep(const UndoStep& step, bool forward);
    void sceneReplaced(std::string_view path);
    void afterChange();
    void restoreSelection(std::span<const EntityGuid> guids);
    void autosave();

    EditorContextOptions m_options;
    VirtualFileSystem m_files;
    std::optional<Project> m_project;
    World m_world;
    GuidGenerator m_guids;
    std::string m_session;
    EntityIndex m_index;
    ChangeJournal m_changes;
    WorldEditor m_editor;
    Selection m_selection;
    SceneDocument m_document;
    UndoStack m_history;
    EventHub m_events;
    CommandRegistry m_commands;
    text::Localization m_localization;
    CrashJournal m_journal;
    SettingsCatalog m_settings;
    ReferenceIndex m_references;
    std::string m_clipboard;
    std::shared_ptr<LogRingSink> m_log;
    u64 m_logCleared = 0;
    f64 m_autosaveTimer = 0.0;
    u32 m_autosaveCounter = 0;
    bool m_running = false;
};

} // namespace illusion::editor

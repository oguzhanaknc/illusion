#include "../PathText.hpp"
#include "CommandSupport.hpp"

#include <illusion/core/VirtualPath.hpp>
#include <illusion/text/Utf8.hpp>

#include <algorithm>
#include <filesystem>

namespace illusion::editor::detail
{
namespace
{

CommandError readError(FileError error, std::string_view path)
{
    switch (error)
    {
    case FileError::InvalidPath:
    case FileError::UnknownMount:
        return CommandError::invalid("/path",
                                     "is not a valid virtual path such as \"assets:/scenes/main.scene\"");
    case FileError::NotFound:
        return CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist");
    default:
        return CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                          std::string(fileErrorName(error)) + ")");
    }
}

CommandError atPath(CommandError error, std::string_view pointer)
{
    return error.kind == ErrorKind::ValidationFailed ? withPointerPrefix(std::move(error), pointer) : error;
}

ProjectInfoResult infoOf(const Project& project)
{
    ProjectInfoResult result;
    result.name = project.manifest().name;
    result.version = project.manifest().version;
    result.engineVersion = project.manifest().engineVersion;
    result.root = pathText(project.root());
    result.startupScene = project.manifest().startupScene;
    result.modules = project.manifest().modules;
    for (const ProjectMount& mount : projectMounts())
    {
        result.mounts.push_back(std::string(mount.name) + ":/");
    }
    return result;
}

Expected<ProjectInfoResult, CommandError> projectInfo(CommandContext& context, const NoParams&)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    return infoOf(*context.editor().project());
}

// The directory @p relative (a relative path with '/' separators) in the workspace.
Expected<std::filesystem::path, CommandError> workspaceDirectory(CommandContext& context,
                                                                 std::string_view relative)
{
    const std::filesystem::path& workspace = context.editor().options().workspace;
    if (workspace.empty())
    {
        return unexpected(CommandError::make(
            ErrorKind::Unsupported, "this editor has no workspace for projects (start it with --workspace)"));
    }
    if (!isValidRelativePath(relative))
    {
        return unexpected(CommandError::invalid(
            "/directory",
            "must be a relative path inside the workspace, such as \"tetris\" or \"games/tetris\""));
    }
    return workspace / utf8Path(relative);
}

Expected<void, CommandError> checkDiscard(CommandContext& context, const std::optional<bool>& discard)
{
    if (context.editor().dirty() && !discard.value_or(false))
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed, "the scene has unsaved changes; save it or pass discardChanges"));
    }
    return {};
}

Expected<ProjectInfoResult, CommandError> projectCreate(CommandContext& context,
                                                        const ProjectCreateParams& params)
{
    Expected<std::filesystem::path, CommandError> root = workspaceDirectory(context, params.directory);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    if (params.name.empty() || !text::isValidUtf8(params.name))
    {
        return unexpected(CommandError::invalid("/name", "must be a non-empty UTF-8 name"));
    }
    std::filesystem::path templates = context.editor().options().templates;
    if (templates.empty())
    {
        templates = executableDirectory() / "editor-data" / "templates";
    }
    const std::string templateName = params.templateName.value_or("empty");
    std::error_code code;
    if (!isValidRelativePath(templateName) || templateName.find('/') != std::string::npos ||
        !std::filesystem::is_directory(templates / utf8Path(templateName), code))
    {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(templates, code))
        {
            if (entry.is_directory(code))
            {
                names.push_back(pathText(entry.path().filename()));
            }
        }
        std::sort(names.begin(), names.end());
        std::vector<std::string_view> candidates(names.begin(), names.end());
        CommandError error = CommandError::invalid("/template", "is not a project template");
        error.suggestions = closestMatches(templateName, candidates);
        if (error.suggestions.empty())
        {
            error.suggestions = names;
        }
        return unexpected(std::move(error));
    }
    if (std::filesystem::exists(*root, code) &&
        (!std::filesystem::is_directory(*root, code) || !std::filesystem::is_empty(*root, code)))
    {
        return unexpected(CommandError::make(
            ErrorKind::AlreadyExists, "'" + params.directory + "' exists and is not an empty directory"));
    }
    if (Expected<void, CommandError> discard = checkDiscard(context, params.discardChanges); !discard)
    {
        return unexpected(std::move(discard).error());
    }
    if (context.dryRun())
    {
        ProjectInfoResult result;
        result.name = params.name;
        result.root = pathText(*root);
        return result;
    }
    Expected<Project, std::string> created =
        Project::create(*root, params.name, templates / utf8Path(templateName));
    if (!created)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, std::move(created).error()));
    }
    if (Expected<void, CommandError> opened = context.editor().openProject(*root); !opened)
    {
        return unexpected(std::move(opened).error());
    }
    return infoOf(*context.editor().project());
}

Expected<ProjectInfoResult, CommandError> projectOpen(CommandContext& context,
                                                      const ProjectOpenParams& params)
{
    Expected<std::filesystem::path, CommandError> root = workspaceDirectory(context, params.directory);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    std::error_code code;
    if (!std::filesystem::is_regular_file(*root / utf8Path(kProjectFileName), code))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "'" + params.directory + "' is not a project directory"));
    }
    if (Expected<void, CommandError> discard = checkDiscard(context, params.discardChanges); !discard)
    {
        return unexpected(std::move(discard).error());
    }
    if (context.dryRun())
    {
        ProjectInfoResult result;
        result.root = pathText(*root);
        return result;
    }
    if (Expected<void, CommandError> opened = context.editor().openProject(*root); !opened)
    {
        return unexpected(std::move(opened).error());
    }
    return infoOf(*context.editor().project());
}

Expected<FileListResult, CommandError> fileList(CommandContext& context, const FileListParams& params)
{
    std::vector<DirectoryEntry> entries;
    VirtualFileSystem& files = context.editor().files();
    const FileError error = params.recursive.value_or(false) ? files.listRecursive(params.path, entries)
                                                             : files.list(params.path, entries);
    if (error != FileError::None)
    {
        return unexpected(readError(error, params.path));
    }
    FileListResult result;
    for (const DirectoryEntry& entry : entries)
    {
        result.entries.push_back(FileEntry{joinPath(params.path, entry.name), entry.isDirectory, entry.size});
    }
    return result;
}

Expected<FileReadResult, CommandError> fileRead(CommandContext& context, const FilePathParams& params)
{
    FileReadResult result;
    if (const FileError error = context.editor().files().readText(params.path, result.text);
        error != FileError::None)
    {
        return unexpected(readError(error, params.path));
    }
    if (!text::isValidUtf8(result.text))
    {
        return unexpected(
            CommandError::make(ErrorKind::Unsupported, "'" + params.path + "' is not a UTF-8 text file"));
    }
    result.path = params.path;
    return result;
}

Expected<FileStatResult, CommandError> fileStat(CommandContext& context, const FilePathParams& params)
{
    FileStat stat;
    FileStatResult result;
    result.path = params.path;
    const FileError error = context.editor().files().stat(params.path, stat);
    if (error == FileError::NotFound)
    {
        return result;
    }
    if (error != FileError::None)
    {
        return unexpected(readError(error, params.path));
    }
    result.exists = true;
    result.isDirectory = stat.isDirectory;
    result.size = stat.size;
    return result;
}

Expected<FileWriteResult, CommandError> fileWrite(CommandContext& context, const FileWriteParams& params)
{
    if (!text::isValidUtf8(params.text))
    {
        return unexpected(CommandError::invalid("/text", "must be valid UTF-8"));
    }
    Expected<bool, CommandError> created = context.edit().writeFile(params.path, params.text);
    if (!created)
    {
        return unexpected(atPath(std::move(created).error(), "/path"));
    }
    return FileWriteResult{params.path, *created};
}

Expected<FileMoveParams, CommandError> fileMove(CommandContext& context, const FileMoveParams& params)
{
    if (Expected<void, CommandError> moved = context.edit().moveFile(params.from, params.to); !moved)
    {
        return unexpected(atPath(std::move(moved).error(), "/from"));
    }
    return params;
}

Expected<FilePathResult, CommandError> fileDelete(CommandContext& context, const FilePathParams& params)
{
    if (Expected<void, CommandError> deleted = context.edit().deleteFile(params.path); !deleted)
    {
        return unexpected(atPath(std::move(deleted).error(), "/path"));
    }
    return FilePathResult{params.path};
}

Expected<FilePathResult, CommandError> fileMkdir(CommandContext& context, const FilePathParams& params)
{
    if (Expected<void, CommandError> created = context.edit().createDirectory(params.path); !created)
    {
        return unexpected(atPath(std::move(created).error(), "/path"));
    }
    return FilePathResult{params.path};
}

Expected<RecoverResult, CommandError> recover(CommandContext& context, const RecoverParams& params)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    if (params.journal &&
        (params.journal->empty() || params.journal->find_first_of("/\\:") != std::string::npos))
    {
        return unexpected(CommandError::invalid("/journal", "must be a file name in .illusion/journal"));
    }
    const std::filesystem::path journal =
        params.journal ? utf8Path(*params.journal) : std::filesystem::path();
    if (context.dryRun())
    {
        const std::vector<std::filesystem::path> journals = context.editor().leftoverJournals();
        if (journals.empty())
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "there is no journal of a crashed session"));
        }
        return RecoverResult{pathText(journal.empty() ? journals.front().filename() : journal), {}, 0};
    }
    Expected<RecoveryResult, CommandError> recovered = context.editor().recover(journal);
    if (!recovered)
    {
        return unexpected(std::move(recovered).error());
    }
    return RecoverResult{recovered->journal, recovered->scene, recovered->operations};
}

} // namespace

void registerProjectCommands(CommandRegistry& registry)
{
    registry.add<&projectInfo>({
        .name = "project.info",
        .title = strings::kCommandProjectInfoTitle,
        .description = strings::kCommandProjectInfoDescription,
        .agentSummary =
            "Returns the project's name, version, root directory, startup scene, modules and mounts. "
            "Example: {}.",
        .category = "Project",
        .icon = "info",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&projectCreate>({
        .name = "project.create",
        .title = strings::kCommandProjectCreateTitle,
        .description = strings::kCommandProjectCreateDescription,
        .agentSummary =
            "Creates a project from a template in a new directory of the editor's workspace and opens it. "
            "Templates: \"empty\", \"3d-starter\". Example: {\"directory\":\"tetris\",\"name\":\"Tetris\","
            "\"template\":\"empty\"}.",
        .category = "Project",
        .icon = "create_new_folder",
        .flags = {.outsideTransactions = true},
        .permission = PermissionClass::Files,
        .palette = true,
    });
    registry.add<&projectOpen>({
        .name = "project.open",
        .title = strings::kCommandProjectOpenTitle,
        .description = strings::kCommandProjectOpenDescription,
        .agentSummary =
            "Opens the project in a directory of the editor's workspace, closing the current one. "
            "Example: {\"directory\":\"tetris\"}.",
        .category = "Project",
        .icon = "folder_open",
        .flags = {.outsideTransactions = true},
        .permission = PermissionClass::System,
        .palette = true,
    });
    registry.add<&fileList>({
        .name = "project.file_list",
        .title = strings::kCommandProjectFileListTitle,
        .description = strings::kCommandProjectFileListDescription,
        .agentSummary =
            "Lists a directory of a project mount (assets:, settings:, modules:, cache:). Example: "
            "{\"path\": \"assets:/scenes\", \"recursive\": true}.",
        .category = "Project",
        .icon = "folder_open",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&fileRead>({
        .name = "project.file_read",
        .title = strings::kCommandProjectFileReadTitle,
        .description = strings::kCommandProjectFileReadDescription,
        .agentSummary = "Reads a UTF-8 text file. Example: {\"path\": \"assets:/materials/frame.material\"}.",
        .category = "Project",
        .icon = "description",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&fileStat>({
        .name = "project.file_stat",
        .title = strings::kCommandProjectFileStatTitle,
        .description = strings::kCommandProjectFileStatDescription,
        .agentSummary = "Tells whether a path exists, whether it is a directory and its size. Example: "
                        "{\"path\": \"assets:/scenes/main.scene\"}.",
        .category = "Project",
        .icon = "info",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&fileWrite>({
        .name = "project.file_write",
        .title = strings::kCommandProjectFileWriteTitle,
        .description = strings::kCommandProjectFileWriteDescription,
        .agentSummary =
            "Creates or replaces a UTF-8 text file; undo restores the previous contents. Example: "
            "{\"path\": \"assets:/notes.txt\", \"text\": \"hello\"}.",
        .category = "Project",
        .icon = "edit_document",
        .flags = {.destructive = true, .idempotent = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&fileMove>({
        .name = "project.file_move",
        .title = strings::kCommandProjectFileMoveTitle,
        .description = strings::kCommandProjectFileMoveDescription,
        .agentSummary = "Moves or renames a file or directory; the target must not exist. Example: "
                        "{\"from\": \"assets:/a.txt\", \"to\": \"assets:/docs/a.txt\"}.",
        .category = "Project",
        .icon = "drive_file_move",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&fileDelete>({
        .name = "project.file_delete",
        .title = strings::kCommandProjectFileDeleteTitle,
        .description = strings::kCommandProjectFileDeleteDescription,
        .agentSummary = "Deletes a file or a directory with its contents; undo restores them from the trash. "
                        "Example: {\"path\": \"assets:/old\"}.",
        .category = "Project",
        .icon = "delete",
        .flags = {.destructive = true, .undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&fileMkdir>({
        .name = "project.file_mkdir",
        .title = strings::kCommandProjectFileMkdirTitle,
        .description = strings::kCommandProjectFileMkdirDescription,
        .agentSummary =
            "Creates a directory and its missing parents. Example: {\"path\": \"assets:/levels\"}.",
        .category = "Project",
        .icon = "create_new_folder",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
    });
    registry.add<&recover>({
        .name = "project.recover",
        .title = strings::kCommandProjectRecoverTitle,
        .description = strings::kCommandProjectRecoverDescription,
        .agentSummary =
            "Replays the crash journal of a session that ended without saving, as one undo step on "
            "top of the scene it started from. Example: {} (the newest journal).",
        .category = "Project",
        .icon = "restore",
        .flags = {.outsideTransactions = true},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
}

} // namespace illusion::editor::detail

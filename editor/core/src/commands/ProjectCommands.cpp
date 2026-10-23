#include "../PathText.hpp"
#include "CommandSupport.hpp"

#include <illusion/text/Utf8.hpp>

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

Expected<ProjectInfoResult, CommandError> projectInfo(CommandContext& context, const NoParams&)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    const Project& project = *context.editor().project();
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
        .flags = {},
        .permission = PermissionClass::Edit,
        .palette = true,
    });
}

} // namespace illusion::editor::detail

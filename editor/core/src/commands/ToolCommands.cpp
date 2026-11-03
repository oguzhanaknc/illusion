// Commands that start External Tools of the preferences (ADR 0043): editor.open_source and editor.reveal.

#include "../PathText.hpp"
#include "CommandSupport.hpp"

#include <illusion/editor/core/ExternalTools.hpp>
#include <illusion/platform/Process.hpp>

#include <filesystem>

namespace illusion::editor::detail
{

namespace
{

// The native file that @p path names: a virtual path of a mount, or an absolute path.
Expected<std::filesystem::path, CommandError> sourceFile(CommandContext& context, const std::string& path)
{
    if (path.empty())
    {
        return unexpected(CommandError::invalid("/path", "must name a file"));
    }
    std::filesystem::path file;
    if (const usize mount = path.find(":/"); mount != std::string::npos && mount > 1)
    {
        if (context.editor().files().resolve(path, file) != FileError::None)
        {
            return unexpected(
                CommandError::invalid("/path", "must be a file of a project mount or an absolute path"));
        }
    }
    else
    {
        file = std::filesystem::path(std::u8string(path.begin(), path.end()));
        if (!file.is_absolute())
        {
            return unexpected(CommandError::invalid(
                "/path", "must be a virtual path such as \"modules:/game/game.cpp\" or an "
                         "absolute path"));
        }
    }
    std::error_code code;
    if (!std::filesystem::is_regular_file(file, code))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "file '" + path + "' does not exist"));
    }
    return file.make_preferred();
}

// Starts @p program with @p arguments detached, unless this is a dry run.
Expected<void, CommandError> spawnDetached(CommandContext& context, const std::filesystem::path& program,
                                           const std::vector<std::string>& arguments)
{
    if (context.dryRun())
    {
        return {};
    }
    Expected<Process, std::string> started =
        Process::spawn(ProcessOptions{.executable = program, .arguments = arguments, .detached = true});
    if (!started)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, std::move(started).error()));
    }
    return {};
}

// The program behind @p arguments[0] (a bare name is looked up through PATH).
Expected<std::filesystem::path, CommandError> programOf(const std::vector<std::string>& arguments)
{
    std::filesystem::path program(std::u8string(arguments[0].begin(), arguments[0].end()));
    if (!program.has_parent_path())
    {
        const std::optional<std::filesystem::path> found = findExecutable(arguments[0]);
        if (!found)
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "program '" + arguments[0] + "' is not on the PATH"));
        }
        program = *found;
    }
    return program;
}

Expected<OpenSourceResult, CommandError> editorOpenSource(CommandContext& context,
                                                          const OpenSourceParams& params)
{
    Expected<std::filesystem::path, CommandError> file = sourceFile(context, params.path);
    if (!file)
    {
        return unexpected(std::move(file).error());
    }
    const std::string fileText = detail::pathText(*file);
    const std::string& editor = context.editor().preferences().codeEditor;
    std::vector<std::string> arguments;
    if (editor.empty())
    {
        arguments = defaultOpenArguments(fileText);
    }
    else
    {
        Expected<std::vector<std::string>, std::string> parsed =
            codeEditorArguments(editor, fileText, params.line.value_or(1), params.column.value_or(1));
        if (!parsed)
        {
            return unexpected(
                CommandError::make(ErrorKind::PreconditionFailed, "preferences: " + parsed.error()));
        }
        arguments = std::move(*parsed);
    }
    Expected<std::filesystem::path, CommandError> program = programOf(arguments);
    if (!program)
    {
        return unexpected(std::move(program).error());
    }
    OpenSourceResult result{.program = detail::pathText(*program),
                            .arguments = std::vector<std::string>(arguments.begin() + 1, arguments.end())};
    if (Expected<void, CommandError> started = spawnDetached(context, *program, result.arguments); !started)
    {
        return unexpected(std::move(started).error());
    }
    return result;
}

Expected<RevealResult, CommandError> editorReveal(CommandContext& context, const RevealParams& params)
{
    if (params.path.empty())
    {
        return unexpected(CommandError::invalid("/path", "must name a file or folder"));
    }
    std::filesystem::path file;
    if (const usize mount = params.path.find(":/"); mount != std::string::npos && mount > 1)
    {
        if (context.editor().files().resolve(params.path, file) != FileError::None)
        {
            return unexpected(
                CommandError::invalid("/path", "must be a file of a project mount or an absolute path"));
        }
    }
    else
    {
        file = std::filesystem::path(std::u8string(params.path.begin(), params.path.end()));
        if (!file.is_absolute())
        {
            return unexpected(CommandError::invalid("/path",
                                                    "must be a virtual path such as "
                                                    "\"assets:/models/ship.glb\" or an absolute path"));
        }
    }
    std::error_code code;
    const bool isDirectory = std::filesystem::is_directory(file, code);
    if (!isDirectory && !std::filesystem::is_regular_file(file, code))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "'" + params.path + "' does not exist"));
    }
    file = file.make_preferred();
    const std::string fileText = detail::pathText(file);
    const std::string& browser = context.editor().preferences().fileBrowser;
    Expected<std::vector<std::string>, std::string> parsed = revealArguments(browser, fileText, isDirectory);
    if (!parsed)
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed, "preferences: " + parsed.error()));
    }
    Expected<std::filesystem::path, CommandError> program = programOf(*parsed);
    if (!program)
    {
        return unexpected(std::move(program).error());
    }
    RevealResult result{.file = fileText,
                        .program = detail::pathText(*program),
                        .arguments = std::vector<std::string>(parsed->begin() + 1, parsed->end())};
    if (Expected<void, CommandError> started = spawnDetached(context, *program, result.arguments); !started)
    {
        return unexpected(std::move(started).error());
    }
    return result;
}

} // namespace

void registerToolCommands(CommandRegistry& registry)
{
    registry.add<&editorOpenSource>({
        .name = "editor.open_source",
        .title = strings::kCommandEditorOpenSourceTitle,
        .description = strings::kCommandEditorOpenSourceDescription,
        .agentSummary =
            "Opens a file at a line and column in the person's code editor (the codeEditor preference, a "
            "command line with {file}, {line} and {column}), or with the system's default application when "
            "none is set. Takes a virtual path or an absolute path; returns the program and arguments it "
            "started. Example: {\"path\": \"modules:/tetris/tetris.cpp\", \"line\": 42, \"column\": 9}.",
        .category = "Editor",
        .icon = "code",
        .flags = {.idempotent = true},
        .permission = PermissionClass::System,
    });
    registry.add<&editorReveal>({
        .name = "editor.reveal",
        .title = strings::kCommandEditorRevealTitle,
        .description = strings::kCommandEditorRevealDescription,
        .agentSummary =
            "Reveals a file or folder in the system's file manager, with the file selected where the "
            "platform can (the fileBrowser preference, a command line with {file}; the system default when "
            "none is set). Takes a virtual path or an absolute path; returns the file, the program and the "
            "arguments it started. Example: {\"path\": \"assets:/models/ship.glb\"}.",
        .category = "Editor",
        .icon = "folder_open",
        .flags = {.idempotent = true},
        .permission = PermissionClass::System,
    });
}

} // namespace illusion::editor::detail

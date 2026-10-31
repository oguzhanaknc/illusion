// Commands of project modules built by the editor (ADR 0043): module.create, module.build and module.reload.

#include "../PathText.hpp"
#include "CommandSupport.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/editor/core/ModuleBuild.hpp>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

constexpr usize kMaxModuleName = 48;
constexpr usize kMaxDiagnostics = 200;
constexpr std::string_view kModulesRoot = "modules:/CMakeLists.txt";

// Posts "module.migrated" for each module in @p names whose last reload migrated Components, with the
// module's name, the stable names of the migrated types and their number of Components (ADR 0044).
void postMigrations(EditorContext& editor, PlayHost& play, std::span<const std::string> names)
{
    std::vector<PlayModuleInfo> modules;
    play.modules(modules);
    for (const PlayModuleInfo& module : modules)
    {
        if (module.migrated.empty() || std::find(names.begin(), names.end(), module.name) == names.end())
        {
            continue;
        }
        // Module names, stable names and C++ type names hold no quotes or backslashes: nothing to escape.
        std::string payload = "{\"module\":\"" + module.name + "\",\"types\":[";
        u32 components = 0;
        for (const PlayComponentMigration& migration : module.migrated)
        {
            payload += payload.back() == '[' ? "\"" : ",\"";
            payload += migration.type + "\"";
            components += migration.components;
        }
        payload += "],\"components\":" + std::to_string(components) + "}";
        editor.events().post("module.migrated", std::move(payload));
    }
}

// The module's directory name doubles as its CMake target and library name.
bool isModuleName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > kMaxModuleName || name[0] < 'a' || name[0] > 'z')
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::string modulesRootFile()
{
    return "# Game modules of this project, built by the editor with the engine's module SDK (ADR 0043).\n"
           "# Every subdirectory with a CMakeLists.txt is one module.\n"
           "cmake_minimum_required(VERSION 3.25)\n"
           "project(illusion_project_modules LANGUAGES CXX)\n"
           "\n"
           "find_package(illusion REQUIRED CONFIG)\n"
           "illusion_project_modules()\n";
}

std::string moduleListFile(std::string_view name)
{
    std::string text = "# The game module ";
    text += name;
    text += ". USES gives it the headers and Components of engine libraries; their code runs in the editor\n"
            "# and is reached through the host's services (see the scripting API reference).\n";
    text += "illusion_add_module(";
    text += name;
    text += " SHARED SOURCES ";
    text += name;
    text += ".cpp USES physics audio input render runtime)\n";
    return text;
}

std::string moduleSourceFile(std::string_view name)
{
    std::string text = "// The game module ";
    text += name;
    text +=
        ".\n"
        "\n"
        "#include <illusion/core/Module.hpp>\n"
        "\n"
        "namespace\n"
        "{\n"
        "\n"
        "// Register and reflect Component types here. There is no World yet: spawn entities in start().\n"
        "bool load(illusion::ModuleContext&)\n"
        "{\n"
        "    return true;\n"
        "}\n"
        "\n"
        "// Once per play session: spawn the game's entities.\n"
        "void start(illusion::ModuleContext&) {}\n"
        "\n"
        "// Every frame of a play session. Keep all game state in Components; it survives hot reloads.\n"
        "void update(illusion::ModuleContext&, illusion::f64) {}\n"
        "\n"
        "constexpr illusion::ModuleApi kModule =\n"
        "    illusion::makeModuleApi(\"";
    text += name;
    text += "\", \"0.1.0\", {.load = &load, .start = &start, .update = &update});\n"
            "\n"
            "} // namespace\n"
            "\n"
            "ILL_MODULE(";
    text += name;
    text += ", kModule)\n";
    return text;
}

Expected<ModuleCreateResult, CommandError> moduleCreate(CommandContext& context,
                                                        const ModuleCreateParams& params)
{
    if (Expected<void, CommandError> project = requireProject(context); !project)
    {
        return unexpected(std::move(project).error());
    }
    if (!isModuleName(params.name))
    {
        return unexpected(CommandError::invalid(
            "/name", "must be 1 to 48 lower case letters, digits and underscores, starting with a letter"));
    }
    const std::string directory = "modules:/" + params.name;
    if (context.editor().files().exists(directory))
    {
        return unexpected(
            CommandError::make(ErrorKind::AlreadyExists, "module directory '" + directory + "' exists"));
    }
    ModuleCreateResult result{.directory = directory};
    if (!context.editor().files().exists(kModulesRoot))
    {
        if (Expected<bool, CommandError> written = context.edit().writeFile(kModulesRoot, modulesRootFile());
            !written)
        {
            return unexpected(std::move(written).error());
        }
        result.files.emplace_back(kModulesRoot);
    }
    const std::string list = directory + "/CMakeLists.txt";
    const std::string source = directory + "/" + params.name + ".cpp";
    for (const auto& [path, text] :
         {std::pair{list, moduleListFile(params.name)}, std::pair{source, moduleSourceFile(params.name)}})
    {
        if (Expected<bool, CommandError> written = context.edit().writeFile(path, text); !written)
        {
            return unexpected(std::move(written).error());
        }
        result.files.push_back(path);
    }
    return result;
}

std::string pathFileText(const std::filesystem::path& path)
{
    return detail::pathText(path.lexically_normal().generic_u8string());
}

// A virtual path for files inside a project mount; other files keep the text the tool printed.
std::string virtualFile(const std::string& printed, const Project& project)
{
    if (printed.empty())
    {
        return printed;
    }
    const std::filesystem::path file(std::u8string(printed.begin(), printed.end()));
    if (!file.is_absolute())
    {
        return printed;
    }
    for (const ProjectMount& mount : projectMounts())
    {
        const std::filesystem::path root = (project.root() / std::string(mount.relative)).lexically_normal();
        const std::filesystem::path relative = file.lexically_normal().lexically_relative(root);
        if (!relative.empty() && *relative.begin() != "..")
        {
            return std::string(mount.name) + ":/" + pathFileText(relative);
        }
    }
    return pathFileText(file);
}

LogLevel logLevelOf(DiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
    case DiagnosticSeverity::Error:
        return LogLevel::Error;
    case DiagnosticSeverity::Warning:
        return LogLevel::Warn;
    case DiagnosticSeverity::Note:
        return LogLevel::Info;
    }
    return LogLevel::Error;
}

void appendModules(PlayHost* play, std::vector<PlayModuleInfo>& out)
{
    if (play != nullptr)
    {
        play->modules(out);
    }
}

class ModuleBuildTask final : public CommandTask<ModuleBuildResult>
{
public:
    ModuleBuildTask(ModuleBuildOptions options, bool load)
        : m_options(std::move(options))
        , m_load(load)
    {
    }

    ~ModuleBuildTask() override
    {
        if (m_editor != nullptr)
        {
            m_editor->endModuleBuild();
        }
    }

    ModuleBuildTask(const ModuleBuildTask&) = delete;
    ModuleBuildTask& operator=(const ModuleBuildTask&) = delete;

    Expected<std::optional<ModuleBuildResult>, CommandError> update(CommandContext& context,
                                                                    CommandProgress& progress) override
    {
        if (!m_build)
        {
            if (Expected<void, CommandError> started = begin(context); !started)
            {
                return unexpected(std::move(started).error());
            }
        }
        if (!m_build->update())
        {
            const BuildProgress done = m_build->progress();
            progress.done = done.done;
            progress.total = done.total;
            progress.message = m_build->stage() == ModuleBuild::Stage::Configure ? "configuring" : "building";
            return std::optional<ModuleBuildResult>{};
        }
        return finish(context);
    }

private:
    Expected<void, CommandError> begin(CommandContext& context)
    {
        if (!context.editor().beginModuleBuild())
        {
            CommandError error = CommandError::make(ErrorKind::Busy, "another module build is running");
            error.retryable = true;
            return unexpected(std::move(error));
        }
        m_editor = &context.editor();
        std::error_code code;
        std::filesystem::create_directories(m_options.outputDirectory, code);
        Expected<std::unique_ptr<ModuleBuild>, std::string> build = ModuleBuild::start(m_options);
        if (!build)
        {
            return unexpected(CommandError::make(ErrorKind::BuildFailed, std::move(build).error()));
        }
        m_build = std::move(*build);
        ILL_LOG_INFO("Build", "Building modules in {}", detail::pathText(m_options.sourceDirectory));
        return {};
    }

    Expected<std::optional<ModuleBuildResult>, CommandError> finish(CommandContext& context)
    {
        const Project* project = context.editor().project();
        ModuleBuildResult result{.succeeded = m_build->succeeded(),
                                 .configured = m_build->configured(),
                                 .output = m_build->outputTail()};
        for (const BuildDiagnostic& diagnostic : m_build->diagnostics())
        {
            const std::string file =
                project != nullptr ? virtualFile(diagnostic.file, *project) : diagnostic.file;
            result.errors += diagnostic.severity == DiagnosticSeverity::Error ? 1u : 0u;
            result.warnings += diagnostic.severity == DiagnosticSeverity::Warning ? 1u : 0u;
            Logger::instance().write(logLevelOf(diagnostic.severity), "Build", diagnostic.message,
                                     file.empty() ? nullptr : file.c_str(), diagnostic.line);
            if (result.diagnostics.size() < kMaxDiagnostics)
            {
                result.diagnostics.push_back(
                    ModuleDiagnostic{file, diagnostic.line, diagnostic.column,
                                     std::string(diagnosticSeverityName(diagnostic.severity)),
                                     diagnostic.code, diagnostic.message});
            }
        }
        PlayHost* play = context.editor().play();
        if (result.succeeded)
        {
            ILL_LOG_INFO("Build", "Modules built ({} warnings)", result.warnings);
            for (const std::filesystem::path& library : m_build->libraries())
            {
                result.libraries.push_back(detail::pathText(library));
            }
            if (m_load && play != nullptr)
            {
                if (Expected<void, CommandError> loaded = loadLibraries(*play, result); !loaded)
                {
                    return unexpected(std::move(loaded).error());
                }
                postMigrations(context.editor(), *play, result.reloaded);
            }
        }
        else
        {
            ILL_LOG_ERROR("Build", "Module build failed ({} errors)", result.errors);
        }
        appendModules(play, result.modules);
        return std::optional<ModuleBuildResult>(std::move(result));
    }

    // Loads each built library that is not loaded yet and hot-reloads the modules loaded from the others.
    static Expected<void, CommandError> loadLibraries(PlayHost& play, ModuleBuildResult& result)
    {
        std::vector<PlayModuleInfo> modules;
        play.modules(modules);
        for (const std::string& library : result.libraries)
        {
            const std::filesystem::path path(std::u8string(library.begin(), library.end()));
            const auto loaded =
                std::find_if(modules.begin(), modules.end(), [&](const PlayModuleInfo& module) {
                    const std::filesystem::path from(std::u8string(module.path.begin(), module.path.end()));
                    return module.shared && from.lexically_normal() == path.lexically_normal();
                });
            if (loaded != modules.end())
            {
                if (Expected<void, CommandError> reloaded = play.reloadModule(loaded->name); !reloaded)
                {
                    return unexpected(std::move(reloaded).error());
                }
                result.reloaded.push_back(loaded->name);
                continue;
            }
            Expected<std::string, CommandError> name = play.loadModule(library);
            if (!name)
            {
                return unexpected(std::move(name).error());
            }
            result.loaded.push_back(std::move(*name));
        }
        return {};
    }

    ModuleBuildOptions m_options;
    bool m_load = true;
    EditorContext* m_editor = nullptr;
    std::unique_ptr<ModuleBuild> m_build;
};

Expected<std::unique_ptr<CommandTask<ModuleBuildResult>>, CommandError>
moduleBuild(CommandContext& context, const ModuleBuildParams& params)
{
    if (Expected<void, CommandError> project = requireProject(context); !project)
    {
        return unexpected(std::move(project).error());
    }
    const std::filesystem::path& sdk = context.editor().options().moduleSdk;
    if (sdk.empty())
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no module SDK"));
    }
    std::vector<std::string> targets = params.modules.value_or(std::vector<std::string>{});
    for (usize i = 0; i < targets.size(); ++i)
    {
        const std::string pointer = "/modules/" + std::to_string(i);
        if (!isModuleName(targets[i]))
        {
            return unexpected(CommandError::invalid(pointer, "must be a module name"));
        }
        if (!context.editor().files().exists("modules:/" + targets[i] + "/CMakeLists.txt"))
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "no module '" + targets[i] + "' in modules:/"));
        }
    }
    if (!context.editor().files().exists(kModulesRoot))
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the project has no modules:/CMakeLists.txt; create a module "
                                             "with module.create first"));
    }
    Expected<ModuleToolchain, std::string> toolchain = readModuleToolchain(sdk);
    if (!toolchain)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "module SDK: " + toolchain.error()));
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    const std::filesystem::path root = context.editor().project()->root();
    const std::filesystem::path build = root / ".illusion" / "build" / "modules";
    ModuleBuildOptions options{.toolchain = std::move(*toolchain),
                               .sourceDirectory = root / "modules",
                               .buildDirectory = build,
                               .outputDirectory = build / "bin",
                               .targets = std::move(targets),
                               .reconfigure = params.reconfigure.value_or(false)};
    return std::unique_ptr<CommandTask<ModuleBuildResult>>(
        new ModuleBuildTask(std::move(options), params.load.value_or(true)));
}

Expected<ModuleReloadResult, CommandError> moduleReload(CommandContext& context,
                                                        const ModuleReloadParams& params)
{
    PlayHost* play = context.editor().play();
    if (play == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "this editor has no game runtime"));
    }
    ModuleReloadResult result;
    std::vector<PlayModuleInfo> before;
    play->modules(before);
    if (params.name)
    {
        const auto found = std::find_if(before.begin(), before.end(), [&](const PlayModuleInfo& module) {
            return module.name == *params.name;
        });
        if (found == before.end())
        {
            return unexpected(
                CommandError::make(ErrorKind::NotFound, "no module '" + *params.name + "' is loaded"));
        }
        if (!found->shared)
        {
            return unexpected(
                CommandError::make(ErrorKind::PreconditionFailed,
                                   "module '" + *params.name + "' is static and cannot be reloaded"));
        }
    }
    if (context.dryRun())
    {
        result.modules = std::move(before);
        return result;
    }
    if (params.name)
    {
        if (Expected<void, CommandError> reloaded = play->reloadModule(*params.name); !reloaded)
        {
            return unexpected(std::move(reloaded).error());
        }
    }
    else
    {
        static_cast<void>(play->reloadChangedModules());
    }
    play->modules(result.modules);
    for (const PlayModuleInfo& module : result.modules)
    {
        const auto old = std::find_if(before.begin(), before.end(),
                                      [&](const PlayModuleInfo& entry) { return entry.name == module.name; });
        if (old != before.end() && old->reloads != module.reloads)
        {
            result.reloaded.push_back(module.name);
        }
    }
    postMigrations(context.editor(), *play, result.reloaded);
    return result;
}

} // namespace

void registerModuleCommands(CommandRegistry& registry)
{
    registry.add<&moduleCreate>({
        .name = "module.create",
        .title = strings::kCommandModuleCreateTitle,
        .description = strings::kCommandModuleCreateDescription,
        .agentSummary =
            "Creates a game module in the project: modules:/<name>/CMakeLists.txt and "
            "modules:/<name>/<name>.cpp "
            "with empty load, start and update functions, plus modules:/CMakeLists.txt when missing. Edit "
            "the "
            "source with the file commands, then run module.build. Example: {\"name\": \"tetris\"}.",
        .category = "Module",
        .icon = "add_box",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
        .palette = true,
    });
    registry.addAsync<&moduleBuild>({
        .name = "module.build",
        .title = strings::kCommandModuleBuildTitle,
        .description = strings::kCommandModuleBuildDescription,
        .agentSummary =
            "Asynchronous: compiles the project's game modules with CMake and the editor's compiler, then "
            "loads new modules and hot-reloads loaded ones (load: false skips that). A failed compile is not "
            "an error: the result has succeeded: false and diagnostics with file (e.g. "
            "\"modules:/tetris/tetris.cpp\"), line, column, severity, code and message. The first build "
            "configures CMake and takes longer. Example: {\"modules\": [\"tetris\"]}.",
        .category = "Module",
        .icon = "build",
        .permission = PermissionClass::Build,
        .palette = true,
        .defaultShortcut = "Ctrl+B",
    });
    registry.add<&moduleReload>({
        .name = "module.reload",
        .title = strings::kCommandModuleReloadTitle,
        .description = strings::kCommandModuleReloadDescription,
        .agentSummary =
            "Hot-reloads the named game module from its library, or every module whose library file changed "
            "when no name is given; Component values in the Worlds are kept. When the new code changed the "
            "fields of a reflected Component, its Components are migrated by field name: modules[].migrated "
            "lists each type with its addedFields, removedFields and resetFields (values that did not fit), "
            "and the module.migrated event is posted. Example: {\"name\": \"tetris\"}.",
        .category = "Module",
        .icon = "refresh",
        .permission = PermissionClass::Build,
        .palette = true,
        .defaultShortcut = "Ctrl+R",
    });
}

} // namespace illusion::editor::detail

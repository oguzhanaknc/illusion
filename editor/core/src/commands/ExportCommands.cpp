// The command that exports the project as a game (ADR 0055): export.build.

#include "../PathText.hpp"
#include "CommandSupport.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/editor/core/Export.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

constexpr usize kMaxConfigurationName = 32;

std::string_view platformDirectory() noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    return "windows";
#elif defined(ILL_PLATFORM_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

bool isConfigurationName(std::string_view name) noexcept
{
    return !name.empty() && name.size() <= kMaxConfigurationName &&
           ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) &&
           std::all_of(name.begin(), name.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
           });
}

// A configuration an export can build: the SDK has the player and illusion_pack for it.
bool exportable(const ModuleToolchain& toolchain)
{
    std::error_code code;
    return toolchain.player && !toolchain.pack.empty() &&
           std::filesystem::is_regular_file(toolchain.pack, code);
}

// The SDK description of @p configuration: the editor's own, or `<sdk>/<Config>/illusion-sdk.json` next to
// it.
std::optional<ModuleToolchain> toolchainOf(const std::filesystem::path& sdk, const ModuleToolchain& own,
                                           std::string_view configuration)
{
    if (configuration == own.config)
    {
        return exportable(own) ? std::optional<ModuleToolchain>(own) : std::nullopt;
    }
    const std::filesystem::path file =
        sdk.parent_path().parent_path() / std::string(configuration) / sdk.filename();
    std::error_code code;
    if (!std::filesystem::is_regular_file(file, code))
    {
        return std::nullopt;
    }
    Expected<ModuleToolchain, std::string> toolchain = readModuleToolchain(file);
    if (!toolchain || toolchain->config != configuration || !exportable(*toolchain))
    {
        return std::nullopt;
    }
    return std::move(*toolchain);
}

// The configurations exports can build, sorted, for error messages.
std::vector<std::string> availableConfigurations(const std::filesystem::path& sdk, const ModuleToolchain& own)
{
    std::vector<std::string> names;
    if (exportable(own))
    {
        names.push_back(own.config);
    }
    std::error_code code;
    for (std::filesystem::directory_iterator it(sdk.parent_path().parent_path(), code), end;
         !code && it != end; it.increment(code))
    {
        const std::string name = pathText(it->path().filename());
        if (name != own.config && isConfigurationName(name) && it->is_directory(code) &&
            toolchainOf(sdk, own, name))
        {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

Expected<ModuleToolchain, CommandError> exportToolchain(const std::filesystem::path& sdk,
                                                        const std::optional<std::string>& configuration)
{
    Expected<ModuleToolchain, std::string> own = readModuleToolchain(sdk);
    if (!own)
    {
        return unexpected(CommandError::make(ErrorKind::Unsupported, "SDK: " + own.error()));
    }
    if (configuration && !isConfigurationName(*configuration))
    {
        return unexpected(CommandError::invalid(
            "/configuration", "must be a build configuration name such as \"Release\" or \"Debug\""));
    }
    std::optional<ModuleToolchain> found;
    if (configuration)
    {
        found = toolchainOf(sdk, *own, *configuration);
    }
    else
    {
        found = toolchainOf(sdk, *own, "Release");
        if (!found)
        {
            found = toolchainOf(sdk, *own, own->config);
        }
    }
    if (found)
    {
        return std::move(*found);
    }
    const std::vector<std::string> available = availableConfigurations(sdk, *own);
    std::string list;
    for (const std::string& name : available)
    {
        list += (list.empty() ? "" : ", ") + name;
    }
    const std::string wanted = configuration.value_or("Release or " + own->config);
    return unexpected(CommandError::make(
        ErrorKind::NotFound,
        "the SDK has no configuration " + wanted + " with the player and illusion_pack (" +
            (available.empty() ? std::string("none has them") : "available: " + list) + ")"));
}

Expected<ExportPackage, CommandError> packageOf(const std::optional<std::string>& name)
{
    for (const ExportPackage package :
         {ExportPackage::None, ExportPackage::Archive, ExportPackage::Installer})
    {
        if (name.value_or("none") == exportPackageName(package))
        {
            if (package != ExportPackage::None)
            {
                return unexpected(
                    CommandError::invalid("/package", "packages are not supported yet; use \"none\""));
            }
            return package;
        }
    }
    return unexpected(CommandError::invalid("/package", "must be \"none\", \"archive\" or \"installer\""));
}

class ExportBuildTask final : public CommandTask<ExportBuildResult>
{
public:
    explicit ExportBuildTask(std::unique_ptr<ProjectExport> exported)
        : m_export(std::move(exported))
    {
    }

    ~ExportBuildTask() override
    {
        m_export.reset();
        if (m_editor != nullptr)
        {
            m_editor->endModuleBuild();
        }
    }

    ExportBuildTask(const ExportBuildTask&) = delete;
    ExportBuildTask& operator=(const ExportBuildTask&) = delete;

    Expected<std::optional<ExportBuildResult>, CommandError> update(CommandContext& context,
                                                                    CommandProgress& progress) override
    {
        if (m_editor == nullptr)
        {
            // Exports and module builds share the SDK's environment and compile with all cores.
            if (!context.editor().beginModuleBuild())
            {
                CommandError error =
                    CommandError::make(ErrorKind::Busy, "a module build or another export is running");
                error.retryable = true;
                return unexpected(std::move(error));
            }
            m_editor = &context.editor();
            ILL_LOG_INFO("Export", "Exporting the project");
        }
        const ProjectExport::Stage stage = m_export->stage();
        if (!m_export->update())
        {
            progress.done = m_export->progress();
            progress.total = 1.0;
            progress.message = std::string(exportStageName(m_export->stage()));
            return std::optional<ExportBuildResult>{};
        }
        return std::optional<ExportBuildResult>(finish(context, stage));
    }

private:
    ExportBuildResult finish(CommandContext& context, ProjectExport::Stage last) const
    {
        const ExportReport& report = m_export->report();
        ExportBuildResult result{
            .succeeded = m_export->succeeded(),
            .stage = std::string(exportStageName(m_export->succeeded() ? ProjectExport::Stage::Done : last)),
            .error = m_export->error(),
            .platform = report.platform,
            .configuration = report.configuration,
            .directory = pathText(report.directory),
            .package = pathText(report.package),
            .files = report.files,
            .totalBytes = report.totalBytes,
            .signedBinaries = report.signedBinaries,
            .notarized = report.notarized,
            .skipped = report.skipped,
            .output = m_export->outputTail()};
        reportDiagnostics(m_export->diagnostics(), context.editor().project(), result.diagnostics,
                          result.errors, result.warnings);
        if (result.succeeded)
        {
            ILL_LOG_INFO("Export", "Exported {} ({} files, {} bytes)", result.directory, result.files.size(),
                         result.totalBytes);
        }
        else
        {
            ILL_LOG_ERROR("Export", "Export failed: {}", result.error);
        }
        return result;
    }

    std::unique_ptr<ProjectExport> m_export;
    EditorContext* m_editor = nullptr;
};

Expected<std::unique_ptr<CommandTask<ExportBuildResult>>, CommandError>
exportBuild(CommandContext& context, const ExportBuildParams& params)
{
    if (Expected<void, CommandError> project = requireProject(context); !project)
    {
        return unexpected(std::move(project).error());
    }
    const std::filesystem::path& sdk = context.editor().options().moduleSdk;
    if (sdk.empty())
    {
        return unexpected(
            CommandError::make(ErrorKind::Unsupported, "this editor has no SDK to export with"));
    }
    Expected<ExportPackage, CommandError> package = packageOf(params.package);
    if (!package)
    {
        return unexpected(std::move(package).error());
    }
    for (const auto& [pointer, value] :
         {std::pair{"/linuxRuntime", &params.linuxRuntime}, std::pair{"/signCommand", &params.signCommand},
          std::pair{"/codesignIdentity", &params.codesignIdentity},
          std::pair{"/notaryProfile", &params.notaryProfile}})
    {
        if (value->has_value() && !(*value)->empty())
        {
            return unexpected(CommandError::invalid(pointer, "is not supported yet; leave it empty"));
        }
    }
    const std::filesystem::path root = context.editor().project()->root();
    std::filesystem::path output = root / ".illusion" / "export" / std::string(platformDirectory()) / "out";
    if (params.output)
    {
        const std::string& text = *params.output;
        if (text.empty() || std::any_of(text.begin(), text.end(),
                                        [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
        {
            return unexpected(CommandError::invalid("/output", "must be a directory path"));
        }
        const std::filesystem::path path = std::filesystem::path(std::u8string(text.begin(), text.end()));
        output = (path.is_absolute() ? path : root / path).lexically_normal();
    }
    Expected<ModuleToolchain, CommandError> toolchain = exportToolchain(sdk, params.configuration);
    if (!toolchain)
    {
        return unexpected(std::move(toolchain).error());
    }
    Expected<std::unique_ptr<ProjectExport>, std::string> started =
        ProjectExport::start(ExportOptions{.toolchain = std::move(*toolchain),
                                           .projectRoot = root,
                                           .outputDirectory = std::move(output),
                                           .package = *package,
                                           .clean = params.clean.value_or(false)});
    if (!started)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, std::move(started).error()));
    }
    return std::unique_ptr<CommandTask<ExportBuildResult>>(new ExportBuildTask(std::move(*started)));
}

} // namespace

void registerExportCommands(CommandRegistry& registry)
{
    registry.addAsync<&exportBuild>({
        .name = "export.build",
        .title = strings::kCommandExportBuildTitle,
        .description = strings::kCommandExportBuildDescription,
        .agentSummary =
            "Asynchronous: exports the project as a game that runs without the editor. Cooks every asset "
            "under "
            "assets:/ into a fresh cache, packs data/assets.illpak (without glTF sources the cache replaces "
            "and "
            "files matching the exclude patterns of settings:/export.json), data/settings.illpak and "
            "data/cache.illpak, builds an executable from the project's modules and the SDK's player with "
            "CMake, and installs <output>/<Product>/<product>[.exe] with its data/ directory. Validation "
            "errors "
            "(no startup scene, a module without sources, a bad product name) fail the command; a failed "
            "cook "
            "or compile does not: the result has succeeded: false, the failed stage and diagnostics with "
            "file "
            "and line, and no partial game is left in the output. The first export configures CMake and "
            "takes "
            "minutes. Example: {\"output\": \"C:/Games/out\", \"configuration\": \"Release\"}.",
        .category = "Export",
        .icon = "rocket_launch",
        .permission = PermissionClass::Build,
        .palette = true,
    });
}

} // namespace illusion::editor::detail

// Exports of a project as a game (ADR 0055): validation, the generated CMake project, cooking and packing
// with the SDK's illusion_pack, and CMake's configure, build and install. Every step but the generation is a
// child process, so update() never blocks.

#include "PathText.hpp"
#include "ToolProcess.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/SettingsFile.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/editor/core/Export.hpp>
#include <illusion/editor/core/Project.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace illusion::editor
{

using detail::commandText;
using detail::drainLines;
using detail::outputDrained;
using detail::pathFromUtf8;
using detail::pathText;
using detail::ToolOutput;

namespace
{

constexpr usize kMaxName = 64;
constexpr usize kMaxModuleName = 48;
constexpr std::string_view kExportSettingsName = "illusion.ExportSettings";
constexpr std::array<std::string_view, 3> kArchives{"assets.illpak", "settings.illpak", "cache.illpak"};

// Share of the whole export when each stage begins; Build is weighted by the build's own progress.
constexpr std::array<f64, 9> kStageStart{0.0, 0.0, 0.02, 0.17, 0.27, 0.40, 0.90, 1.0, 1.0};

std::string_view platformName() noexcept
{
#if defined(ILL_PLATFORM_WINDOWS)
    return "windows";
#elif defined(ILL_PLATFORM_MACOS)
    return "macos";
#else
    return "linux";
#endif
}

std::string relativeText(const std::filesystem::path& path, const std::filesystem::path& base)
{
    return pathText(path.lexically_relative(base).generic_u8string());
}

std::optional<std::string> readText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

// Writes @p text unless the file already holds it, so that an unchanged build does not configure again.
bool writeIfChanged(const std::filesystem::path& path, const std::string& text)
{
    if (readText(path) == text)
    {
        return true;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    return out.good();
}

bool isAsciiLetter(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAsciiAlnum(char c) noexcept
{
    return isAsciiLetter(c) || (c >= '0' && c <= '9');
}

bool hasControl(std::string_view text) noexcept
{
    return std::any_of(text.begin(), text.end(),
                       [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; });
}

// Device names Windows reserves in every directory, with or without an extension.
bool isReservedName(std::string_view name) noexcept
{
    std::string stem(name.substr(0, name.find('.')));
    std::transform(stem.begin(), stem.end(), stem.begin(),
                   [](char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
    {
        return true;
    }
    return stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '1' &&
           stem[3] <= '9';
}

// A name that is a directory name on every platform and safe in CMake and C++ string literals.
bool isPortableName(std::string_view name) noexcept
{
    constexpr std::string_view kForbidden = "<>:\"/\\|?*;$";
    return !name.empty() && name.size() <= kMaxName && !hasControl(name) &&
           name.find_first_of(kForbidden) == std::string_view::npos && name.front() != ' ' &&
           name.back() != ' ' && name.back() != '.' && !isReservedName(name);
}

// The executable's file name: the product name in lower case, other characters joined into underscores.
std::string executableName(std::string_view product)
{
    std::string name;
    for (const char c : product)
    {
        if (isAsciiAlnum(c))
        {
            name += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }
        else if (!name.empty() && name.back() != '_')
        {
            name += '_';
        }
    }
    while (!name.empty() && name.back() == '_')
    {
        name.pop_back();
    }
    return name;
}

bool isModuleName(std::string_view name) noexcept
{
    return !name.empty() && name.size() <= kMaxModuleName && name[0] >= 'a' && name[0] <= 'z' &&
           std::all_of(name.begin(), name.end(),
                       [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool isIdentifier(std::string_view text) noexcept
{
    usize parts = 0;
    while (true)
    {
        const usize dot = text.find('.');
        const std::string_view part = text.substr(0, dot);
        if (part.empty() ||
            !std::all_of(part.begin(), part.end(), [](char c) { return isAsciiAlnum(c) || c == '-'; }))
        {
            return false;
        }
        ++parts;
        if (dot == std::string_view::npos)
        {
            return parts >= 2;
        }
        text.remove_prefix(dot + 1);
    }
}

// `*` matches a run of characters without '/', `**` any run, and `**/` also no directory at all.
bool matchPattern(std::string_view pattern, std::string_view path) noexcept
{
    while (!pattern.empty())
    {
        if (pattern.starts_with("**"))
        {
            std::string_view rest = pattern.substr(2);
            if (rest.starts_with('/') && matchPattern(rest.substr(1), path))
            {
                return true;
            }
            for (usize i = 0; i <= path.size(); ++i)
            {
                if (matchPattern(rest, path.substr(i)))
                {
                    return true;
                }
            }
            return false;
        }
        if (pattern.front() == '*')
        {
            const std::string_view rest = pattern.substr(1);
            for (usize i = 0; i <= path.size(); ++i)
            {
                if (matchPattern(rest, path.substr(i)))
                {
                    return true;
                }
                if (i < path.size() && path[i] == '/')
                {
                    return false;
                }
            }
            return false;
        }
        if (path.empty() || path.front() != pattern.front())
        {
            return false;
        }
        pattern.remove_prefix(1);
        path.remove_prefix(1);
    }
    return path.empty();
}

bool excluded(const std::vector<std::string>& patterns, std::string_view path) noexcept
{
    return std::any_of(patterns.begin(), patterns.end(),
                       [&](const std::string& pattern) { return matchPattern(pattern, path); });
}

std::string lowerExtension(std::string_view name)
{
    const usize dot = name.rfind('.');
    std::string extension(dot == std::string_view::npos ? std::string_view() : name.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return extension;
}

// Every regular file below @p root as a path relative to it, sorted.
std::vector<std::string> filesBelow(const std::filesystem::path& root)
{
    std::vector<std::string> files;
    std::error_code code;
    for (std::filesystem::recursive_directory_iterator it(root, code), end; !code && it != end;
         it.increment(code))
    {
        if (it->is_regular_file(code))
        {
            files.push_back(relativeText(it->path(), root));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// The external buffers a glTF file names (relative to the assets directory): the cooked model replaces them.
void addGltfBuffers(const std::filesystem::path& assets, const std::string& gltf, std::set<std::string>& out)
{
    const std::optional<std::string> text = readText(assets / pathFromUtf8(gltf));
    if (!text)
    {
        return;
    }
    Expected<JsonDocument, JsonError> json = JsonDocument::parse(*text);
    if (!json)
    {
        return;
    }
    const std::filesystem::path directory = pathFromUtf8(gltf).parent_path();
    for (const JsonValue buffer : json->root().find("buffers").elements())
    {
        const std::string_view uri = buffer.find("uri").asString().value_or("");
        // Percent-encoded names are shipped as they are rather than decoded here.
        if (uri.empty() || uri.starts_with("data:") || uri.find_first_of("%:") != std::string_view::npos)
        {
            continue;
        }
        const std::filesystem::path file = (directory / pathFromUtf8(uri)).lexically_normal();
        if (!file.empty() && *file.begin() != "..")
        {
            out.insert(pathText(file.generic_u8string()));
        }
    }
}

// A CMake bracket argument holding @p text unchanged.
std::string bracket(std::string_view text)
{
    std::string equals = "=";
    while (text.find("]" + equals + "]") != std::string_view::npos)
    {
        equals += '=';
    }
    return "[" + equals + "[" + std::string(text) + "]" + equals + "]";
}

struct ExportPlan
{
    std::string product;    // directory of the game
    std::string executable; // file name without extension
    std::string organization;
    std::string version;
    std::string startupScene;
    std::vector<std::string> modules;
    std::vector<std::string> exclude;
};

Expected<ExportSettings, std::string> readExportSettings(const std::filesystem::path& root)
{
    ExportSettings settings;
    const std::filesystem::path file = root / "settings" / "export.json";
    std::error_code code;
    if (!std::filesystem::exists(file, code))
    {
        return settings;
    }
    const std::optional<std::string> text = readText(file);
    if (!text)
    {
        return unexpected(std::string("cannot read settings:/export.json"));
    }
    ComponentRegistry& registry = ComponentRegistry::instance();
    if (registry.structs().findByStableName(kExportSettingsName) == kInvalidStructId)
    {
        reflectExportSettings(registry);
    }
    const StructReflection* type =
        registry.structs().reflection(registry.structs().findByStableName(kExportSettingsName));
    if (Expected<void, std::string> read = readSettingsFile(*type, *text, &settings); !read)
    {
        return unexpected("settings:/export.json: " + read.error());
    }
    return settings;
}

void checkOptions(const ExportOptions& options, std::vector<std::string>& errors)
{
    if (!options.toolchain.player)
    {
        errors.emplace_back("the SDK of configuration '" + options.toolchain.config +
                            "' has no player component");
    }
    std::error_code code;
    if (options.toolchain.pack.empty() || !std::filesystem::is_regular_file(options.toolchain.pack, code))
    {
        errors.emplace_back(
            "the SDK of configuration '" + options.toolchain.config + "' has no illusion_pack" +
            (options.toolchain.pack.empty() ? "" : " at " + pathText(options.toolchain.pack)));
    }
    if (options.package != ExportPackage::None)
    {
        errors.emplace_back("packages ('" + std::string(exportPackageName(options.package)) +
                            "') are not supported yet; export with package 'none'");
    }
    if (!options.signing.signCommand.empty() || !options.signing.codesignIdentity.empty() ||
        !options.signing.notaryProfile.empty())
    {
        errors.emplace_back("signing and notarization are not supported yet");
    }
    if (!options.linuxRuntime.empty())
    {
        errors.emplace_back("the Steam Runtime check (linuxRuntime) is not supported yet");
    }
    if (options.outputDirectory.empty() || !options.outputDirectory.is_absolute())
    {
        errors.emplace_back("the output directory must be an absolute path");
    }
    else
    {
        for (const std::string_view mount : {"assets", "settings", "modules"})
        {
            const std::filesystem::path relative =
                options.outputDirectory.lexically_normal().lexically_relative(
                    (options.projectRoot / std::string(mount)).lexically_normal());
            if (!relative.empty() && *relative.begin() != "..")
            {
                errors.emplace_back("the output directory must not be inside the project's " +
                                    std::string(mount) + " directory");
            }
        }
    }
}

// Every module of the project: those the manifest names and every directory of modules/ with a
// CMakeLists.txt.
void collectModules(const std::filesystem::path& root, const ProjectManifest& manifest, ExportPlan& plan,
                    std::vector<std::string>& errors)
{
    std::set<std::string> names(manifest.modules.begin(), manifest.modules.end());
    std::error_code code;
    for (std::filesystem::directory_iterator it(root / "modules", code), end; !code && it != end;
         it.increment(code))
    {
        if (it->is_directory(code) && std::filesystem::is_regular_file(it->path() / "CMakeLists.txt", code))
        {
            names.insert(pathText(it->path().filename()));
        }
    }
    for (const std::string& name : names)
    {
        if (!isModuleName(name))
        {
            errors.emplace_back("module '" + name +
                                "' is not a module name (lower case letters, digits and underscores)");
        }
        else if (!std::filesystem::is_regular_file(root / "modules" / name / "CMakeLists.txt", code))
        {
            errors.emplace_back("module '" + name + "' has no sources (modules:/" + name +
                                "/CMakeLists.txt)");
        }
        else
        {
            plan.modules.push_back(name);
        }
    }
}

Expected<ExportPlan, std::string> planExport(const ExportOptions& options)
{
    std::vector<std::string> errors;
    checkOptions(options, errors);
    ExportPlan plan;
    const std::filesystem::path& root = options.projectRoot;
    const std::optional<std::string> manifestText = readText(root / std::string(kProjectFileName));
    Expected<ProjectManifest, std::string> manifest =
        manifestText ? parseProjectManifest(*manifestText)
                     : Expected<ProjectManifest, std::string>(unexpected(std::string("cannot be read")));
    if (!manifest)
    {
        errors.push_back("project.illusion: " + manifest.error());
        manifest = ProjectManifest{};
    }
    Expected<ExportSettings, std::string> settings = readExportSettings(root);
    if (!settings)
    {
        errors.push_back(settings.error());
        settings = ExportSettings{};
    }

    plan.product = settings->productName.empty() ? manifest->name : settings->productName;
    plan.executable = executableName(plan.product);
    if (!isPortableName(plan.product))
    {
        errors.emplace_back(
            "the product name '" + plan.product +
            "' is not a valid directory name (1 to 64 characters without < > : \" / \\ | ? * ; "
            "$ or control characters, not ending in a space or a dot)");
    }
    else if (plan.executable.empty() || !isAsciiLetter(plan.executable.front()))
    {
        errors.emplace_back("the product name '" + plan.product +
                            "' must start with an ASCII letter to name the executable");
    }
    plan.organization = settings->organization.empty() ? plan.product : settings->organization;
    if (!settings->organization.empty() && !isPortableName(settings->organization))
    {
        errors.emplace_back("the organization '" + settings->organization +
                            "' is not a valid directory name");
    }
    if (!settings->identifier.empty() && !isIdentifier(settings->identifier))
    {
        errors.emplace_back("the identifier '" + settings->identifier +
                            "' must be reverse-DNS, e.g. \"com.example.game\"");
    }
    plan.version = manifest->version;
    if (plan.version.empty() || hasControl(plan.version))
    {
        errors.emplace_back("the project's version must be a non-empty line of text");
    }
    for (usize i = 0; i < settings->exclude.size(); ++i)
    {
        const std::string& pattern = settings->exclude[i];
        if (pattern.empty() || pattern.front() == '/' || pattern.find_first_of("\\:") != std::string::npos ||
            hasControl(pattern))
        {
            errors.emplace_back("exclude pattern " + std::to_string(i) + " ('" + pattern +
                                "') must be a path relative to assets:/ with '/' separators");
        }
    }
    plan.exclude = settings->exclude;

    plan.startupScene = manifest->startupScene;
    constexpr std::string_view kAssets = "assets:/";
    std::error_code code;
    if (plan.startupScene.empty())
    {
        errors.emplace_back("the project has no startup scene");
    }
    else if (!plan.startupScene.starts_with(kAssets))
    {
        errors.emplace_back("the startup scene '" + plan.startupScene + "' must be under assets:/");
    }
    else if (!std::filesystem::is_regular_file(
                 root / "assets" / pathFromUtf8(std::string_view(plan.startupScene).substr(kAssets.size())),
                 code))
    {
        errors.emplace_back("the startup scene '" + plan.startupScene + "' does not exist");
    }
    else if (excluded(plan.exclude, std::string_view(plan.startupScene).substr(kAssets.size())))
    {
        errors.emplace_back("the startup scene '" + plan.startupScene + "' is excluded from the export");
    }
    collectModules(root, *manifest, plan, errors);

    if (!errors.empty())
    {
        std::string message = "cannot export: ";
        for (usize i = 0; i < errors.size(); ++i)
        {
            message += (i == 0 ? "" : "; ") + errors[i];
        }
        return unexpected(std::move(message));
    }
    return plan;
}

std::string generatedCMakeLists(const ExportPlan& plan, const std::filesystem::path& root)
{
    const std::string target = plan.executable + "_game";
    std::string text = "# Generated by export.build (ADR 0055) for the game " + plan.product +
                       "; every export rewrites it.\n"
                       "cmake_minimum_required(VERSION 3.28)\n"
                       "project(illusion_export LANGUAGES CXX)\n"
                       "\n"
                       "set(CMAKE_CXX_STANDARD 20)\n"
                       "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
                       "set(CMAKE_CXX_EXTENSIONS OFF)\n"
                       "\n"
                       "find_package(illusion REQUIRED CONFIG COMPONENTS player)\n"
                       "\n"
                       "# The project's modules, built from their own CMakeLists as static libraries.\n"
                       "set(ILLUSION_STATIC_MODULES ON)\n";
    for (const std::string& module : plan.modules)
    {
        text += "add_subdirectory(" + bracket(pathText((root / "modules" / module).generic_u8string())) +
                " modules/" + module + ")\n";
    }
    text += "\nillusion_add_game(" + target + "\n    MODULES";
    for (const std::string& module : plan.modules)
    {
        text += " " + module;
    }
    text += "\n    NAME " + bracket(plan.product) + "\n    ORGANIZATION " + bracket(plan.organization) +
            "\n    VERSION " + bracket(plan.version) + "\n    STARTUP_SCENE " + bracket(plan.startupScene) +
            ")\n"
            "set_target_properties(" +
            target + " PROPERTIES OUTPUT_NAME " + plan.executable +
            ")\n"
            "\n"
            "install(TARGETS " +
            target + " RUNTIME DESTINATION .)\ninstall(FILES";
    for (const std::string_view archive : kArchives)
    {
        text += " data/" + std::string(archive);
    }
    text += " DESTINATION data)\n";
    return text;
}

} // namespace

void reflectExportSettings(ComponentRegistry& registry)
{
    reflectStruct<ExportSettings>(kExportSettingsName, 1, registry.structs())
        .meta({.displayName = "Export", .docsAnchor = "manual/editor/export.md#export-settings"})
        .field("productName", &ExportSettings::productName,
               {.description = "Name of the game and its directory (empty: the project's name)."})
        .field(
            "organization", &ExportSettings::organization,
            {.description = "Publisher; parent of the game's user data directory (empty: the product name)."})
        .field("identifier", &ExportSettings::identifier,
               {.description = "Reverse-DNS identifier, e.g. com.example.game."})
        .field("copyright", &ExportSettings::copyright, {.description = "Copyright line of the game."})
        .field("icon", &ExportSettings::icon,
               {.description = "Square PNG of at least 256 pixels (empty: the engine's icon).",
                .assetKind = AssetKind::Texture})
        .field("exclude", &ExportSettings::exclude,
               {.description = "Files under assets:/ that are not shipped, e.g. raw/** or *.psd."});
}

std::string_view exportPackageName(ExportPackage package) noexcept
{
    switch (package)
    {
    case ExportPackage::None:
        return "none";
    case ExportPackage::Archive:
        return "archive";
    case ExportPackage::Installer:
        return "installer";
    }
    return "none";
}

std::string_view exportStageName(ProjectExport::Stage stage) noexcept
{
    switch (stage)
    {
    case ProjectExport::Stage::Validate:
        return "validate";
    case ProjectExport::Stage::Generate:
        return "generate";
    case ProjectExport::Stage::Cook:
        return "cook";
    case ProjectExport::Stage::Pack:
        return "pack";
    case ProjectExport::Stage::Configure:
        return "configure";
    case ProjectExport::Stage::Build:
        return "build";
    case ProjectExport::Stage::Install:
        return "install";
    case ProjectExport::Stage::Package:
        return "package";
    case ProjectExport::Stage::Done:
        return "done";
    }
    return "done";
}

struct ProjectExport::Impl
{
    ExportOptions options;
    ExportPlan plan;
    Stage stage = Stage::Validate;
    bool succeeded = false;
    std::string error;
    ExportReport report;
    ToolOutput output;
    Process process;
    std::string line;
    u32 archive = 0; // the archive the Pack stage writes: 0 assets, 1 settings
    std::unique_ptr<ModuleBuild> build;
    usize buildDiagnostics = 0;

    std::filesystem::path exportRoot() const
    {
        return options.projectRoot / ".illusion" / "export" / std::string(platformName());
    }
    std::filesystem::path dataDirectory() const { return exportRoot() / "data"; }
    std::filesystem::path buildDirectory() const { return exportRoot() / "build"; }
    std::filesystem::path gameDirectory() const
    {
        return options.outputDirectory / pathFromUtf8(plan.product);
    }
    // The install goes here first and is renamed when complete, so no partial game is ever in the output.
    std::filesystem::path stagingDirectory() const
    {
        return options.outputDirectory / pathFromUtf8(plan.product + ".partial");
    }

    void fail(std::string message)
    {
        output.addUnparsed(message);
        error = std::move(message);
        stage = Stage::Done;
        process = Process{};
        build.reset();
        removeStaging();
    }

    void removeStaging() const
    {
        std::error_code code;
        std::filesystem::remove_all(stagingDirectory(), code);
    }

    void spawn(const std::filesystem::path& executable, std::vector<std::string> arguments, Stage next)
    {
        stage = next;
        output.addUnparsed("> " + pathText(executable.filename()) + " " + commandText(arguments));
        Expected<Process, std::string> started =
            Process::spawn(ProcessOptions{.executable = executable, .arguments = std::move(arguments)});
        if (!started)
        {
            fail("cannot start " + pathText(executable) + ": " + started.error());
            return;
        }
        process = std::move(*started);
    }

    void generate()
    {
        std::error_code code;
        std::filesystem::create_directories(dataDirectory(), code);
        if (options.clean)
        {
            std::filesystem::remove_all(buildDirectory(), code);
        }
        if (!writeIfChanged(exportRoot() / "CMakeLists.txt", generatedCMakeLists(plan, options.projectRoot)))
        {
            fail("cannot write " + pathText(exportRoot() / "CMakeLists.txt"));
            return;
        }
        // Excluded files ship in no form: the cook sees only the files that are not excluded.
        const std::filesystem::path assets = options.projectRoot / "assets";
        std::vector<std::string> cooked = filesBelow(assets);
        std::erase_if(cooked, [&](const std::string& file) {
            if (excluded(plan.exclude, file))
            {
                report.skipped.push_back("assets:/" + file);
                return true;
            }
            return false;
        });
        std::set<std::string> replaced;
        for (const std::string& file : cooked)
        {
            if (lowerExtension(file) == ".gltf")
            {
                addGltfBuffers(assets, file, replaced);
            }
        }
        std::vector<std::string> shipped = cooked;
        std::erase_if(shipped, [&](const std::string& file) {
            const std::string extension = lowerExtension(file);
            return extension == ".gltf" || extension == ".glb" || replaced.contains(file);
        });
        std::vector<std::string> settings = filesBelow(options.projectRoot / "settings");
        // The export settings describe how to export; the game never reads them.
        std::erase(settings, std::string(kExportSettingsFile.substr(kExportSettingsFile.find(":/") + 2)));
        if (!writeList("cook.files", cooked) || !writeList("assets.files", shipped) ||
            !writeList("settings.files", settings))
        {
            return;
        }
        spawn(options.toolchain.pack,
              {"--cook", "assets", pathText(assets), pathText(exportRoot() / "cook.files"),
               pathText(dataDirectory() / std::string(kArchives[2]))},
              Stage::Cook);
    }

    bool writeList(std::string_view name, const std::vector<std::string>& files)
    {
        const std::filesystem::path list = exportRoot() / std::string(name);
        std::string text;
        for (const std::string& file : files)
        {
            text += file + "\n";
        }
        if (!writeIfChanged(list, text))
        {
            fail("cannot write " + pathText(list));
            return false;
        }
        return true;
    }

    // Packs the archive @p index (0 assets, 1 settings) from the list generate() wrote.
    void pack(u32 index)
    {
        archive = index;
        const std::filesystem::path source = options.projectRoot / (index == 0 ? "assets" : "settings");
        const std::filesystem::path list = exportRoot() / (index == 0 ? "assets.files" : "settings.files");
        spawn(options.toolchain.pack,
              {"--files", pathText(source), pathText(list),
               pathText(dataDirectory() / std::string(kArchives[index]))},
              Stage::Pack);
    }

    void startBuild()
    {
        stage = Stage::Configure;
        Expected<std::unique_ptr<ModuleBuild>, std::string> started =
            ModuleBuild::start(ModuleBuildOptions{.toolchain = options.toolchain,
                                                  .sourceDirectory = exportRoot(),
                                                  .buildDirectory = buildDirectory(),
                                                  .reconfigure = options.clean});
        if (!started)
        {
            fail(std::move(started).error());
            return;
        }
        build = std::move(*started);
        buildDiagnostics = 0;
    }

    void advanceBuild()
    {
        const bool done = build->update();
        const std::vector<BuildDiagnostic>& found = build->diagnostics();
        output.diagnostics.insert(output.diagnostics.end(),
                                  found.begin() + static_cast<std::ptrdiff_t>(buildDiagnostics), found.end());
        buildDiagnostics = found.size();
        stage = build->stage() == ModuleBuild::Stage::Build ? Stage::Build : Stage::Configure;
        if (!done)
        {
            return;
        }
        stage = Stage::Build;
        output.addUnparsed(build->outputTail());
        const bool built = build->succeeded();
        build.reset();
        if (!built)
        {
            fail("the build of the game failed");
            return;
        }
        removeStaging();
        spawn(options.toolchain.cmake,
              {"--install", pathText(buildDirectory()), "--config", options.toolchain.config, "--prefix",
               pathText(stagingDirectory())},
              Stage::Install);
    }

    void finishInstall()
    {
        std::error_code code;
        const std::filesystem::path game = gameDirectory();
        std::filesystem::remove_all(game, code);
        std::filesystem::rename(stagingDirectory(), game, code);
        if (code)
        {
            fail("cannot move the installed game to " + pathText(game) + ": " + code.message());
            return;
        }
        report.platform = std::string(platformName());
        report.configuration = options.toolchain.config;
        report.directory = game;
        for (const std::string& file : filesBelow(game))
        {
            const u64 bytes = std::filesystem::file_size(game / pathFromUtf8(file), code);
            report.files.push_back(
                ExportedFile{.path = plan.product + "/" + file, .bytes = code ? 0 : bytes});
            report.totalBytes += report.files.back().bytes;
        }
        std::sort(report.skipped.begin(), report.skipped.end());
        succeeded = true;
        stage = Stage::Done;
    }

    void advanceProcess()
    {
        drainLines(process, line, [this](std::string text) { output.add(std::move(text)); });
        const std::optional<i32> exitCode = process.poll();
        if (!exitCode || !outputDrained(process))
        {
            return;
        }
        drainLines(process, line, [this](std::string text) { output.add(std::move(text)); });
        process = Process{};
        if (*exitCode != 0)
        {
            const std::string what = stage == Stage::Cook      ? "cooking the assets"
                                     : stage == Stage::Install ? "installing the game"
                                                               : "packing " + std::string(kArchives[archive]);
            fail(what + " failed (exit code " + std::to_string(*exitCode) + ")");
            return;
        }
        if (stage == Stage::Cook)
        {
            pack(0);
        }
        else if (stage == Stage::Pack && archive == 0)
        {
            pack(1);
        }
        else if (stage == Stage::Pack)
        {
            startBuild();
        }
        else
        {
            finishInstall();
        }
    }
};

ProjectExport::ProjectExport()
    : m_impl(std::make_unique<Impl>())
{
}

ProjectExport::~ProjectExport()
{
    Impl& impl = *m_impl;
    if (impl.process.valid() && !impl.process.poll())
    {
        static_cast<void>(impl.process.terminate());
        static_cast<void>(impl.process.wait());
    }
    impl.build.reset();
    if (impl.stage != Stage::Validate)
    {
        impl.removeStaging();
    }
}

Expected<std::unique_ptr<ProjectExport>, std::string> ProjectExport::start(ExportOptions options)
{
    Expected<ExportPlan, std::string> plan = planExport(options);
    if (!plan)
    {
        return unexpected(std::move(plan).error());
    }
    std::unique_ptr<ProjectExport> exported(new ProjectExport());
    exported->m_impl->options = std::move(options);
    exported->m_impl->plan = std::move(*plan);
    exported->m_impl->stage = Stage::Generate;
    return exported;
}

bool ProjectExport::update()
{
    Impl& impl = *m_impl;
    switch (impl.stage)
    {
    case Stage::Generate:
        impl.generate();
        break;
    case Stage::Cook:
    case Stage::Pack:
    case Stage::Install:
        impl.advanceProcess();
        break;
    case Stage::Configure:
    case Stage::Build:
        impl.advanceBuild();
        break;
    case Stage::Validate:
    case Stage::Package:
    case Stage::Done:
        break;
    }
    return impl.stage == Stage::Done;
}

ProjectExport::Stage ProjectExport::stage() const noexcept
{
    return m_impl->stage;
}

f64 ProjectExport::progress() const noexcept
{
    const Impl& impl = *m_impl;
    const usize index = static_cast<usize>(impl.stage);
    f64 progress = kStageStart[index];
    if (impl.stage == Stage::Build && impl.build != nullptr && impl.build->progress().total > 0.0)
    {
        const BuildProgress built = impl.build->progress();
        progress += (kStageStart[index + 1] - progress) * std::min(1.0, built.done / built.total);
    }
    return progress;
}

bool ProjectExport::succeeded() const noexcept
{
    return m_impl->succeeded;
}

const std::string& ProjectExport::error() const noexcept
{
    return m_impl->error;
}

const std::vector<BuildDiagnostic>& ProjectExport::diagnostics() const noexcept
{
    return m_impl->output.diagnostics;
}

std::string ProjectExport::outputTail() const
{
    const Impl& impl = *m_impl;
    if (impl.build == nullptr)
    {
        return impl.output.tail();
    }
    ToolOutput merged;
    merged.addUnparsed(impl.output.tail());
    merged.addUnparsed(impl.build->outputTail());
    return merged.tail();
}

const ExportReport& ProjectExport::report() const noexcept
{
    return m_impl->report;
}

} // namespace illusion::editor

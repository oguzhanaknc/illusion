// Exports of a project as a game (ADR 0055): validation, the generated CMake project, cooking and packing
// with the SDK's illusion_pack, CMake's configure, build and install, signing, and CPack. Every step but the
// generation is a child process, so update() never blocks.
//
// Signing changes the files in the build tree, before `cmake --install`: the generated project sets
// CMAKE_SKIP_INSTALL_ALL_DEPENDENCY, so the install and CPack (which installs the build tree again) copy the
// signed files without building, and the game's directory and every package hold the same bytes.

#include "PathText.hpp"
#include "ToolProcess.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/SettingsFile.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/editor/core/Export.hpp>
#include <illusion/editor/core/Project.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <deque>
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
constexpr std::array<f64, 9> kStageStart{0.0, 0.0, 0.02, 0.15, 0.25, 0.35, 0.80, 0.88, 1.0};

constexpr std::string_view kFilePlaceholder = "{file}";

enum class Platform : u8
{
    Windows,
    MacOS,
    Linux,
};

constexpr Platform kPlatform =
#if defined(ILL_PLATFORM_WINDOWS)
    Platform::Windows;
#elif defined(ILL_PLATFORM_MACOS)
    Platform::MacOS;
#else
    Platform::Linux;
#endif

std::string_view platformName() noexcept
{
    switch (kPlatform)
    {
    case Platform::Windows:
        return "windows";
    case Platform::MacOS:
        return "macos";
    case Platform::Linux:
        return "linux";
    }
    return "linux";
}

// The CPack generator of @p package and the extension of the file it writes.
struct PackageFormat
{
    std::string_view generator;
    std::string_view extension;
    std::string_view suffix; // of the file in the output directory, after the stem
};

PackageFormat packageFormat(ExportPackage package) noexcept
{
    const bool installer = package == ExportPackage::Installer;
    switch (kPlatform)
    {
    case Platform::Windows:
        return installer ? PackageFormat{.generator = "NSIS", .extension = ".exe", .suffix = "-setup.exe"}
                         : PackageFormat{.generator = "ZIP", .extension = ".zip", .suffix = ".zip"};
    case Platform::MacOS:
        return installer ? PackageFormat{.generator = "DragNDrop", .extension = ".dmg", .suffix = ".dmg"}
                         : PackageFormat{.generator = "ZIP", .extension = ".zip", .suffix = ".zip"};
    case Platform::Linux:
        break;
    }
    return PackageFormat{.generator = "TGZ", .extension = ".tar.gz", .suffix = ".tar.gz"};
}

// Splits a command line at spaces and tabs; double quotes group a part with spaces and are removed.
// Backslashes are ordinary characters, so Windows paths need no escaping.
std::vector<std::string> splitCommandLine(std::string_view line)
{
    std::vector<std::string> parts;
    std::string part;
    bool quoted = false;
    bool started = false;
    for (const char c : line)
    {
        if (c == '"')
        {
            quoted = !quoted;
            started = true;
        }
        else if ((c == ' ' || c == '\t') && !quoted)
        {
            if (started)
            {
                parts.push_back(std::move(part));
                part.clear();
                started = false;
            }
        }
        else
        {
            part += c;
            started = true;
        }
    }
    if (started)
    {
        parts.push_back(std::move(part));
    }
    return parts;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to)
{
    for (usize at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
    {
        text.replace(at, from.size(), to);
    }
    return text;
}

// makensis on PATH or in the default install directories of NSIS.
std::optional<std::filesystem::path> findMakensis()
{
    if (std::optional<std::filesystem::path> found = findExecutable("makensis"))
    {
        return found;
    }
    for (const std::string_view variable : {"ProgramFiles(x86)", "ProgramFiles"})
    {
        if (const std::optional<std::string> directory = environmentVariable(variable))
        {
            const std::filesystem::path candidate = pathFromUtf8(*directory) / "NSIS" / "makensis.exe";
            std::error_code code;
            if (std::filesystem::is_regular_file(candidate, code))
            {
                return candidate;
            }
        }
    }
    return std::nullopt;
}

// cpack of the SDK's CMake: next to the cmake program, with its extension.
std::filesystem::path cpackOf(const std::filesystem::path& cmake)
{
    return cmake.parent_path() / ("cpack" + pathText(cmake.extension()));
}

// The size of a square PNG from its header, or nullopt if @p path is not a PNG.
std::optional<std::pair<u32, u32>> pngSize(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::array<unsigned char, 24> header{};
    if (!in.read(reinterpret_cast<char*>(header.data()), header.size()))
    {
        return std::nullopt;
    }
    constexpr std::array<unsigned char, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (!std::equal(kSignature.begin(), kSignature.end(), header.begin()) ||
        std::string_view(reinterpret_cast<const char*>(header.data() + 12), 4) != "IHDR")
    {
        return std::nullopt;
    }
    const auto big = [&](usize at) {
        return (u32{header[at]} << 24) | (u32{header[at + 1]} << 16) | (u32{header[at + 2]} << 8) |
               u32{header[at + 3]};
    };
    return std::pair{big(16), big(20)};
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
    std::string identifier;
    std::string copyright;
    std::filesystem::path icon; // empty for the engine's icon
    std::vector<std::string> modules;
    std::vector<std::string> exclude;
    // Signing and packaging, resolved during validation.
    std::filesystem::path signProgram;
    std::vector<std::string> signArguments;
    std::filesystem::path cpack;
    std::filesystem::path makensis;
    std::filesystem::path codesign;
    std::filesystem::path xcrun;
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

// The programs that the package and signing options need, each an error naming it when it is missing.
void checkTools(const ExportOptions& options, ExportPlan& plan, std::vector<std::string>& errors)
{
    std::error_code code;
    const ExportSigning& signing = options.signing;
    if (options.package != ExportPackage::None)
    {
        plan.cpack = cpackOf(options.toolchain.cmake);
        if (!std::filesystem::is_regular_file(plan.cpack, code))
        {
            errors.emplace_back("packaging needs cpack, which is not next to the SDK's cmake at " +
                                pathText(plan.cpack));
        }
    }
    if (options.package == ExportPackage::Installer)
    {
        if constexpr (kPlatform == Platform::Windows)
        {
            const std::optional<std::filesystem::path> makensis = findMakensis();
            if (makensis)
            {
                plan.makensis = *makensis;
            }
            else
            {
                errors.emplace_back(
                    "the installer needs makensis (NSIS, https://nsis.sourceforge.io), which is "
                    "neither on PATH nor in Program Files\\NSIS");
            }
        }
        else if constexpr (kPlatform == Platform::MacOS)
        {
            if (!findExecutable("hdiutil"))
            {
                errors.emplace_back("the installer (a DMG) needs hdiutil, which is not on PATH");
            }
        }
    }

    if (!signing.signCommand.empty())
    {
        const std::vector<std::string> parts = splitCommandLine(signing.signCommand);
        if constexpr (kPlatform != Platform::Windows)
        {
            errors.emplace_back("signCommand signs Windows exports; use codesignIdentity on macOS");
        }
        else if (signing.signCommand.find(kFilePlaceholder) == std::string::npos || parts.size() < 2)
        {
            errors.emplace_back("the sign command must name a program and {file}, e.g. "
                                "signtool sign /fd SHA256 /a {file}");
        }
        else if (const std::optional<std::filesystem::path> program = findExecutable(parts[0]))
        {
            plan.signProgram = *program;
            plan.signArguments.assign(parts.begin() + 1, parts.end());
        }
        else
        {
            errors.emplace_back("the sign command's program '" + parts[0] + "' was not found");
        }
    }
    if (!signing.codesignIdentity.empty() || !signing.notaryProfile.empty())
    {
        if constexpr (kPlatform != Platform::MacOS)
        {
            errors.emplace_back("codesignIdentity and notaryProfile sign macOS exports; use signCommand on "
                                "Windows");
        }
        else
        {
            if (!signing.codesignIdentity.empty())
            {
                const std::optional<std::filesystem::path> codesign = findExecutable("codesign");
                if (codesign)
                {
                    plan.codesign = *codesign;
                }
                else
                {
                    errors.emplace_back(
                        "signing needs codesign (the Xcode command line tools), which is not on "
                        "PATH");
                }
            }
            if (!signing.notaryProfile.empty())
            {
                if (signing.codesignIdentity.empty() || signing.codesignIdentity == "-")
                {
                    errors.emplace_back(
                        "notarization needs a Developer ID codesignIdentity, not none or the ad "
                        "hoc identity \"-\"");
                }
                if (options.package != ExportPackage::Installer)
                {
                    errors.emplace_back("notarization submits and staples the DMG; export with package "
                                        "'installer'");
                }
                const std::optional<std::filesystem::path> xcrun = findExecutable("xcrun");
                if (xcrun)
                {
                    plan.xcrun = *xcrun;
                }
                else
                {
                    errors.emplace_back(
                        "notarization needs xcrun (notarytool and stapler), which is not on PATH");
                }
            }
        }
    }
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
    if (hasControl(options.signing.signCommand) || hasControl(options.signing.codesignIdentity) ||
        hasControl(options.signing.notaryProfile))
    {
        errors.emplace_back("the signing settings must be single lines of text");
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

// The icon of the export settings: a square PNG of at least 256 pixels under assets:/.
void checkIcon(const std::filesystem::path& root, const AssetRef& icon, ExportPlan& plan,
               std::vector<std::string>& errors)
{
    const std::string_view path = icon.path();
    if (path.empty())
    {
        return;
    }
    constexpr std::string_view kAssets = "assets:/";
    if (!path.starts_with(kAssets) || path.find_first_of("#\\") != std::string_view::npos ||
        path.find("..") != std::string_view::npos || lowerExtension(path) != ".png")
    {
        errors.emplace_back("the icon '" + std::string(path) + "' must be a PNG file under assets:/");
        return;
    }
    const std::filesystem::path file = root / "assets" / pathFromUtf8(path.substr(kAssets.size()));
    std::error_code code;
    if (!std::filesystem::is_regular_file(file, code))
    {
        errors.emplace_back("the icon '" + std::string(path) + "' does not exist");
        return;
    }
    const std::optional<std::pair<u32, u32>> size = pngSize(file);
    if (!size || size->first != size->second || size->first < 256)
    {
        errors.emplace_back(
            "the icon '" + std::string(path) + "' must be a square PNG of at least 256 pixels" +
            (size ? " (it is " + std::to_string(size->first) + "x" + std::to_string(size->second) + ")"
                  : std::string(" (it is not a PNG)")));
        return;
    }
    plan.icon = file;
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
    plan.identifier = settings->identifier;
    plan.copyright = settings->copyright;
    if (hasControl(plan.copyright))
    {
        errors.emplace_back("the copyright must be one line of text");
    }
    checkIcon(root, settings->icon, plan, errors);
    checkTools(options, plan, errors);
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
    std::string text =
        "# Generated by export.build (ADR 0055) for the game " + plan.product +
        "; every export rewrites it.\n"
        "cmake_minimum_required(VERSION 3.28)\n"
        "project(illusion_export LANGUAGES CXX)\n"
        "\n"
        "set(CMAKE_CXX_STANDARD 20)\n"
        "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "set(CMAKE_CXX_EXTENSIONS OFF)\n"
        "\n"
        "# Exports sign the executable in the build tree: installing and packaging must not build.\n"
        "set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON)\n"
        "# A macOS bundle must carry the Vulkan loader and MoltenVK: without them configuring fails.\n"
        "set(ILLUSION_GAME_REQUIRE_VULKAN ON)\n"
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
            "\n    VERSION " + bracket(plan.version) + "\n    STARTUP_SCENE " + bracket(plan.startupScene);
    if (!plan.icon.empty())
    {
        text += "\n    ICON " + bracket(pathText(plan.icon.generic_u8string()));
    }
    if (!plan.identifier.empty())
    {
        text += "\n    IDENTIFIER " + bracket(plan.identifier);
    }
    if (!plan.copyright.empty())
    {
        text += "\n    COPYRIGHT " + bracket(plan.copyright);
    }
    // A macOS bundle is named after the product, with the executable of the same name inside.
    const std::string outputName = kPlatform == Platform::MacOS ? plan.product : plan.executable;
    text += ")\nset_target_properties(" + target + " PROPERTIES OUTPUT_NAME " + bracket(outputName) +
            "\n    RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/game\")\n\nillusion_package_game(" +
            target + " DATA";
    for (const std::string_view archive : kArchives)
    {
        text += " data/" + std::string(archive);
    }
    text += ")\n";
    return text;
}

std::string entitlements(bool adHoc)
{
    // The player needs no entitlement under the hardened runtime when its libraries carry the same team's
    // signature; ad hoc signatures have no team, so library validation would refuse the bundled loader.
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                       "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                       "<plist version=\"1.0\">\n<dict>\n";
    if (adHoc)
    {
        text += "    <key>com.apple.security.cs.disable-library-validation</key>\n    <true/>\n";
    }
    return text + "</dict>\n</plist>\n";
}

// A file name made of @p text: characters that are not portable in file names become '_'.
std::string fileNamePart(std::string_view text)
{
    std::string part;
    for (const char c : text)
    {
        part += isAsciiAlnum(c) || c == '.' || c == '-' || c == '+' || c == '_' ? c : '_';
    }
    return part;
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

namespace
{

constexpr usize kQuotedLines = 20;

// One child process of an export.
struct ToolCommand
{
    std::filesystem::path executable{};
    std::vector<std::string> arguments{};
    std::string shown{};      // the line of the output that names the command
    std::string what{};       // what failed, e.g. "signing tetris.exe"
    bool quoteOutput = false; // a failure quotes the tool's last lines in the error
};

} // namespace

struct ProjectExport::Impl
{
    // The work that the current process belongs to.
    enum class Phase : u8
    {
        Cook,
        Pack,
        Sign,
        Install,
        Package,
        PackageSign,
    };

    ExportOptions options;
    ExportPlan plan;
    Stage stage = Stage::Validate;
    Phase phase = Phase::Cook;
    bool succeeded = false;
    std::string error;
    ExportReport report;
    ToolOutput output;
    Process process;
    std::string line;
    ToolCommand running;
    std::deque<ToolCommand> queue;
    std::deque<std::string> runningLines; // the last lines of the running process
    u32 archive = 0;                      // the archive the Pack stage writes: 0 assets, 1 settings
    std::unique_ptr<ModuleBuild> build;
    usize buildDiagnostics = 0;

    std::filesystem::path exportRoot() const
    {
        return options.projectRoot / ".illusion" / "export" / std::string(platformName());
    }
    std::filesystem::path dataDirectory() const { return exportRoot() / "data"; }
    std::filesystem::path buildDirectory() const { return exportRoot() / "build"; }
    std::filesystem::path packageDirectory() const { return exportRoot() / "package"; }
    // The name of the game's directory in the output directory (the bundle on macOS).
    std::string gameName() const
    {
        return kPlatform == Platform::MacOS ? plan.product + ".app" : plan.product;
    }
    std::filesystem::path gameDirectory() const { return options.outputDirectory / pathFromUtf8(gameName()); }
    // The install goes here first and is renamed when complete, so no partial game is ever in the output.
    std::filesystem::path stagingDirectory() const
    {
        return options.outputDirectory / pathFromUtf8(plan.product + ".partial");
    }
    // What the build made and signing changes: the executable, or the bundle on macOS.
    std::filesystem::path builtGame() const
    {
        const std::filesystem::path directory = buildDirectory() / "game";
        const std::string name = kPlatform == Platform::Windows ? plan.executable + ".exe"
                                 : kPlatform == Platform::MacOS ? gameName()
                                                                : plan.executable;
        return directory / pathFromUtf8(name);
    }
    std::filesystem::path packageFile() const
    {
        return options.outputDirectory /
               pathFromUtf8(plan.product + "-" + fileNamePart(plan.version) + "-" +
                            std::string(platformName()) + std::string(packageFormat(options.package).suffix));
    }

    void fail(std::string message)
    {
        output.addUnparsed(message);
        error = std::move(message);
        stage = Stage::Done;
        process = Process{};
        queue.clear();
        build.reset();
        removeStaging();
        if (!report.package.empty())
        {
            std::error_code code;
            std::filesystem::remove(report.package, code);
            report.package.clear();
        }
    }

    void removeStaging() const
    {
        std::error_code code;
        std::filesystem::remove_all(stagingDirectory(), code);
    }

    void spawn(ToolCommand command, Stage next)
    {
        stage = next;
        output.addUnparsed(command.shown);
        runningLines.clear();
        running = std::move(command);
        Expected<Process, std::string> started =
            Process::spawn(ProcessOptions{.executable = running.executable, .arguments = running.arguments});
        if (!started)
        {
            fail("cannot start " + pathText(running.executable) + ": " + started.error());
            return;
        }
        process = std::move(*started);
    }

    ToolCommand tool(const std::filesystem::path& executable, std::vector<std::string> arguments,
                     std::string what) const
    {
        std::string shown = "> " + pathText(executable.filename()) + " " + commandText(arguments);
        return ToolCommand{.executable = executable,
                           .arguments = std::move(arguments),
                           .shown = std::move(shown),
                           .what = std::move(what),
                           .quoteOutput = false};
    }

    void runQueue(Phase next, Stage nextStage)
    {
        phase = next;
        ToolCommand command = std::move(queue.front());
        queue.pop_front();
        spawn(std::move(command), nextStage);
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
        phase = Phase::Cook;
        spawn(tool(options.toolchain.pack,
                   {"--cook", "assets", pathText(assets), pathText(exportRoot() / "cook.files"),
                    pathText(dataDirectory() / std::string(kArchives[2]))},
                   "cooking the assets"),
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
        phase = Phase::Pack;
        const std::filesystem::path source = options.projectRoot / (index == 0 ? "assets" : "settings");
        const std::filesystem::path list = exportRoot() / (index == 0 ? "assets.files" : "settings.files");
        spawn(tool(options.toolchain.pack,
                   {"--files", pathText(source), pathText(list),
                    pathText(dataDirectory() / std::string(kArchives[index]))},
                   "packing " + std::string(kArchives[index])),
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
        startSigning();
    }

    // The sign command of the export settings on @p file; its arguments are never shown, they may hold a
    // password.
    ToolCommand signCommand(const std::filesystem::path& file) const
    {
        const std::string name = pathText(file.filename());
        std::vector<std::string> arguments;
        for (const std::string& argument : plan.signArguments)
        {
            arguments.push_back(replaceAll(argument, kFilePlaceholder, pathText(file)));
        }
        return ToolCommand{.executable = plan.signProgram,
                           .arguments = std::move(arguments),
                           .shown = "> " + pathText(plan.signProgram.filename()) + " (signing " + name + ")",
                           .what = "signing " + name,
                           .quoteOutput = true};
    }

    // `codesign` of @p file with the hardened runtime; ad hoc signatures get no timestamp.
    ToolCommand codesignCommand(const std::filesystem::path& file,
                                const std::filesystem::path& entitlementsFile) const
    {
        const std::string& identity = options.signing.codesignIdentity;
        std::vector<std::string> arguments{"--force", "--sign", identity, "--options", "runtime"};
        if (identity != "-")
        {
            arguments.emplace_back("--timestamp");
        }
        if (!entitlementsFile.empty())
        {
            arguments.emplace_back("--entitlements");
            arguments.push_back(pathText(entitlementsFile));
        }
        arguments.push_back(pathText(file));
        ToolCommand command =
            tool(plan.codesign, std::move(arguments), "signing " + pathText(file.filename()));
        command.quoteOutput = true;
        return command;
    }

    // Signs what the build made, before the install copies it: Windows the executable, macOS the bundle from
    // the inside out (the libraries in Contents/Frameworks, then the app with its entitlements).
    void startSigning()
    {
        const std::filesystem::path game = builtGame();
        std::error_code code;
        if (!std::filesystem::exists(game, code))
        {
            fail("the build made no game at " + pathText(game));
            return;
        }
        if constexpr (kPlatform == Platform::Windows)
        {
            if (!plan.signProgram.empty())
            {
                queue.push_back(signCommand(game.lexically_normal().make_preferred()));
            }
        }
        else if constexpr (kPlatform == Platform::MacOS)
        {
            if (!options.signing.codesignIdentity.empty())
            {
                std::vector<std::filesystem::path> libraries;
                for (std::filesystem::directory_iterator it(game / "Contents" / "Frameworks", code), end;
                     !code && it != end; it.increment(code))
                {
                    if (it->is_regular_file(code) &&
                        lowerExtension(pathText(it->path().filename())) == ".dylib")
                    {
                        libraries.push_back(it->path());
                    }
                }
                std::sort(libraries.begin(), libraries.end());
                for (const std::filesystem::path& library : libraries)
                {
                    queue.push_back(codesignCommand(library, {}));
                }
                const std::filesystem::path file = exportRoot() / "entitlements.plist";
                if (!writeIfChanged(file, entitlements(options.signing.codesignIdentity == "-")))
                {
                    fail("cannot write " + pathText(file));
                    return;
                }
                queue.push_back(codesignCommand(game, file));
            }
        }
        if (queue.empty())
        {
            startInstall();
            return;
        }
        runQueue(Phase::Sign, Stage::Build);
    }

    void startInstall()
    {
        removeStaging();
        phase = Phase::Install;
        spawn(tool(options.toolchain.cmake,
                   {"--install", pathText(buildDirectory()), "--config", options.toolchain.config, "--prefix",
                    pathText(stagingDirectory())},
                   "installing the game"),
              Stage::Install);
    }

    void finishInstall()
    {
        std::error_code code;
        const std::filesystem::path game = gameDirectory();
        std::filesystem::remove_all(game, code);
        if constexpr (kPlatform == Platform::MacOS)
        {
            std::filesystem::rename(stagingDirectory() / pathFromUtf8(gameName()), game, code);
            if (!code)
            {
                removeStaging();
            }
        }
        else
        {
            std::filesystem::rename(stagingDirectory(), game, code);
        }
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
            report.files.push_back(ExportedFile{.path = gameName() + "/" + file, .bytes = code ? 0 : bytes});
            report.totalBytes += report.files.back().bytes;
        }
        if (options.package == ExportPackage::None)
        {
            complete();
            return;
        }
        startPackage();
    }

    void startPackage()
    {
        std::error_code code;
        std::filesystem::remove_all(packageDirectory(), code);
        const PackageFormat format = packageFormat(options.package);
        std::vector<std::string> arguments{"--config", pathText(buildDirectory() / "CPackConfig.cmake"),
                                           "-G",       std::string(format.generator),
                                           "-C",       options.toolchain.config,
                                           "-B",       pathText(packageDirectory()),
                                           "-D",       "CPACK_PACKAGE_FILE_NAME=" + plan.product};
        if (format.generator == "NSIS" && !plan.makensis.empty())
        {
            arguments.emplace_back("-D");
            arguments.push_back("CPACK_NSIS_EXECUTABLE=" + pathText(plan.makensis));
        }
        phase = Phase::Package;
        spawn(tool(plan.cpack, std::move(arguments), "packaging the game"), Stage::Package);
    }

    void finishPackage()
    {
        const PackageFormat format = packageFormat(options.package);
        const std::filesystem::path made =
            packageDirectory() / pathFromUtf8(plan.product + std::string(format.extension));
        const std::filesystem::path package = packageFile();
        std::error_code code;
        if (!std::filesystem::is_regular_file(made, code))
        {
            fail("cpack made no package at " + pathText(made));
            return;
        }
        std::filesystem::remove(package, code);
        std::filesystem::rename(made, package, code);
        if (code)
        {
            // The output directory may be on another volume.
            code.clear();
            std::filesystem::copy_file(made, package, std::filesystem::copy_options::overwrite_existing,
                                       code);
        }
        if (code)
        {
            fail("cannot move the package to " + pathText(package) + ": " + code.message());
            return;
        }
        report.package = package;
        const bool installer = options.package == ExportPackage::Installer;
        if constexpr (kPlatform == Platform::Windows)
        {
            if (installer && !plan.signProgram.empty())
            {
                queue.push_back(signCommand(package.lexically_normal().make_preferred()));
            }
        }
        else if constexpr (kPlatform == Platform::MacOS)
        {
            const std::string& identity = options.signing.codesignIdentity;
            if (installer && !identity.empty() && identity != "-")
            {
                queue.push_back(codesignCommand(package, {}));
            }
            if (!options.signing.notaryProfile.empty())
            {
                // The profile names a keychain item; the secret stays in the keychain.
                ToolCommand submit = tool(plan.xcrun,
                                          {"notarytool", "submit", pathText(package), "--wait",
                                           "--keychain-profile", options.signing.notaryProfile},
                                          "notarizing " + pathText(package.filename()));
                submit.quoteOutput = true;
                queue.push_back(std::move(submit));
                ToolCommand staple = tool(plan.xcrun, {"stapler", "staple", pathText(package)},
                                          "stapling " + pathText(package.filename()));
                staple.quoteOutput = true;
                queue.push_back(std::move(staple));
            }
        }
        if (queue.empty())
        {
            complete();
            return;
        }
        runQueue(Phase::PackageSign, Stage::Package);
    }

    void complete()
    {
        if (phase == Phase::PackageSign && !options.signing.notaryProfile.empty())
        {
            report.notarized = true;
        }
        std::sort(report.skipped.begin(), report.skipped.end());
        succeeded = true;
        stage = Stage::Done;
    }

    void keepLine(std::string text)
    {
        if (running.quoteOutput)
        {
            runningLines.push_back(text);
            if (runningLines.size() > kQuotedLines)
            {
                runningLines.pop_front();
            }
        }
        output.add(std::move(text));
    }

    void advanceProcess()
    {
        drainLines(process, line, [this](std::string text) { keepLine(std::move(text)); });
        const std::optional<i32> exitCode = process.poll();
        if (!exitCode || !outputDrained(process))
        {
            return;
        }
        drainLines(process, line, [this](std::string text) { keepLine(std::move(text)); });
        process = Process{};
        if (*exitCode != 0)
        {
            std::string message = running.what + " failed (exit code " + std::to_string(*exitCode) + ")";
            if (!runningLines.empty())
            {
                message += ":";
                for (const std::string& text : runningLines)
                {
                    message += "\n" + text;
                }
            }
            fail(std::move(message));
            return;
        }
        if (!queue.empty())
        {
            runQueue(phase, stage);
            return;
        }
        switch (phase)
        {
        case Phase::Cook:
            pack(0);
            break;
        case Phase::Pack:
            if (archive == 0)
            {
                pack(1);
            }
            else
            {
                startBuild();
            }
            break;
        case Phase::Sign:
            report.signedBinaries = true;
            startInstall();
            break;
        case Phase::Install:
            finishInstall();
            break;
        case Phase::Package:
            finishPackage();
            break;
        case Phase::PackageSign:
            complete();
            break;
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
    case Stage::Package:
        impl.advanceProcess();
        break;
    case Stage::Configure:
    case Stage::Build:
        // After the build, the Build stage signs what it made.
        if (impl.build != nullptr)
        {
            impl.advanceBuild();
        }
        else
        {
            impl.advanceProcess();
        }
        break;
    case Stage::Validate:
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

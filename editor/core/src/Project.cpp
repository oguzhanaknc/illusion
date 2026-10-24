#include "PathText.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Version.hpp>
#include <illusion/editor/core/Project.hpp>

#include <array>
#include <fstream>
#include <sstream>
#include <system_error>

namespace illusion::editor
{
namespace
{

constexpr std::array<ProjectMount, 4> kMounts{{
    {"assets", "assets"},
    {"settings", "settings"},
    {"modules", "modules"},
    {"cache", ".illusion/cache"},
}};

Expected<std::string, std::string> readFileText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return unexpected("cannot read " + detail::pathText(path));
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool writeFileText(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

std::optional<std::string> readString(JsonValue object, std::string_view key, std::string& error)
{
    const JsonValue value = object.find(key);
    if (value.isAbsent())
    {
        return std::string();
    }
    const std::optional<std::string_view> text = value.asString();
    if (!text)
    {
        error = "\"" + std::string(key) + "\" must be a string";
        return std::nullopt;
    }
    return std::string(*text);
}

} // namespace

Expected<ProjectManifest, std::string> parseProjectManifest(std::string_view json)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return unexpected("invalid JSON at line " + std::to_string(parsed.error().line) + ": " +
                          parsed.error().message);
    }
    const JsonValue root = parsed->root();
    if (!root.isObject() || root.find("format").asString() != kProjectFormat)
    {
        return unexpected("not a project manifest (\"format\" must be \"" + std::string(kProjectFormat) +
                          "\")");
    }
    ProjectManifest manifest;
    std::string error;
    const std::optional<std::string> name = readString(root, "name", error);
    const std::optional<std::string> version = readString(root, "version", error);
    const std::optional<std::string> engine = readString(root, "engineVersion", error);
    const std::optional<std::string> startup = readString(root, "startupScene", error);
    if (!name || !version || !engine || !startup)
    {
        return unexpected(error);
    }
    if (name->empty())
    {
        return unexpected(std::string("\"name\" must not be empty"));
    }
    manifest.name = *name;
    if (!version->empty())
    {
        if (!SemanticVersion::parse(*version))
        {
            return unexpected("\"version\" must be a semantic version, not \"" + *version + "\"");
        }
        manifest.version = *version;
    }
    manifest.engineVersion = *engine;
    manifest.startupScene = *startup;
    const JsonValue modules = root.find("modules");
    if (!modules.isAbsent() && !modules.isArray())
    {
        return unexpected(std::string("\"modules\" must be an array of names"));
    }
    for (const JsonValue module : modules.elements())
    {
        const std::optional<std::string_view> moduleName = module.asString();
        if (!moduleName || moduleName->empty())
        {
            return unexpected(std::string("\"modules\" must be an array of names"));
        }
        manifest.modules.emplace_back(*moduleName);
    }
    return manifest;
}

bool writeMcpConfig(const std::filesystem::path& projectRoot, const std::filesystem::path& server)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    JsonMutValue entry = root.addObject("mcpServers").addObject("illusion");
    entry.add("type", doc.makeString("stdio"));
    entry.add("command", doc.makeString(detail::pathText(server)));
    JsonMutValue args = entry.addArray("args");
    args.push(doc.makeString("--project"));
    args.push(doc.makeString("."));
    entry.add("timeout", doc.makeU64(600000));
    doc.setRoot(root);
    std::string text;
    if (!doc.write(text))
    {
        return false;
    }
    return writeFileText(projectRoot / std::string(kMcpConfigFileName), text);
}

std::string writeProjectManifest(const ProjectManifest& manifest)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    root.add("format", doc.makeString(kProjectFormat));
    root.add("name", doc.makeString(manifest.name));
    root.add("version", doc.makeString(manifest.version));
    root.add("engineVersion", doc.makeString(manifest.engineVersion));
    root.add("startupScene", doc.makeString(manifest.startupScene));
    JsonMutValue modules = root.addArray("modules");
    for (const std::string& module : manifest.modules)
    {
        modules.push(doc.makeString(module));
    }
    doc.setRoot(root);
    std::string out;
    if (!doc.write(out))
    {
        return {};
    }
    out += '\n';
    return out;
}

std::span<const ProjectMount> projectMounts() noexcept
{
    return kMounts;
}

Expected<Project, std::string> Project::open(const std::filesystem::path& root)
{
    std::error_code code;
    const std::filesystem::path absolute = std::filesystem::absolute(root, code).lexically_normal();
    if (code || !std::filesystem::is_directory(absolute, code))
    {
        return unexpected("no project directory at " + detail::pathText(root));
    }
    Expected<std::string, std::string> text = readFileText(absolute / std::string(kProjectFileName));
    if (!text)
    {
        return unexpected("no " + std::string(kProjectFileName) + " in " + detail::pathText(absolute));
    }
    Expected<ProjectManifest, std::string> manifest = parseProjectManifest(*text);
    if (!manifest)
    {
        return unexpected(std::string(kProjectFileName) + ": " + manifest.error());
    }
    for (const ProjectMount& mount : kMounts)
    {
        std::filesystem::create_directories(absolute / std::string(mount.relative), code);
        if (code)
        {
            return unexpected("cannot create " + detail::pathText(absolute / std::string(mount.relative)));
        }
    }
    Project project;
    project.m_root = absolute;
    project.m_manifest = std::move(manifest).value();
    return project;
}

Expected<Project, std::string> Project::create(const std::filesystem::path& root, std::string_view name,
                                               const std::filesystem::path& templateDirectory)
{
    std::error_code code;
    if (std::filesystem::exists(root, code) && !std::filesystem::is_empty(root, code))
    {
        return unexpected("the directory " + detail::pathText(root) + " is not empty");
    }
    Expected<std::string, std::string> templateText =
        readFileText(templateDirectory / std::string(kProjectFileName));
    if (!templateText)
    {
        return unexpected("no project template at " + detail::pathText(templateDirectory));
    }
    Expected<ProjectManifest, std::string> manifest = parseProjectManifest(*templateText);
    if (!manifest)
    {
        return unexpected("template " + detail::pathText(templateDirectory) + ": " + manifest.error());
    }
    if (name.empty())
    {
        return unexpected(std::string("the project name must not be empty"));
    }
    std::filesystem::create_directories(root, code);
    std::filesystem::copy(templateDirectory, root, std::filesystem::copy_options::recursive, code);
    if (code)
    {
        return unexpected("cannot copy the template: " + code.message());
    }
    manifest->name = std::string(name);
    manifest->engineVersion = engineVersion().toString();
    if (!writeFileText(root / std::string(kProjectFileName), writeProjectManifest(*manifest)))
    {
        return unexpected("cannot write " + detail::pathText(root / std::string(kProjectFileName)));
    }
    return open(root);
}

FileError Project::mount(VirtualFileSystem& files) const
{
    for (const ProjectMount& mount : kMounts)
    {
        if (const FileError error =
                files.mount(mount.name, m_root / std::string(mount.relative), MountAccess::ReadWrite);
            error != FileError::None)
        {
            unmount(files);
            return error;
        }
    }
    return FileError::None;
}

void Project::unmount(VirtualFileSystem& files) const
{
    for (const ProjectMount& mount : kMounts)
    {
        files.unmount(mount.name);
    }
}

} // namespace illusion::editor

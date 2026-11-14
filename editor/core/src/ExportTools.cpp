#include "ToolProcess.hpp"

#include <illusion/editor/core/ExportTools.hpp>
#include <illusion/platform/Process.hpp>

#include <system_error>

namespace illusion::editor::detail
{

std::string_view exportPlatformName(ExportPlatform platform) noexcept
{
    switch (platform)
    {
    case ExportPlatform::Windows:
        return "windows";
    case ExportPlatform::MacOS:
        return "macos";
    case ExportPlatform::Linux:
        return "linux";
    }
    return "linux";
}

ExportPackageFormat exportPackageFormat(ExportPackage package, ExportPlatform platform) noexcept
{
    const bool installer = package == ExportPackage::Installer;
    switch (platform)
    {
    case ExportPlatform::Windows:
        return installer
                   ? ExportPackageFormat{.generator = "NSIS", .extension = ".exe", .suffix = "-setup.exe"}
                   : ExportPackageFormat{.generator = "ZIP", .extension = ".zip", .suffix = ".zip"};
    case ExportPlatform::MacOS:
        return installer
                   ? ExportPackageFormat{.generator = "DragNDrop", .extension = ".dmg", .suffix = ".dmg"}
                   : ExportPackageFormat{.generator = "ZIP", .extension = ".zip", .suffix = ".zip"};
    case ExportPlatform::Linux:
        break;
    }
    return ExportPackageFormat{.generator = "TGZ", .extension = ".tar.gz", .suffix = ".tar.gz"};
}

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

std::vector<std::string> substituteFile(const std::vector<std::string>& arguments, std::string_view file)
{
    constexpr std::string_view kPlaceholder = "{file}";
    std::vector<std::string> result;
    for (std::string argument : arguments)
    {
        for (usize at = argument.find(kPlaceholder); at != std::string::npos;
             at = argument.find(kPlaceholder, at + file.size()))
        {
            argument.replace(at, kPlaceholder.size(), file);
        }
        result.push_back(std::move(argument));
    }
    return result;
}

std::string codesignEntitlements(bool adHoc)
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

std::vector<std::string> codesignArguments(std::string_view identity, std::string_view file,
                                           std::string_view entitlements)
{
    std::vector<std::string> arguments{"--force", "--sign", std::string(identity), "--options", "runtime"};
    if (identity != "-")
    {
        arguments.emplace_back("--timestamp");
    }
    if (!entitlements.empty())
    {
        arguments.emplace_back("--entitlements");
        arguments.emplace_back(entitlements);
    }
    arguments.emplace_back(file);
    return arguments;
}

std::vector<std::string> notarizeArguments(std::string_view package, std::string_view profile)
{
    return {"notarytool",        "submit", std::string(package), "--wait", "--keychain-profile",
            std::string(profile)};
}

std::vector<std::string> stapleArguments(std::string_view package)
{
    return {"stapler", "staple", std::string(package)};
}

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

} // namespace illusion::editor::detail

// illusion_textc: generates string table code from English locale files and checks translations.
//
//   illusion_textc generate --source <en.json> --table <name> --namespace <ns> --header <out.hpp>
//                           --cpp <out.cpp> --include <path the .cpp includes the header with>
//   illusion_textc check [--strict] <locale directory>...
//
// `check` reads en.json in every directory and checks it and every other <locale>.json against it. It
// prints one line per problem and fails if there is an error (with --strict, also for warnings).

#include "TextTools.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace illusion;
using namespace illusion::text;

namespace
{

constexpr std::string_view kUsage =
    "usage:\n"
    "  illusion_textc generate --source <en.json> --table <name> --namespace <ns> --header <out.hpp>\n"
    "                          --cpp <out.cpp> --include <header include path>\n"
    "  illusion_textc check [--strict] <locale directory>...\n";

int usage()
{
    std::fputs(kUsage.data(), stderr);
    return 2;
}

// Paths are passed around as UTF-8 strings and converted at the file system boundary.
fs::path pathFromUtf8(std::string_view text)
{
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string utf8FromPath(const fs::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::optional<std::string> readFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad())
    {
        return std::nullopt;
    }
    return std::move(contents).str();
}

// Writes @p contents unless the file already holds them, so that unchanged tables do not trigger rebuilds.
bool writeIfChanged(const fs::path& path, const std::string& contents)
{
    if (const std::optional<std::string> existing = readFile(path); existing && *existing == contents)
    {
        return true;
    }
    std::error_code error;
    if (path.has_parent_path())
    {
        fs::create_directories(path.parent_path(), error);
        if (error)
        {
            return false;
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(stream);
}

int generate(const std::vector<std::string_view>& arguments)
{
    std::string sourcePath;
    std::string headerPath;
    std::string cppPath;
    tools::GenerateOptions options;
    for (usize i = 0; i < arguments.size(); i += 2)
    {
        if (i + 1 >= arguments.size())
        {
            return usage();
        }
        const std::string_view name = arguments[i];
        const std::string value(arguments[i + 1]);
        if (name == "--source")
        {
            sourcePath = value;
        }
        else if (name == "--table")
        {
            options.table = value;
        }
        else if (name == "--namespace")
        {
            options.nameSpace = value;
        }
        else if (name == "--header")
        {
            headerPath = value;
        }
        else if (name == "--cpp")
        {
            cppPath = value;
        }
        else if (name == "--include")
        {
            options.headerInclude = value;
        }
        else
        {
            return usage();
        }
    }
    if (sourcePath.empty() || options.table.empty() || options.nameSpace.empty() || headerPath.empty() ||
        cppPath.empty() || options.headerInclude.empty())
    {
        return usage();
    }
    const std::optional<std::string> english = readFile(pathFromUtf8(sourcePath));
    if (!english)
    {
        std::fprintf(stderr, "illusion_textc: cannot read %s\n", sourcePath.c_str());
        return 1;
    }
    options.sourceName = utf8FromPath(pathFromUtf8(sourcePath).filename());
    const auto generated = tools::generateStringTable(*english, options);
    if (!generated)
    {
        std::fprintf(stderr, "%s", generated.error().c_str());
        return 1;
    }
    if (!writeIfChanged(pathFromUtf8(headerPath), generated->header) ||
        !writeIfChanged(pathFromUtf8(cppPath), generated->source))
    {
        std::fprintf(stderr, "illusion_textc: cannot write %s or %s\n", headerPath.c_str(), cppPath.c_str());
        return 1;
    }
    return 0;
}

void checkDirectory(const fs::path& directory, const tools::CheckOptions& options, tools::CheckReport& report)
{
    const std::string directoryName = utf8FromPath(directory);
    const fs::path englishPath = directory / "en.json";
    const std::optional<std::string> englishText = readFile(englishPath);
    if (!englishText)
    {
        report.diagnostics.push_back({tools::Severity::Error, utf8FromPath(englishPath), "", "cannot read the file"});
        return;
    }
    const std::optional<LocaleFile> english = tools::checkEnglish(*englishText, utf8FromPath(englishPath), report);

    std::vector<fs::path> translations;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error))
    {
        if (entry.path().extension() == ".json" && entry.path().filename() != "en.json")
        {
            translations.push_back(entry.path());
        }
    }
    if (error)
    {
        report.diagnostics.push_back({tools::Severity::Error, directoryName, "", "cannot list the directory"});
        return;
    }
    std::sort(translations.begin(), translations.end());
    for (const fs::path& path : translations)
    {
        const std::string name = utf8FromPath(path);
        const std::optional<std::string> text = readFile(path);
        if (!text)
        {
            report.diagnostics.push_back({tools::Severity::Error, name, "", "cannot read the file"});
            continue;
        }
        if (const auto parsed = LocaleFile::parse(*text); parsed && parsed->locale.tag() != utf8FromPath(path.stem()))
        {
            report.diagnostics.push_back({tools::Severity::Error, name, "",
                                          "the file must be named after its locale (" +
                                              std::string(parsed->locale.tag()) + ".json)"});
        }
        if (english)
        {
            tools::checkTranslation(*english, *text, name, options, report);
        }
    }
}

int check(const std::vector<std::string_view>& arguments)
{
    tools::CheckOptions options;
    std::vector<fs::path> directories;
    for (const std::string_view argument : arguments)
    {
        if (argument == "--strict")
        {
            options.strict = true;
        }
        else
        {
            directories.push_back(pathFromUtf8(argument));
        }
    }
    if (directories.empty())
    {
        return usage();
    }
    tools::CheckReport report;
    for (const fs::path& directory : directories)
    {
        checkDirectory(directory, options, report);
    }
    std::fputs(report.format().c_str(), stdout);
    std::printf("illusion_textc: %zu error(s), %zu warning(s) in %zu director%s\n", report.errorCount(),
                report.warningCount(), directories.size(), directories.size() == 1 ? "y" : "ies");
    return report.errorCount() == 0 ? 0 : 1;
}

int run(const std::vector<std::string>& utf8Arguments)
{
    if (utf8Arguments.size() < 2)
    {
        return usage();
    }
    const std::string_view command = utf8Arguments[1];
    const std::vector<std::string_view> arguments(utf8Arguments.begin() + 2, utf8Arguments.end());
    if (command == "generate")
    {
        return generate(arguments);
    }
    if (command == "check")
    {
        return check(arguments);
    }
    return usage();
}

} // namespace

#if defined(_WIN32)
// Windows passes narrow arguments in the ANSI code page, which cannot hold every path; take the UTF-16
// arguments and convert them to UTF-8.
int wmain(int argc, wchar_t** argv)
{
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i)
    {
        arguments.push_back(utf8FromPath(fs::path(argv[i])));
    }
    return run(arguments);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv, argv + argc));
}
#endif

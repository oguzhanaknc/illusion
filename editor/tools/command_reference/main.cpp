// Generates the command table of docs/content/{en,tr}/reference/editor-commands.md from the command
// registry, or checks that the pages are up to date (CTest editor.command_reference).
//
//     illusion_editor_command_reference --check <docs/content>
//     illusion_editor_command_reference --write <docs/content>

#include <illusion/editor/core/EditorContext.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr std::string_view kBegin = "<!-- BEGIN GENERATED: editor commands -->";
constexpr std::string_view kEnd = "<!-- END GENERATED: editor commands -->";

std::string flagsOf(const CommandFlags& flags)
{
    std::string out;
    out += flags.readOnly ? "R" : "";
    out += flags.undoable ? "U" : "";
    out += flags.destructive ? "D" : "";
    out += flags.idempotent ? "I" : "";
    return out.empty() ? std::string("-") : out;
}

// "Does X. Example: {...}." -> "Does X. Example: `{...}`".
std::string summaryOf(std::string_view summary)
{
    constexpr std::string_view kExample = "Example: ";
    const usize example = summary.find(kExample);
    if (example == std::string_view::npos)
    {
        return std::string(summary);
    }
    std::string_view call = summary.substr(example + kExample.size());
    std::string_view rest;
    if (const usize space = call.find(' ', call.rfind('}')); space != std::string_view::npos)
    {
        rest = call.substr(space);
        call = call.substr(0, space);
    }
    if (call.ends_with('.'))
    {
        call.remove_suffix(1);
    }
    return std::string(summary.substr(0, example)) + std::string(kExample) + "`" + std::string(call) + "`" +
           std::string(rest);
}

std::string generate(EditorContext& editor)
{
    std::string out(kBegin);
    out += "\n\n| Command | Category | Flags | Permission | Shortcut | Summary |\n";
    out += "| --- | --- | --- | --- | --- | --- |\n";
    for (const CommandDescriptor* command : editor.commands().commands())
    {
        const CommandInfo& info = command->info;
        out += "| `" + std::string(info.name) + "` | " + std::string(info.category) + " | " +
               flagsOf(info.flags) + " | " + std::string(permissionClassName(info.permission)) + " | " +
               (info.defaultShortcut.empty() ? std::string("-") : std::string(info.defaultShortcut)) + " | " +
               summaryOf(info.agentSummary) + " |\n";
    }
    out += "\n";
    out += kEnd;
    return out;
}

bool readFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    out = text.str();
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string_view mode = argc == 3 ? std::string_view(argv[1]) : std::string_view();
    if (mode != "--check" && mode != "--write")
    {
        std::fprintf(stderr, "usage: illusion_editor_command_reference --check|--write <docs/content>\n");
        return 2;
    }
    EditorContext editor(EditorContextOptions{.seed = 1, .crashJournal = false});
    const std::string table = generate(editor);
    int status = 0;
    for (const char* language : {"en", "tr"})
    {
        // argv is in the native narrow encoding, which is what std::filesystem::path expects.
        const std::filesystem::path page =
            std::filesystem::path(argv[2]) / language / "reference" / "editor-commands.md";
        std::string text;
        if (!readFile(page, text))
        {
            std::fprintf(stderr, "cannot read %s\n", page.generic_string().c_str());
            return 1;
        }
        const usize begin = text.find(kBegin);
        const usize end = text.find(kEnd);
        if (begin == std::string::npos || end == std::string::npos || end < begin)
        {
            std::fprintf(stderr, "%s has no generated command table markers\n",
                         page.generic_string().c_str());
            return 1;
        }
        const std::string updated = text.substr(0, begin) + table + text.substr(end + kEnd.size());
        if (updated == text)
        {
            continue;
        }
        if (mode == "--check")
        {
            std::fprintf(stderr,
                         "%s is out of date: run illusion_editor_command_reference --write docs/content\n",
                         page.generic_string().c_str());
            status = 1;
            continue;
        }
        std::ofstream(page, std::ios::binary) << updated;
        std::printf("updated %s\n", page.generic_string().c_str());
    }
    return status;
}

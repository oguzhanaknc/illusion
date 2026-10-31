// Command lines of the External Tools preferences (ADR 0043).

#include <illusion/editor/core/ExternalTools.hpp>

namespace illusion::editor
{

namespace
{

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    for (usize at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
    {
        text.replace(at, from.size(), to);
    }
}

} // namespace

Expected<std::vector<std::string>, std::string>
codeEditorArguments(std::string_view command, std::string_view file, u32 line, u32 column)
{
    std::vector<std::string> arguments;
    std::string current;
    bool quoted = false;
    bool started = false;
    for (const char c : command)
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
                arguments.push_back(std::move(current));
                current.clear();
                started = false;
            }
        }
        else
        {
            current += c;
            started = true;
        }
    }
    if (quoted)
    {
        return unexpected(std::string("the code editor command has an unclosed quote"));
    }
    if (started)
    {
        arguments.push_back(std::move(current));
    }
    if (arguments.empty() || arguments[0].empty())
    {
        return unexpected(std::string("the code editor command names no program"));
    }
    const std::string lineText = std::to_string(line == 0 ? 1u : line);
    const std::string columnText = std::to_string(column == 0 ? 1u : column);
    bool namesFile = false;
    for (usize i = 1; i < arguments.size(); ++i)
    {
        namesFile = namesFile || arguments[i].find("{file}") != std::string::npos;
        replaceAll(arguments[i], "{file}", file);
        replaceAll(arguments[i], "{line}", lineText);
        replaceAll(arguments[i], "{column}", columnText);
    }
    if (!namesFile)
    {
        arguments.emplace_back(file);
    }
    return arguments;
}

std::vector<std::string> defaultOpenArguments(std::string_view file)
{
#if defined(ILL_PLATFORM_WINDOWS)
    return {"explorer", std::string(file)};
#elif defined(ILL_PLATFORM_MACOS)
    return {"open", std::string(file)};
#else
    return {"xdg-open", std::string(file)};
#endif
}

} // namespace illusion::editor

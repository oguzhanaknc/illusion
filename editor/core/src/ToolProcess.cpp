#include "ToolProcess.hpp"

namespace illusion::editor::detail
{

std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string commandText(const std::vector<std::string>& arguments)
{
    std::string text;
    for (const std::string& argument : arguments)
    {
        text += text.empty() ? "" : " ";
        text += argument;
    }
    return text;
}

void ToolOutput::add(std::string line)
{
    m_parser.feed(line, diagnostics);
    keep(std::move(line));
}

void ToolOutput::addUnparsed(std::string_view text)
{
    while (!text.empty())
    {
        const usize end = text.find('\n');
        keep(std::string(text.substr(0, end)));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    }
}

std::string ToolOutput::tail() const
{
    std::string text;
    for (const std::string& line : m_tail)
    {
        text += line;
        text += '\n';
    }
    return text;
}

void ToolOutput::keep(std::string line)
{
    if (m_tail.size() == kToolTailLines)
    {
        m_tail.pop_front();
    }
    m_tail.push_back(std::move(line));
}

bool outputDrained(const Process& process)
{
    return process.drained(ProcessStream::Output) && process.drained(ProcessStream::Error);
}

} // namespace illusion::editor::detail

#pragma once

// What ModuleBuild and ProjectExport share about the build tools they run as child processes: reading their
// output line by line without blocking, the diagnostics in it and the last lines for the result.

#include <illusion/editor/core/ModuleBuild.hpp>
#include <illusion/platform/Process.hpp>

#include <deque>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor::detail
{

inline constexpr usize kToolTailLines = 64;

/// The path that the UTF-8 text @p text names.
std::filesystem::path pathFromUtf8(std::string_view text);

/// @p arguments joined with spaces, for the `> cmake ...` lines of the output.
std::string commandText(const std::vector<std::string>& arguments);

/// The output of build tools: diagnostics parsed from it and its last lines.
class ToolOutput
{
public:
    /// Parses @p line for diagnostics and keeps it in the tail.
    void add(std::string line);

    /// Keeps the lines of @p text in the tail without parsing them (they were parsed elsewhere).
    void addUnparsed(std::string_view text);

    /// The last lines (at most kToolTailLines), oldest first, each followed by a line break.
    [[nodiscard]] std::string tail() const;

    std::vector<BuildDiagnostic> diagnostics; ///< In output order.

private:
    void keep(std::string line);

    BuildOutputParser m_parser;
    std::deque<std::string> m_tail;
};

/// Moves the complete lines that arrived on both streams of @p process to @p sink, without blocking.
template <class Sink>
void drainLines(Process& process, std::string& buffer, Sink&& sink)
{
    for (const ProcessStream stream : {ProcessStream::Output, ProcessStream::Error})
    {
        while (process.readLine(stream, buffer, std::chrono::milliseconds(0)))
        {
            sink(std::move(buffer));
            buffer.clear();
        }
    }
}

/// True once both captured streams of @p process reached their end.
[[nodiscard]] bool outputDrained(const Process& process);

} // namespace illusion::editor::detail

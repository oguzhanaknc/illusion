// The Console panel on top of the editor's LogRingSink: level filters, search, collapsing of repeated
// messages, timestamps, source links and a command line.

#include "Panels.hpp"

#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

bool containsFolded(std::string_view text, std::string_view needle) noexcept
{
    if (needle.empty())
    {
        return true;
    }
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (usize i = 0; i + needle.size() <= text.size(); ++i)
    {
        usize j = 0;
        while (j < needle.size() && lower(text[i + j]) == lower(needle[j]))
        {
            ++j;
        }
        if (j == needle.size())
        {
            return true;
        }
    }
    return false;
}

ui::ThemeToken levelToken(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Warn:
        return ui::ThemeToken::Warning;
    case LogLevel::Error:
    case LogLevel::Fatal:
        return ui::ThemeToken::Error;
    case LogLevel::Info:
        return ui::ThemeToken::Info;
    default:
        return ui::ThemeToken::TextSecondary;
    }
}

ui::Icon levelIcon(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Warn:
        return ui::icons::kWarning;
    case LogLevel::Error:
    case LogLevel::Fatal:
        return ui::icons::kError;
    case LogLevel::Info:
        return ui::icons::kInfo;
    default:
        return ui::icons::kCode;
    }
}

} // namespace

struct Console::State
{
    explicit State(usize ringCapacity)
        : capacity(ringCapacity)
        , records(std::make_unique<LogRecord[]>(ringCapacity))
    {
        visible.reserve(ringCapacity);
        repeats.reserve(ringCapacity);
    }

    usize capacity;
    std::unique_ptr<LogRecord[]> records; // ring of the newest messages
    usize count = 0;                      // stored messages
    usize head = 0;                       // index of the oldest message
    u64 last = 0;                         // sequence of the newest message read
    u64 dropped = 0;                      // messages the ring of the sink lost

    // Filters.
    bool debug = false;
    bool info = true;
    bool warnings = true;
    bool errors = true;
    bool collapse = true;
    bool timestamps = false;
    std::array<char, 128> search{};
    std::array<char, 512> command{};

    // Rows shown: indices into the ring and how many equal messages each one stands for.
    std::vector<u32> visible;
    std::vector<u32> repeats;
    bool dirty = true;
    std::array<char, 128> lastSearch{};
    u64 lastCleared = 0;
    bool scrollToBottom = true;

    const LogRecord& at(usize index) const noexcept { return records[(head + index) % capacity]; }

    void push(const LogRecord& record) noexcept
    {
        if (count < capacity)
        {
            records[(head + count) % capacity] = record;
            ++count;
        }
        else
        {
            records[head] = record;
            head = (head + 1) % capacity;
        }
    }

    bool shows(const LogRecord& record, u64 cleared) const noexcept
    {
        if (record.sequence <= cleared)
        {
            return false;
        }
        switch (record.level)
        {
        case LogLevel::Trace:
        case LogLevel::Debug:
            if (!debug)
            {
                return false;
            }
            break;
        case LogLevel::Info:
            if (!info)
            {
                return false;
            }
            break;
        case LogLevel::Warn:
            if (!warnings)
            {
                return false;
            }
            break;
        default:
            if (!errors)
            {
                return false;
            }
            break;
        }
        const std::string_view needle(search.data());
        return containsFolded(record.textView(), needle) || containsFolded(record.categoryView(), needle);
    }

    void filter(u64 cleared)
    {
        visible.clear();
        repeats.clear();
        for (usize i = 0; i < count; ++i)
        {
            const LogRecord& record = at(i);
            if (!shows(record, cleared))
            {
                continue;
            }
            if (collapse && !visible.empty())
            {
                const LogRecord& previous = at(visible.back());
                if (previous.level == record.level && previous.textView() == record.textView() &&
                    previous.categoryView() == record.categoryView())
                {
                    ++repeats.back();
                    continue;
                }
            }
            visible.push_back(static_cast<u32>(i));
            repeats.push_back(1);
        }
        dirty = false;
    }
};

Console::Console(usize capacity)
    : m_state(std::make_unique<State>(capacity))
{
}

Console::~Console() = default;

usize Console::size() const noexcept
{
    return m_state->count;
}

void Console::update(Impl& shell)
{
    State& state = *m_state;
    const LogRingSink* sink = shell.editor.log();
    if (sink == nullptr || sink->latestSequence() == state.last)
    {
        return;
    }
    const u64 before = state.last;
    state.last = sink->readSince(state.last, [&](const LogRecord& record) {
        if (before != 0 && record.sequence > state.last + 1)
        {
            state.dropped += record.sequence - state.last - 1;
        }
        state.last = record.sequence;
        state.push(record);
    });
    state.dirty = true;
    state.scrollToBottom = true;
}

void Console::draw(Impl& shell)
{
    State& state = *m_state;
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const u64 cleared = shell.editor.logCleared();

    bool changed = false;
    changed |= ImGui::Checkbox(text.label(s::kConsoleLevelDebug, "console.debug"), &state.debug);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(text.label(s::kConsoleLevelInfo, "console.info"), &state.info);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(text.label(s::kConsoleLevelWarn, "console.warnings"), &state.warnings);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(text.label(s::kConsoleLevelError, "console.errors"), &state.errors);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(text.label(s::kConsoleCollapse, "console.collapse"), &state.collapse);
    ImGui::SameLine();
    ImGui::Checkbox(text.label(s::kConsoleTimestamps, "console.timestamps"), &state.timestamps);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(
        std::max(120.0f * kit.scale, ImGui::GetContentRegionAvail().x - 90.0f * kit.scale));
    ui::searchField(kit, "console.search", state.search, text.format(s::kConsoleSearch));
    ImGui::SameLine();
    if (ImGui::Button(text.iconLabel(ui::icons::kBlock, s::kConsoleClear, "console.clear")))
    {
        (void)shell.run("log.clear", {}, "console");
    }
    if (changed || state.dirty || state.lastCleared != cleared ||
        std::strcmp(state.lastSearch.data(), state.search.data()) != 0)
    {
        state.lastSearch = state.search;
        state.lastCleared = cleared;
        state.filter(cleared);
    }
    if (state.dropped > 0)
    {
        ImGui::TextDisabled("%s", text.format(s::kConsoleDropped, {{"count", state.dropped}}));
    }

    const f32 commandHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild(text.hidden("console.messages"), ImVec2(0.0f, -commandHeight)))
    {
        if (state.visible.empty())
        {
            ImGui::TextDisabled("%s", text.format(s::kConsoleEmpty));
        }
        const ScopedFont font(shell.options.fonts, ui::FontRole::Mono);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(state.visible.size()));
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const LogRecord& record = state.at(state.visible[static_cast<usize>(row)]);
                const u32 repeat = state.repeats[static_cast<usize>(row)];
                ImGui::PushID(row);
                ImGui::TextColored(imvec(shell.theme.color(levelToken(record.level))), "%s",
                                   levelIcon(record.level).c_str());
                ImGui::SameLine();
                if (state.timestamps)
                {
                    ImGui::TextDisabled("%8.3f", record.timeSeconds);
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("[%.*s]", static_cast<int>(record.categoryLength), record.category);
                ImGui::SameLine();
                ImGui::TextWrapped("%.*s", static_cast<int>(record.textLength), record.text);
                if (repeat > 1)
                {
                    ImGui::SameLine();
                    char count[24];
                    std::snprintf(count, sizeof(count), "%u", repeat);
                    ui::badge(kit, count, ui::ThemeToken::TextSecondary);
                }
                if (record.fileLength > 0)
                {
                    // The source link opens a project file (a virtual path, as build diagnostics have) in the
                    // code editor (editor.open_source) and copies any other "file:line".
                    char source[LogRecord::kMaxFileBytes + 16];
                    std::snprintf(source, sizeof(source), "%.*s:%u", static_cast<int>(record.fileLength),
                                  record.file, record.line);
                    ImGui::SameLine();
                    if (ImGui::TextLink(source))
                    {
                        const std::string_view file(record.file, record.fileLength);
                        if (const usize mount = file.find(":/"); mount != std::string_view::npos && mount > 1)
                        {
                            (void)shell.run("editor.open_source",
                                            "{\"path\":" + jsonQuote(file) + ",\"line\":" +
                                                std::to_string(std::max(record.line, 1u)) + "}",
                                            "console");
                        }
                        else
                        {
                            ImGui::SetClipboardText(source);
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        if (state.scrollToBottom &&
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeight())
        {
            ImGui::SetScrollHereY(1.0f);
        }
        state.scrollToBottom = false;
    }
    ImGui::EndChild();

    // The command line: "name {json params}".
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputTextWithHint(text.hidden("console.command"), text.format(s::kConsoleCommandHint),
                                 state.command.data(), state.command.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
    {
        std::string_view line(state.command.data());
        while (!line.empty() && line.front() == ' ')
        {
            line.remove_prefix(1);
        }
        const usize space = line.find(' ');
        const std::string name(line.substr(0, space));
        const std::string params(space == std::string_view::npos ? std::string_view()
                                                                 : line.substr(space + 1));
        if (!name.empty())
        {
            if (const std::optional<std::string> result = shell.run(name, params, "console"))
            {
                ILL_LOG_INFO("Console", "{} -> {}", name,
                             result->size() > 2000 ? result->substr(0, 2000) + "..." : *result);
            }
            state.command[0] = '\0';
        }
        ImGui::SetKeyboardFocusHere(-1);
    }
}

} // namespace illusion::editor::shell::detail

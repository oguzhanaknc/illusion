// The History panel: the undo steps with origin badges, jumping to a step, and undoing all changes of an
// agent.

#include "Panels.hpp"

#include <illusion/editor/ui/Icons.hpp>

#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr usize kMaxAgents = 8;

} // namespace

void History::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const UndoStack& stack = shell.editor.history();
    const std::span<const UndoStep> steps = stack.steps();

    // "Undo <agent>'s changes" for each agent with applied steps.
    std::array<std::string_view, kMaxAgents> agents{};
    std::array<u32, kMaxAgents> counts{};
    usize agentCount = 0;
    for (usize i = 0; i < stack.position(); ++i)
    {
        const std::string_view origin = steps[i].origin;
        if (!origin.starts_with("rpc:"))
        {
            continue;
        }
        usize slot = 0;
        while (slot < agentCount && agents[slot] != origin)
        {
            ++slot;
        }
        if (slot == agentCount && agentCount < kMaxAgents)
        {
            agents[agentCount++] = origin;
        }
        if (slot < agentCount)
        {
            ++counts[slot];
        }
    }
    for (usize slot = 0; slot < agentCount; ++slot)
    {
        const std::string_view client = agents[slot].substr(4);
        if (ImGui::Button(text.iconLabel(ui::icons::kUndo, s::kHistoryUndoAgent,
                                         text.concat("history.undo_agent.", client), {{"client", client}})))
        {
            char params[64];
            std::snprintf(params, sizeof(params), R"({"steps":%u,"scope":"mine"})", counts[slot]);
            (void)shell.run("history.undo", params, "history", agents[slot]);
        }
        ImGui::SameLine();
    }
    if (agentCount > 0)
    {
        ImGui::NewLine();
        ImGui::Separator();
    }

    if (steps.empty())
    {
        ui::emptyState(kit, ui::icons::kHistory, text.format(s::kHistoryEmpty), "");
        return;
    }
    if (!ImGui::BeginChild(text.hidden("history.steps")))
    {
        ImGui::EndChild();
        return;
    }
    // Row 0 is the state before the first step; row i the state after step i.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(steps.size() + 1));
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const usize position = static_cast<usize>(row);
            const bool current = position == stack.position();
            const bool undone = position > stack.position();
            char id[48];
            std::snprintf(id, sizeof(id), "history.step.%zu", position);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  imvec(shell.theme.color(undone ? ui::ThemeToken::TextDisabled
                                                                 : ui::ThemeToken::TextPrimary)));
            const char* label = position == 0 ? text.label(s::kHistoryStart, id)
                                              : text.verbatim(steps[position - 1].label, id);
            if (ImGui::Selectable(label, current) && !current)
            {
                char params[48];
                std::snprintf(params, sizeof(params), R"({"position":%zu})", position);
                (void)shell.run("history.jump", params, "history");
            }
            ImGui::PopStyleColor();
            if (position > 0)
            {
                const std::string_view origin = steps[position - 1].origin;
                ImGui::SameLine();
                if (origin.starts_with("rpc:"))
                {
                    ui::badge(kit, text.copy(origin.substr(4)), ui::ThemeToken::Accent);
                }
                else
                {
                    ui::badge(kit, text.format(s::kHistoryOriginYou), ui::ThemeToken::TextSecondary);
                }
            }
        }
    }
    ImGui::EndChild();
}

} // namespace illusion::editor::shell::detail

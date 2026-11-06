// The Profiler panel: the editor's frame times and the time per place of the frame (CPU), the render passes
// of each viewport (GPU), and the problems that `diagnostics.get` finds (Diagnostics).

#include "Panels.hpp"

#include <illusion/core/FrameProfiler.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <algorithm>
#include <cstdio>
#include <implot.h>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

enum class Tab : u8
{
    Cpu,
    Gpu,
    Diagnostics,
};

} // namespace

struct ProfilerPanel::State
{
    // CPU: the graph's points and the places, refilled every frame without growing.
    std::vector<f32> times;
    std::vector<f32> indices;
    std::vector<ProfileScopeStats> places;
    // GPU: the viewports, read again when the host's list changes.
    std::vector<std::string> viewports;
    u64 viewportsVersion = ~u64{0};
    // Diagnostics: the latest `diagnostics.get`.
    std::vector<DiagnosticEntry> issues;
    u32 errors = 0;
    u32 warnings = 0;
    bool loaded = false;

    void refreshDiagnostics(Impl& shell)
    {
        loaded = true;
        issues.clear();
        errors = 0;
        warnings = 0;
        const std::optional<std::string> result = shell.run("diagnostics.get", "", "profiler");
        if (!result)
        {
            return;
        }
        const Expected<JsonDocument, JsonError> json = JsonDocument::parse(*result);
        if (!json)
        {
            return;
        }
        const JsonValue root = json->root();
        errors = static_cast<u32>(root.find("errors").asU64().value_or(0));
        warnings = static_cast<u32>(root.find("warnings").asU64().value_or(0));
        for (const JsonValue item : root.find("issues").elements())
        {
            issues.push_back(DiagnosticEntry{std::string(item.find("severity").asString().value_or("")),
                                             std::string(item.find("code").asString().value_or("")),
                                             std::string(item.find("subject").asString().value_or("")),
                                             static_cast<u32>(item.find("count").asU64().value_or(0)),
                                             std::string(item.find("message").asString().value_or(""))});
        }
    }
};

ProfilerPanel::ProfilerPanel()
    : m_state(std::make_unique<State>())
{
}

ProfilerPanel::~ProfilerPanel() = default;

namespace
{

void drawCpu(Impl& shell, ProfilerPanel::State& state)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const FrameProfiler& profiler = shell.editor.profiler();
    const usize count = profiler.frameCount();
    if (count == 0)
    {
        ui::emptyState(kit, ui::icons::kSpeed, text.format(s::kProfilerEmpty),
                       text.format(s::kProfilerEmptyHint));
        return;
    }
    // Oldest first, as the graph runs from left to right.
    state.times.resize(count);
    state.indices.resize(count);
    f32 total = 0.0f;
    f32 longest = 0.0f;
    bool truncated = false;
    for (usize i = 0; i < count; ++i)
    {
        const ProfileFrame frame = profiler.frame(count - 1 - i);
        state.times[i] = frame.durationMs;
        state.indices[i] = static_cast<f32>(i);
        total += frame.durationMs;
        longest = std::max(longest, frame.durationMs);
        truncated = truncated || frame.truncated;
    }
    const f32 average = total / static_cast<f32>(count);
    ImGui::TextUnformatted(text.format(s::kProfilerSummary,
                                       {{"average", static_cast<f64>(average)},
                                        {"max", static_cast<f64>(longest)},
                                        {"fps", average > 0.0f ? 1000.0 / static_cast<f64>(average) : 0.0}}));
    if (ImPlot::BeginPlot(text.hidden("profiler.graph"), ImVec2(-FLT_MIN, 120.0f * kit.scale),
                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText |
                              ImPlotFlags_NoLegend))
    {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0.0, static_cast<f64>(profiler.desc().frames - 1), 0.0,
                                std::max(16.7, static_cast<f64>(longest) * 1.1), ImPlotCond_Always);
        ImPlotSpec line;
        line.LineColor = imvec(shell.theme.color(ui::ThemeToken::Accent));
        line.LineWeight = 1.5f * kit.scale;
        ImPlot::PlotLine(text.format(s::kProfilerFrameTime), state.indices.data(), state.times.data(),
                         static_cast<int>(count), line);
        ImPlot::EndPlot();
    }

    profiler.summarize(count, state.places);
    if (ImGui::BeginTable(text.hidden("profiler.scopes"), 5,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(text.format(s::kProfilerColumnScope), ImGuiTableColumnFlags_WidthStretch,
                                3.0f);
        ImGui::TableSetupColumn(text.format(s::kProfilerColumnLast), ImGuiTableColumnFlags_WidthStretch,
                                1.0f);
        ImGui::TableSetupColumn(text.format(s::kProfilerColumnAverage), ImGuiTableColumnFlags_WidthStretch,
                                1.0f);
        ImGui::TableSetupColumn(text.format(s::kProfilerColumnMax), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(text.format(s::kProfilerColumnCalls), ImGuiTableColumnFlags_WidthStretch,
                                0.7f);
        ImGui::TableHeadersRow();
        for (const ProfileScopeStats& place : state.places)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // Indent(0) would indent by the default spacing, so places at depth 0 skip it.
            const f32 indent = static_cast<f32>(place.depth) * ImGui::GetStyle().IndentSpacing;
            if (indent > 0.0f)
            {
                ImGui::Indent(indent);
            }
            ImGui::TextUnformatted(text.copy(place.name));
            if (indent > 0.0f)
            {
                ImGui::Unindent(indent);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", static_cast<f64>(place.lastMs));
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", static_cast<f64>(place.averageMs));
            ImGui::TableNextColumn();
            ImGui::Text("%.2f", static_cast<f64>(place.maxMs));
            ImGui::TableNextColumn();
            ImGui::Text("%u", place.calls);
        }
        ImGui::EndTable();
    }
    if (truncated)
    {
        ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Warning)), "%s",
                           text.format(s::kProfilerTruncated));
    }
}

void drawGpu(Impl& shell, ProfilerPanel::State& state)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    ViewportHost* host = shell.editor.viewports();
    if (host == nullptr)
    {
        ui::emptyState(kit, ui::icons::kMemory, text.format(s::kProfilerGpuNone),
                       text.format(s::kProfilerGpuNoneHint));
        return;
    }
    if (host->viewportsVersion() != state.viewportsVersion)
    {
        state.viewportsVersion = host->viewportsVersion();
        state.viewports.clear();
        for (const ViewportInfo& info : host->viewports())
        {
            state.viewports.push_back(info.id);
        }
    }
    for (const std::string& id : state.viewports)
    {
        const Expected<ViewportRenderStats, CommandError> stats = host->renderStats(id);
        if (!stats)
        {
            continue;
        }
        ui::sectionHeader(kit, text.copy(id));
        const ViewportGpuTimings& gpu = stats->gpu;
        if (!gpu.measured)
        {
            ImGui::TextDisabled("%s", text.format(s::kProfilerGpuWaiting));
            continue;
        }
        if (ImGui::BeginTable(text.concat("profiler.gpu.", id), 2, ImGuiTableFlags_SizingStretchProp))
        {
            setupStretchColumns({3.0f, 1.0f});
            const std::pair<text::TextKey, f32> passes[] = {
                {s::kProfilerGpuShadows, gpu.shadows},
                {s::kProfilerGpuDepth, gpu.depth},
                {s::kProfilerGpuAmbientOcclusion, gpu.ambientOcclusion},
                {s::kProfilerGpuShading, gpu.shading},
                {s::kProfilerGpuPost, gpu.post},
                {s::kProfilerGpuTotal,
                 gpu.shadows + gpu.depth + gpu.ambientOcclusion + gpu.shading + gpu.post},
            };
            for (const auto& [name, ms] : passes)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text.format(name));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text.format(s::kStatusFrameTime, {{"ms", static_cast<f64>(ms)}}));
            }
            ImGui::EndTable();
        }
    }
}

// The localized line of @p issue (its English message for a code this panel does not know).
const char* issueText(ui::FrameText& text, const DiagnosticEntry& issue)
{
    const text::MessageArgument count{"count", static_cast<u64>(issue.count)};
    const text::MessageArgument subject{"subject", std::string_view(issue.subject)};
    if (issue.code == "render.validation")
    {
        return text.format(s::kDiagnosticsRenderValidation, {count});
    }
    if (issue.code == "render.lights_dropped")
    {
        return text.format(s::kDiagnosticsRenderLightsDropped, {count, subject});
    }
    if (issue.code == "log.errors")
    {
        return text.format(s::kDiagnosticsLogErrors, {count});
    }
    if (issue.code == "log.warnings")
    {
        return text.format(s::kDiagnosticsLogWarnings, {count});
    }
    if (issue.code == "asset.missing")
    {
        return text.format(s::kDiagnosticsAssetMissing, {count, subject});
    }
    if (issue.code == "prefab.unresolved")
    {
        return text.format(s::kDiagnosticsPrefabUnresolved, {subject});
    }
    return text.copy(issue.message);
}

// Shows where @p issue is: the Console for log messages, the asset in the browser, the entity selected.
void openIssue(Impl& shell, const DiagnosticEntry& issue)
{
    if (issue.code.starts_with("log."))
    {
        (void)shell.run("ui.show_panel", R"({"panel":"panel.console"})", "profiler");
    }
    else if (issue.code == "asset.missing")
    {
        const std::string_view path = issue.subject;
        const usize slash = path.rfind('/');
        shell.assetBrowser->reveal(slash == std::string_view::npos ? path : path.substr(0, slash));
        (void)shell.run("ui.show_panel", R"({"panel":"panel.assets"})", "profiler");
    }
    else if (issue.code == "prefab.unresolved")
    {
        (void)shell.run("selection.set", "{\"entities\":[" + jsonQuote(issue.subject) + "]}", "profiler");
    }
}

void drawDiagnostics(Impl& shell, ProfilerPanel::State& state)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    if (!state.loaded)
    {
        state.refreshDiagnostics(shell);
    }
    if (ImGui::Button(text.iconLabel(ui::icons::kRefresh, s::kDiagnosticsRefresh, "diagnostics.refresh")))
    {
        state.refreshDiagnostics(shell);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(
        text.format(s::kDiagnosticsSummary, {{"errors", static_cast<u64>(state.errors)},
                                             {"warnings", static_cast<u64>(state.warnings)}}));
    ImGui::Separator();
    if (state.issues.empty())
    {
        ui::emptyState(kit, ui::icons::kCheckCircle, text.format(s::kDiagnosticsNone),
                       text.format(s::kDiagnosticsNoneHint));
        return;
    }
    for (usize i = 0; i < state.issues.size(); ++i)
    {
        const DiagnosticEntry& issue = state.issues[i];
        const bool error = issue.severity == "error";
        ImGui::TextColored(imvec(shell.theme.color(error ? ui::ThemeToken::Error : ui::ThemeToken::Warning)),
                           "%s", error ? ui::icons::kError.c_str() : ui::icons::kWarning.c_str());
        ImGui::SameLine();
        char id[40];
        std::snprintf(id, sizeof(id), "diagnostics.issue.%zu", i);
        if (ImGui::Selectable(text.verbatim(issueText(text, issue), id)))
        {
            openIssue(shell, issue);
        }
        ui::richTooltip(kit, text.copy(issue.code), text.copy(issue.message), nullptr);
    }
}

} // namespace

void ProfilerPanel::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    State& state = *m_state;
    const bool paused = shell.editor.profiler().paused();
    if (ui::iconButton(kit, paused ? ui::icons::kPlayArrow : ui::icons::kPause,
                       paused ? "profiler.resume" : "profiler.pause",
                       text.format(paused ? s::kProfilerResume : s::kProfilerPause)))
    {
        (void)shell.run("profiler.set", paused ? R"({"paused":false})" : R"({"paused":true})", "profiler");
    }
    ImGui::SameLine();
    if (ui::iconButton(kit, ui::icons::kDeleteSweep, "profiler.clear", text.format(s::kProfilerClear)))
    {
        (void)shell.run("profiler.set", R"({"clear":true})", "profiler");
    }
    if (paused)
    {
        ImGui::SameLine();
        ui::badge(kit, text.format(s::kProfilerPaused), ui::ThemeToken::Warning);
    }
    if (!ImGui::BeginTabBar(text.hidden("profiler.tabs")))
    {
        return;
    }
    const std::pair<Tab, std::pair<text::TextKey, std::string_view>> tabs[] = {
        {Tab::Cpu, {s::kProfilerTabCpu, "profiler.tab.cpu"}},
        {Tab::Gpu, {s::kProfilerTabGpu, "profiler.tab.gpu"}},
        {Tab::Diagnostics, {s::kProfilerTabDiagnostics, "profiler.tab.diagnostics"}},
    };
    for (const auto& [tab, label] : tabs)
    {
        if (!ImGui::BeginTabItem(text.label(label.first, label.second)))
        {
            continue;
        }
        switch (tab)
        {
        case Tab::Cpu:
            drawCpu(shell, state);
            break;
        case Tab::Gpu:
            drawGpu(shell, state);
            break;
        case Tab::Diagnostics:
            drawDiagnostics(shell, state);
            break;
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

} // namespace illusion::editor::shell::detail

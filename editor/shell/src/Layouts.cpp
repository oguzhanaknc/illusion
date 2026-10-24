// The panel table and the built-in dock layouts.

#include "ShellImpl.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr PanelInfo kPanels[kPanelCount] = {
    {"panel.outliner", s::kPanelOutliner, "account_tree", true, ""},
    {"panel.inspector", s::kPanelInspector, "tune", true, ""},
    {"panel.scene", s::kPanelScene, "view_in_ar", true, "M7o"},
    {"panel.game", s::kPanelGame, "sports_esports", true, "M7r"},
    {"panel.assets", s::kPanelAssets, "folder", true, "M7u"},
    {"panel.console", s::kPanelConsole, "terminal", true, ""},
    {"panel.history", s::kPanelHistory, "history", true, ""},
    {"panel.ai_activity", s::kPanelAiActivity, "smart_toy", true, ""},
    {"panel.profiler", s::kPanelProfiler, "speed", true, "M7w"},
    {"panel.rendering", s::kPanelRendering, "brightness_6", true, "M7w"},
    {"panel.project_settings", s::kPanelProjectSettings, "settings_applications", true, "M7w"},
    {"window.preferences", s::kWindowPreferences, "settings", false, ""},
    {"window.welcome", s::kWindowWelcome, "waving_hand", false, ""},
    {"window.about", s::kWindowAbout, "info", false, ""},
    {"window.licenses", s::kWindowLicenses, "gavel", false, ""},
    {"window.notifications", s::kWindowNotifications, "notifications", false, ""},
};

// "###panel.console" for every panel, built once.
struct WindowIds
{
    std::array<std::array<char, 40>, kPanelCount> ids{};

    WindowIds() noexcept
    {
        for (usize i = 0; i < kPanelCount; ++i)
        {
            const std::string_view id = kPanels[i].id;
            ids[i][0] = '#';
            ids[i][1] = '#';
            ids[i][2] = '#';
            for (usize c = 0; c < id.size() && c + 4 < ids[i].size(); ++c)
            {
                ids[i][c + 3] = id[c];
            }
        }
    }
};

const WindowIds& windowIds() noexcept
{
    static const WindowIds ids;
    return ids;
}

void dock(Panel panel, ImGuiID node)
{
    ImGui::DockBuilderDockWindow(windowId(panel), node);
}

} // namespace

const PanelInfo& panelInfo(Panel panel) noexcept
{
    return kPanels[static_cast<usize>(panel)];
}

std::optional<Panel> findPanel(std::string_view id) noexcept
{
    for (usize i = 0; i < kPanelCount; ++i)
    {
        if (kPanels[i].id == id)
        {
            return static_cast<Panel>(i);
        }
    }
    return std::nullopt;
}

const char* windowId(Panel panel) noexcept
{
    return windowIds().ids[static_cast<usize>(panel)].data();
}

text::TextKey builtInLayoutTitle(std::string_view name) noexcept
{
    if (name == "default")
    {
        return s::kLayoutDefault;
    }
    if (name == "level-design")
    {
        return s::kLayoutLevelDesign;
    }
    if (name == "debug")
    {
        return s::kLayoutDebug;
    }
    if (name == "profiling")
    {
        return s::kLayoutProfiling;
    }
    if (name == "agent")
    {
        return s::kLayoutAgent;
    }
    return {};
}

std::array<bool, kPanelCount> builtInLayoutPanels(std::string_view name) noexcept
{
    std::array<bool, kPanelCount> open{};
    const auto show = [&](std::initializer_list<Panel> panels) {
        for (const Panel panel : panels)
        {
            open[static_cast<usize>(panel)] = true;
        }
    };
    show({Panel::Outliner, Panel::Inspector, Panel::Scene, Panel::Game, Panel::Console});
    if (name == "default")
    {
        show({Panel::Assets, Panel::History, Panel::Profiler, Panel::AiActivity, Panel::Rendering,
              Panel::ProjectSettings});
    }
    else if (name == "level-design")
    {
        show({Panel::Assets, Panel::History});
    }
    else if (name == "debug")
    {
        show({Panel::History, Panel::Profiler});
    }
    else if (name == "profiling")
    {
        show({Panel::Profiler, Panel::Rendering});
    }
    else if (name == "agent")
    {
        show({Panel::AiActivity, Panel::History});
    }
    return open;
}

void buildLayout(std::string_view name, u32 dockspace, Vec2 size)
{
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImVec2(size.x, size.y));
    ImGuiID center = dockspace;
    const auto split = [&](ImGuiDir direction, f32 ratio) {
        return ImGui::DockBuilderSplitNode(center, direction, ratio, nullptr, &center);
    };
    if (name == "level-design")
    {
        const ImGuiID left = split(ImGuiDir_Left, 0.22f);
        const ImGuiID right = split(ImGuiDir_Right, 0.30f);
        const ImGuiID bottom = split(ImGuiDir_Down, 0.20f);
        ImGuiID leftTop = left;
        const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f, nullptr, &leftTop);
        dock(Panel::Outliner, leftTop);
        dock(Panel::Assets, leftBottom);
        dock(Panel::Inspector, right);
        dock(Panel::Console, bottom);
        dock(Panel::History, bottom);
    }
    else if (name == "debug")
    {
        const ImGuiID bottom = split(ImGuiDir_Down, 0.38f);
        const ImGuiID left = split(ImGuiDir_Left, 0.18f);
        const ImGuiID right = split(ImGuiDir_Right, 0.30f);
        dock(Panel::Outliner, left);
        dock(Panel::Inspector, right);
        dock(Panel::History, right);
        dock(Panel::Console, bottom);
        dock(Panel::Profiler, bottom);
    }
    else if (name == "profiling")
    {
        const ImGuiID bottom = split(ImGuiDir_Down, 0.45f);
        const ImGuiID left = split(ImGuiDir_Left, 0.18f);
        const ImGuiID right = split(ImGuiDir_Right, 0.25f);
        dock(Panel::Outliner, left);
        dock(Panel::Rendering, right);
        dock(Panel::Inspector, right);
        dock(Panel::Profiler, bottom);
        dock(Panel::Console, bottom);
    }
    else if (name == "agent")
    {
        const ImGuiID right = split(ImGuiDir_Right, 0.34f);
        const ImGuiID left = split(ImGuiDir_Left, 0.20f);
        const ImGuiID bottom = split(ImGuiDir_Down, 0.25f);
        ImGuiID rightTop = right;
        const ImGuiID rightBottom =
            ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, nullptr, &rightTop);
        dock(Panel::Outliner, left);
        dock(Panel::Inspector, left);
        dock(Panel::AiActivity, rightTop);
        dock(Panel::History, rightBottom);
        dock(Panel::Console, bottom);
    }
    else
    {
        // The default: Outliner left (20%), Inspector tabs right (25%), tool panels along the bottom (28%).
        const ImGuiID left = split(ImGuiDir_Left, 0.20f);
        const ImGuiID right = split(ImGuiDir_Right, 0.3125f);
        const ImGuiID bottom = split(ImGuiDir_Down, 0.28f);
        dock(Panel::Outliner, left);
        dock(Panel::Inspector, right);
        dock(Panel::Rendering, right);
        dock(Panel::ProjectSettings, right);
        dock(Panel::Assets, bottom);
        dock(Panel::Console, bottom);
        dock(Panel::History, bottom);
        dock(Panel::Profiler, bottom);
        dock(Panel::AiActivity, bottom);
    }
    dock(Panel::Scene, center);
    dock(Panel::Game, center);
    ImGui::DockBuilderFinish(dockspace);
}

} // namespace illusion::editor::shell::detail

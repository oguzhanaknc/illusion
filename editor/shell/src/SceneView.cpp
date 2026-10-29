// The Scene panel: the "scene" viewport's image with camera navigation, click picking and a toolbar for the
// shading mode, the overlays and the standard views. The viewport itself lives behind ViewportHost.

#include "Panels.hpp"

#include <illusion/editor/ui/Icons.hpp>

#include <array>
#include <cstdio>
#include <imgui.h>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr std::string_view kViewport = "scene";

struct ShadingItem
{
    ViewportShading shading;
    text::TextKey title;
    std::string_view id;
    std::string_view params;
};

constexpr std::array kShadings = {
    ShadingItem{ViewportShading::Lit, s::kViewportShadingLit, "viewport.shading.lit", R"({"shading":"lit"})"},
    ShadingItem{ViewportShading::Unlit, s::kViewportShadingUnlit, "viewport.shading.unlit",
                R"({"shading":"unlit"})"},
    ShadingItem{ViewportShading::Albedo, s::kViewportShadingAlbedo, "viewport.shading.albedo",
                R"({"shading":"albedo"})"},
    ShadingItem{ViewportShading::Normals, s::kViewportShadingNormals, "viewport.shading.normals",
                R"({"shading":"normals"})"},
    ShadingItem{ViewportShading::Roughness, s::kViewportShadingRoughness, "viewport.shading.roughness",
                R"({"shading":"roughness"})"},
    ShadingItem{ViewportShading::Metallic, s::kViewportShadingMetallic, "viewport.shading.metallic",
                R"({"shading":"metallic"})"},
    ShadingItem{ViewportShading::AmbientOcclusion, s::kViewportShadingAmbientOcclusion,
                "viewport.shading.ambient_occlusion", R"({"shading":"ambientOcclusion"})"},
    ShadingItem{ViewportShading::ShadowCascades, s::kViewportShadingShadowCascades,
                "viewport.shading.shadow_cascades", R"({"shading":"shadowCascades"})"},
    ShadingItem{ViewportShading::MotionVectors, s::kViewportShadingMotionVectors,
                "viewport.shading.motion_vectors", R"({"shading":"motionVectors"})"},
};

struct ViewItem
{
    text::TextKey title;
    std::string_view id;
    std::string_view params;
};

constexpr std::array kViews = {
    ViewItem{s::kViewportViewPerspective, "viewport.view.perspective", R"({"view":"perspective"})"},
    ViewItem{s::kViewportViewTop, "viewport.view.top", R"({"view":"top"})"},
    ViewItem{s::kViewportViewBottom, "viewport.view.bottom", R"({"view":"bottom"})"},
    ViewItem{s::kViewportViewFront, "viewport.view.front", R"({"view":"front"})"},
    ViewItem{s::kViewportViewBack, "viewport.view.back", R"({"view":"back"})"},
    ViewItem{s::kViewportViewLeft, "viewport.view.left", R"({"view":"left"})"},
    ViewItem{s::kViewportViewRight, "viewport.view.right", R"({"view":"right"})"},
};

ImVec2 toImVec(Vec2 value) noexcept
{
    return ImVec2(value.x, value.y);
}

} // namespace

void SceneView::drawToolbar(Impl& shell)
{
    ui::FrameText& text = shell.text;
    if (ImGui::Button(text.iconLabel(ui::icons::kContrast, s::kViewportShading, "viewport.shading")))
    {
        ImGui::OpenPopup("###viewport.shading.menu");
    }
    if (ImGui::BeginPopup("###viewport.shading.menu"))
    {
        for (const ShadingItem& item : kShadings)
        {
            if (ImGui::MenuItem(text.label(item.title, item.id), nullptr,
                                m_last.settings.shading == item.shading))
            {
                (void)shell.run("viewport.set_mode", item.params, "ui");
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(text.label(s::kViewportWireframe, "viewport.wireframe"), nullptr,
                            m_last.settings.wireframe))
        {
            (void)shell.run("viewport.set_mode",
                            m_last.settings.wireframe ? R"({"wireframe":false})" : R"({"wireframe":true})",
                            "ui");
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(text.iconLabel(ui::icons::kLayers, s::kViewportOverlays, "viewport.overlays")))
    {
        ImGui::OpenPopup("###viewport.overlays.menu");
    }
    if (ImGui::BeginPopup("###viewport.overlays.menu"))
    {
        if (ImGui::MenuItem(text.label(s::kViewportGrid, "viewport.grid"), nullptr, m_last.settings.grid))
        {
            (void)shell.run("viewport.set_overlays",
                            m_last.settings.grid ? R"({"grid":false})" : R"({"grid":true})", "ui");
        }
        if (ImGui::MenuItem(text.label(s::kViewportOutline, "viewport.outline"), nullptr,
                            m_last.settings.selectionOutline))
        {
            (void)shell.run("viewport.set_overlays",
                            m_last.settings.selectionOutline ? R"({"selectionOutline":false})"
                                                             : R"({"selectionOutline":true})",
                            "ui");
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(text.iconLabel(ui::icons::k3dRotation, s::kViewportView, "viewport.view")))
    {
        ImGui::OpenPopup("###viewport.view.menu");
    }
    if (ImGui::BeginPopup("###viewport.view.menu"))
    {
        for (const ViewItem& item : kViews)
        {
            if (ImGui::MenuItem(text.label(item.title, item.id)))
            {
                (void)shell.run("viewport.view", item.params, "ui");
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(text.iconLabel(ui::icons::kCenterFocusStrong, s::kActionViewFrameSelected,
                                     "viewport.frame_selected")))
    {
        (void)shell.run("viewport.frame", "", "ui");
    }
}

void SceneView::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ViewportHost* host = shell.editor.viewports();
    if (host == nullptr)
    {
        shell.viewportNavigating = false;
        ui::emptyState(kit, ui::icons::kViewInAr, shell.text.format(s::kViewportUnavailableTitle),
                       shell.text.format(s::kViewportUnavailableHint));
        return;
    }
    drawToolbar(shell);

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 extent(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f));
    const Vec2 scale(io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    ImGui::InvisibleButton("###viewport.image", extent,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();

    ViewportInput input;
    input.cursor = (Vec2(io.MousePos.x, io.MousePos.y) - Vec2(origin.x, origin.y)) * scale;
    input.cursorDelta = Vec2(io.MouseDelta.x, io.MouseDelta.y) * scale;
    input.wheel = hovered ? io.MouseWheel : 0.0f;
    input.hovered = hovered;
    input.focused = ImGui::IsWindowFocused();
    input.buttons = {ImGui::IsMouseDown(ImGuiMouseButton_Left), ImGui::IsMouseDown(ImGuiMouseButton_Right),
                     ImGui::IsMouseDown(ImGuiMouseButton_Middle)};
    input.alt = io.KeyAlt;
    input.shift = io.KeyShift;
    const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
    };
    input.move =
        Vec3(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S));
    input.deltaSeconds = io.DeltaTime;

    const ui::Theme& theme = kit.theme;
    ViewportStyle style;
    style.selection = theme.color(ui::ThemeToken::ViewportSelection);
    style.grid = theme.color(ui::ThemeToken::TextDisabled);
    style.grid.w = 0.55f;
    style.axisX = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbX : ui::ThemeToken::AxisX);
    style.axisY = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbY : ui::ThemeToken::AxisY);
    style.axisZ = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbZ : ui::ThemeToken::AxisZ);

    const UVec2 pixels(static_cast<u32>(extent.x * scale.x + 0.5f),
                       static_cast<u32>(extent.y * scale.y + 0.5f));
    m_last = host->frame(kViewport, pixels, input, style);
    shell.viewportNavigating = m_last.navigating;
    if (m_last.texture != 0)
    {
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(m_last.texture), origin,
                                             toImVec(Vec2(origin.x, origin.y) + Vec2(extent.x, extent.y)));
    }

    // A click (no drag, no Alt) picks; Ctrl toggles and Shift adds. Clicking the same spot again cycles.
    if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !io.KeyAlt &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < io.MouseDragThreshold * io.MouseDragThreshold &&
        !m_last.navigating && m_last.size.x != 0)
    {
        // The image is stretched while a resize settles: map the cursor into it.
        const Vec2 pixel =
            glm::clamp(input.cursor / Vec2(pixels) * Vec2(m_last.size), Vec2(0.0f), Vec2(m_last.size) - 1.0f);
        const char* select = io.KeyCtrl ? "toggle" : io.KeyShift ? "add" : "replace";
        std::array<char, 128> params{};
        const int written =
            std::snprintf(params.data(), params.size(), R"({"x":%.1f,"y":%.1f,"cycle":true,"select":"%s"})",
                          static_cast<double>(pixel.x), static_cast<double>(pixel.y), select);
        if (written > 0 && static_cast<usize>(written) < params.size())
        {
            (void)shell.run("viewport.pick", std::string_view(params.data(), static_cast<usize>(written)),
                            "ui");
        }
    }
}

} // namespace illusion::editor::shell::detail

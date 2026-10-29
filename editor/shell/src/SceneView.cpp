// The viewport panels: the image of one viewport (Scene, Game or a secondary Scene viewport) with camera
// navigation, click and box picking, camera bookmarks and a toolbar for the shading mode, the overlays, the
// standard views and the aspect. The viewports themselves live behind ViewportHost.

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

struct AspectItem
{
    ViewportAspect aspect;
    text::TextKey title;
    std::string_view id;
    std::string_view params;
};

constexpr std::array kAspects = {
    AspectItem{ViewportAspect::Free, s::kViewportAspectFree, "viewport.aspect.free", R"({"aspect":"free"})"},
    AspectItem{ViewportAspect::Wide16x9, s::kViewportAspect16x9, "viewport.aspect.16x9",
               R"({"aspect":"16:9"})"},
    AspectItem{ViewportAspect::Wide16x10, s::kViewportAspect16x10, "viewport.aspect.16x10",
               R"({"aspect":"16:10"})"},
    AspectItem{ViewportAspect::Standard4x3, s::kViewportAspect4x3, "viewport.aspect.4x3",
               R"({"aspect":"4:3"})"},
    AspectItem{ViewportAspect::Ultrawide21x9, s::kViewportAspect21x9, "viewport.aspect.21x9",
               R"({"aspect":"21:9"})"},
    AspectItem{ViewportAspect::Square, s::kViewportAspect1x1, "viewport.aspect.1x1", R"({"aspect":"1:1"})"},
};

// Fixed resolutions of the aspect menu.
constexpr std::array<UVec2, 3> kResolutions = {UVec2(640, 360), UVec2(1280, 720), UVec2(1920, 1080)};

ImVec2 toImVec(Vec2 value) noexcept
{
    return ImVec2(value.x, value.y);
}

// @p params (a JSON object, "" for none) with the viewport id added in front.
std::string withViewport(std::string_view id, std::string_view params)
{
    std::string out = "{\"viewport\":" + jsonQuote(id);
    if (params.size() > 2)
    {
        out += ',';
        out += params.substr(1);
    }
    else
    {
        out += '}';
    }
    return out;
}

// The largest rectangle of @p image's shape inside @p panel, centred (top left, size).
std::pair<Vec2, Vec2> fitImage(Vec2 panel, UVec2 image) noexcept
{
    if (image.x == 0 || image.y == 0 || panel.x <= 0.0f || panel.y <= 0.0f)
    {
        return {Vec2(0.0f), panel};
    }
    const f32 scale = std::min(panel.x / static_cast<f32>(image.x), panel.y / static_cast<f32>(image.y));
    const Vec2 size = Vec2(image) * scale;
    return {(panel - size) * 0.5f, size};
}

} // namespace

SceneView::SceneView(std::string id)
    : m_id(std::move(id))
{
}

void SceneView::run(Impl& shell, std::string_view command, std::string_view params)
{
    (void)shell.run(command, withViewport(m_id, params), "ui");
}

void SceneView::drawAspectMenu(Impl& shell)
{
    ui::FrameText& text = shell.text;
    if (ImGui::Button(text.iconLabel(ui::icons::kAspectRatio, s::kViewportAspect, "viewport.aspect")))
    {
        ImGui::OpenPopup("###viewport.aspect.menu");
    }
    if (!ImGui::BeginPopup("###viewport.aspect.menu"))
    {
        return;
    }
    const ViewportSettings& settings = m_last.settings;
    for (const AspectItem& item : kAspects)
    {
        if (ImGui::MenuItem(text.label(item.title, item.id), nullptr, settings.aspect == item.aspect))
        {
            run(shell, "viewport.set_aspect", item.params);
        }
    }
    ImGui::Separator();
    for (const UVec2 resolution : kResolutions)
    {
        std::array<char, 40> id{};
        (void)std::snprintf(id.data(), id.size(), "viewport.aspect.%ux%u", resolution.x, resolution.y);
        const bool chosen = settings.aspect == ViewportAspect::Fixed && settings.width == resolution.x &&
                            settings.height == resolution.y;
        if (ImGui::MenuItem(text.label(s::kViewportAspectFixed, id.data(),
                                       {{"width", static_cast<u64>(resolution.x)},
                                        {"height", static_cast<u64>(resolution.y)}}),
                            nullptr, chosen))
        {
            std::array<char, 80> params{};
            (void)std::snprintf(params.data(), params.size(), R"({"aspect":"fixed","width":%u,"height":%u})",
                                resolution.x, resolution.y);
            run(shell, "viewport.set_aspect", params.data());
        }
    }
    ImGui::EndPopup();
}

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
                run(shell, "viewport.set_mode", item.params);
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(text.label(s::kViewportWireframe, "viewport.wireframe"), nullptr,
                            m_last.settings.wireframe))
        {
            run(shell, "viewport.set_mode",
                m_last.settings.wireframe ? R"({"wireframe":false})" : R"({"wireframe":true})");
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
        if (m_last.kind == ViewportKind::Scene &&
            ImGui::MenuItem(text.label(s::kViewportGrid, "viewport.grid"), nullptr, m_last.settings.grid))
        {
            run(shell, "viewport.set_overlays",
                m_last.settings.grid ? R"({"grid":false})" : R"({"grid":true})");
        }
        if (ImGui::MenuItem(text.label(s::kViewportOutline, "viewport.outline"), nullptr,
                            m_last.settings.selectionOutline))
        {
            run(shell, "viewport.set_overlays",
                m_last.settings.selectionOutline ? R"({"selectionOutline":false})"
                                                 : R"({"selectionOutline":true})");
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (m_last.kind == ViewportKind::Scene)
    {
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
                    run(shell, "viewport.view", item.params);
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(text.iconLabel(ui::icons::kCenterFocusStrong, s::kActionViewFrameSelected,
                                         "viewport.frame_selected")))
        {
            run(shell, "viewport.frame", "");
        }
        ImGui::SameLine();
    }
    drawAspectMenu(shell);
    if (m_id == "scene")
    {
        ImGui::SameLine();
        if (ImGui::Button(text.iconLabel(ui::icons::kAddToQueue, s::kViewportNew, "viewport.new")))
        {
            (void)shell.run("viewport.create", "", "ui");
        }
    }
}

// Ctrl+1..9 moves the camera to a bookmark, Ctrl+Shift+1..9 stores it.
void SceneView::handleBookmarks(Impl& shell, bool active)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!active || !io.KeyCtrl || io.KeyAlt || m_last.kind != ViewportKind::Scene)
    {
        return;
    }
    for (u32 slot = 1; slot <= kCameraBookmarkSlots; ++slot)
    {
        if (!ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(slot)), false))
        {
            continue;
        }
        std::array<char, 32> params{};
        (void)std::snprintf(params.data(), params.size(), R"({"slot":%u})", slot);
        run(shell, io.KeyShift ? "camera.bookmark_set" : "camera.bookmark_go", params.data());
    }
}

void SceneView::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ViewportHost* host = shell.editor.viewports();
    if (host == nullptr)
    {
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
    const bool focused = ImGui::IsWindowFocused();

    // Where the image goes in the panel: letterboxed when its shape differs (the shape of the last frame).
    const Vec2 panelOrigin(origin.x, origin.y);
    const UVec2 pixels(static_cast<u32>(extent.x * scale.x + 0.5f),
                       static_cast<u32>(extent.y * scale.y + 0.5f));
    const UVec2 shape = m_last.settings.aspect == ViewportAspect::Free
                            ? UVec2(0)
                            : viewportImageSize(pixels, m_last.settings);
    const auto [offset, display] = fitImage(Vec2(extent.x, extent.y), shape);
    const Vec2 imageOrigin = panelOrigin + offset;
    // Cursor positions in image pixels.
    const UVec2 imageSize = m_last.size.x != 0 ? m_last.size : pixels;
    const Vec2 toImage = Vec2(imageSize) / glm::max(display, Vec2(1.0f));

    ViewportInput input;
    input.cursor = (Vec2(io.MousePos.x, io.MousePos.y) - imageOrigin) * toImage;
    input.cursorDelta = Vec2(io.MouseDelta.x, io.MouseDelta.y) * toImage;
    input.wheel = hovered ? io.MouseWheel : 0.0f;
    input.hovered = hovered;
    input.focused = focused;
    input.buttons = {ImGui::IsMouseDown(ImGuiMouseButton_Left), ImGui::IsMouseDown(ImGuiMouseButton_Right),
                     ImGui::IsMouseDown(ImGuiMouseButton_Middle)};
    input.alt = io.KeyAlt;
    input.shift = io.KeyShift;
    const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
        return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
    };
    // Ctrl+digits are bookmarks, not flying.
    if (!io.KeyCtrl)
    {
        input.move =
            Vec3(axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S));
    }
    input.deltaSeconds = io.DeltaTime;

    const ui::Theme& theme = kit.theme;
    ViewportStyle style;
    style.selection = theme.color(ui::ThemeToken::ViewportSelection);
    style.grid = theme.color(ui::ThemeToken::TextDisabled);
    style.grid.w = 0.55f;
    style.axisX = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbX : ui::ThemeToken::AxisX);
    style.axisY = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbY : ui::ThemeToken::AxisY);
    style.axisZ = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbZ : ui::ThemeToken::AxisZ);

    m_last = host->frame(m_id, pixels, input, style);
    shell.viewportNavigating = shell.viewportNavigating || m_last.navigating;
    ImDrawList& draw = *ImGui::GetWindowDrawList();
    if (m_last.texture != 0)
    {
        draw.AddImage(static_cast<ImTextureID>(m_last.texture), toImVec(imageOrigin),
                      toImVec(imageOrigin + display));
    }
    else if (!m_last.hasCamera)
    {
        ImGui::SetCursorScreenPos(origin);
        ui::emptyState(kit, ui::icons::kVideocamOff, shell.text.format(s::kViewportNoCameraTitle),
                       shell.text.format(s::kViewportNoCameraHint));
    }
    handleBookmarks(shell, hovered || focused);

    if (m_last.size.x == 0)
    {
        m_boxing = false;
        return;
    }
    // The cursor in image pixels, inside the image.
    const auto imagePixel = [&](ImVec2 position) {
        return glm::clamp((Vec2(position.x, position.y) - imageOrigin) * toImage, Vec2(0.0f),
                          Vec2(m_last.size) - 1.0f);
    };
    const f32 threshold = io.MouseDragThreshold * io.MouseDragThreshold;
    const bool dragged = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] >= threshold;
    // A left drag without Alt draws a selection box.
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt && dragged &&
        !m_last.navigating)
    {
        m_boxing = true;
    }
    if (m_boxing && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 start = io.MouseClickedPos[ImGuiMouseButton_Left];
        draw.AddRectFilled(start, io.MousePos,
                           ImGui::GetColorU32(detail::imvec(Vec4(Vec3(style.selection), 0.15f))));
        draw.AddRect(start, io.MousePos, ImGui::GetColorU32(detail::imvec(style.selection)));
    }
    const char* select = io.KeyCtrl ? "toggle" : io.KeyShift ? "add" : "replace";
    if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (m_boxing)
        {
            const Vec2 a = imagePixel(io.MouseClickedPos[ImGuiMouseButton_Left]);
            const Vec2 b = imagePixel(io.MousePos);
            const Vec2 low = glm::min(a, b);
            const Vec2 size = glm::max(glm::abs(b - a), Vec2(1.0f));
            std::array<char, 160> params{};
            const int written = std::snprintf(
                params.data(), params.size(),
                R"({"x":%.1f,"y":%.1f,"width":%.1f,"height":%.1f,"select":"%s"})", static_cast<double>(low.x),
                static_cast<double>(low.y), static_cast<double>(size.x), static_cast<double>(size.y), select);
            if (written > 0 && static_cast<usize>(written) < params.size())
            {
                run(shell, "viewport.pick_rect", params.data());
            }
        }
        // A click (no drag, no Alt) picks; Ctrl toggles and Shift adds. Clicking the same spot again cycles.
        else if (!io.KeyAlt && !dragged && !m_last.navigating)
        {
            const Vec2 pixel = imagePixel(io.MousePos);
            std::array<char, 128> params{};
            const int written = std::snprintf(
                params.data(), params.size(), R"({"x":%.1f,"y":%.1f,"cycle":true,"select":"%s"})",
                static_cast<double>(pixel.x), static_cast<double>(pixel.y), select);
            if (written > 0 && static_cast<usize>(written) < params.size())
            {
                run(shell, "viewport.pick", params.data());
            }
        }
        m_boxing = false;
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_boxing = false;
    }
}

void drawSecondaryViewports(Impl& shell)
{
    ViewportHost* host = shell.editor.viewports();
    if (host == nullptr)
    {
        shell.secondaryViews.clear();
        return;
    }
    // The list changes only when a viewport opens or closes.
    if (host->viewportsVersion() != shell.secondaryVersion)
    {
        std::vector<std::unique_ptr<SceneView>> views;
        for (const ViewportInfo& info : host->viewports())
        {
            if (info.kind != ViewportKind::Scene || info.id == "scene")
            {
                continue;
            }
            const auto kept = std::find_if(shell.secondaryViews.begin(), shell.secondaryViews.end(),
                                           [&](const std::unique_ptr<SceneView>& view) {
                                               return view != nullptr && view->id() == info.id;
                                           });
            views.push_back(kept != shell.secondaryViews.end() ? std::move(*kept)
                                                               : std::make_unique<SceneView>(info.id));
        }
        shell.secondaryViews = std::move(views);
        shell.secondaryVersion = host->viewportsVersion();
    }
    ui::FrameText& text = shell.text;
    const ui::UiKit kit = shell.kit();
    for (const std::unique_ptr<SceneView>& view : shell.secondaryViews)
    {
        const std::string_view id = view->id();
        // "scene-2" is "Scene 2".
        const std::string_view number = id.substr(id.find('-') + 1);
        bool keep = true;
        ImGui::SetNextWindowSize(ImVec2(640.0f * kit.scale, 400.0f * kit.scale), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(text.iconLabel(ui::icons::kViewInAr, s::kViewportSecondary,
                                        text.concat("panel.viewport.", id), {{"number", number}}),
                         &keep))
        {
            view->draw(shell);
        }
        ImGui::End();
        if (!keep)
        {
            (void)shell.run("viewport.close", withViewport(id, ""), "ui");
        }
    }
}

} // namespace illusion::editor::shell::detail

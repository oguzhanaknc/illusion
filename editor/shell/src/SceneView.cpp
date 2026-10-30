// The viewport panels: the image of one viewport (Scene, Game or a secondary Scene viewport) with camera
// navigation, click and box picking, camera bookmarks, the view cube and axis navigator, icon billboards, the
// stats overlay, the picture-in-picture camera preview, the Inspector's eyedropper and a toolbar for the
// shading mode, the overlays, the standard views and the aspect. The viewports themselves live behind
// ViewportHost.

#include "Panels.hpp"

#include <illusion/ecs/Name.hpp>
#include <illusion/editor/core/ViewCube.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <array>
#include <cmath>
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

struct VisualiserItem
{
    bool ViewportVisualisers::* kind;
    text::TextKey title;
    std::string_view id;
    std::string_view name;
};

constexpr std::array kVisualiserItems = {
    VisualiserItem{&ViewportVisualisers::lights, s::kViewportVisualisersLights, "viewport.visualisers.lights",
                   "lights"},
    VisualiserItem{&ViewportVisualisers::cameras, s::kViewportVisualisersCameras,
                   "viewport.visualisers.cameras", "cameras"},
    VisualiserItem{&ViewportVisualisers::colliders, s::kViewportVisualisersColliders,
                   "viewport.visualisers.colliders", "colliders"},
    VisualiserItem{&ViewportVisualisers::characters, s::kViewportVisualisersCharacters,
                   "viewport.visualisers.characters", "characters"},
    VisualiserItem{&ViewportVisualisers::joints, s::kViewportVisualisersJoints, "viewport.visualisers.joints",
                   "joints"},
    VisualiserItem{&ViewportVisualisers::audio, s::kViewportVisualisersAudio, "viewport.visualisers.audio",
                   "audio"},
};

struct OverlayItem
{
    bool ViewportSettings::* flag;
    text::TextKey title;
    std::string_view id;
    std::string_view name;
    bool sceneOnly;
};

constexpr std::array kOverlayItems = {
    OverlayItem{&ViewportSettings::grid, s::kViewportGrid, "viewport.grid", "grid", true},
    OverlayItem{&ViewportSettings::selectionOutline, s::kViewportOutline, "viewport.outline",
                "selectionOutline", false},
    OverlayItem{&ViewportSettings::hover, s::kViewportHover, "viewport.hover", "hover", false},
    OverlayItem{&ViewportSettings::icons, s::kViewportIcons, "viewport.icons", "icons", true},
    OverlayItem{&ViewportSettings::viewCube, s::kViewportViewCube, "viewport.view_cube", "viewCube", true},
    OverlayItem{&ViewportSettings::cameraPreview, s::kViewportCameraPreview, "viewport.camera_preview",
                "cameraPreview", true},
    OverlayItem{&ViewportSettings::stats, s::kViewportStats, "viewport.stats", "stats", false},
};

// The view cube's half size and margin, the axis navigator's length and the icon size, in unscaled pixels.
constexpr f32 kCubeSize = 26.0f;
constexpr f32 kCubeMargin = 14.0f;
constexpr f32 kAxisLength = 22.0f;
constexpr f32 kAxisEnd = 7.0f;
constexpr f32 kIconRadius = 11.0f;
constexpr f32 kOverlayMargin = 10.0f;
// Faces of the cube darker than this are not labelled.
constexpr f32 kLabelFacing = 0.35f;

ImU32 color32(Vec4 color) noexcept
{
    return ImGui::GetColorU32(detail::imvec(color));
}

Vec4 withAlpha(Vec4 color, f32 alpha) noexcept
{
    return Vec4(Vec3(color), alpha);
}

ui::Icon iconOf(ViewportIconKind kind) noexcept
{
    switch (kind)
    {
    case ViewportIconKind::Camera:
        return ui::icons::kVideocam;
    case ViewportIconKind::DirectionalLight:
        return ui::icons::kLightMode;
    case ViewportIconKind::PointLight:
        return ui::icons::kLightbulb;
    case ViewportIconKind::SkyLight:
        return ui::icons::kCloud;
    case ViewportIconKind::AudioSource:
        return ui::icons::kVolumeUp;
    case ViewportIconKind::AudioListener:
        return ui::icons::kHearing;
    }
    return ui::icons::kVideocam;
}

text::TextKey axisLabel(Vec3 direction) noexcept
{
    return direction.x != 0.0f   ? s::kViewportAxisX
           : direction.y != 0.0f ? s::kViewportAxisY
                                 : s::kViewportAxisZ;
}

// Where the overlays of a panel are, in screen pixels.
struct OverlayLayout
{
    Vec2 cube{0.0f};      // centre of the view cube
    f32 cubeScale = 0.0f; // pixels per cube unit
    Vec2 axes{0.0f};      // centre of the axis navigator
    f32 axisLength = 0.0f;
    f32 axisEnd = 0.0f;
    bool cubeShown = false;
};

OverlayLayout overlayLayout(Vec2 imageOrigin, Vec2 display, f32 scale, bool cube) noexcept
{
    OverlayLayout layout;
    layout.cubeShown = cube;
    layout.cubeScale = kCubeSize * scale;
    // A cube corner is sqrt(3) units from its centre.
    const f32 reach = layout.cubeScale * 1.75f;
    layout.cube = imageOrigin + Vec2(display.x - kCubeMargin * scale - reach, kCubeMargin * scale + reach);
    layout.axisLength = kAxisLength * scale;
    layout.axisEnd = kAxisEnd * scale;
    layout.axes = layout.cube + Vec2(0.0f, reach + layout.axisLength + layout.axisEnd * 2.0f);
    return layout;
}

// The axis end under @p mouse (std::nullopt for none).
std::optional<Vec3> axisEndAt(const OverlayLayout& layout, const Quat& rotation, Vec2 mouse) noexcept
{
    if (!layout.cubeShown)
    {
        return std::nullopt;
    }
    const std::array<ViewAxisEnd, 6> ends = viewAxisEnds(rotation);
    // Nearest first: the end drawn on top wins.
    for (auto it = ends.rbegin(); it != ends.rend(); ++it)
    {
        if (glm::length(layout.axes + it->point * layout.axisLength - mouse) <= layout.axisEnd)
        {
            return it->direction;
        }
    }
    return std::nullopt;
}

bool overCube(const OverlayLayout& layout, Vec2 mouse) noexcept
{
    return layout.cubeShown && glm::length(mouse - layout.cube) <= layout.cubeScale * 1.75f;
}

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

// The view cube and the axis navigator of @p layout for @p camera, with the part under @p mouse highlighted.
void drawViewCube(Impl& shell, ImDrawList& draw, const OverlayLayout& layout, const ViewportCamera& camera,
                  Vec2 mouse, bool hovered)
{
    const ui::Theme& theme = shell.theme;
    ui::FrameText& text = shell.text;
    const Quat rotation = viewportRotation(camera);
    const std::optional<Vec3> hit = hovered && overCube(layout, mouse)
                                        ? viewCubePick(rotation, (mouse - layout.cube) / layout.cubeScale)
                                        : std::nullopt;
    std::array<ViewCubeFace, 6> faces{};
    const u32 count = viewCubeFaces(rotation, faces);
    const Vec4 fill = theme.color(ui::ThemeToken::BgRaised);
    const Vec4 border = theme.color(ui::ThemeToken::Border);
    const Vec4 accent = theme.color(ui::ThemeToken::Accent);
    const Vec4 label = theme.color(ui::ThemeToken::TextPrimary);
    const f32 fontSize = ImGui::GetFontSize() * 0.7f;
    for (u32 i = 0; i < count; ++i)
    {
        const ViewCubeFace& face = faces[i];
        std::array<ImVec2, 4> points{};
        for (usize c = 0; c < points.size(); ++c)
        {
            points[c] = toImVec(layout.cube + face.corners[c] * layout.cubeScale);
        }
        const bool faceHit = hit && *hit == face.normal;
        // Faces turned away from the camera are darker.
        const Vec4 shade =
            faceHit ? withAlpha(accent, 0.9f) : Vec4(Vec3(fill) * (0.55f + 0.45f * face.facing), 0.92f);
        draw.AddConvexPolyFilled(points.data(), static_cast<int>(points.size()), color32(shade));
        draw.AddPolyline(points.data(), static_cast<int>(points.size()), color32(border), 1.0f,
                         ImDrawFlags_Closed);
        if (face.facing >= kLabelFacing)
        {
            const char* name = text.format(kViews[static_cast<usize>(face.view)].title);
            const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name);
            const Vec2 at = layout.cube + face.center * layout.cubeScale - Vec2(size.x, size.y) * 0.5f;
            draw.AddText(ImGui::GetFont(), fontSize, toImVec(at), color32(withAlpha(label, face.facing)),
                         name);
        }
    }
    // Edges and corners light up as a dot where they are.
    if (hit && !viewCubeView(*hit))
    {
        const Vec2 at = layout.cube + viewCubeProject(rotation, *hit) * layout.cubeScale;
        draw.AddCircleFilled(toImVec(at), layout.cubeScale * 0.22f, color32(accent));
    }

    // The axis navigator: lines to the positive ends, filled circles at those and hollow ones opposite.
    const std::array<ViewAxisEnd, 6> ends = viewAxisEnds(rotation);
    const std::optional<Vec3> endHit = hovered ? axisEndAt(layout, rotation, mouse) : std::nullopt;
    const ui::UiKit kit = shell.kit();
    for (const ViewAxisEnd& end : ends)
    {
        const Vec3 axis = glm::abs(end.direction);
        const ui::ThemeToken token =
            axis.x > 0.0f   ? (kit.colorBlindAxes ? ui::ThemeToken::AxisCbX : ui::ThemeToken::AxisX)
            : axis.y > 0.0f ? (kit.colorBlindAxes ? ui::ThemeToken::AxisCbY : ui::ThemeToken::AxisY)
                            : (kit.colorBlindAxes ? ui::ThemeToken::AxisCbZ : ui::ThemeToken::AxisZ);
        const Vec4 color = endHit && *endHit == end.direction ? accent : theme.color(token);
        const bool positive = end.direction.x + end.direction.y + end.direction.z > 0.0f;
        const Vec2 at = layout.axes + end.point * layout.axisLength;
        if (positive)
        {
            draw.AddLine(toImVec(layout.axes), toImVec(at), color32(color), 2.0f * kit.scale);
            draw.AddCircleFilled(toImVec(at), layout.axisEnd, color32(color));
            const char* name = text.format(axisLabel(end.direction));
            const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name);
            draw.AddText(ImGui::GetFont(), fontSize, toImVec(at - Vec2(size.x, size.y) * 0.5f),
                         color32(theme.color(ui::ThemeToken::BgBase)), name);
        }
        else
        {
            draw.AddCircleFilled(toImVec(at), layout.axisEnd,
                                 color32(withAlpha(theme.color(ui::ThemeToken::BgBase), 0.7f)));
            draw.AddCircle(toImVec(at), layout.axisEnd, color32(color), 0, 1.5f * kit.scale);
        }
    }
}

// The icon under @p mouse among @p icons drawn at @p imageOrigin with @p toPanel (image pixels to panel).
const ViewportIcon* iconAt(std::span<const ViewportIcon> icons, Vec2 imageOrigin, f32 toPanel, f32 radius,
                           Vec2 mouse) noexcept
{
    // Nearest first: the icon drawn on top wins.
    for (auto it = icons.rbegin(); it != icons.rend(); ++it)
    {
        if (glm::length(imageOrigin + it->pixel * toPanel - mouse) <= radius)
        {
            return &*it;
        }
    }
    return nullptr;
}

void drawIcons(Impl& shell, ImDrawList& draw, std::span<const ViewportIcon> icons, Vec2 imageOrigin,
               f32 toPanel, f32 radius, const ViewportIcon* hovered)
{
    const ui::Theme& theme = shell.theme;
    const Vec4 background = withAlpha(theme.color(ui::ThemeToken::BgPanel), 0.75f);
    const f32 fontSize = radius * 1.3f;
    for (const ViewportIcon& icon : icons)
    {
        const Vec2 at = imageOrigin + icon.pixel * toPanel;
        const Vec4 color = icon.selected      ? theme.color(ui::ThemeToken::ViewportSelection)
                           : &icon == hovered ? theme.color(ui::ThemeToken::ViewportHover)
                                              : theme.color(ui::ThemeToken::TextPrimary);
        draw.AddCircleFilled(toImVec(at), radius, color32(background));
        draw.AddCircle(toImVec(at), radius, color32(withAlpha(color, 0.8f)), 0, 1.0f);
        const char* glyph = iconOf(icon.kind).c_str();
        const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyph);
        draw.AddText(ImGui::GetFont(), fontSize, toImVec(at - Vec2(size.x, size.y) * 0.5f), color32(color),
                     glyph);
    }
}

// The statistics of the last frame of @p id in the top left corner of the image.
void drawStats(Impl& shell, ImDrawList& draw, ViewportHost& host, std::string_view id, Vec2 imageOrigin,
               UVec2 size)
{
    const Expected<ViewportRenderStats, CommandError> found = host.renderStats(id);
    if (!found)
    {
        return;
    }
    const ViewportRenderStats& stats = *found;
    ui::FrameText& text = shell.text;
    std::array<char, 16> ms{};
    std::array<char, 16> fps{};
    (void)std::snprintf(ms.data(), ms.size(), "%.2f", shell.frameMs);
    (void)std::snprintf(fps.data(), fps.size(), "%.0f", shell.frameMs > 0.0 ? 1000.0 / shell.frameMs : 0.0);
    const std::array<const char*, 6> lines = {
        text.format(s::kViewportStatsFrame,
                    {{"ms", std::string_view(ms.data())}, {"fps", std::string_view(fps.data())}}),
        text.format(s::kViewportStatsDrawCalls, {{"count", static_cast<u64>(stats.drawCalls)}}),
        text.format(s::kViewportStatsTriangles, {{"count", static_cast<u64>(stats.triangles)}}),
        text.format(s::kViewportStatsVisible, {{"visible", static_cast<u64>(stats.visible)},
                                               {"candidates", static_cast<u64>(stats.candidates)}}),
        text.format(s::kViewportStatsShadows, {{"count", static_cast<u64>(stats.shadowDrawCalls)}}),
        text.format(s::kViewportStatsSize,
                    {{"width", static_cast<u64>(size.x)}, {"height", static_cast<u64>(size.y)}}),
    };
    const f32 scale = shell.kit().scale;
    const f32 lineHeight = ImGui::GetTextLineHeight();
    f32 width = 0.0f;
    for (const char* line : lines)
    {
        width = std::max(width, ImGui::CalcTextSize(line).x);
    }
    const Vec2 padding(6.0f * scale);
    const Vec2 at = imageOrigin + Vec2(kOverlayMargin * scale);
    const Vec2 extent(width, lineHeight * static_cast<f32>(lines.size()));
    draw.AddRectFilled(toImVec(at), toImVec(at + extent + padding * 2.0f),
                       color32(withAlpha(shell.theme.color(ui::ThemeToken::BgBase), 0.7f)), 4.0f * scale);
    const ImU32 color = color32(shell.theme.color(ui::ThemeToken::TextPrimary));
    for (usize i = 0; i < lines.size(); ++i)
    {
        draw.AddText(toImVec(at + padding + Vec2(0.0f, lineHeight * static_cast<f32>(i))), color, lines[i]);
    }
}

// The picture-in-picture view through the selected Camera in the bottom right corner of the image.
void drawPreview(Impl& shell, ImDrawList& draw, const ViewportFrame& frame, Vec2 imageOrigin, Vec2 display,
                 Vec2 framebufferScale)
{
    const f32 scale = shell.kit().scale;
    const Vec2 size = Vec2(frame.previewSize) / glm::max(framebufferScale, Vec2(1e-3f));
    const Vec2 at = imageOrigin + display - size - Vec2(kOverlayMargin * scale);
    if (at.x < imageOrigin.x || at.y < imageOrigin.y)
    {
        return; // the panel is too small
    }
    draw.AddImage(static_cast<ImTextureID>(frame.preview), toImVec(at), toImVec(at + size));
    draw.AddRect(toImVec(at), toImVec(at + size), color32(shell.theme.color(ui::ThemeToken::Border)), 0.0f,
                 1.0f);
    World& world = shell.editor.world();
    if (const Name* name =
            world.isAlive(frame.previewCamera) ? world.tryGet<Name>(frame.previewCamera) : nullptr;
        name != nullptr && !name->value.empty())
    {
        const char* caption = shell.text.copy(name->value);
        const Vec2 padding(4.0f * scale);
        const ImVec2 textSize = ImGui::CalcTextSize(caption);
        draw.AddRectFilled(toImVec(at), toImVec(at + Vec2(textSize.x, textSize.y) + padding * 2.0f),
                           color32(withAlpha(shell.theme.color(ui::ThemeToken::BgBase), 0.7f)));
        draw.AddText(toImVec(at + padding), color32(shell.theme.color(ui::ThemeToken::TextPrimary)), caption);
    }
}

// Selects @p guid as a click with the current modifiers does (Ctrl toggles, Shift adds).
void selectFromViewport(Impl& shell, EntityGuid guid)
{
    const ImGuiIO& io = ImGui::GetIO();
    const auto hex = formatGuid(guid);
    const std::string params = "{\"entities\":[" + jsonQuote(std::string_view(hex.data(), hex.size())) + "]}";
    const char* command =
        io.KeyCtrl    ? (shell.editor.selection().contains(guid) ? "selection.remove" : "selection.add")
        : io.KeyShift ? "selection.add"
                      : "selection.set";
    (void)shell.run(command, params, "ui");
}

// Assigns @p entity to the eyedropper's field and ends the eyedropper.
void finishEyedropper(Impl& shell, Entity entity)
{
    Impl::Eyedropper eyedropper = std::move(shell.eyedropper);
    shell.eyedropper = {};
    const EntityGuid guid = shell.editor.entities().guidOf(entity);
    if (guid.isNull())
    {
        return;
    }
    const auto hex = formatGuid(guid);
    setEntityField(shell, eyedropper.targets, eyedropper.type, eyedropper.path,
                   jsonQuote(std::string_view(hex.data(), hex.size())));
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

void SceneView::drawOverlaysMenu(Impl& shell)
{
    ui::FrameText& text = shell.text;
    if (ImGui::Button(text.iconLabel(ui::icons::kLayers, s::kViewportOverlays, "viewport.overlays")))
    {
        ImGui::OpenPopup("###viewport.overlays.menu");
    }
    if (!ImGui::BeginPopup("###viewport.overlays.menu"))
    {
        return;
    }
    const ViewportSettings& settings = m_last.settings;
    const bool scene = m_last.kind == ViewportKind::Scene;
    for (const OverlayItem& item : kOverlayItems)
    {
        if (item.sceneOnly && !scene)
        {
            continue;
        }
        const bool on = settings.*item.flag;
        if (ImGui::MenuItem(text.label(item.title, item.id), nullptr, on))
        {
            run(shell, "viewport.set_overlays",
                text.concat(text.concat(R"({")", item.name), on ? R"(":false})" : R"(":true})"));
        }
    }
    if (scene && ImGui::BeginMenu(text.label(s::kViewportVisualisers, "viewport.visualisers")))
    {
        for (const VisualiserItem& item : kVisualiserItems)
        {
            const bool on = settings.visualisers.*item.kind;
            if (ImGui::MenuItem(text.label(item.title, item.id), nullptr, on))
            {
                run(shell, "viewport.set_overlays",
                    text.concat(text.concat(R"({"visualisers":{")", item.name),
                                on ? R"(":false}})" : R"(":true}})"));
            }
        }
        ImGui::Separator();
        const bool all = settings.visualiserScope == ViewportVisualiserScope::All;
        if (ImGui::MenuItem(text.label(s::kViewportVisualisersSelected, "viewport.visualisers.selected"),
                            nullptr, !all))
        {
            run(shell, "viewport.set_overlays", R"({"visualiserScope":"selected"})");
        }
        if (ImGui::MenuItem(text.label(s::kViewportVisualisersAll, "viewport.visualisers.all"), nullptr, all))
        {
            run(shell, "viewport.set_overlays", R"({"visualiserScope":"all"})");
        }
        ImGui::EndMenu();
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
    drawOverlaysMenu(shell);
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
    const f32 toPanel = 1.0f / std::max(toImage.y, 1e-6f);

    // The overlays drawn over the image take the clicks above them.
    const Vec2 mouse(io.MousePos.x, io.MousePos.y);
    const bool scene = m_last.kind == ViewportKind::Scene;
    const OverlayLayout layout = overlayLayout(imageOrigin, display, kit.scale,
                                               scene && m_last.settings.viewCube && m_last.size.x != 0);
    const Quat rotation = viewportRotation(m_last.camera);
    const f32 iconRadius = kIconRadius * kit.scale;
    const ViewportIcon* iconHovered =
        hovered ? iconAt(m_last.icons, imageOrigin, toPanel, iconRadius, mouse) : nullptr;
    const std::optional<Vec3> axisHovered = hovered ? axisEndAt(layout, rotation, mouse) : std::nullopt;
    const bool cubeHovered = hovered && overCube(layout, mouse);
    const bool overOverlay = cubeHovered || axisHovered || iconHovered != nullptr;
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_cubeDrag = cubeHovered;
    }

    // The transform gizmo of the selection takes the left drags that start on one of its handles.
    ViewportGizmo::Frame gizmoFrame;
    gizmoFrame.viewport = scene && m_last.size.x != 0 ? &m_last : nullptr;
    gizmoFrame.imageOrigin = imageOrigin;
    gizmoFrame.toPanel = toPanel;
    gizmoFrame.pixelScale = kit.scale * toImage.y;
    gizmoFrame.cursor = (mouse - imageOrigin) * toImage;
    gizmoFrame.hovered = hovered && !overOverlay && !shell.eyedropper.active;
    gizmoFrame.id = m_id;
    if (scene && hovered)
    {
        shell.hoveredSceneView = m_id;
    }
    // gizmo.modal starts in the Scene view under the cursor, or in the main one.
    if (scene && shell.pendingModal &&
        m_id == (shell.lastHoveredSceneView.empty() ? std::string_view("scene")
                                                    : std::string_view(shell.lastHoveredSceneView)))
    {
        (void)m_gizmo.beginModal(shell, gizmoFrame, *shell.pendingModal);
        shell.pendingModal.reset();
    }
    const GizmoHandle gizmoHovered = m_gizmo.hoveredHandle(shell, gizmoFrame);
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_gizmo.modal())
    {
        // The click confirms the modal transform: its release neither picks nor selects.
        m_gizmoPressed = true;
    }
    else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
             gizmoHovered != GizmoHandle::None && !m_cubeDrag)
    {
        m_gizmoPressed = m_gizmo.begin(shell, gizmoFrame, gizmoHovered);
    }
    else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gizmoFrame.hovered &&
             !io.KeyAlt && !m_cubeDrag)
    {
        // The Measure tool draws a ruler with left drags (its release neither picks nor selects).
        m_gizmoPressed = m_gizmo.beginMeasure(shell, gizmoFrame);
    }
    const bool gizmoDragging = m_gizmo.dragging() || m_gizmo.measuring();

    ViewportInput input;
    input.cursor = (mouse - imageOrigin) * toImage;
    input.cursorDelta = Vec2(io.MouseDelta.x, io.MouseDelta.y) * toImage;
    input.hovered = hovered && (!overOverlay || m_cubeDrag) && !gizmoDragging;
    input.wheel = input.hovered ? io.MouseWheel : 0.0f;
    input.focused = focused;
    input.buttons = {ImGui::IsMouseDown(ImGuiMouseButton_Left) && !gizmoDragging,
                     ImGui::IsMouseDown(ImGuiMouseButton_Right) && !gizmoDragging,
                     ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !gizmoDragging};
    input.alt = io.KeyAlt;
    input.shift = io.KeyShift;
    input.ctrl = io.KeyCtrl;
    // A left drag that starts on the view cube orbits.
    input.orbit = m_cubeDrag;
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
    style.hover = theme.color(ui::ThemeToken::ViewportHover);
    style.grid = theme.color(ui::ThemeToken::TextDisabled);
    style.grid.w = 0.55f;
    style.axisX = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbX : ui::ThemeToken::AxisX);
    style.axisY = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbY : ui::ThemeToken::AxisY);
    style.axisZ = theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbZ : ui::ThemeToken::AxisZ);

    m_last = host->frame(m_id, pixels, input, style);
    shell.viewportNavigating = shell.viewportNavigating || m_last.navigating || gizmoDragging;
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
        m_cubeDrag = false;
        m_gizmoPressed = false;
        // A drag cannot go on without an image: update() cancels it.
        gizmoFrame.viewport = nullptr;
        m_gizmo.update(shell, gizmoFrame, draw, GizmoHandle::None);
        return;
    }
    const ViewportSettings& settings = m_last.settings;
    gizmoFrame.viewport = scene ? &m_last : nullptr;
    m_gizmo.update(shell, gizmoFrame, draw, gizmoHovered);
    if (!m_last.icons.empty())
    {
        drawIcons(shell, draw, m_last.icons, imageOrigin, toPanel, iconRadius, iconHovered);
    }
    if (m_last.preview != 0)
    {
        drawPreview(shell, draw, m_last, imageOrigin, display, scale);
    }
    if (settings.stats)
    {
        drawStats(shell, draw, *host, m_id, imageOrigin, m_last.size);
    }
    if (layout.cubeShown)
    {
        drawViewCube(shell, draw, layout, m_last.camera, mouse, hovered && !m_last.navigating);
    }
    Impl::Eyedropper& eyedropper = shell.eyedropper;
    if (eyedropper.active)
    {
        const char* hint =
            shell.text.format(s::kViewportEyedropper, {{"field", std::string_view(eyedropper.field)}});
        const Vec2 padding(6.0f * kit.scale);
        const ImVec2 size = ImGui::CalcTextSize(hint);
        const Vec2 at =
            imageOrigin + Vec2((display.x - size.x) * 0.5f - padding.x, kOverlayMargin * kit.scale);
        draw.AddRectFilled(toImVec(at), toImVec(at + Vec2(size.x, size.y) + padding * 2.0f),
                           color32(withAlpha(theme.color(ui::ThemeToken::AccentFill), 0.9f)),
                           4.0f * kit.scale);
        draw.AddText(toImVec(at + padding), color32(theme.color(ui::ThemeToken::AccentText)), hint);
        if (hovered)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    // The cursor in image pixels, inside the image.
    const auto imagePixel = [&](ImVec2 position) {
        return glm::clamp((Vec2(position.x, position.y) - imageOrigin) * toImage, Vec2(0.0f),
                          Vec2(m_last.size) - 1.0f);
    };
    const f32 threshold = io.MouseDragThreshold * io.MouseDragThreshold;
    const bool dragged = io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] >= threshold;
    // A left drag without Alt draws a selection box (not while the eyedropper picks one entity).
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt && dragged &&
        !m_last.navigating && !m_cubeDrag && !eyedropper.active && !m_gizmoPressed)
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
    // A press that went to the gizmo neither picks nor selects when it is released.
    if (ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !m_gizmoPressed)
    {
        if (m_cubeDrag)
        {
            // A click on the cube turns to the face, edge or corner under it.
            const std::optional<Vec3> part =
                dragged ? std::nullopt : viewCubePick(rotation, (mouse - layout.cube) / layout.cubeScale);
            if (const std::optional<ViewportView> view = part ? viewCubeView(*part) : std::nullopt)
            {
                run(shell, "viewport.view", kViews[static_cast<usize>(*view)].params);
            }
            else if (part)
            {
                const ViewportCamera turned = viewCubeCamera(m_last.camera, *part);
                std::array<char, 64> params{};
                (void)std::snprintf(params.data(), params.size(), R"({"yaw":%.3f,"pitch":%.3f})",
                                    static_cast<double>(turned.yaw), static_cast<double>(turned.pitch));
                run(shell, "viewport.camera_set", params.data());
            }
        }
        else if (axisHovered && !dragged)
        {
            if (const std::optional<ViewportView> view = viewCubeView(*axisHovered))
            {
                run(shell, "viewport.view", kViews[static_cast<usize>(*view)].params);
            }
        }
        else if (iconHovered != nullptr && !dragged)
        {
            if (eyedropper.active)
            {
                finishEyedropper(shell, iconHovered->entity);
            }
            else
            {
                selectFromViewport(shell, shell.editor.entities().guidOf(iconHovered->entity));
            }
        }
        else if (m_boxing)
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
            if (eyedropper.active)
            {
                // The eyedropper reads the entity without selecting it (updateEyedropper() assigns it).
                if (const Expected<u64, CommandError> ticket = host->requestPick(m_id, pixel, false))
                {
                    eyedropper.ticket = *ticket;
                    eyedropper.viewport = m_id;
                }
            }
            else
            {
                std::array<char, 128> params{};
                const int written = std::snprintf(
                    params.data(), params.size(), R"({"x":%.1f,"y":%.1f,"cycle":true,"select":"%s"})",
                    static_cast<double>(pixel.x), static_cast<double>(pixel.y), select);
                if (written > 0 && static_cast<usize>(written) < params.size())
                {
                    run(shell, "viewport.pick", params.data());
                }
            }
        }
        m_boxing = false;
        m_cubeDrag = false;
        m_gizmoPressed = false;
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_boxing = false;
        m_cubeDrag = false;
        m_gizmoPressed = false;
    }
}

void startEyedropper(Impl& shell, std::span<const EntityGuid> entities, std::string_view type,
                     std::string_view path, std::string_view field)
{
    Impl::Eyedropper& eyedropper = shell.eyedropper;
    eyedropper = {};
    if (entities.empty())
    {
        return;
    }
    eyedropper.active = true;
    eyedropper.targets.assign(entities.begin(), entities.end());
    eyedropper.type = std::string(type);
    eyedropper.path = std::string(path);
    eyedropper.field = std::string(field);
}

void updateEyedropper(Impl& shell)
{
    Impl::Eyedropper& eyedropper = shell.eyedropper;
    if (!eyedropper.active)
    {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        eyedropper = {};
        return;
    }
    ViewportHost* host = shell.editor.viewports();
    if (eyedropper.ticket == 0 || host == nullptr)
    {
        return;
    }
    std::optional<Expected<std::optional<ViewportHit>, CommandError>> picked = host->pick(eyedropper.ticket);
    if (!picked)
    {
        return; // still reading
    }
    eyedropper.ticket = 0;
    // A miss keeps the eyedropper active for another click.
    if (picked->hasValue() && picked->value().has_value())
    {
        finishEyedropper(shell, picked->value()->entity);
    }
}

void followAgent(Impl& shell)
{
    EditorContext& editor = shell.editor;
    const u64 version = editor.agentSelectionVersion();
    if (version == shell.followedSelection)
    {
        return;
    }
    shell.followedSelection = version;
    // Only the agent's latest selection, while nobody has changed it since.
    if (!editor.preferences().followAgent || editor.viewports() == nullptr ||
        version != editor.selection().version() || editor.selection().empty())
    {
        return;
    }
    (void)shell.run("viewport.frame", R"({"viewport":"scene"})", "follow");
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

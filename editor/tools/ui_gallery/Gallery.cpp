#include "Gallery.hpp"

#include <illusion/core/Log.hpp>
#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/text/Localization.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <imgui.h>
#include <implot.h>
#include <tuple>
#include <utility>

namespace illusion::editor::gallery
{

namespace
{

namespace icons = ui::icons;
namespace strings = editor::strings;
using ui::ThemeToken;

ImVec4 tokenVec(const ui::UiKit& kit, ThemeToken token) noexcept
{
    const Vec4 c = kit.theme.color(token);
    return ImVec4(c.r, c.g, c.b, c.a);
}

void coloredText(const ui::UiKit& kit, ThemeToken token, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, token));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void pushFont(const ui::UiKit& kit, ui::FontRole role)
{
    if (kit.fonts != nullptr)
    {
        kit.fonts->push(role);
    }
}

void popFont(const ui::UiKit& kit)
{
    if (kit.fonts != nullptr)
    {
        ImGui::PopFont();
    }
}

// A filled colour square as tall as a line of text.
void swatch(const ui::UiKit& kit, ThemeToken token)
{
    const f32 side = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 y = pos.y + std::floor((ImGui::GetFrameHeight() - side) * 0.5f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + side, y + side),
                                              ui::tokenColor(kit, token), ImGui::GetStyle().FrameRounding);
    ImGui::Dummy(ImVec2(side, ImGui::GetFrameHeight()));
}

// An icon at @p size pixels (unscaled) from the icon font.
void bigIcon(const ui::UiKit& kit, const ui::Icon& icon, f32 size, ThemeToken token)
{
    if (kit.fonts != nullptr)
    {
        ImGui::PushFont(kit.fonts->font(ui::FontRole::Icon), size);
    }
    coloredText(kit, token, icon.c_str());
    if (kit.fonts != nullptr)
    {
        ImGui::PopFont();
    }
}

struct ComponentIcon
{
    const char* name;
    ui::Icon icon;
};

constexpr std::array<ComponentIcon, 14> kComponentIcons = {{
    {"Transform", icons::kOpenWith},
    {"MeshRenderer", icons::kDeployedCode},
    {"Camera", icons::kVideocam},
    {"DirectionalLight", icons::kLightMode},
    {"PointLight", icons::kLightbulb},
    {"SkyLight", icons::kWbTwilight},
    {"RigidBody", icons::kWeight},
    {"Collider", icons::kSelectAll},
    {"CharacterController", icons::kDirectionsWalk},
    {"Joint", icons::kLink},
    {"AudioSource", icons::kVolumeUp},
    {"AudioListener", icons::kHearing},
    {"Outliner", icons::kAccountTree},
    {"AI agent", icons::kSmartToy},
}};

struct OutlinerRow
{
    const char* name;
    ui::Icon icon;
    int depth;
};

constexpr std::array<OutlinerRow, 7> kOutliner = {{
    {"Main Camera", icons::kVideocam, 0},
    {"Sun", icons::kLightMode, 0},
    {"Level", icons::kInventory2, 0},
    {"Crate", icons::kDeployedCode, 1},
    {"Crate (1)", icons::kDeployedCode, 1},
    {"Floor", icons::kDeployedCode, 1},
    {"Player", icons::kDirectionsWalk, 0},
}};

struct LogLine
{
    ThemeToken token;
    ui::Icon icon;
    const char* text;
};

constexpr std::array<LogLine, 5> kConsole = {{
    {ThemeToken::Info, icons::kInfo, "[Assets] Imported models/crate.glb (2 meshes, 3 materials) in 48 ms"},
    {ThemeToken::Success, icons::kCheckCircle, "[Build] game module built in 3.2 s"},
    {ThemeToken::Warning, icons::kWarning, "[Render] Texture wood_albedo.png is not a power of two"},
    {ThemeToken::Error, icons::kError, "[Shader] Water.slang(42): undeclared identifier 'foam'"},
    {ThemeToken::Info, icons::kSmartToy, "[Agent] scene.create_entity {\"name\": \"Crate (1)\"}"},
}};

} // namespace

bool mountEditorData(VirtualFileSystem& vfs)
{
    const std::filesystem::path directory = executableDirectory() / "editor-data";
    const FileError error = vfs.mount("editor", directory);
    if (error != FileError::None)
    {
        ILL_LOG_ERROR("Gallery", "cannot mount '{}': {}", directory.string(), fileErrorName(error));
        return false;
    }
    return true;
}

Expected<ui::Theme, std::string> loadTheme(const VirtualFileSystem& vfs, std::string_view name)
{
    const std::string path = std::format("editor:/themes/{}.json", name);
    std::string json;
    if (const FileError error = vfs.readText(path, json); error != FileError::None)
    {
        return unexpected(std::format("cannot read '{}': {}", path, fileErrorName(error)));
    }
    auto theme = ui::Theme::parse(json);
    if (!theme)
    {
        return unexpected(std::format("{}: {}", path, theme.error()));
    }
    return std::move(theme.value());
}

Gallery::Gallery(text::Localization& localization, const ui::Theme& theme, const ui::FontManager* fonts)
    : m_localization(localization)
    , m_theme(&theme)
    , m_fonts(fonts)
    , m_text(localization)
{
    m_localization.addTable(strings::kTable);
    constexpr std::string_view kName = "Crate";
    std::copy(kName.begin(), kName.end(), m_state.name.begin());
    // Deterministic sample frame times around 8 ms with a spike.
    for (usize i = 0; i < m_frameTimes.size(); ++i)
    {
        const f32 x = static_cast<f32>(i);
        m_frameIndices[i] = x;
        m_frameTimes[i] =
            8.0f + 0.9f * std::sin(x * 0.21f) + 0.4f * std::sin(x * 0.83f) + (i == 74 ? 6.5f : 0.0f);
    }
}

void Gallery::setTheme(const ui::Theme& theme, f32 scale)
{
    m_theme = &theme;
    m_scale = scale;
    theme.apply(ImGui::GetStyle(), scale);
    const auto color = [&theme](ThemeToken token) {
        const Vec4 c = theme.color(token);
        return ImVec4(c.r, c.g, c.b, c.a);
    };
    ImPlotStyle& plot = ImPlot::GetStyle();
    plot.Colors[ImPlotCol_PlotBg] = color(ThemeToken::BgInput);
    plot.Colors[ImPlotCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    plot.Colors[ImPlotCol_AxisGrid] = color(ThemeToken::Separator);
    plot.Colors[ImPlotCol_AxisText] = color(ThemeToken::TextSecondary);
    plot.Colors[ImPlotCol_LegendBg] = color(ThemeToken::BgRaised);
    plot.Colors[ImPlotCol_LegendText] = color(ThemeToken::TextPrimary);
}

void Gallery::setPage(GalleryPage page) noexcept
{
    m_page = page;
    m_pageChanged = true;
}

void Gallery::showToast(f64 now)
{
    m_toasts.push(icons::kDelete, ThemeToken::Info,
                  m_text.format(strings::kGalleryToastDeleted, {{"count", 12}}), now, 8.0,
                  m_text.format(strings::kCommonUndo), 1);
}

ui::UiKit Gallery::kit()
{
    ui::UiKit kit(*m_theme, m_fonts, m_text);
    kit.strings.clearSearch = m_text.format(strings::kWidgetClearSearch);
    kit.strings.moreOptions = m_text.format(strings::kWidgetMoreOptions);
    kit.strings.enabled = m_text.format(strings::kWidgetEnabled);
    kit.strings.intensity = m_text.format(strings::kWidgetIntensity);
    kit.scale = m_scale;
    return kit;
}

void Gallery::draw(f64 now)
{
    m_text.reset();
    ui::UiKit kit = this->kit();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, tokenVec(kit, ThemeToken::BgBase));
    if (ImGui::Begin("###gallery", nullptr, kFlags))
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, tokenVec(kit, ThemeToken::BgPanel));
        if (ImGui::BeginTabBar("###gallery.tabs"))
        {
            const std::array<std::pair<GalleryPage, const char*>, 3> tabs = {{
                {GalleryPage::Widgets, m_text.label(strings::kGalleryTabWidgets, "gallery.tab.widgets")},
                {GalleryPage::Typography,
                 m_text.label(strings::kGalleryTabTypography, "gallery.tab.typography")},
                {GalleryPage::Editor, m_text.label(strings::kGalleryTabEditor, "gallery.tab.editor")},
            }};
            for (const auto& [page, label] : tabs)
            {
                const ImGuiTabItemFlags flags =
                    m_pageChanged && page == m_page ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(label, nullptr, flags))
                {
                    if (!m_pageChanged)
                    {
                        m_page = page;
                    }
                    ImGui::EndTabItem();
                }
            }
            m_pageChanged = false;
            ImGui::EndTabBar();
        }
        ImGui::PopStyleColor();

        switch (m_page)
        {
        case GalleryPage::Widgets:
            drawWidgets(kit, now);
            break;
        case GalleryPage::Typography:
            drawTypography(kit);
            break;
        case GalleryPage::Editor:
            drawEditor(kit);
            break;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();

    if (const std::optional<u32> action = m_toasts.draw(kit, now); action && *action == 1)
    {
        ++m_undoCount;
    }
}

void Gallery::drawToolButtons(ui::UiKit& kit, const char* id)
{
    const std::array<std::pair<ui::Icon, const char*>, 3> tools = {{
        {icons::kOpenWith, m_text.format(strings::kToolMove)},
        {icons::k3dRotation, m_text.format(strings::kToolRotate)},
        {icons::kOpenInFull, m_text.format(strings::kToolScale)},
    }};
    constexpr std::array<const char*, 3> kShortcuts = {"W", "E", "R"};
    for (usize i = 0; i < tools.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine(0.0f, 2.0f * m_scale);
        }
        char suffix[] = ".tool0";
        suffix[5] = static_cast<char>('0' + i);
        if (ui::iconButton(kit, tools[i].first, m_text.concat(id, suffix), tools[i].second, kShortcuts[i],
                           m_state.tool == static_cast<int>(i)))
        {
            m_state.tool = static_cast<int>(i);
        }
    }
}

void Gallery::drawTransformCard(ui::UiKit& kit, const char* id)
{
    if (ui::beginComponentCard(kit, id, icons::kOpenWith, m_text.format(strings::kComponentTransform),
                               nullptr))
    {
        if (ui::beginPropertyTable(kit, m_text.concat(id, ".props")))
        {
            ui::propertyLabel(kit, m_text.format(strings::kPropertyPosition));
            ui::vec3Field(kit, m_text.concat(id, ".position"), m_state.position);
            ui::propertyLabel(kit, m_text.format(strings::kPropertyRotation));
            ui::vec3Field(kit, m_text.concat(id, ".rotation"), m_state.rotation, 0.5f, "%.1f");
            ui::propertyLabel(kit, m_text.format(strings::kPropertyScale));
            ui::vec3Field(kit, m_text.concat(id, ".scale"), m_state.scale);
            ui::endPropertyTable();
        }
        ui::endComponentCard();
    }
    if (ui::beginComponentCardMenu(kit, id))
    {
        ImGui::MenuItem(m_text.label(strings::kCommonUndo, m_text.concat(id, ".menu.undo")));
        ui::endComponentCardMenu();
    }
}

void Gallery::drawMeshCard(ui::UiKit& kit, const char* id)
{
    if (ui::beginComponentCard(kit, id, icons::kDeployedCode, m_text.format(strings::kComponentMeshRenderer),
                               &m_state.meshEnabled))
    {
        if (ui::beginPropertyTable(kit, m_text.concat(id, ".props")))
        {
            ui::propertyLabel(kit, m_text.format(strings::kPropertyMesh));
            ImGui::Button(m_text.verbatim(m_text.concat(icons::kDeployedCode.view(), "  crate.glb"),
                                          m_text.concat(id, ".mesh")),
                          ImVec2(-FLT_MIN, 0.0f));
            ui::propertyLabel(kit, m_text.format(strings::kPropertyCastShadows));
            ImGui::Checkbox(m_text.hidden(m_text.concat(id, ".shadows")), &m_state.castShadows);
            ui::endPropertyTable();
        }
        ui::endComponentCard();
    }
}

void Gallery::drawLightCard(ui::UiKit& kit, const char* id)
{
    if (ui::beginComponentCard(kit, id, icons::kLightbulb, m_text.format(strings::kComponentPointLight),
                               &m_state.lightEnabled))
    {
        if (ui::beginPropertyTable(kit, m_text.concat(id, ".props")))
        {
            ui::propertyLabel(kit, m_text.format(strings::kPropertyColor));
            f32 intensity = m_state.intensity;
            ui::colorHdr(kit, m_text.concat(id, ".color"), m_state.lightColor, intensity);
            m_state.intensity = intensity;
            ui::propertyLabel(kit, m_text.format(strings::kPropertyIntensity));
            ImGui::DragFloat(m_text.hidden(m_text.concat(id, ".intensity")), &m_state.intensity, 1.0f, 0.0f,
                             100000.0f, "%.0f cd");
            ui::richTooltip(kit, m_text.format(strings::kPropertyIntensity),
                            m_text.format(strings::kPropertyIntensityDescription),
                            m_text.format(strings::kPropertyIntensityDetail));
            ui::propertyLabel(kit, m_text.format(strings::kPropertyRange));
            ImGui::DragFloat(m_text.hidden(m_text.concat(id, ".range")), &m_state.range, 0.05f, 0.0f, 1000.0f,
                             "%.2f m");
            ui::endPropertyTable();
        }
        ui::endComponentCard();
    }
}

void Gallery::drawWidgets(ui::UiKit& kit, f64 now)
{
    if (!ImGui::BeginTable("###gallery.widgets", 2,
                           ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX |
                               ImGuiTableFlags_NoSavedSettings))
    {
        return;
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionButtons));
    if (ui::primaryButton(kit, m_text.label(strings::kCommonSave, "gallery.save")))
    {
        ++m_state.clicks;
    }
    ImGui::SameLine();
    ImGui::Button(m_text.label(strings::kCommonCancel, "gallery.cancel"));
    ImGui::SameLine();
    ImGui::BeginDisabled();
    ImGui::Button(m_text.label(strings::kGalleryButtonDisabled, "gallery.disabled"));
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Button(m_text.iconLabel(icons::kAdd, strings::kGalleryButtonCreateEntity, "gallery.create"));
    drawToolButtons(kit, "gallery");
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    ui::iconButton(kit, icons::kPlayArrow, "gallery.play", m_text.format(strings::kToolPlay), "Ctrl+P");
    ImGui::SameLine(0.0f, 2.0f * m_scale);
    ui::iconButton(kit, icons::kPause, "gallery.pause", m_text.format(strings::kToolPause), "Ctrl+Shift+P");
    ImGui::SameLine(0.0f, 2.0f * m_scale);
    ui::iconButton(kit, icons::kSkipNext, "gallery.step", m_text.format(strings::kToolStep), "Ctrl+Alt+P");

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionChoices));
    ui::toggleSwitch(kit, m_text.label(strings::kGalleryToggleSnap, "gallery.snap"), m_state.snap);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    ui::toggleSwitch(kit, m_text.label(strings::kGalleryToggleGizmos, "gallery.gizmos"), m_state.gizmos);
    const std::array<const char*, 2> spaces = {m_text.format(strings::kGallerySpaceLocal),
                                               m_text.format(strings::kGallerySpaceWorld)};
    ui::segmentedControl(kit, "gallery.space", spaces, m_state.space);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    const std::array<const char*, 2> pivots = {m_text.format(strings::kGalleryPivotPivot),
                                               m_text.format(strings::kGalleryPivotCenter)};
    ui::segmentedControl(kit, "gallery.pivot", pivots, m_state.pivot);
    ImGui::Checkbox(m_text.label(strings::kPropertyCastShadows, "gallery.shadows"), &m_state.castShadows);

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionFields));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ui::searchField(kit, "gallery.search", m_state.search, m_text.format(strings::kOutlinerSearch));
    if (ui::beginPropertyTable(kit, "gallery.fields"))
    {
        ui::propertyLabel(kit, m_text.format(strings::kPropertyName));
        ImGui::InputText(m_text.hidden("gallery.name"), m_state.name.data(), m_state.name.size());
        ui::propertyLabel(kit, m_text.format(strings::kPropertyPosition));
        ui::vec3Field(kit, "gallery.position", m_state.position);
        ui::endPropertyTable();
    }

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionNavigation));
    constexpr std::array<const char*, 4> kPath = {"game:", "models", "props", "crate.glb"};
    ui::breadcrumbs(kit, "gallery.path", kPath);

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionAssets));
    const std::array<std::tuple<ui::Icon, const char*, const char*>, 4> tiles = {{
        {icons::kDeployedCode, "crate.glb", m_text.format(strings::kGalleryBadgeMesh)},
        {icons::kPalette, "crate_wood.material", m_text.format(strings::kGalleryBadgeMaterial)},
        {icons::kImage, "wood_albedo.png", m_text.format(strings::kGalleryBadgeTexture)},
        {icons::kLandscape, "intro.scene", m_text.format(strings::kGalleryBadgeScene)},
    }};
    for (usize i = 0; i < tiles.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
        }
        char id[] = "gallery.tile0";
        id[12] = static_cast<char>('0' + i);
        const auto& [icon, name, badgeText] = tiles[i];
        if (ui::assetTile(kit, id, icon, name, badgeText, m_state.selectedTile == static_cast<int>(i)))
        {
            m_state.selectedTile = static_cast<int>(i);
        }
    }

    ImGui::TableSetColumnIndex(1);
    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionCards));
    drawTransformCard(kit, "gallery.transform");
    drawMeshCard(kit, "gallery.mesh");
    drawLightCard(kit, "gallery.light");

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionStatus));
    const std::array<std::tuple<ThemeToken, ui::Icon, const char*>, 4> status = {{
        {ThemeToken::Success, icons::kCheckCircle, m_text.format(strings::kStatusBuildSucceeded)},
        {ThemeToken::Warning, icons::kWarning, m_text.format(strings::kStatusWarnings, {{"count", 3}})},
        {ThemeToken::Error, icons::kError, m_text.format(strings::kStatusBuildFailed)},
        {ThemeToken::Info, icons::kSmartToy, m_text.format(strings::kStatusAgentConnected)},
    }};
    for (const auto& [token, icon, label] : status)
    {
        coloredText(kit, token, icon.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    }
    ui::badge(kit, m_text.format(strings::kGalleryBadgeMesh), ThemeToken::Info);
    ImGui::SameLine();
    ui::badge(kit, m_text.format(strings::kStatusWarnings, {{"count", 3}}), ThemeToken::Warning);
    ImGui::SameLine();
    ui::badge(kit, m_text.format(strings::kToolPlay), ThemeToken::PlayTint);

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionFeedback));
    if (ImGui::Button(m_text.label(strings::kGalleryButtonShowToast, "gallery.toast")))
    {
        showToast(now);
    }
    if (ImGui::BeginChild("###gallery.empty", ImVec2(-FLT_MIN, 150.0f * m_scale), ImGuiChildFlags_Borders))
    {
        ui::emptyState(kit, icons::kAccountTree, m_text.format(strings::kOutlinerEmptyTitle),
                       m_text.format(strings::kOutlinerEmptyHint));
    }
    ImGui::EndChild();

    ui::sectionHeader(kit, m_text.format(strings::kGallerySectionPlots));
    if (ImPlot::BeginPlot("###gallery.plot", ImVec2(-FLT_MIN, 140.0f * m_scale),
                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText))
    {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_None);
        ImPlot::SetupAxesLimits(0.0, static_cast<f64>(m_frameTimes.size() - 1), 0.0, 16.0, ImPlotCond_Always);
        ImPlotSpec line;
        line.LineColor = tokenVec(kit, ThemeToken::Accent);
        line.LineWeight = 1.5f * m_scale;
        ImPlot::PlotLine(m_text.format(strings::kGalleryPlotFrameTime), m_frameIndices.data(),
                         m_frameTimes.data(), static_cast<int>(m_frameTimes.size()), line);
        ImPlot::EndPlot();
    }
    ImGui::EndTable();
}

void Gallery::drawTypography(ui::UiKit& kit)
{
    const char* sample = m_text.format(strings::kGalleryTypeSample);
    const std::array<std::pair<ui::FontRole, const char*>, 5> roles = {{
        {ui::FontRole::Title, m_text.format(strings::kGalleryTypeTitle)},
        {ui::FontRole::Strong, m_text.format(strings::kGalleryTypeStrong)},
        {ui::FontRole::Body, m_text.format(strings::kGalleryTypeBody)},
        {ui::FontRole::Caption, m_text.format(strings::kGalleryTypeCaption)},
        {ui::FontRole::Mono, m_text.format(strings::kGalleryTypeMono)},
    }};
    for (const auto& [role, label] : roles)
    {
        coloredText(kit, ThemeToken::TextSecondary, label);
        pushFont(kit, role);
        ImGui::TextUnformatted(sample);
        popFont(kit);
        ImGui::Spacing();
    }
    coloredText(kit, ThemeToken::TextDisabled, m_text.copy("Iñtërnâtiônàlizætiøn · ğüşıöç İ · 1,234.56"));

    ui::sectionHeader(kit, m_text.format(strings::kGalleryTypeIcons));
    const f32 cell = 112.0f * m_scale;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));
    if (ImGui::BeginTable("###gallery.icons", columns,
                          ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoSavedSettings))
    {
        for (const ComponentIcon& entry : kComponentIcons)
        {
            ImGui::TableNextColumn();
            bigIcon(kit, entry.icon, 32.0f, ThemeToken::TextPrimary);
            pushFont(kit, ui::FontRole::Caption);
            coloredText(kit, ThemeToken::TextSecondary, entry.name);
            popFont(kit);
            ImGui::Spacing();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    for (const ThemeToken token : {ThemeToken::Accent, ThemeToken::Success, ThemeToken::Warning,
                                   ThemeToken::Error, ThemeToken::Info, ThemeToken::PlayTint})
    {
        swatch(kit, token);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        coloredText(kit, ThemeToken::TextSecondary, m_text.copy(ui::themeTokenName(token)));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    }
    ImGui::NewLine();
    for (usize axis = 0; axis < 3; ++axis)
    {
        constexpr std::array<ThemeToken, 6> kAxes = {ThemeToken::AxisX,   ThemeToken::AxisY,
                                                     ThemeToken::AxisZ,   ThemeToken::AxisCbX,
                                                     ThemeToken::AxisCbY, ThemeToken::AxisCbZ};
        swatch(kit, kAxes[axis]);
        ImGui::SameLine();
        swatch(kit, kAxes[axis + 3]);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 3.0f);
    }
    ImGui::NewLine();
}

void Gallery::drawEditor(ui::UiKit& kit)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    // Toolbar.
    const f32 lineStart = ImGui::GetCursorPosX();
    const f32 lineWidth = ImGui::GetContentRegionAvail().x;
    drawToolButtons(kit, "editor");
    const f32 playGroup = ImGui::GetFrameHeight() * 3.0f + 4.0f * m_scale;
    ImGui::SameLine(lineStart + std::floor((lineWidth - playGroup) * 0.5f));
    ui::iconButton(kit, icons::kPlayArrow, "editor.play", m_text.format(strings::kToolPlay), "Ctrl+P");
    ImGui::SameLine(0.0f, 2.0f * m_scale);
    ui::iconButton(kit, icons::kPause, "editor.pause", m_text.format(strings::kToolPause), "Ctrl+Shift+P");
    ImGui::SameLine(0.0f, 2.0f * m_scale);
    ui::iconButton(kit, icons::kSkipNext, "editor.step", m_text.format(strings::kToolStep), "Ctrl+Alt+P");
    ImGui::SameLine(lineStart + lineWidth - ImGui::GetFrameHeight() * 2.0f - style.ItemSpacing.x);
    ui::iconButton(kit, icons::kSmartToy, "editor.agent", m_text.format(strings::kStatusAgentConnected));
    ImGui::SameLine();
    ui::iconButton(kit, icons::kNotifications, "editor.notifications");

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const f32 statusHeight = ImGui::GetFrameHeight();
    const f32 bottomHeight = std::floor((avail.y - statusHeight) * 0.28f);
    const f32 topHeight = avail.y - statusHeight - bottomHeight - style.ItemSpacing.y * 2.0f;
    const f32 leftWidth = std::floor(avail.x * 0.2f);
    const f32 rightWidth = std::floor(avail.x * 0.25f);
    const f32 centerWidth = avail.x - leftWidth - rightWidth - style.ItemSpacing.x * 2.0f;

    const auto panelTitle = [&](const ui::Icon& icon, const char* title) {
        pushFont(kit, ui::FontRole::Strong);
        coloredText(kit, ThemeToken::TextSecondary, m_text.concat(icon.view(), m_text.concat("  ", title)));
        popFont(kit);
        ImGui::Separator();
    };

    ImGui::PushStyleColor(ImGuiCol_ChildBg, tokenVec(kit, ThemeToken::BgPanel));
    // Outliner.
    if (ImGui::BeginChild("###editor.outliner", ImVec2(leftWidth, topHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        panelTitle(icons::kAccountTree, m_text.format(strings::kPanelOutliner));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ui::searchField(kit, "editor.outliner.search", m_state.outlinerSearch,
                        m_text.format(strings::kOutlinerSearch));
        for (usize i = 0; i < kOutliner.size(); ++i)
        {
            const OutlinerRow& row = kOutliner[i];
            ImGui::Indent(static_cast<f32>(row.depth) * style.IndentSpacing + 1.0f);
            char id[] = "editor.outliner.row0";
            id[19] = static_cast<char>('0' + i);
            const bool selected = m_state.selectedEntity == static_cast<int>(i);
            if (ImGui::Selectable(
                    m_text.verbatim(m_text.concat(row.icon.view(), m_text.concat("  ", row.name)), id),
                    selected))
            {
                m_state.selectedEntity = static_cast<int>(i);
            }
            ImGui::Unindent(static_cast<f32>(row.depth) * style.IndentSpacing + 1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Viewport.
    if (ImGui::BeginChild("###editor.viewport", ImVec2(centerWidth, topHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        if (ImGui::BeginTabBar("###editor.viewport.tabs"))
        {
            if (ImGui::BeginTabItem(
                    m_text.iconLabel(icons::kViewInAr, strings::kPanelScene, "editor.tab.scene")))
            {
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(
                    m_text.iconLabel(icons::kSportsEsports, strings::kPanelGame, "editor.tab.game")))
            {
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, IM_COL32(24, 27, 33, 255));
        // A ground grid seen from above, with the world axes and a selected object.
        const f32 step = 32.0f * m_scale;
        for (f32 x = min.x + std::fmod(size.x * 0.5f, step); x < max.x; x += step)
        {
            drawList->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), IM_COL32(255, 255, 255, 18));
        }
        for (f32 y = min.y + std::fmod(size.y * 0.5f, step); y < max.y; y += step)
        {
            drawList->AddLine(ImVec2(min.x, y), ImVec2(max.x, y), IM_COL32(255, 255, 255, 18));
        }
        const ImVec2 center(std::floor(min.x + size.x * 0.5f), std::floor(min.y + size.y * 0.5f));
        drawList->AddLine(ImVec2(min.x, center.y), ImVec2(max.x, center.y),
                          ui::tokenColor(kit, ThemeToken::AxisX, 0.6f));
        drawList->AddLine(ImVec2(center.x, min.y), ImVec2(center.x, max.y),
                          ui::tokenColor(kit, ThemeToken::AxisZ, 0.6f));
        const f32 half = 28.0f * m_scale;
        drawList->AddRectFilled(ImVec2(center.x - half, center.y - half),
                                ImVec2(center.x + half, center.y + half), IM_COL32(150, 110, 70, 255), 2.0f);
        drawList->AddRect(ImVec2(center.x - half, center.y - half), ImVec2(center.x + half, center.y + half),
                          ui::tokenColor(kit, ThemeToken::ViewportSelection), 2.0f, 2.0f * m_scale);
        const f32 arm = 60.0f * m_scale;
        drawList->AddLine(center, ImVec2(center.x + arm, center.y), ui::tokenColor(kit, ThemeToken::AxisX),
                          3.0f * m_scale);
        drawList->AddLine(center, ImVec2(center.x, center.y - arm), ui::tokenColor(kit, ThemeToken::AxisY),
                          3.0f * m_scale);
        ImGui::Dummy(size);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Inspector.
    if (ImGui::BeginChild("###editor.inspector", ImVec2(rightWidth, topHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        panelTitle(icons::kTune, m_text.format(strings::kPanelInspector));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText(m_text.hidden("editor.inspector.name"), m_state.name.data(), m_state.name.size());
        drawTransformCard(kit, "editor.transform");
        drawMeshCard(kit, "editor.mesh");
    }
    ImGui::EndChild();

    // Console.
    if (ImGui::BeginChild("###editor.console", ImVec2(avail.x, bottomHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding))
    {
        panelTitle(icons::kTerminal, m_text.format(strings::kPanelConsole));
        pushFont(kit, ui::FontRole::Mono);
        for (const LogLine& line : kConsole)
        {
            coloredText(kit, line.token, line.icon.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(line.text);
        }
        popFont(kit);
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Status bar.
    pushFont(kit, ui::FontRole::Caption);
    ImGui::AlignTextToFramePadding();
    coloredText(kit, ThemeToken::TextSecondary, m_text.format(strings::kStatusSelection, {{"count", 1}}));
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
    coloredText(kit, ThemeToken::TextSecondary, m_text.format(strings::kStatusFps, {{"fps", 144}}));
    ImGui::SameLine(0.0f, style.ItemSpacing.x * 4.0f);
    coloredText(kit, ThemeToken::Success,
                m_text.concat(icons::kCheckCircle.view(),
                              m_text.concat(" ", m_text.format(strings::kStatusBuildSucceeded))));
    popFont(kit);
}

} // namespace illusion::editor::gallery

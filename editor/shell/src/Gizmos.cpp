#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/editor/ui/Icons.hpp>

#include <array>
#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

// Sizes in display points.
constexpr f32 kLineWidth = 2.5f;
constexpr f32 kRingWidth = 2.0f;
constexpr f32 kArrowLength = 12.0f;
constexpr f32 kArrowHalfWidth = 5.0f;
constexpr f32 kScaleBox = 4.5f;
constexpr f32 kLetterGap = 6.0f;
// Opacity of the back halves of rings, and of the plane fills.
constexpr f32 kBackAlpha = 0.3f;
constexpr f32 kPlaneAlpha = 0.3f;

constexpr std::array<text::TextKey, 3> kAxisLetters = {s::kViewportAxisX, s::kViewportAxisY,
                                                       s::kViewportAxisZ};

ImVec2 toWindow(const ViewportGizmo::Frame& frame, Vec2 pixel) noexcept
{
    const Vec2 at = frame.imageOrigin + pixel * frame.toPanel;
    return ImVec2(at.x, at.y);
}

ImU32 packed(Vec4 color, f32 alpha) noexcept
{
    return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

void appendNumber(std::string& out, f32 value)
{
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    out.append(buffer.data(), written > 0 ? static_cast<usize>(written) : 0);
}

void appendNumbers(std::string& out, std::initializer_list<f32> values)
{
    out.push_back('[');
    bool first = true;
    for (const f32 value : values)
    {
        if (!first)
        {
            out.push_back(',');
        }
        first = false;
        appendNumber(out, value);
    }
    out.push_back(']');
}

// The undo label of a drag of @p handle.
text::TextKey dragLabel(GizmoHandle handle) noexcept
{
    if (handle >= GizmoHandle::RotateX && handle <= GizmoHandle::RotateTrackball)
    {
        return s::kToolRotate;
    }
    if (handle >= GizmoHandle::ScaleX)
    {
        return s::kToolScale;
    }
    return s::kToolMove;
}

bool sameDelta(const GizmoDelta& a, const GizmoDelta& b) noexcept
{
    return a.translation == b.translation && a.rotation == b.rotation && a.scale == b.scale;
}

GizmoCamera cameraOf(const ViewportGizmo::Frame& frame) noexcept
{
    return gizmoCamera(frame.viewport->camera, frame.viewport->size, frame.pixelScale);
}

GizmoHandle axisHandle(GizmoHandle first, u32 axis) noexcept
{
    return static_cast<GizmoHandle>(static_cast<u32>(first) + axis);
}

} // namespace

void ViewportGizmo::refreshTargets(Impl& shell)
{
    EditorContext& editor = shell.editor;
    if (m_cached && editor.selection().version() == m_selectionVersion && editor.revision() == m_revision)
    {
        return;
    }
    gizmoTargets(editor, m_targets);
    m_start.resize(m_targets.size());
    for (usize i = 0; i < m_targets.size(); ++i)
    {
        m_start[i] = m_targets[i].placement;
    }
    m_selectionVersion = editor.selection().version();
    m_revision = editor.revision();
    m_cached = true;
}

std::optional<GizmoPose> ViewportGizmo::pose(Impl& shell, const Frame& frame)
{
    const ViewportFrame* viewport = frame.viewport;
    if (viewport == nullptr || viewport->kind != ViewportKind::Scene || viewport->size.x == 0 ||
        viewport->size.y == 0)
    {
        return std::nullopt;
    }
    const GizmoSettings& settings = shell.editor.gizmo();
    const GizmoTool tool = m_drag ? m_dragTool : settings.tool;
    if (tool == GizmoTool::Select)
    {
        return std::nullopt;
    }
    if (!m_drag)
    {
        refreshTargets(shell);
    }
    const std::span<const Transform> placements = m_drag ? std::span<const Transform>(m_placed) : m_start;
    return gizmoPose(placements, placements.size() - (placements.empty() ? 0 : 1), tool, settings.space,
                     m_drag ? m_dragPivot : settings.pivot, cameraOf(frame));
}

GizmoHandle ViewportGizmo::hoveredHandle(Impl& shell, const Frame& frame)
{
    if (!frame.hovered || m_drag || ImGui::GetIO().KeyAlt)
    {
        return GizmoHandle::None;
    }
    const std::optional<GizmoPose> placed = pose(shell, frame);
    if (!placed)
    {
        return GizmoHandle::None;
    }
    return gizmoHitTest(gizmoShape(shell.editor.gizmo().tool, cameraOf(frame), *placed), frame.cursor);
}

bool ViewportGizmo::begin(Impl& shell, const Frame& frame, GizmoHandle handle)
{
    const std::optional<GizmoPose> placed = pose(shell, frame);
    if (!placed || m_drag)
    {
        return false;
    }
    const GizmoSettings& settings = shell.editor.gizmo();
    std::optional<GizmoDrag> drag =
        GizmoDrag::begin(settings.tool, handle, cameraOf(frame), *placed, frame.cursor);
    if (!drag)
    {
        return false;
    }
    m_params = "{\"label\":";
    m_params += jsonQuote(shell.text.format(dragLabel(handle)));
    m_params += '}';
    if (!shell.run("transaction.begin", m_params, "gizmo"))
    {
        return false;
    }
    m_drag = drag;
    m_dragTool = settings.tool;
    m_dragPivot = settings.pivot;
    m_placed.assign(m_start.begin(), m_start.end());
    m_delta = GizmoDelta{};
    return true;
}

void ViewportGizmo::apply(Impl& shell, const GizmoDelta& delta)
{
    ILL_PROFILE_SCOPE();
    m_delta = delta;
    applyGizmoDelta(m_start, m_drag->pose(), m_dragPivot, delta, m_placed);
    m_params = "{\"steps\":[";
    for (usize i = 0; i < m_targets.size(); ++i)
    {
        if (i > 0)
        {
            m_params.push_back(',');
        }
        const auto hex = formatGuid(m_targets[i].guid);
        const Transform& placed = m_placed[i];
        m_params += R"({"method":"transform.set","params":{"entities":[")";
        m_params.append(hex.data(), hex.size());
        m_params += R"("],"position":)";
        appendNumbers(m_params, {placed.position.x, placed.position.y, placed.position.z});
        m_params += R"(,"rotation":)";
        appendNumbers(m_params, {placed.rotation.x, placed.rotation.y, placed.rotation.z, placed.rotation.w});
        m_params += R"(,"scale":)";
        appendNumbers(m_params, {placed.scale.x, placed.scale.y, placed.scale.z});
        m_params += "}}";
    }
    m_params += "]}";
    (void)shell.run("editor.batch", m_params, "gizmo");
}

void ViewportGizmo::end(Impl& shell, bool commit)
{
    m_drag.reset();
    m_cached = false;
    (void)shell.run(commit ? "transaction.commit" : "transaction.cancel", "", "gizmo");
}

void ViewportGizmo::update(Impl& shell, const Frame& frame, ImDrawList& draw, GizmoHandle hovered)
{
    const ImGuiIO& io = ImGui::GetIO();
    const GizmoSettings& settings = shell.editor.gizmo();
    if (m_drag)
    {
        if (frame.viewport == nullptr || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            end(shell, false);
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            end(shell, true);
        }
        else
        {
            const bool snapping = settings.snap.enabled != io.KeyCtrl;
            const GizmoDelta delta = m_drag->update(frame.cursor, snapping ? &settings.snap : nullptr);
            if (!sameDelta(delta, m_delta))
            {
                apply(shell, delta);
            }
        }
    }

    const std::optional<GizmoPose> placed = pose(shell, frame);
    if (!placed)
    {
        return;
    }
    const GizmoTool tool = m_drag ? m_dragTool : settings.tool;
    const GizmoShape shape = gizmoShape(tool, cameraOf(frame), *placed);
    if (!shape.visible)
    {
        return;
    }
    const ui::UiKit kit = shell.kit();
    const ui::Theme& theme = kit.theme;
    const std::array<Vec4, 3> axisColors = {
        theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbX : ui::ThemeToken::AxisX),
        theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbY : ui::ThemeToken::AxisY),
        theme.color(kit.colorBlindAxes ? ui::ThemeToken::AxisCbZ : ui::ThemeToken::AxisZ)};
    const Vec4 highlight = theme.color(ui::ThemeToken::ViewportHover);
    const Vec4 neutral = theme.color(ui::ThemeToken::TextPrimary);
    const GizmoHandle active = m_drag ? m_drag->handle() : hovered;
    const auto colorOf = [&](GizmoHandle handle, Vec4 base, f32 alpha) {
        return packed(handle == active ? highlight : base, alpha);
    };
    const f32 points = kit.scale;
    const ImVec2 center = toWindow(frame, shape.center);

    // Back halves of the rings first, so that everything else covers them.
    const auto drawRings = [&](GizmoSegment which) {
        for (u32 i = 0; i < 3; ++i)
        {
            const GizmoRingShape& ring = shape.rings[i];
            if (!(ring.visibility > 0.0f))
            {
                continue;
            }
            const GizmoHandle handle = axisHandle(GizmoHandle::RotateX, i);
            const ImU32 color =
                colorOf(handle, axisColors[i], which == GizmoSegment::Front ? 1.0f : kBackAlpha);
            for (u32 k = 0; k < kGizmoRingSegments; ++k)
            {
                if (ring.segments[k] == which)
                {
                    draw.AddLine(toWindow(frame, ring.points[k]),
                                 toWindow(frame, ring.points[(k + 1) % kGizmoRingSegments]), color,
                                 kRingWidth * points);
                }
            }
        }
    };
    drawRings(GizmoSegment::Back);
    if (shape.trackball > 0.0f)
    {
        draw.AddCircleFilled(center, shape.trackball * frame.toPanel,
                             colorOf(GizmoHandle::RotateTrackball, neutral, 0.06f), 64);
    }
    if (shape.viewRing > 0.0f)
    {
        draw.AddCircle(center, shape.viewRing * frame.toPanel,
                       colorOf(GizmoHandle::RotateView, neutral, 0.8f), 64, kRingWidth * points);
    }
    drawRings(GizmoSegment::Front);

    for (u32 i = 0; i < 3; ++i)
    {
        const GizmoPlaneShape& plane = shape.planes[i];
        if (!(plane.visibility > 0.0f))
        {
            continue;
        }
        const GizmoHandle handle = axisHandle(GizmoHandle::TranslateYZ, i);
        const ImVec2 a = toWindow(frame, plane.corners[0]);
        const ImVec2 b = toWindow(frame, plane.corners[1]);
        const ImVec2 c = toWindow(frame, plane.corners[2]);
        const ImVec2 d = toWindow(frame, plane.corners[3]);
        draw.AddQuadFilled(a, b, c, d, colorOf(handle, axisColors[i], kPlaneAlpha * plane.visibility));
        draw.AddQuad(a, b, c, d, colorOf(handle, axisColors[i], plane.visibility), points);
    }

    const bool scaleLines = tool == GizmoTool::Scale;
    for (u32 i = 0; i < 3; ++i)
    {
        const GizmoAxisShape& line = shape.lines[i];
        if (!(line.visibility > 0.0f))
        {
            continue;
        }
        const GizmoHandle handle = axisHandle(scaleLines ? GizmoHandle::ScaleX : GizmoHandle::TranslateX, i);
        const ImU32 color = colorOf(handle, axisColors[i], line.visibility);
        const ImVec2 start = toWindow(frame, line.start);
        const ImVec2 end = toWindow(frame, line.end);
        draw.AddLine(start, end, color, kLineWidth * points);
        const Vec2 along = Vec2(end.x - start.x, end.y - start.y);
        const f32 length = glm::length(along);
        const Vec2 direction = length > 1e-3f ? along / length : Vec2(1.0f, 0.0f);
        const Vec2 tip(end.x, end.y);
        f32 reach = kScaleBox * points;
        if (scaleLines)
        {
            const Vec2 half(kScaleBox * points);
            draw.AddRectFilled(ImVec2(tip.x - half.x, tip.y - half.y), ImVec2(tip.x + half.x, tip.y + half.y),
                               color);
        }
        else
        {
            const Vec2 side(-direction.y, direction.x);
            const Vec2 head = tip + direction * (kArrowLength * points);
            const Vec2 left = tip + side * (kArrowHalfWidth * points);
            const Vec2 right = tip - side * (kArrowHalfWidth * points);
            draw.AddTriangleFilled(ImVec2(head.x, head.y), ImVec2(left.x, left.y), ImVec2(right.x, right.y),
                                   color);
            reach = kArrowLength * points;
        }
        // The axis letter beyond the end.
        const char* letter = shell.text.format(kAxisLetters[i]);
        const ImVec2 size = ImGui::CalcTextSize(letter);
        const Vec2 at = tip + direction * (reach + kLetterGap * points + std::max(size.x, size.y) * 0.5f);
        draw.AddText(ImVec2(at.x - size.x * 0.5f, at.y - size.y * 0.5f), color, letter);
    }

    if (shape.centerBox > 0.0f)
    {
        const f32 half = shape.centerBox * frame.toPanel;
        const ImVec2 low(center.x - half, center.y - half);
        const ImVec2 high(center.x + half, center.y + half);
        if (tool == GizmoTool::Translate)
        {
            draw.AddRect(low, high, colorOf(GizmoHandle::TranslateScreen, neutral, 1.0f), 0.0f,
                         points * 1.5f);
        }
        else
        {
            draw.AddRectFilled(low, high, colorOf(GizmoHandle::ScaleUniform, neutral, 0.9f));
        }
    }
}

void drawGizmoToolbar(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const GizmoSettings& settings = shell.editor.gizmo();

    struct Choice
    {
        const ui::Icon* icon;
        text::TextKey title;
        const char* id;
        const char* params;
    };
    static const std::array<Choice, 3> kSpaces = {
        Choice{&ui::icons::kPublic, s::kGizmoSpaceWorld, "toolbar.gizmo.space.world", R"({"space":"world"})"},
        Choice{&ui::icons::kDeployedCode, s::kGizmoSpaceLocal, "toolbar.gizmo.space.local",
               R"({"space":"local"})"},
        Choice{&ui::icons::kVideocam, s::kGizmoSpaceView, "toolbar.gizmo.space.view", R"({"space":"view"})"}};
    static const std::array<Choice, 3> kPivots = {
        Choice{&ui::icons::kFilterCenterFocus, s::kGizmoPivotMedian, "toolbar.gizmo.pivot.median",
               R"({"pivot":"median"})"},
        Choice{&ui::icons::kAdjust, s::kGizmoPivotActive, "toolbar.gizmo.pivot.active",
               R"({"pivot":"active"})"},
        Choice{&ui::icons::kScatterPlot, s::kGizmoPivotIndividual, "toolbar.gizmo.pivot.individual",
               R"({"pivot":"individual"})"}};

    const auto menu = [&](const std::array<Choice, 3>& choices, usize current, text::TextKey title,
                          const char* id, const char* popup, const char* command) {
        const Choice& shown = choices[std::min<usize>(current, choices.size() - 1)];
        if (ui::iconButton(kit, *shown.icon, id, text.format(title), text.format(shown.title)))
        {
            ImGui::OpenPopup(popup);
        }
        if (ImGui::BeginPopup(popup))
        {
            for (usize i = 0; i < choices.size(); ++i)
            {
                if (ImGui::MenuItem(text.label(choices[i].title, choices[i].id), nullptr, i == current))
                {
                    (void)shell.run(command, choices[i].params, "toolbar");
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
    };
    menu(kSpaces, static_cast<usize>(settings.space), s::kGizmoSpace, "toolbar.gizmo.space",
         "###toolbar.gizmo.space.menu", "gizmo.set_space");
    menu(kPivots, static_cast<usize>(settings.pivot), s::kGizmoPivot, "toolbar.gizmo.pivot",
         "###toolbar.gizmo.pivot.menu", "gizmo.set_pivot");

    if (ui::iconButton(kit, ui::icons::kGridOn, "toolbar.gizmo.snap", text.format(s::kGizmoSnap), nullptr,
                       settings.snap.enabled))
    {
        (void)shell.run("gizmo.set_snap",
                        settings.snap.enabled ? R"({"enabled":false})" : R"({"enabled":true})", "toolbar");
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (ui::iconButton(kit, ui::icons::kExpandMore, "toolbar.gizmo.snap.more",
                       text.format(s::kGizmoSnapIncrements)))
    {
        ImGui::OpenPopup("###toolbar.gizmo.snap.menu");
    }
    if (ImGui::BeginPopup("###toolbar.gizmo.snap.menu"))
    {
        struct Step
        {
            text::TextKey title;
            const char* id;
            const char* member;
            f32 value;
            f32 speed;
            f32 low;
            f32 high;
            const char* format;
        };
        const std::array<Step, 3> steps = {
            Step{s::kGizmoSnapTranslate, "toolbar.gizmo.snap.translate", "translate", settings.snap.translate,
                 0.01f, kGizmoMinTranslateSnap, kGizmoMaxTranslateSnap, "%.3f"},
            Step{s::kGizmoSnapRotate, "toolbar.gizmo.snap.rotate", "rotate", settings.snap.rotate, 0.5f,
                 kGizmoMinRotateSnap, kGizmoMaxRotateSnap, "%.2f"},
            Step{s::kGizmoSnapScale, "toolbar.gizmo.snap.scale", "scale", settings.snap.scale, 0.005f,
                 kGizmoMinScaleSnap, kGizmoMaxScaleSnap, "%.3f"}};
        for (const Step& step : steps)
        {
            f32 value = step.value;
            ImGui::SetNextItemWidth(120.0f * kit.scale);
            if (ImGui::DragFloat(text.label(step.title, step.id), &value, step.speed, step.low, step.high,
                                 step.format, ImGuiSliderFlags_AlwaysClamp))
            {
                std::array<char, 64> params{};
                (void)std::snprintf(params.data(), params.size(), R"({"%s":%.9g})", step.member,
                                    static_cast<double>(value));
                (void)shell.run("gizmo.set_snap", params.data(), "toolbar");
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
}

} // namespace illusion::editor::shell::detail

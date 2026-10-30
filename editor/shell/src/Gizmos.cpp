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

// The undo label of a modal @p operation.
text::TextKey modalLabel(GizmoModalOperation operation) noexcept
{
    switch (operation)
    {
    case GizmoModalOperation::Rotate:
        return s::kToolRotate;
    case GizmoModalOperation::Scale:
        return s::kToolScale;
    case GizmoModalOperation::Translate:
        break;
    }
    return s::kToolMove;
}

// "1.234" for the ruler's label.
std::array<char, 24> rulerNumber(f32 value) noexcept
{
    std::array<char, 24> out{};
    (void)std::snprintf(out.data(), out.size(), "%.3f", static_cast<double>(value));
    return out;
}

} // namespace

void SnapTracker::update(ViewportHost& host, std::string_view id, Vec2 pixel, ViewportSnapTarget target,
                         f32 tolerance, std::span<const Entity> exclude)
{
    if (m_ticket != 0 && target != m_target)
    {
        m_drop = true;
    }
    if (m_ticket != 0)
    {
        std::optional<Expected<std::optional<ViewportSnap>, CommandError>> done = host.snap(m_ticket);
        if (!done)
        {
            return;
        }
        if (!m_drop)
        {
            m_found = *done ? done->value() : std::nullopt;
            m_answered = true;
            m_any = true;
        }
        m_ticket = 0;
        m_drop = false;
    }
    if (m_answered && pixel == m_pixel && target == m_target)
    {
        return;
    }
    ViewportSnapRequest request;
    request.pixel = pixel;
    request.target = target;
    request.tolerance = tolerance;
    request.exclude = exclude;
    if (const Expected<u64, CommandError> ticket = host.requestSnap(id, request))
    {
        m_ticket = *ticket;
        m_pixel = pixel;
        m_target = target;
        m_answered = false;
    }
}

void SnapTracker::reset() noexcept
{
    m_drop = m_ticket != 0;
    m_answered = false;
    m_any = false;
    m_pixel = Vec2(-1.0f);
    m_found.reset();
}

void drawRuler(Impl& shell, const ViewportGizmo::Frame& frame, ImDrawList& draw, const GizmoRuler& ruler)
{
    const ViewportFrame* viewport = frame.viewport;
    if (viewport == nullptr)
    {
        return;
    }
    const ViewportPoint from = projectToViewport(viewport->camera, viewport->size, ruler.from);
    const ViewportPoint to = projectToViewport(viewport->camera, viewport->size, ruler.to);
    if (!from.inFront || !to.inFront)
    {
        return;
    }
    const ui::UiKit kit = shell.kit();
    const ui::Theme& theme = kit.theme;
    const f32 points = kit.scale;
    const ImU32 line = packed(theme.color(ui::ThemeToken::ViewportHover), 1.0f);
    const ImVec2 a = toWindow(frame, from.pixel);
    const ImVec2 b = toWindow(frame, to.pixel);
    draw.AddLine(a, b, line, kLineWidth * points);
    draw.AddCircleFilled(a, kScaleBox * points, line);
    draw.AddCircleFilled(b, kScaleBox * points, line);
    const Measurement measured = measure(ruler.from, ruler.to);
    const auto distance = rulerNumber(measured.distance);
    const auto x = rulerNumber(measured.delta.x);
    const auto y = rulerNumber(measured.delta.y);
    const auto z = rulerNumber(measured.delta.z);
    const auto elevation = rulerNumber(measured.elevation);
    const char* label = shell.text.format(s::kGizmoRuler, {{"distance", distance.data()},
                                                           {"x", x.data()},
                                                           {"y", y.data()},
                                                           {"z", z.data()},
                                                           {"elevation", elevation.data()}});
    const ImVec2 size = ImGui::CalcTextSize(label);
    const Vec2 padding(4.0f * points);
    const Vec2 middle((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const Vec2 low = middle - Vec2(size.x, size.y) * 0.5f - padding;
    const Vec2 high = middle + Vec2(size.x, size.y) * 0.5f + padding;
    draw.AddRectFilled(ImVec2(low.x, low.y), ImVec2(high.x, high.y),
                       packed(theme.color(ui::ThemeToken::BgRaised), 0.85f), 3.0f * points);
    draw.AddText(ImVec2(low.x + padding.x, low.y + padding.y),
                 packed(theme.color(ui::ThemeToken::TextPrimary), 1.0f), label);
}

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
    if (tool == GizmoTool::Select || m_modal)
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
    if (!frame.hovered || dragging() || ImGui::GetIO().KeyAlt)
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

bool ViewportGizmo::beginTransaction(Impl& shell, text::TextKey label)
{
    m_params = "{\"label\":";
    m_params += jsonQuote(shell.text.format(label));
    m_params += '}';
    if (!shell.run("transaction.begin", m_params, "gizmo"))
    {
        return false;
    }
    m_placed.assign(m_start.begin(), m_start.end());
    m_delta = GizmoDelta{};
    m_typed.clear();
    m_excluded.clear();
    for (const GizmoTarget& target : m_targets)
    {
        if (const Entity entity = shell.editor.entities().lookup(target.guid); !entity.isNull())
        {
            m_excluded.push_back(entity);
        }
    }
    m_snap.reset();
    m_snapped = false;
    return true;
}

bool ViewportGizmo::begin(Impl& shell, const Frame& frame, GizmoHandle handle)
{
    const std::optional<GizmoPose> placed = pose(shell, frame);
    if (!placed || dragging())
    {
        return false;
    }
    const GizmoSettings& settings = shell.editor.gizmo();
    std::optional<GizmoDrag> drag =
        GizmoDrag::begin(settings.tool, handle, cameraOf(frame), *placed, frame.cursor);
    if (!drag || !beginTransaction(shell, dragLabel(handle)))
    {
        return false;
    }
    m_drag = drag;
    m_dragTool = settings.tool;
    m_dragPivot = settings.pivot;
    return true;
}

bool ViewportGizmo::beginModal(Impl& shell, const Frame& frame, GizmoModalOperation operation)
{
    const ViewportFrame* viewport = frame.viewport;
    if (dragging() || viewport == nullptr || viewport->kind != ViewportKind::Scene)
    {
        return false;
    }
    refreshTargets(shell);
    const GizmoSettings& settings = shell.editor.gizmo();
    const GizmoCamera camera = cameraOf(frame);
    const std::optional<GizmoPose> placed =
        gizmoPose(m_start, m_start.empty() ? 0 : m_start.size() - 1, GizmoTool::Translate, GizmoSpace::World,
                  settings.pivot, camera);
    if (!placed)
    {
        return false;
    }
    std::optional<GizmoModal> modal =
        GizmoModal::begin(operation, camera, placed->origin, m_start.back().rotation, frame.cursor);
    if (!modal || !beginTransaction(shell, modalLabel(operation)))
    {
        return false;
    }
    m_modal = modal;
    m_dragPivot = settings.pivot;
    return true;
}

void ViewportGizmo::typeNumbers()
{
    const ImGuiIO& io = ImGui::GetIO();
    for (const ImWchar character : io.InputQueueCharacters)
    {
        if (character < 128)
        {
            (void)m_typed.type(static_cast<char>(character));
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
    {
        m_typed.erase();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
    {
        m_typed.next();
    }
}

void ViewportGizmo::apply(Impl& shell, const GizmoPose& pose, const GizmoDelta& delta)
{
    ILL_PROFILE_SCOPE();
    m_delta = delta;
    applyGizmoDelta(m_start, pose, m_dragPivot, delta, m_placed);
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

bool ViewportGizmo::snapDelta(Impl& shell, const Frame& frame, GizmoDelta& delta)
{
    const ImGuiIO& io = ImGui::GetIO();
    const bool vertex = ImGui::IsKeyDown(ImGuiKey_V);
    const bool surface = io.KeyCtrl && io.KeyShift;
    const bool translating = m_modal ? m_modal->operation() == GizmoModalOperation::Translate
                                     : m_drag && gizmoHandleAxes(m_drag->handle()) != 0;
    ViewportHost* host = shell.editor.viewports();
    m_snapped = false;
    if ((!vertex && !surface) || !translating || host == nullptr || frame.id.empty() || m_typed.active())
    {
        m_snap.reset();
        return false;
    }
    m_snap.update(*host, frame.id, frame.cursor,
                  vertex ? ViewportSnapTarget::Vertex : ViewportSnapTarget::Surface,
                  kVertexSnapTolerance * frame.pixelScale, m_excluded);
    const std::optional<ViewportSnap>& found = m_snap.found();
    if (!found)
    {
        return false;
    }
    const GizmoPose& pose = m_modal ? m_modal->pose() : m_drag->pose();
    const u32 axes = m_modal ? m_modal->axes() : gizmoHandleAxes(m_drag->handle());
    delta = GizmoDelta{};
    delta.translation = constrainOffset(pose, axes, found->position - pose.origin);
    if (found->kind == ViewportSnapTarget::Surface && shell.editor.gizmo().snap.alignToNormal &&
        !m_start.empty())
    {
        // The active entity's +y turns onto the normal; the others turn with it about the pivot.
        const Quat start = m_start.back().rotation;
        delta.rotation = glm::normalize(alignToNormal(start, found->normal) * glm::conjugate(start));
    }
    m_snapped = true;
    return true;
}

void ViewportGizmo::drawSnapMarker(Impl& shell, const Frame& frame, ImDrawList& draw)
{
    const std::optional<ViewportSnap>& found = m_snap.found();
    if (!m_snapped || !found || frame.viewport == nullptr)
    {
        return;
    }
    const ViewportPoint point =
        projectToViewport(frame.viewport->camera, frame.viewport->size, found->position);
    if (!point.inFront)
    {
        return;
    }
    const ui::UiKit kit = shell.kit();
    const f32 points = kit.scale;
    const ImU32 color = packed(kit.theme.color(ui::ThemeToken::ViewportHover), 1.0f);
    const ImVec2 at = toWindow(frame, point.pixel);
    draw.AddCircle(at, kArrowLength * points, color, 24, kRingWidth * points);
    const char* label = shell.text.format(found->kind == ViewportSnapTarget::Vertex ? s::kGizmoSnapVertex
                                                                                    : s::kGizmoSnapSurface);
    draw.AddText(
        ImVec2(at.x + kArrowLength * points + kLetterGap * points, at.y - ImGui::GetTextLineHeight()), color,
        label);
}

bool ViewportGizmo::beginMeasure(Impl& shell, const Frame& frame)
{
    if (shell.editor.gizmo().tool != GizmoTool::Measure || dragging() || frame.viewport == nullptr ||
        frame.id.empty())
    {
        return false;
    }
    m_measuring = true;
    m_measurePress = frame.cursor;
    m_measureFrom.reset();
    m_measureTo.reset();
    return true;
}

void ViewportGizmo::updateMeasure(Impl& shell, const Frame& frame, ImDrawList& draw)
{
    const GizmoSettings& settings = shell.editor.gizmo();
    if (settings.ruler && !m_measuring)
    {
        drawRuler(shell, frame, draw, *settings.ruler);
    }
    ViewportHost* host = shell.editor.viewports();
    if (m_measuring &&
        (frame.viewport == nullptr || host == nullptr || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
    {
        m_measuring = false;
        return;
    }
    if (!m_measuring)
    {
        if (settings.tool == GizmoTool::Measure && frame.hovered && frame.viewport != nullptr)
        {
            if (settings.ruler && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                (void)shell.run("gizmo.clear_ruler", "", "gizmo");
            }
            const ui::UiKit kit = shell.kit();
            const ImVec2 cursor = toWindow(frame, frame.cursor);
            draw.AddText(ImVec2(cursor.x + 16.0f * kit.scale, cursor.y + 16.0f * kit.scale),
                         packed(kit.theme.color(ui::ThemeToken::TextPrimary), 0.7f),
                         shell.text.format(s::kGizmoMeasureHint));
        }
        return;
    }
    // Vertices with V, otherwise surfaces; the ground plane where there is none.
    const ViewportSnapTarget target =
        ImGui::IsKeyDown(ImGuiKey_V) ? ViewportSnapTarget::Vertex : ViewportSnapTarget::Surface;
    const f32 tolerance = kVertexSnapTolerance * frame.pixelScale;
    m_measureFrom.update(*host, frame.id, m_measurePress, target, tolerance, {});
    m_measureTo.update(*host, frame.id, frame.cursor, target, tolerance, {});
    const GizmoCamera camera = cameraOf(frame);
    const auto pointOf = [&](const SnapTracker& tracker, Vec2 pixel) -> std::optional<Vec3> {
        if (!tracker.answered())
        {
            return std::nullopt;
        }
        if (tracker.found())
        {
            return tracker.found()->position;
        }
        const GizmoRay ray = gizmoRay(camera, pixel);
        const f32 along = std::abs(ray.direction.y) > 1e-6f ? -ray.origin.y / ray.direction.y : -1.0f;
        return along > 0.0f ? std::optional<Vec3>(ray.origin + ray.direction * along) : std::nullopt;
    };
    const std::optional<Vec3> from = pointOf(m_measureFrom, m_measurePress);
    const std::optional<Vec3> to = pointOf(m_measureTo, frame.cursor);
    if (from && to)
    {
        drawRuler(shell, frame, draw, GizmoRuler{*from, *to});
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_measuring = false;
        if (from && to && *from != *to)
        {
            std::string params = R"({"from":)";
            appendNumbers(params, {from->x, from->y, from->z});
            params += R"(,"to":)";
            appendNumbers(params, {to->x, to->y, to->z});
            params += '}';
            (void)shell.run("gizmo.measure", params, "gizmo");
        }
    }
}

void ViewportGizmo::end(Impl& shell, bool commit)
{
    m_drag.reset();
    m_modal.reset();
    m_typed.clear();
    m_snap.reset();
    m_snapped = false;
    m_cached = false;
    (void)shell.run(commit ? "transaction.commit" : "transaction.cancel", "", "gizmo");
}

void ViewportGizmo::update(Impl& shell, const Frame& frame, ImDrawList& draw, GizmoHandle hovered)
{
    const ImGuiIO& io = ImGui::GetIO();
    const GizmoSettings& settings = shell.editor.gizmo();
    const bool confirm =
        ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
    const GizmoSnap* snap = settings.snap.enabled != io.KeyCtrl ? &settings.snap : nullptr;
    updateMeasure(shell, frame, draw);
    if (dragging())
    {
        if (frame.viewport == nullptr || ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            end(shell, false);
        }
        else if (confirm || (m_drag && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) ||
                 (m_modal && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))
        {
            end(shell, true);
        }
        else
        {
            typeNumbers();
            GizmoDelta delta;
            if (m_modal)
            {
                constexpr std::array<ImGuiKey, 3> kAxisKeys = {ImGuiKey_X, ImGuiKey_Y, ImGuiKey_Z};
                for (u32 i = 0; i < 3; ++i)
                {
                    if (ImGui::IsKeyPressed(kAxisKeys[i], false))
                    {
                        m_modal->constrain(i, io.KeyShift);
                    }
                }
                delta = m_modal->update(frame.cursor, snap, m_typed);
            }
            else
            {
                delta = m_typed.active() ? m_drag->typed(m_typed) : m_drag->update(frame.cursor, snap);
            }
            // A vertex or surface under the cursor replaces the cursor's translation.
            GizmoDelta snapped;
            if (snapDelta(shell, frame, snapped))
            {
                delta = snapped;
            }
            if (!sameDelta(delta, m_delta))
            {
                apply(shell, m_modal ? m_modal->pose() : m_drag->pose(), delta);
            }
        }
    }
    if (dragging() && frame.viewport != nullptr)
    {
        drawSnapMarker(shell, frame, draw);
    }
    if (m_modal && frame.viewport != nullptr)
    {
        drawModal(shell, frame, draw);
        return;
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

void ViewportGizmo::drawModal(Impl& shell, const Frame& frame, ImDrawList& draw)
{
    const GizmoModal& modal = *m_modal;
    const GizmoShape shape = gizmoShape(GizmoTool::Translate, cameraOf(frame), modal.pose());
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
    const Vec4 neutral = theme.color(ui::ThemeToken::TextPrimary);
    const f32 points = kit.scale;
    const ImVec2 center = toWindow(frame, shape.center);

    // Each constrained axis as a line through the pivot across the image.
    const f32 reach = static_cast<f32>(frame.viewport->size.x + frame.viewport->size.y);
    for (u32 i = 0; i < 3; ++i)
    {
        if ((modal.axes() & (1u << i)) == 0)
        {
            continue;
        }
        const Vec2 along = shape.lines[i].end - shape.center;
        const f32 length = glm::length(along);
        if (!(length > 1e-3f))
        {
            continue;
        }
        const Vec2 direction = along / length * reach;
        draw.AddLine(toWindow(frame, shape.center - direction), toWindow(frame, shape.center + direction),
                     packed(axisColors[i], 0.8f), kRingWidth * points);
    }
    const ImVec2 cursor = toWindow(frame, frame.cursor);
    if (modal.operation() != GizmoModalOperation::Translate)
    {
        draw.AddLine(center, cursor, packed(neutral, 0.6f), points);
    }
    draw.AddCircleFilled(center, kScaleBox * points, packed(neutral, 0.9f));

    // What it does, its constraint and the typed numbers next to the cursor, the keys below.
    ui::FrameText& text = shell.text;
    std::string_view line = text.format(modalLabel(modal.operation()));
    if (modal.axes() != 0)
    {
        line = text.concat(line, "  ");
        for (u32 i = 0; i < 3; ++i)
        {
            if ((modal.axes() & (1u << i)) != 0)
            {
                line = text.concat(line, text.format(kAxisLetters[i]));
            }
        }
        if (modal.local())
        {
            line = text.concat(text.concat(line, " "), text.format(s::kGizmoModalLocal));
        }
    }
    if (m_typed.active())
    {
        for (u32 field = 0; field < GizmoTypedInput::kFields; ++field)
        {
            line = text.concat(line, field == 0 ? "  " : ", ");
            line = text.concat(line, m_typed.text(field));
            if (field == m_typed.field())
            {
                line = text.concat(line, "|");
            }
        }
    }
    const ImVec2 at(cursor.x + 16.0f * points, cursor.y + 16.0f * points);
    draw.AddText(at, packed(neutral, 1.0f), line.data(), line.data() + line.size());
    const char* hint = text.format(s::kGizmoModalHint);
    draw.AddText(ImVec2(at.x, at.y + ImGui::GetTextLineHeightWithSpacing()), packed(neutral, 0.7f), hint);
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
    static const std::array<Choice, 4> kPivots = {
        Choice{&ui::icons::kFilterCenterFocus, s::kGizmoPivotMedian, "toolbar.gizmo.pivot.median",
               R"({"pivot":"median"})"},
        Choice{&ui::icons::kAdjust, s::kGizmoPivotActive, "toolbar.gizmo.pivot.active",
               R"({"pivot":"active"})"},
        Choice{&ui::icons::kScatterPlot, s::kGizmoPivotIndividual, "toolbar.gizmo.pivot.individual",
               R"({"pivot":"individual"})"},
        Choice{&ui::icons::kSelectAll, s::kGizmoPivotBoundsCenter, "toolbar.gizmo.pivot.bounds_center",
               R"({"pivot":"bounds_center"})"}};

    const auto menu = [&](std::span<const Choice> choices, usize current, text::TextKey title, const char* id,
                          const char* popup, const char* command) {
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
        if (ImGui::MenuItem(text.label(s::kGizmoSnapAlignNormal, "toolbar.gizmo.snap.align_normal"), nullptr,
                            settings.snap.alignToNormal))
        {
            (void)shell.run("gizmo.set_snap",
                            settings.snap.alignToNormal ? R"({"alignToNormal":false})"
                                                        : R"({"alignToNormal":true})",
                            "toolbar");
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
}

} // namespace illusion::editor::shell::detail

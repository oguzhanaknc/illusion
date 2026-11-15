// Canvas editing in the Game view (ADR 0057, addendum UI-3d): the math is UiPlacementEdit.hpp in the editor
// core; this file draws its handles over the Game view's image and turns drags into commands.

#include "Panels.hpp"

#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ui/UiComponents.hpp>

#include <array>
#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

// Sizes in display points.
constexpr f32 kHandleHalf = 4.0f;   // half the side of a resize handle
constexpr f32 kGrabRadius = 7.0f;   // how far from a handle the cursor grabs it
constexpr f32 kAnchorRadius = 5.0f; // anchor markers
constexpr f32 kOutlineWidth = 1.5f;
// Opacity of the rectangle the primary selection is placed in.
constexpr f32 kParentAlpha = 0.45f;
// Snap steps of drags in canvas units: whole units, coarse with Ctrl.
constexpr f32 kSnapFine = 1.0f;
constexpr f32 kSnapCoarse = 8.0f;

ImU32 packed(Vec4 color, f32 alpha = 1.0f) noexcept
{
    return ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * alpha));
}

ImVec2 toImVec(Vec2 value) noexcept
{
    return ImVec2(value.x, value.y);
}

bool placedByParent(const World& world, Entity entity)
{
    const Entity parent = parentOf(world, entity);
    return !parent.isNull() && (world.has<UiStack>(parent) || world.has<UiGrid>(parent));
}

bool isUiElement(const World& world, Entity entity)
{
    return !entity.isNull() && world.isAlive(entity) && world.has<UiElement>(entity) &&
           !world.has<UiCanvas>(entity);
}

// The index of @p entity in @p layout (none when the layout skipped it).
std::optional<u32> elementIndex(const UiLayout& layout, Entity entity) noexcept
{
    for (u32 i = 0; i < layout.elements.size(); ++i)
    {
        if (layout.elements[i].entity == entity)
        {
            return i;
        }
    }
    return std::nullopt;
}

f32 pixelsPerUnit(const UiLayout& layout, const UiLayoutElement& element) noexcept
{
    const f32 value = element.canvas < layout.canvases.size()
                          ? layout.canvases[element.canvas].metrics.pixelsPerUnit
                          : 1.0f;
    return value > 0.0f ? value : 1.0f;
}

text::TextKey dragLabel(UiHandle handle) noexcept
{
    switch (handle)
    {
    case UiHandle::Body:
        return s::kCanvasMove;
    case UiHandle::AnchorMin:
    case UiHandle::AnchorMax:
    case UiHandle::Anchors:
        return s::kCanvasAnchors;
    default:
        break;
    }
    return s::kCanvasResize;
}

ImGuiMouseCursor cursorOf(UiHandle handle) noexcept
{
    switch (handle)
    {
    case UiHandle::Body:
        return ImGuiMouseCursor_ResizeAll;
    case UiHandle::Left:
    case UiHandle::Right:
        return ImGuiMouseCursor_ResizeEW;
    case UiHandle::Top:
    case UiHandle::Bottom:
        return ImGuiMouseCursor_ResizeNS;
    case UiHandle::TopLeft:
    case UiHandle::BottomRight:
        return ImGuiMouseCursor_ResizeNWSE;
    case UiHandle::TopRight:
    case UiHandle::BottomLeft:
        return ImGuiMouseCursor_ResizeNESW;
    case UiHandle::AnchorMin:
    case UiHandle::AnchorMax:
    case UiHandle::Anchors:
        return ImGuiMouseCursor_Hand;
    case UiHandle::None:
        break;
    }
    return ImGuiMouseCursor_Arrow;
}

void appendNumber(std::string& out, f32 value)
{
    std::array<char, 32> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    out.append(buffer.data(), written > 0 ? static_cast<usize>(written) : 0);
}

void appendPair(std::string& out, Vec2 value)
{
    out.push_back('[');
    appendNumber(out, value.x);
    out.push_back(',');
    appendNumber(out, value.y);
    out.push_back(']');
}

void appendTarget(std::string& out, UVec2 target)
{
    std::array<char, 48> buffer{};
    const int written =
        std::snprintf(buffer.data(), buffer.size(), R"(,"width":%u,"height":%u})", target.x, target.y);
    out.append(buffer.data(), written > 0 ? static_cast<usize>(written) : 0);
}

} // namespace

bool CanvasEditor::refresh(Impl& shell, const Frame& frame)
{
    m_primary.valid = false;
    m_hasLayout = false;
    ViewportHost* host = shell.editor.viewports();
    if (!frame.active || host == nullptr)
    {
        return false;
    }
    // Nothing to draw or pick while the cursor is elsewhere and no UI element is selected.
    if (!frame.hovered && !pressed())
    {
        const World& world = shell.editor.world();
        bool selected = false;
        for (const EntityGuid guid : shell.editor.selection().items())
        {
            if (isUiElement(world, shell.editor.entities().lookup(guid)))
            {
                selected = true;
                break;
            }
        }
        if (!selected)
        {
            return false;
        }
    }
    m_hasLayout = host->gameUiLayout(frame.id, m_layout);
    if (m_hasLayout)
    {
        updatePrimary(shell);
    }
    return m_hasLayout;
}

void CanvasEditor::updatePrimary(Impl& shell)
{
    const World& world = shell.editor.world();
    const EntityGuid guid = shell.editor.selection().primary();
    const Entity entity = guid.isNull() ? Entity{} : shell.editor.entities().lookup(guid);
    if (!isUiElement(world, entity))
    {
        return;
    }
    const std::optional<u32> index = elementIndex(m_layout, entity);
    if (!index)
    {
        return;
    }
    const UiElement& element = world.get<UiElement>(entity);
    const std::optional<UiHandleFrame> frame =
        handleFrameOf(m_layout, *index, element.placement, element.size, !placedByParent(world, entity));
    if (!frame)
    {
        return;
    }
    m_primary.frame = *frame;
    m_primary.pixelsPerUnit = pixelsPerUnit(m_layout, m_layout.elements[*index]);
    m_primary.guid = guid;
    m_primary.valid = true;
}

Entity CanvasEditor::pick(Vec2 cursor)
{
    // Canvases may scale differently: each is picked alone, the topmost first.
    for (u32 c = static_cast<u32>(m_layout.canvases.size()); c-- > 0;)
    {
        m_scratch.elements.clear();
        u32 first = UiLayoutElement::kNoParent;
        for (u32 i = 0; i < m_layout.elements.size(); ++i)
        {
            UiLayoutElement element = m_layout.elements[i];
            if (element.canvas != c)
            {
                continue;
            }
            first = std::min(first, i);
            element.canvas = 0;
            if (element.parent != UiLayoutElement::kNoParent)
            {
                element.parent -= first;
            }
            m_scratch.elements.push_back(element);
        }
        if (m_scratch.elements.empty())
        {
            continue;
        }
        const f32 scale = m_layout.canvases[c].metrics.pixelsPerUnit;
        const Entity hit = pickUiElement(m_scratch, cursor / (scale > 0.0f ? scale : 1.0f));
        if (!hit.isNull())
        {
            return hit;
        }
    }
    return {};
}

UiHandle CanvasEditor::handleAt(const Frame& frame, Vec2 cursor, f32 scale) const
{
    if (!m_primary.valid)
    {
        return UiHandle::None;
    }
    const f32 radius = kGrabRadius * scale / std::max(frame.toPanel, 1e-6f) / m_primary.pixelsPerUnit;
    return uiHandleHitTest(m_primary.frame, cursor / m_primary.pixelsPerUnit, radius);
}

bool CanvasEditor::press(Impl& shell, const Frame& frame)
{
    m_consumed = false;
    m_pressed = UiHandle::None;
    if (!frame.active || !frame.hovered || !m_hasLayout || dragging() || ImGui::GetIO().KeyAlt)
    {
        return false;
    }
    const UiHandle handle = handleAt(frame, frame.cursor, shell.kit().scale);
    if (handle == UiHandle::None)
    {
        return false;
    }
    m_pressed = handle;
    m_pressCursor = frame.cursor;
    return true;
}

void CanvasEditor::begin(Impl& shell, const Frame& frame)
{
    m_consumed = true;
    if (!m_primary.valid ||
        !m_drag.begin(m_primary.frame, m_pressed, m_pressCursor / m_primary.pixelsPerUnit))
    {
        m_pressed = UiHandle::None;
        m_drag = UiHandleDrag{};
        return;
    }
    m_params = "{\"label\":";
    m_params += jsonQuote(shell.text.format(dragLabel(m_pressed)));
    m_params += '}';
    if (!shell.run("transaction.begin", m_params, "gizmo"))
    {
        m_pressed = UiHandle::None;
        m_drag = UiHandleDrag{};
        return;
    }
    m_dragged = m_primary;
    m_target = frame.size;
    m_applied = UiHandleEdit{.placement = m_primary.frame.placement, .size = m_primary.frame.size};
}

void CanvasEditor::apply(Impl& shell, const UiHandleEdit& edit)
{
    ILL_PROFILE_SCOPE();
    m_applied = edit;
    const UiHandleFrame& frame = m_dragged.frame;
    const auto hex = formatGuid(m_dragged.guid);
    m_params = R"({"entity":")";
    m_params.append(hex.data(), hex.size());
    const UiHandle handle = m_drag.handle();
    if (handle == UiHandle::AnchorMin || handle == UiHandle::AnchorMax || handle == UiHandle::Anchors)
    {
        m_params += R"(","anchorMin":)";
        appendPair(m_params, edit.placement.anchorMin);
        m_params += R"(,"anchorMax":)";
        appendPair(m_params, edit.placement.anchorMax);
        m_params += R"(,"keepRect":true)";
        appendTarget(m_params, m_target);
        (void)shell.run("gameui.set_anchors", m_params, "gizmo");
        return;
    }
    // A placed element reaches the rectangle its new offsets resolve to; a child of a stack or grid only
    // changes its size (max - min).
    const UiRect rect = frame.placed ? resolvePlacement(edit.placement, frame.parent)
                                     : UiRect{frame.rect.min, frame.rect.min + edit.size};
    m_params += R"(","min":)";
    appendPair(m_params, rect.min);
    m_params += R"(,"max":)";
    appendPair(m_params, rect.max);
    appendTarget(m_params, m_target);
    (void)shell.run("gameui.place", m_params, "gizmo");
}

void CanvasEditor::end(Impl& shell, bool commit)
{
    m_drag = UiHandleDrag{};
    m_pressed = UiHandle::None;
    (void)shell.run(commit ? "transaction.commit" : "transaction.cancel", "", "gizmo");
}

void CanvasEditor::update(Impl& shell, const Frame& frame, ImDrawList& draw)
{
    ILL_PROFILE_SCOPE();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !pressed())
    {
        m_consumed = false; // a new press that is not on a handle
    }
    if (!frame.active)
    {
        if (dragging())
        {
            end(shell, false);
        }
        m_pressed = UiHandle::None;
        m_hasLayout = false;
        m_primary.valid = false;
        m_hovered = {};
        return;
    }
    if (pressed() &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
    {
        if (dragging())
        {
            end(shell, false);
        }
        m_pressed = UiHandle::None;
        m_consumed = true;
    }
    const bool live = refresh(shell, frame);
    if (pressed())
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (dragging())
            {
                end(shell, true);
            }
            m_pressed = UiHandle::None;
        }
        else
        {
            if (!dragging() &&
                glm::length((frame.cursor - m_pressCursor) * frame.toPanel) >= io.MouseDragThreshold)
            {
                begin(shell, frame);
            }
            if (dragging())
            {
                const f32 snap = io.KeyAlt ? 0.0f : io.KeyCtrl ? kSnapCoarse : kSnapFine;
                const UiHandleEdit edit = m_drag.update(frame.cursor / m_dragged.pixelsPerUnit, snap);
                if (edit.placement != m_applied.placement || edit.size != m_applied.size)
                {
                    apply(shell, edit);
                }
            }
        }
    }
    if (!live)
    {
        m_hovered = {};
        return;
    }
    m_hovered = frame.hovered && !dragging() ? pick(frame.cursor) : Entity{};
    const UiHandle hot = dragging()      ? m_drag.handle()
                         : pressed()     ? m_pressed
                         : frame.hovered ? handleAt(frame, frame.cursor, shell.kit().scale)
                                         : UiHandle::None;
    if (hot != UiHandle::None)
    {
        ImGui::SetMouseCursor(cursorOf(hot));
    }
    drawOverlays(shell, frame, draw, hot);
}

void CanvasEditor::drawOverlays(Impl& shell, const Frame& frame, ImDrawList& draw, UiHandle hot)
{
    const ui::UiKit kit = shell.kit();
    const ui::Theme& theme = kit.theme;
    const f32 points = kit.scale;
    const World& world = shell.editor.world();
    const Vec4 selection = theme.color(ui::ThemeToken::CanvasSelection);
    const Vec4 hover = theme.color(ui::ThemeToken::ViewportHover);
    const auto toWindow = [&](Vec2 point, f32 pixelsPerUnit) {
        return toImVec(frame.imageOrigin + point * (pixelsPerUnit * frame.toPanel));
    };
    const auto quad = [&](const std::array<Vec2, 4>& corners, f32 pixelsPerUnit, ImU32 color, f32 width) {
        std::array<ImVec2, 4> at{};
        for (usize i = 0; i < corners.size(); ++i)
        {
            at[i] = toWindow(corners[i], pixelsPerUnit);
        }
        draw.AddPolyline(at.data(), static_cast<int>(at.size()), color, width, ImDrawFlags_Closed);
    };
    const auto cornersOf = [](const UiRect& rect, const UiTransform2D& transform) {
        return std::array<Vec2, 4>{transform.apply(rect.min), transform.apply(Vec2(rect.max.x, rect.min.y)),
                                   transform.apply(rect.max), transform.apply(Vec2(rect.min.x, rect.max.y))};
    };

    draw.PushClipRect(toImVec(frame.imageOrigin), toImVec(frame.imageOrigin + frame.display), true);
    if (!m_hovered.isNull() && !shell.editor.selection().contains(shell.editor.entities().guidOf(m_hovered)))
    {
        if (const std::optional<u32> index = elementIndex(m_layout, m_hovered))
        {
            const UiLayoutElement& element = m_layout.elements[*index];
            quad(cornersOf(element.rect, element.transform), pixelsPerUnit(m_layout, element), packed(hover),
                 points);
        }
    }
    for (const EntityGuid guid : shell.editor.selection().items())
    {
        const Entity entity = shell.editor.entities().lookup(guid);
        if (!isUiElement(world, entity))
        {
            continue;
        }
        if (const std::optional<u32> index = elementIndex(m_layout, entity))
        {
            const UiLayoutElement& element = m_layout.elements[*index];
            quad(cornersOf(element.rect, element.transform), pixelsPerUnit(m_layout, element),
                 packed(selection), kOutlineWidth * points);
        }
    }
    if (m_primary.valid)
    {
        const UiHandleFrame& primary = m_primary.frame;
        const f32 scale = m_primary.pixelsPerUnit;
        quad(cornersOf(primary.parent, primary.parentTransform), scale, packed(selection, kParentAlpha),
             points);

        // Corners, then the middles of the edges, in the order of UiHandle.
        const std::array<Vec2, 4> corners = handleCorners(primary);
        constexpr std::array<UiHandle, 8> kHandles = {
            UiHandle::TopLeft, UiHandle::TopRight, UiHandle::BottomRight, UiHandle::BottomLeft,
            UiHandle::Top,     UiHandle::Right,    UiHandle::Bottom,      UiHandle::Left};
        const Vec4 fill = theme.color(ui::ThemeToken::CanvasHandle);
        const f32 half = kHandleHalf * points;
        for (usize i = 0; i < kHandles.size(); ++i)
        {
            const Vec2 point = i < 4 ? corners[i] : (corners[i - 4] + corners[(i - 3) % 4]) * 0.5f;
            const ImVec2 at = toWindow(point, scale);
            const ImVec2 low(at.x - half, at.y - half);
            const ImVec2 high(at.x + half, at.y + half);
            draw.AddRectFilled(low, high, packed(kHandles[i] == hot ? hover : fill));
            draw.AddRect(low, high, packed(selection), 0.0f, points);
        }

        if (primary.placed)
        {
            const UiAnchors markers = anchorMarkers(primary);
            const Vec4 anchor = theme.color(ui::ThemeToken::CanvasAnchor);
            const f32 radius = kAnchorRadius * points;
            const auto marker = [&](Vec2 point, bool lit) {
                const ImVec2 at = toWindow(point, scale);
                draw.AddCircleFilled(at, radius, packed(lit ? hover : anchor));
                draw.AddCircle(at, radius, packed(fill), 0, points);
            };
            if (primary.placement.anchorMin == primary.placement.anchorMax)
            {
                marker(markers.min, hot == UiHandle::Anchors);
            }
            else
            {
                draw.AddLine(toWindow(markers.min, scale), toWindow(markers.max, scale), packed(anchor, 0.6f),
                             points);
                marker(markers.min, hot == UiHandle::AnchorMin);
                marker(markers.max, hot == UiHandle::AnchorMax);
            }
        }
    }
    draw.PopClipRect();
}

} // namespace illusion::editor::shell::detail

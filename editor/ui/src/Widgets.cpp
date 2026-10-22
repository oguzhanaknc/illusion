#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/FrameText.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/editor/ui/Widgets.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

namespace illusion::editor::ui
{

namespace
{

ImVec4 tokenVec(const UiKit& kit, ThemeToken token, f32 alphaFactor = 1.0f) noexcept
{
    const Vec4 c = kit.theme.color(token);
    return ImVec4(c.r, c.g, c.b, c.a * alphaFactor);
}

// "<id>.<index>"
const char* indexedId(UiKit& kit, std::string_view id, usize index)
{
    char suffix[24] = {'.'};
    const auto result = std::to_chars(suffix + 1, suffix + sizeof(suffix), index);
    return kit.text.concat(id, std::string_view(suffix, static_cast<usize>(result.ptr - suffix)));
}

// Black or white, whichever reads better on @p background.
u32 textOn(Vec4 background) noexcept
{
    const Vec4 black(0.0f, 0.0f, 0.0f, 1.0f);
    const Vec4 white(1.0f);
    return contrastRatio(background, black) >= contrastRatio(background, white) ? IM_COL32(0, 0, 0, 255)
                                                                                : IM_COL32_WHITE;
}

ImFont* iconFont(const UiKit& kit) noexcept
{
    return kit.fonts != nullptr ? kit.fonts->font(FontRole::Icon) : ImGui::GetFont();
}

void pushFont(const UiKit& kit, FontRole role) noexcept
{
    if (kit.fonts != nullptr)
    {
        kit.fonts->push(role);
    }
}

void popFont(const UiKit& kit) noexcept
{
    if (kit.fonts != nullptr)
    {
        ImGui::PopFont();
    }
}

// Draws @p icon centred in [min, max] at @p size pixels (final, already scaled).
void drawIcon(ImDrawList* drawList, const UiKit& kit, const Icon& icon, ImVec2 min, ImVec2 max, f32 size,
              u32 color)
{
    ImFont* font = iconFont(kit);
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon.c_str());
    const ImVec2 pos(std::floor((min.x + max.x - extent.x) * 0.5f),
                     std::floor((min.y + max.y - extent.y) * 0.5f));
    drawList->AddText(font, size, pos, color, icon.c_str());
}

void copyText(std::span<char> destination, std::string_view source) noexcept
{
    usize length = std::min(source.size(), destination.size() - 1);
    // Never cut a UTF-8 sequence in half.
    while (length > 0 && length < source.size() && (static_cast<u8>(source[length]) & 0xC0) == 0x80)
    {
        --length;
    }
    // An empty string_view may have a null data(), which memcpy must not receive.
    if (length > 0)
    {
        std::memcpy(destination.data(), source.data(), length);
    }
    destination[length] = '\0';
}

Vec4 axisColor(const UiKit& kit, usize axis) noexcept
{
    static constexpr std::array<ThemeToken, 3> kAxes = {ThemeToken::AxisX, ThemeToken::AxisY,
                                                        ThemeToken::AxisZ};
    static constexpr std::array<ThemeToken, 3> kColorBlindAxes = {ThemeToken::AxisCbX, ThemeToken::AxisCbY,
                                                                  ThemeToken::AxisCbZ};
    return kit.theme.color(kit.colorBlindAxes ? kColorBlindAxes[axis] : kAxes[axis]);
}

} // namespace

u32 tokenColor(const UiKit& kit, ThemeToken token, f32 alphaFactor) noexcept
{
    return ImGui::ColorConvertFloat4ToU32(tokenVec(kit, token, alphaFactor));
}

bool iconButton(UiKit& kit, const Icon& icon, std::string_view id, const char* tooltip, const char* shortcut,
                bool selected)
{
    const f32 side = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_Button,
                          selected ? tokenVec(kit, ThemeToken::AccentSubtle) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          tokenVec(kit, selected ? ThemeToken::Accent : ThemeToken::TextPrimary));
    const bool clicked = ImGui::Button(kit.text.iconOnly(icon, id), ImVec2(side, side));
    ImGui::PopStyleColor(2);
    if (tooltip != nullptr && ImGui::BeginItemTooltip())
    {
        ImGui::TextUnformatted(tooltip);
        if (shortcut != nullptr)
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::TextSecondary));
            ImGui::TextUnformatted(shortcut);
            ImGui::PopStyleColor();
        }
        ImGui::EndTooltip();
    }
    return clicked;
}

bool primaryButton(UiKit& kit, const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Button, tokenVec(kit, ThemeToken::AccentFill));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tokenVec(kit, ThemeToken::AccentFill, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, tokenVec(kit, ThemeToken::AccentFill, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::AccentText));
    pushFont(kit, FontRole::Strong);
    const bool clicked = ImGui::Button(label);
    popFont(kit);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool toggleSwitch(UiKit& kit, const char* label, bool& value)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
    {
        return false;
    }
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const f32 frameHeight = ImGui::GetFrameHeight();
    const f32 trackHeight = std::round(frameHeight * 0.68f);
    const f32 trackWidth = std::round(trackHeight * 1.8f);
    const ImVec2 pos = window->DC.CursorPos;
    const f32 labelWidth = labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f;
    const ImRect bb(pos, ImVec2(pos.x + trackWidth + labelWidth, pos.y + frameHeight));
    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
    {
        return false;
    }
    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
    {
        value = !value;
        ImGui::MarkItemEdited(id);
    }

    ImDrawList* drawList = window->DrawList;
    const ImVec2 trackMin(pos.x, pos.y + std::round((frameHeight - trackHeight) * 0.5f));
    const ImVec2 trackMax(trackMin.x + trackWidth, trackMin.y + trackHeight);
    const f32 radius = trackHeight * 0.5f;
    const ThemeToken trackToken =
        value ? ThemeToken::AccentFill : (hovered ? ThemeToken::BgHover : ThemeToken::BgActive);
    drawList->AddRectFilled(trackMin, trackMax, tokenColor(kit, trackToken, value && hovered ? 0.85f : 1.0f),
                            radius);
    if (kit.theme.isHighContrast() && !value)
    {
        drawList->AddRect(trackMin, trackMax, tokenColor(kit, ThemeToken::Border), radius);
    }
    const f32 knobRadius = radius - std::max(2.0f, std::round(radius * 0.2f));
    const f32 knobX = value ? trackMax.x - radius : trackMin.x + radius;
    drawList->AddCircleFilled(ImVec2(knobX, trackMin.y + radius), knobRadius,
                              tokenColor(kit, value ? ThemeToken::AccentText : ThemeToken::TextSecondary));
    ImGui::RenderNavCursor(bb, id);
    if (labelSize.x > 0.0f)
    {
        ImGui::RenderText(ImVec2(trackMax.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
    }
    IMGUI_TEST_ENGINE_ITEM_INFO(id, label,
                                g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Checkable |
                                    (value ? ImGuiItemStatusFlags_Checked : 0));
    return pressed;
}

bool segmentedControl(UiKit& kit, std::string_view id, std::span<const char* const> labels, int& selected)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 height = ImGui::GetFrameHeight();
    const f32 inset = std::max(2.0f, std::round(style.FramePadding.y * 0.5f));
    f32 width = 0.0f;
    for (const char* label : labels)
    {
        width += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + width + inset * 2.0f, origin.y + height),
                            tokenColor(kit, ThemeToken::BgInput), style.FrameRounding);
    if (kit.theme.isHighContrast())
    {
        drawList->AddRect(origin, ImVec2(origin.x + width + inset * 2.0f, origin.y + height),
                          tokenColor(kit, ThemeToken::Border), style.FrameRounding);
    }

    bool changed = false;
    f32 x = origin.x + inset;
    for (usize i = 0; i < labels.size(); ++i)
    {
        const f32 segment = ImGui::CalcTextSize(labels[i]).x + style.FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
        const char* itemId = kit.text.verbatim(labels[i], indexedId(kit, id, i));
        if (ImGui::InvisibleButton(itemId, ImVec2(segment, height)))
        {
            changed = selected != static_cast<int>(i);
            selected = static_cast<int>(i);
        }
        const bool isSelected = selected == static_cast<int>(i);
        const ImVec2 min(x, origin.y + inset);
        const ImVec2 max(x + segment, origin.y + height - inset);
        if (isSelected || ImGui::IsItemHovered())
        {
            drawList->AddRectFilled(
                min, max, tokenColor(kit, isSelected ? ThemeToken::AccentSubtle : ThemeToken::BgHover),
                std::max(0.0f, style.FrameRounding - 1.0f));
        }
        if (isSelected && kit.theme.isHighContrast())
        {
            drawList->AddRect(min, max, tokenColor(kit, ThemeToken::Accent),
                              std::max(0.0f, style.FrameRounding - 1.0f));
        }
        ImGui::RenderNavCursor(ImRect(min, max), ImGui::GetItemID());
        const ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
        drawList->AddText(ImVec2(std::floor(x + (segment - textSize.x) * 0.5f),
                                 std::floor(origin.y + (height - textSize.y) * 0.5f)),
                          tokenColor(kit, isSelected ? ThemeToken::TextPrimary : ThemeToken::TextSecondary),
                          labels[i]);
        x += segment;
    }
    // Leave the cursor after the control, as one item would.
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(width + inset * 2.0f, height));
    return changed;
}

bool searchField(UiKit& kit, std::string_view id, std::span<char> buffer, const char* hint)
{
    ILL_ASSERT(!buffer.empty(), "searchField needs a buffer");
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 height = ImGui::GetFrameHeight();
    const f32 iconSize = ImGui::GetFontSize();
    const f32 width = ImGui::CalcItemWidth();
    const bool hasText = buffer[0] != '\0';

    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(style.FramePadding.x + iconSize + style.ItemInnerSpacing.x, style.FramePadding.y));
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextItemAllowOverlap();
    bool changed = ImGui::InputTextWithHint(kit.text.hidden(id), hint, buffer.data(), buffer.size());
    ImGui::PopStyleVar();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    drawIcon(ImGui::GetWindowDrawList(), kit, icons::kSearch, ImVec2(min.x + style.FramePadding.x, min.y),
             ImVec2(min.x + style.FramePadding.x + iconSize, max.y), iconSize,
             tokenColor(kit, ThemeToken::TextSecondary));

    if (hasText)
    {
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(max.x - height, min.y));
        if (iconButton(kit, icons::kClose, kit.text.concat(id, ".clear"), kit.strings.clearSearch))
        {
            buffer[0] = '\0';
            changed = true;
        }
        ImGui::SetCursorScreenPos(after);
    }
    return changed;
}

bool vec3Field(UiKit& kit, std::string_view id, Vec3& value, f32 speed, const char* format)
{
    static constexpr std::array<const char*, 3> kLetters = {"X", "Y", "Z"};
    static constexpr std::array<std::string_view, 3> kSuffixes = {".x", ".y", ".z"};
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 spacing = style.ItemInnerSpacing.x;
    const f32 total = ImGui::CalcItemWidth();
    const f32 fieldWidth = std::max(1.0f, std::floor((total - spacing * 2.0f) / 3.0f));
    const f32 chipWidth = std::round(ImGui::GetFontSize() * 1.1f);
    bool changed = false;
    for (usize axis = 0; axis < 3; ++axis)
    {
        if (axis > 0)
        {
            ImGui::SameLine(0.0f, spacing);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x + chipWidth, style.FramePadding.y));
        ImGui::SetNextItemWidth(fieldWidth);
        changed |= ImGui::DragFloat(kit.text.hidden(kit.text.concat(id, kSuffixes[axis])),
                                    &value[static_cast<int>(axis)], speed, 0.0f, 0.0f, format);
        ImGui::PopStyleVar();

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const Vec4 color = axisColor(kit, axis);
        const ImVec2 chipMax(min.x + chipWidth, max.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, chipMax,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, 1.0f)),
                                style.FrameRounding, ImDrawFlags_RoundLeft);
        const ImVec2 letter = ImGui::CalcTextSize(kLetters[axis]);
        drawList->AddText(ImVec2(std::floor((min.x + chipMax.x - letter.x) * 0.5f),
                                 std::floor((min.y + max.y - letter.y) * 0.5f)),
                          textOn(color), kLetters[axis]);
    }
    return changed;
}

bool colorHdr(UiKit& kit, std::string_view id, Vec3& color, f32& intensity)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const f32 total = ImGui::CalcItemWidth();
    bool changed = ImGui::ColorEdit3(kit.text.hidden(id), &color.x,
                                     ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel |
                                         ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(std::max(1.0f, total - ImGui::GetFrameHeight() - style.ItemInnerSpacing.x));
    changed |= ImGui::DragFloat(kit.text.hidden(kit.text.concat(id, ".intensity")), &intensity, 0.01f, 0.0f,
                                1.0e6f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (kit.strings.intensity[0] != '\0')
    {
        ImGui::SetItemTooltip("%s", kit.strings.intensity);
    }
    return changed;
}

bool beginComponentCard(UiKit& kit, std::string_view id, const Icon& icon, const char* name, bool* enabled)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_Header, tokenVec(kit, ThemeToken::BgRaised));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tokenVec(kit, ThemeToken::BgHover));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, tokenVec(kit, ThemeToken::BgActive));
    pushFont(kit, FontRole::Strong);
    const char* label = kit.text.verbatim(kit.text.concat(icon.view(), kit.text.concat("  ", name)), id);
    const bool open =
        ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                     ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth |
                                     ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_FramePadding);
    popFont(kit);
    ImGui::PopStyleColor(3);

    // Enable toggle and menu button on the right of the header.
    const ImVec2 headerMin = ImGui::GetItemRectMin();
    const ImVec2 headerMax = ImGui::GetItemRectMax();
    const ImVec2 after = ImGui::GetCursorScreenPos();
    const f32 button = ImGui::GetFrameHeight();
    f32 right = headerMax.x - style.FramePadding.x * 0.5f;
    const f32 y = headerMin.y + std::floor((headerMax.y - headerMin.y - button) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(right - button, y));
    const char* popupId = kit.text.concat(id, ".popup");
    if (iconButton(kit, icons::kMoreVert, kit.text.concat(id, ".menu"), kit.strings.moreOptions))
    {
        ImGui::OpenPopup(popupId);
    }
    right -= button + style.ItemInnerSpacing.x;
    if (enabled != nullptr)
    {
        const f32 switchWidth = std::round(std::round(button * 0.68f) * 1.8f);
        ImGui::SetCursorScreenPos(ImVec2(right - switchWidth, y));
        toggleSwitch(kit, kit.text.hidden(kit.text.concat(id, ".enabled")), *enabled);
        if (kit.strings.enabled[0] != '\0')
        {
            ImGui::SetItemTooltip("%s", kit.strings.enabled);
        }
    }
    ImGui::SetCursorScreenPos(after);
    if (open)
    {
        ImGui::Indent(style.FramePadding.x * 0.5f);
        ImGui::Spacing();
    }
    return open;
}

void endComponentCard()
{
    ImGui::Unindent(ImGui::GetStyle().FramePadding.x * 0.5f);
    ImGui::Spacing();
}

bool beginComponentCardMenu(UiKit& kit, std::string_view id)
{
    return ImGui::BeginPopup(kit.text.concat(id, ".popup"));
}

void endComponentCardMenu()
{
    ImGui::EndPopup();
}

bool beginPropertyTable(UiKit& kit, std::string_view id)
{
    if (!ImGui::BeginTable(kit.text.copy(id), 2,
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                               ImGuiTableFlags_NoSavedSettings))
    {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.38f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.62f);
    return true;
}

void propertyLabel(UiKit& kit, const char* label, const char* tooltip)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::TextSecondary));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (tooltip != nullptr)
    {
        ImGui::SetItemTooltip("%s", tooltip);
    }
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void endPropertyTable()
{
    ImGui::EndTable();
}

std::optional<usize> breadcrumbs(UiKit& kit, std::string_view id, std::span<const char* const> parts)
{
    std::optional<usize> clicked;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(style.FramePadding.x * 0.5f, style.FramePadding.y));
    for (usize i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::TextDisabled));
            ImGui::TextUnformatted(icons::kChevronRight.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
        }
        const bool last = i + 1 == parts.size();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              tokenVec(kit, last ? ThemeToken::TextPrimary : ThemeToken::TextSecondary));
        if (ImGui::Button(kit.text.verbatim(parts[i], indexedId(kit, id, i))))
        {
            clicked = i;
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return clicked;
}

bool assetTile(UiKit& kit, std::string_view id, const Icon& icon, const char* name, const char* badgeText,
               bool selected, f32 size)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
    {
        return false;
    }
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const char* label = kit.text.verbatim(name, id);
    const ImGuiID itemId = window->GetID(label);
    const f32 side = std::round(size * kit.scale);
    const f32 textHeight = ImGui::GetTextLineHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + side, pos.y + side + textHeight + style.ItemInnerSpacing.y * 2.0f));
    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, itemId))
    {
        return false;
    }
    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, itemId, &hovered, &held);

    ImDrawList* drawList = window->DrawList;
    const ThemeToken background =
        selected ? ThemeToken::AccentSubtle : (hovered ? ThemeToken::BgHover : ThemeToken::BgRaised);
    drawList->AddRectFilled(bb.Min, bb.Max, tokenColor(kit, background), style.ChildRounding);
    if (selected)
    {
        drawList->AddRect(bb.Min, bb.Max, tokenColor(kit, ThemeToken::Accent), style.ChildRounding,
                          std::max(1.0f, kit.scale));
    }
    drawIcon(drawList, kit, icon, bb.Min, ImVec2(bb.Max.x, bb.Min.y + side), std::round(side * 0.45f),
             tokenColor(kit, ThemeToken::TextSecondary));
    if (badgeText != nullptr && badgeText[0] != '\0')
    {
        const ImVec2 textSize = ImGui::CalcTextSize(badgeText);
        const ImVec2 badgeMin(bb.Min.x + style.ItemInnerSpacing.x, bb.Min.y + style.ItemInnerSpacing.y);
        const ImVec2 badgeMax(badgeMin.x + textSize.x + style.ItemInnerSpacing.x * 2.0f,
                              badgeMin.y + textSize.y);
        drawList->AddRectFilled(badgeMin, badgeMax, tokenColor(kit, ThemeToken::Info, 0.2f),
                                textSize.y * 0.5f);
        drawList->AddText(ImVec2(badgeMin.x + style.ItemInnerSpacing.x, badgeMin.y),
                          tokenColor(kit, ThemeToken::Info), badgeText);
    }
    const ImVec2 textMin(bb.Min.x + style.ItemInnerSpacing.x, bb.Min.y + side + style.ItemInnerSpacing.y);
    const ImVec2 textMax(bb.Max.x - style.ItemInnerSpacing.x, bb.Max.y);
    ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::TextPrimary));
    ImGui::RenderTextEllipsis(drawList, textMin, textMax, textMax.x, name, nullptr, nullptr);
    ImGui::PopStyleColor();
    ImGui::RenderNavCursor(bb, itemId);
    IMGUI_TEST_ENGINE_ITEM_INFO(itemId, label, g.LastItemData.StatusFlags);
    return pressed;
}

void badge(UiKit& kit, const char* text, ThemeToken token)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const f32 padX = style.ItemInnerSpacing.x;
    const f32 height = ImGui::GetFrameHeight();
    const f32 pillHeight = textSize.y + 2.0f * std::round(style.FramePadding.y * 0.4f);
    const ImVec2 min(pos.x, pos.y + std::floor((height - pillHeight) * 0.5f));
    const ImVec2 max(pos.x + textSize.x + padX * 2.0f, min.y + pillHeight);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, tokenColor(kit, token, 0.18f), pillHeight * 0.5f);
    drawList->AddText(ImVec2(min.x + padX, min.y + std::floor((pillHeight - textSize.y) * 0.5f)),
                      tokenColor(kit, token), text);
    ImGui::Dummy(ImVec2(max.x - min.x, height));
}

void sectionHeader(UiKit& kit, const char* label)
{
    ImGui::Spacing();
    pushFont(kit, FontRole::Strong);
    ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::TextSecondary));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    popFont(kit);
    ImGui::Separator();
}

void emptyState(UiKit& kit, const Icon& icon, const char* title, const char* hint)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const f32 iconSize = std::round(48.0f * kit.scale);
    pushFont(kit, FontRole::Strong);
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    popFont(kit);
    const ImVec2 hintSize = ImGui::CalcTextSize(hint);
    const f32 height = iconSize + style.ItemSpacing.y * 2.0f + titleSize.y + hintSize.y;
    const f32 top = origin.y + std::max(0.0f, std::floor((avail.y - height) * 0.5f));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawIcon(drawList, kit, icon, ImVec2(origin.x, top), ImVec2(origin.x + avail.x, top + iconSize), iconSize,
             tokenColor(kit, ThemeToken::TextDisabled));

    const auto centredLine = [&](const char* text, ImVec2 textSize, f32 y, ThemeToken token) {
        ImGui::SetCursorScreenPos(
            ImVec2(origin.x + std::max(0.0f, std::floor((avail.x - textSize.x) * 0.5f)), y));
        ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, token));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    };
    const f32 titleY = top + iconSize + style.ItemSpacing.y;
    pushFont(kit, FontRole::Strong);
    centredLine(title, titleSize, titleY, ThemeToken::TextPrimary);
    popFont(kit);
    centredLine(hint, hintSize, titleY + titleSize.y + style.ItemSpacing.y, ThemeToken::TextSecondary);
}

void richTooltip(UiKit& kit, const char* title, const char* description, const char* detail)
{
    if (!ImGui::BeginItemTooltip())
    {
        return;
    }
    if (title != nullptr)
    {
        pushFont(kit, FontRole::Strong);
        ImGui::TextUnformatted(title);
        popFont(kit);
    }
    if (description != nullptr)
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
    }
    if (detail != nullptr)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::TextSecondary));
        ImGui::TextUnformatted(detail);
        ImGui::PopStyleColor();
    }
    ImGui::EndTooltip();
}

void ToastQueue::push(const Icon& icon, ThemeToken token, std::string_view message, f64 now, f64 duration,
                      std::string_view action, u32 actionId)
{
    if (m_count == kCapacity)
    {
        std::move(m_toasts.begin() + 1, m_toasts.end(), m_toasts.begin());
        --m_count;
    }
    Toast& toast = m_toasts[m_count++];
    toast.icon = icon;
    toast.token = token;
    toast.start = now;
    toast.end = now + duration;
    toast.actionId = actionId;
    copyText(toast.message, message);
    copyText(toast.action, action);
}

std::optional<u32> ToastQueue::draw(UiKit& kit, f64 now)
{
    static constexpr f64 kFade = 0.15;
    static constexpr std::array<std::string_view, kCapacity> kWindows = {"###toast.0", "###toast.1",
                                                                         "###toast.2", "###toast.3"};
    std::optional<u32> clicked;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiStyle& style = ImGui::GetStyle();
    f32 bottom = viewport->WorkPos.y + viewport->WorkSize.y - style.WindowPadding.y;
    const f32 right = viewport->WorkPos.x + viewport->WorkSize.x - style.WindowPadding.x;
    std::array<bool, kCapacity> remove{};

    // Newest at the bottom.
    for (usize n = 0; n < m_count; ++n)
    {
        const usize i = m_count - 1 - n;
        Toast& toast = m_toasts[i];
        if (now >= toast.end)
        {
            remove[i] = true;
            continue;
        }
        const f32 appear = easeOut(now, toast.start, kFade, kit.reduceMotion);
        const f32 vanish =
            kit.reduceMotion ? 1.0f : static_cast<f32>(std::clamp((toast.end - now) / kFade, 0.0, 1.0));
        const f32 alpha = std::min(appear, vanish);
        const f32 slide = (1.0f - appear) * 12.0f * kit.scale;

        ImGui::SetNextWindowPos(ImVec2(right, bottom + slide), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, tokenVec(kit, ThemeToken::BgRaised));
        constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin(kWindows[n].data(), nullptr, kFlags))
        {
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, toast.token));
            ImGui::TextUnformatted(toast.icon.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(toast.message.data());
            if (toast.action[0] != '\0')
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, tokenVec(kit, ThemeToken::Accent));
                if (ImGui::Button(kit.text.verbatim(toast.action.data(),
                                                    kit.text.concat(indexedId(kit, "toast", n), ".action"))))
                {
                    clicked = toast.actionId;
                    remove[i] = true;
                }
                ImGui::PopStyleColor();
            }
            ImGui::SameLine();
            if (iconButton(kit, icons::kClose, kit.text.concat(indexedId(kit, "toast", n), ".close")))
            {
                remove[i] = true;
            }
            bottom -= ImGui::GetWindowHeight() + style.ItemSpacing.y;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    usize kept = 0;
    for (usize i = 0; i < m_count; ++i)
    {
        if (!remove[i])
        {
            m_toasts[kept++] = m_toasts[i];
        }
    }
    m_count = kept;
    return clicked;
}

f32 easeOut(f64 now, f64 start, f64 duration, bool reduceMotion) noexcept
{
    if (reduceMotion || duration <= 0.0)
    {
        return 1.0f;
    }
    const f32 t = static_cast<f32>(std::clamp((now - start) / duration, 0.0, 1.0));
    const f32 inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

} // namespace illusion::editor::ui

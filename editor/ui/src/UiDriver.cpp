#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiDriver.hpp>

#include <array>
#include <imgui.h>
#include <string>
#include <utility>

namespace illusion::editor::ui
{

namespace
{

constexpr std::array<ImGuiKey, 4> kModifierKeys = {ImGuiMod_Ctrl, ImGuiMod_Shift, ImGuiMod_Alt,
                                                   ImGuiMod_Super};

} // namespace

UiDriver::UiDriver(UiContext& context, FrameFunction frame)
    : m_context(context)
    , m_frame(std::move(frame))
    , m_registry(context.imgui())
{
}

void UiDriver::runFrames(u32 count)
{
    for (u32 i = 0; i < count; ++i)
    {
        m_context.makeCurrent();
        m_registry.beginFrame();
        m_frame();
    }
}

std::optional<UiItem> UiDriver::find(std::string_view stableId) const
{
    return m_registry.find(stableId);
}

void UiDriver::moveMouse(Vec2 position)
{
    m_context.makeCurrent();
    ImGui::GetIO().AddMousePosEvent(position.x, position.y);
    runFrames(1);
}

bool UiDriver::hover(std::string_view stableId)
{
    const std::optional<UiItem> item = find(stableId);
    if (!item)
    {
        return false;
    }
    moveMouse(item->center());
    // Hover is resolved against the previous frame's items; one more frame makes it visible to the UI.
    runFrames(1);
    return true;
}

bool UiDriver::click(std::string_view stableId, int button)
{
    if (!hover(stableId))
    {
        return false;
    }
    m_context.makeCurrent();
    ImGui::GetIO().AddMouseButtonEvent(button, true);
    runFrames(1);
    ImGui::GetIO().AddMouseButtonEvent(button, false);
    runFrames(1);
    return true;
}

bool UiDriver::drag(std::string_view stableId, Vec2 delta, u32 steps)
{
    const std::optional<UiItem> item = find(stableId);
    if (!item || steps == 0)
    {
        return false;
    }
    const Vec2 start = item->center();
    moveMouse(start);
    runFrames(1);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    runFrames(1);
    for (u32 step = 1; step <= steps; ++step)
    {
        moveMouse(start + delta * (static_cast<f32>(step) / static_cast<f32>(steps)));
    }
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    runFrames(1);
    return true;
}

void UiDriver::typeText(std::string_view utf8)
{
    m_context.makeCurrent();
    // AddInputCharactersUTF8 needs a zero-terminated string.
    const std::string text(utf8);
    ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
    runFrames(1);
}

int UiDriver::shortcutModifier() noexcept
{
#if defined(ILL_PLATFORM_MACOS)
    return ImGuiMod_Super;
#else
    return ImGuiMod_Ctrl;
#endif
}

void UiDriver::pressKey(int key, int modifiers)
{
    m_context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    for (const ImGuiKey modifier : kModifierKeys)
    {
        if ((modifiers & modifier) != 0)
        {
            io.AddKeyEvent(modifier, true);
        }
    }
    io.AddKeyEvent(static_cast<ImGuiKey>(key), true);
    runFrames(1);
    io.AddKeyEvent(static_cast<ImGuiKey>(key), false);
    for (const ImGuiKey modifier : kModifierKeys)
    {
        if ((modifiers & modifier) != 0)
        {
            io.AddKeyEvent(modifier, false);
        }
    }
    runFrames(1);
}

} // namespace illusion::editor::ui

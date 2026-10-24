#include <illusion/core/Assert.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Window.hpp>

#include <array>
#include <cfloat>
#include <imgui.h>

namespace illusion::editor::ui
{

namespace
{

// ImGuiKey of every illusion::Key, in enum order.
constexpr std::array<ImGuiKey, static_cast<usize>(Key::Count)> kKeys = {
    ImGuiKey_None,
    ImGuiKey_Space,
    ImGuiKey_Apostrophe,
    ImGuiKey_Comma,
    ImGuiKey_Minus,
    ImGuiKey_Period,
    ImGuiKey_Slash,
    ImGuiKey_0,
    ImGuiKey_1,
    ImGuiKey_2,
    ImGuiKey_3,
    ImGuiKey_4,
    ImGuiKey_5,
    ImGuiKey_6,
    ImGuiKey_7,
    ImGuiKey_8,
    ImGuiKey_9,
    ImGuiKey_Semicolon,
    ImGuiKey_Equal,
    ImGuiKey_A,
    ImGuiKey_B,
    ImGuiKey_C,
    ImGuiKey_D,
    ImGuiKey_E,
    ImGuiKey_F,
    ImGuiKey_G,
    ImGuiKey_H,
    ImGuiKey_I,
    ImGuiKey_J,
    ImGuiKey_K,
    ImGuiKey_L,
    ImGuiKey_M,
    ImGuiKey_N,
    ImGuiKey_O,
    ImGuiKey_P,
    ImGuiKey_Q,
    ImGuiKey_R,
    ImGuiKey_S,
    ImGuiKey_T,
    ImGuiKey_U,
    ImGuiKey_V,
    ImGuiKey_W,
    ImGuiKey_X,
    ImGuiKey_Y,
    ImGuiKey_Z,
    ImGuiKey_LeftBracket,
    ImGuiKey_Backslash,
    ImGuiKey_RightBracket,
    ImGuiKey_GraveAccent,
    ImGuiKey_Escape,
    ImGuiKey_Enter,
    ImGuiKey_Tab,
    ImGuiKey_Backspace,
    ImGuiKey_Insert,
    ImGuiKey_Delete,
    ImGuiKey_RightArrow,
    ImGuiKey_LeftArrow,
    ImGuiKey_DownArrow,
    ImGuiKey_UpArrow,
    ImGuiKey_PageUp,
    ImGuiKey_PageDown,
    ImGuiKey_Home,
    ImGuiKey_End,
    ImGuiKey_CapsLock,
    ImGuiKey_ScrollLock,
    ImGuiKey_NumLock,
    ImGuiKey_PrintScreen,
    ImGuiKey_Pause,
    ImGuiKey_F1,
    ImGuiKey_F2,
    ImGuiKey_F3,
    ImGuiKey_F4,
    ImGuiKey_F5,
    ImGuiKey_F6,
    ImGuiKey_F7,
    ImGuiKey_F8,
    ImGuiKey_F9,
    ImGuiKey_F10,
    ImGuiKey_F11,
    ImGuiKey_F12,
    ImGuiKey_F13,
    ImGuiKey_F14,
    ImGuiKey_F15,
    ImGuiKey_F16,
    ImGuiKey_F17,
    ImGuiKey_F18,
    ImGuiKey_F19,
    ImGuiKey_F20,
    ImGuiKey_F21,
    ImGuiKey_F22,
    ImGuiKey_F23,
    ImGuiKey_F24,
    ImGuiKey_Keypad0,
    ImGuiKey_Keypad1,
    ImGuiKey_Keypad2,
    ImGuiKey_Keypad3,
    ImGuiKey_Keypad4,
    ImGuiKey_Keypad5,
    ImGuiKey_Keypad6,
    ImGuiKey_Keypad7,
    ImGuiKey_Keypad8,
    ImGuiKey_Keypad9,
    ImGuiKey_KeypadDecimal,
    ImGuiKey_KeypadDivide,
    ImGuiKey_KeypadMultiply,
    ImGuiKey_KeypadSubtract,
    ImGuiKey_KeypadAdd,
    ImGuiKey_KeypadEnter,
    ImGuiKey_KeypadEqual,
    ImGuiKey_LeftShift,
    ImGuiKey_LeftCtrl,
    ImGuiKey_LeftAlt,
    ImGuiKey_LeftSuper,
    ImGuiKey_RightShift,
    ImGuiKey_RightCtrl,
    ImGuiKey_RightAlt,
    ImGuiKey_RightSuper,
    ImGuiKey_Menu,
};

ImGuiKey toImGuiKey(Key key) noexcept
{
    const auto index = static_cast<usize>(key);
    return index < kKeys.size() ? kKeys[index] : ImGuiKey_None;
}

void addModifiers(ImGuiIO& io, const KeyModifiers& modifiers)
{
    io.AddKeyEvent(ImGuiMod_Ctrl, modifiers.control);
    io.AddKeyEvent(ImGuiMod_Shift, modifiers.shift);
    io.AddKeyEvent(ImGuiMod_Alt, modifiers.alt);
    io.AddKeyEvent(ImGuiMod_Super, modifiers.super);
}

CursorShape toCursorShape(ImGuiMouseCursor cursor) noexcept
{
    switch (cursor)
    {
    case ImGuiMouseCursor_TextInput:
        return CursorShape::TextInput;
    case ImGuiMouseCursor_ResizeAll:
        return CursorShape::ResizeAll;
    case ImGuiMouseCursor_ResizeNS:
        return CursorShape::ResizeNS;
    case ImGuiMouseCursor_ResizeEW:
        return CursorShape::ResizeEW;
    case ImGuiMouseCursor_ResizeNESW:
        return CursorShape::ResizeNESW;
    case ImGuiMouseCursor_ResizeNWSE:
        return CursorShape::ResizeNWSE;
    case ImGuiMouseCursor_Hand:
        return CursorShape::Hand;
    case ImGuiMouseCursor_NotAllowed:
        return CursorShape::NotAllowed;
    default:
        return CursorShape::Arrow;
    }
}

UiPlatformBackend& backendOf(ImGuiContext*)
{
    return *static_cast<UiPlatformBackend*>(ImGui::GetIO().BackendPlatformUserData);
}

const char* getClipboard(ImGuiContext* context)
{
    return backendOf(context).clipboardText();
}

void setClipboard(ImGuiContext* context, const char* text)
{
    backendOf(context).setClipboardText(text);
}

void setImeData(ImGuiContext* context, ImGuiViewport*, ImGuiPlatformImeData* data)
{
    backendOf(context).setImeRect(Vec2(data->InputPos.x, data->InputPos.y), data->InputLineHeight,
                                  data->WantVisible);
}

} // namespace

int imguiKey(Key key) noexcept
{
    return static_cast<int>(toImGuiKey(key));
}

UiPlatformBackend::UiPlatformBackend(UiContext& context, Window& window, EventBus& events)
    : m_context(context)
    , m_window(window)
    , m_events(events)
{
    m_context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    ILL_ASSERT(io.BackendPlatformUserData == nullptr, "the context already has a platform backend");
    io.BackendPlatformUserData = this;
    io.BackendPlatformName = "illusion";
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Platform_GetClipboardTextFn = &getClipboard;
    platform.Platform_SetClipboardTextFn = &setClipboard;
    platform.Platform_SetImeDataFn = &setImeData;
    subscribe();
}

UiPlatformBackend::~UiPlatformBackend()
{
    m_context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendPlatformUserData = nullptr;
    io.BackendPlatformName = nullptr;
    io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    platform.Platform_GetClipboardTextFn = nullptr;
    platform.Platform_SetClipboardTextFn = nullptr;
    platform.Platform_SetImeDataFn = nullptr;
}

void UiPlatformBackend::subscribe()
{
    const WindowId self = m_window.id();
    m_keys = m_events.subscribe<KeyEvent>([this, self](const KeyEvent& event) {
        if (event.window != self)
        {
            return;
        }
        m_context.makeCurrent();
        ImGuiIO& io = ImGui::GetIO();
        addModifiers(io, event.modifiers);
        if (const ImGuiKey key = toImGuiKey(event.key); key != ImGuiKey_None)
        {
            io.AddKeyEvent(key, event.down);
        }
    });
    m_text = m_events.subscribe<TextInputEvent>([this, self](const TextInputEvent& event) {
        if (event.window == self)
        {
            m_context.makeCurrent();
            ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(event.codepoint));
        }
    });
    m_buttons = m_events.subscribe<MouseButtonEvent>([this, self](const MouseButtonEvent& event) {
        if (event.window != self)
        {
            return;
        }
        m_context.makeCurrent();
        ImGuiIO& io = ImGui::GetIO();
        addModifiers(io, event.modifiers);
        io.AddMousePosEvent(event.position.x, event.position.y);
        io.AddMouseButtonEvent(static_cast<int>(event.button), event.down);
    });
    m_moves = m_events.subscribe<MouseMovedEvent>([this, self](const MouseMovedEvent& event) {
        if (event.window == self)
        {
            m_context.makeCurrent();
            ImGui::GetIO().AddMousePosEvent(event.position.x, event.position.y);
        }
    });
    m_scroll = m_events.subscribe<MouseScrolledEvent>([this, self](const MouseScrolledEvent& event) {
        if (event.window == self)
        {
            m_context.makeCurrent();
            ImGui::GetIO().AddMouseWheelEvent(event.delta.x, event.delta.y);
        }
    });
    m_focus = m_events.subscribe<WindowFocusEvent>([this, self](const WindowFocusEvent& event) {
        if (event.window == self)
        {
            m_context.makeCurrent();
            ImGui::GetIO().AddFocusEvent(event.focused);
        }
    });
    m_enter = m_events.subscribe<CursorEnteredEvent>([this, self](const CursorEnteredEvent& event) {
        if (event.window == self && !event.entered && m_window.cursorMode() != CursorMode::Captured)
        {
            m_context.makeCurrent();
            ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX); // the mouse is gone
        }
    });
    m_scale =
        m_events.subscribe<ContentScaleChangedEvent>([this, self](const ContentScaleChangedEvent& event) {
            if (event.window == self)
            {
                m_scaleChanged = true;
            }
        });
}

void UiPlatformBackend::newFrame(f64 deltaSeconds)
{
    m_context.makeCurrent();
    ImGuiIO& io = ImGui::GetIO();
    const UVec2 size = m_window.size();
    const UVec2 framebuffer = m_window.framebufferSize();
    io.DisplaySize = ImVec2(static_cast<f32>(size.x), static_cast<f32>(size.y));
    if (size.x > 0 && size.y > 0)
    {
        io.DisplayFramebufferScale = ImVec2(static_cast<f32>(framebuffer.x) / static_cast<f32>(size.x),
                                            static_cast<f32>(framebuffer.y) / static_cast<f32>(size.y));
    }
    io.DeltaTime = deltaSeconds > 0.0 ? static_cast<f32>(deltaSeconds) : 1.0f / 60.0f;

    const bool infiniteDrag = m_infiniteDragRequested;
    m_infiniteDragRequested = false;
    if (infiniteDrag)
    {
        if (m_window.cursorMode() != CursorMode::Captured)
        {
            m_window.setCursorMode(CursorMode::Captured);
        }
        m_infiniteDragging = true;
    }
    else if (m_infiniteDragging)
    {
        m_infiniteDragging = false;
        if (m_window.cursorMode() == CursorMode::Captured)
        {
            m_window.setCursorMode(CursorMode::Normal);
        }
    }

    // The shape the previous frame asked for (Dear ImGui decides it while building the frame).
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0 &&
        m_window.cursorMode() != CursorMode::Captured)
    {
        const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
        const CursorMode mode = cursor == ImGuiMouseCursor_None ? CursorMode::Hidden : CursorMode::Normal;
        if (m_window.cursorMode() != mode)
        {
            m_window.setCursorMode(mode);
        }
        if (cursor != ImGuiMouseCursor_None && m_window.cursorShape() != toCursorShape(cursor))
        {
            m_window.setCursorShape(toCursorShape(cursor));
        }
    }
}

bool UiPlatformBackend::consumeContentScaleChange() noexcept
{
    const bool changed = m_scaleChanged;
    m_scaleChanged = false;
    return changed;
}

Vec2 UiPlatformBackend::contentScale() const noexcept
{
    return m_window.contentScale();
}

void UiPlatformBackend::setClipboardText(const char* text)
{
    m_window.setClipboardText(text != nullptr ? text : "");
}

const char* UiPlatformBackend::clipboardText()
{
    m_clipboard = m_window.clipboardText();
    return m_clipboard.c_str();
}

void UiPlatformBackend::setImeRect(Vec2 position, f32 lineHeight, bool wantVisible) noexcept
{
    if (wantVisible)
    {
        m_window.setImeRect(position, Vec2(1.0f, lineHeight));
    }
}

} // namespace illusion::editor::ui

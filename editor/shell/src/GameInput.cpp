// The Game view's input capture: the window's keyboard and mouse go to the running game.

#include "Panels.hpp"

#include <illusion/editor/ui/UiPlatformBackend.hpp>

namespace illusion::editor::shell::detail
{
namespace
{

// True while a play session runs the whole game (not paused, not Simulate).
bool gameRuns(Impl& shell)
{
    const PlayHost* play = shell.editor.play();
    if (play == nullptr || !shell.editor.playing())
    {
        return false;
    }
    const PlayStatus status = play->status();
    return status.state == PlayState::Playing && status.mode == PlayMode::Play;
}

} // namespace

void GameInput::capture() noexcept
{
    m_captured = true;
}

void GameInput::release(Impl& shell)
{
    if (!m_captured)
    {
        return;
    }
    m_captured = false;
    PlayHost* play = shell.editor.play();
    for (usize i = 0; i < m_keys.size(); ++i)
    {
        if (m_keys[i] && play != nullptr)
        {
            (void)play->inject(
                {.kind = PlayInputEvent::Kind::Key, .key = static_cast<Key>(i), .down = false});
        }
        m_keys[i] = false;
    }
    for (usize i = 0; i < m_buttons.size(); ++i)
    {
        if (m_buttons[i] && play != nullptr)
        {
            (void)play->inject({.kind = PlayInputEvent::Kind::MouseButton,
                                .button = static_cast<MouseButton>(i),
                                .down = false});
        }
        m_buttons[i] = false;
    }
    m_cursor = Vec2(-1.0f);
}

void GameInput::setImage(Vec2 origin, Vec2 toImage) noexcept
{
    m_origin = origin;
    m_toImage = toImage;
}

void GameInput::update(Impl& shell)
{
    if (!m_captured)
    {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (!gameRuns(shell) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F1, false)))
    {
        release(shell);
        return;
    }
    PlayHost* play = shell.editor.play();
    if (play == nullptr)
    {
        return;
    }
    ui::requestCursorCapture();
    // Only changes are sent: keys an agent injects in between stay as it left them.
    for (usize i = 1; i < m_keys.size(); ++i)
    {
        const Key key = static_cast<Key>(i);
        const int imgui = ui::imguiKey(key);
        const bool down = imgui != ImGuiKey_None && ImGui::IsKeyDown(static_cast<ImGuiKey>(imgui));
        if (down != m_keys[i])
        {
            m_keys[i] = down;
            (void)play->inject({.kind = PlayInputEvent::Kind::Key, .key = key, .down = down});
        }
    }
    for (usize i = 0; i < m_buttons.size(); ++i)
    {
        const bool down = i < static_cast<usize>(ImGuiMouseButton_COUNT) && io.MouseDown[i];
        if (down != m_buttons[i])
        {
            m_buttons[i] = down;
            (void)play->inject({.kind = PlayInputEvent::Kind::MouseButton,
                                .button = static_cast<MouseButton>(i),
                                .down = down});
        }
    }
    if (ImGui::IsMousePosValid())
    {
        const Vec2 cursor = (Vec2(io.MousePos.x, io.MousePos.y) - m_origin) * m_toImage;
        if (cursor != m_cursor)
        {
            m_cursor = cursor;
            (void)play->inject({.kind = PlayInputEvent::Kind::MousePosition, .position = cursor});
        }
    }
}

} // namespace illusion::editor::shell::detail

#pragma once

// Translation between GLFW key codes and illusion::Key, shared by the window callbacks and
// Platform::keyDisplayName().

#include <illusion/platform/Input.hpp>

namespace illusion::detail
{

// The Key of a GLFW key code (Key::Unknown for keys illusion does not name).
Key keyFromGlfw(int key) noexcept;

// The GLFW key code of @p key (GLFW_KEY_UNKNOWN for Key::Unknown).
int glfwFromKey(Key key) noexcept;

// The character @p key types on a US keyboard without modifiers, or nullptr if it types none.
const char* usCharacter(Key key) noexcept;

} // namespace illusion::detail

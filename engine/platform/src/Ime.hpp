#pragma once

// Placement of the input method's candidate window (Window::setImeRect()). Only Windows needs to be told;
// elsewhere this does nothing.

#include <illusion/core/Math.hpp>

namespace illusion::detail
{

// Places the composition and candidate windows of the input method of @p glfwWindow next to the text area
// at @p position with @p size (window coordinates, which are pixels on Windows).
void setImeRect(void* glfwWindow, Vec2 position, Vec2 size) noexcept;

} // namespace illusion::detail

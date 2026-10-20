#include "Ime.hpp"

#include <GLFW/glfw3.h>

#if defined(ILL_PLATFORM_WINDOWS)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
// imm.h needs the Windows types that glfw3native.h brings in with windows.h.
#include <imm.h>
#endif

namespace illusion::detail
{

#if defined(ILL_PLATFORM_WINDOWS)

void setImeRect(void* glfwWindow, Vec2 position, Vec2 size) noexcept
{
    // Headless windows have no native window.
    if (glfwGetPlatform() != GLFW_PLATFORM_WIN32)
    {
        return;
    }
    HWND window = glfwGetWin32Window(static_cast<GLFWwindow*>(glfwWindow));
    HIMC context = ImmGetContext(window);
    if (context == nullptr)
    {
        return;
    }
    const auto left = static_cast<LONG>(position.x);
    const auto top = static_cast<LONG>(position.y);
    const auto right = static_cast<LONG>(position.x + size.x);
    const auto bottom = static_cast<LONG>(position.y + size.y);

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_FORCE_POSITION;
    composition.ptCurrentPos = POINT{left, top};
    ImmSetCompositionWindow(context, &composition);

    // The candidate list opens below the text area and never covers it.
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = POINT{left, bottom};
    candidate.rcArea = RECT{left, top, right, bottom};
    ImmSetCandidateWindow(context, &candidate);

    ImmReleaseContext(window, context);
}

#else

void setImeRect(void*, Vec2, Vec2) noexcept
{
}

#endif

} // namespace illusion::detail

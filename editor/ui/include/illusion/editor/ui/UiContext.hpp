#pragma once

/// @file
/// The Dear ImGui and ImPlot contexts of one editor window, with docking enabled and settings kept in
/// memory instead of an imgui.ini file.

#include <illusion/core/Base.hpp>

#include <string>
#include <string_view>

struct ImGuiContext;
struct ImPlotContext;

namespace illusion::editor::ui
{

/// Owns an ImGui context and an ImPlot context and makes them current while it is alive. Dear ImGui keeps
/// one current context per thread, so create and use a UiContext on the UI thread only.
class UiContext : NonCopyable
{
public:
    /// Creates the contexts, enables docking and keyboard navigation, and installs the counting allocator
    /// (see allocationCount()).
    UiContext();

    /// Destroys the contexts. The renderer must have released its textures (UiRenderer does so when it is
    /// destroyed first).
    ~UiContext();

    /// Makes this context current on the calling thread.
    void makeCurrent() const noexcept;

    /// The Dear ImGui context.
    [[nodiscard]] ImGuiContext* imgui() const noexcept { return m_imgui; }

    /// The ImPlot context.
    [[nodiscard]] ImPlotContext* implot() const noexcept { return m_implot; }

    /// The window layout and other settings in Dear ImGui's ini format, for the editor to save.
    [[nodiscard]] std::string saveSettings() const;

    /// Restores settings returned by saveSettings(). Call before the first frame.
    void loadSettings(std::string_view settings) const;

    /// Number of heap allocations Dear ImGui and ImPlot made on this thread since the program started.
    /// Tests compare it before and after frames to check that idle frames do not allocate.
    [[nodiscard]] static u64 allocationCount() noexcept;

private:
    ImGuiContext* m_imgui = nullptr;
    ImPlotContext* m_implot = nullptr;
};

} // namespace illusion::editor::ui

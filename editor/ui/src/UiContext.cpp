#include <illusion/editor/ui/UiContext.hpp>

#include <cstdlib>
#include <imgui.h>
#include <implot.h>

namespace illusion::editor::ui
{

namespace
{

thread_local u64 t_allocations = 0;

void* allocate(size_t size, void*)
{
    ++t_allocations;
    return std::malloc(size);
}

void release(void* pointer, void*)
{
    std::free(pointer);
}

} // namespace

UiContext::UiContext()
{
    // The allocator functions are global to Dear ImGui; installing the same pair again is harmless.
    ImGui::SetAllocatorFunctions(&allocate, &release, nullptr);
    m_imgui = ImGui::CreateContext();
    m_implot = ImPlot::CreateContext();
    ImPlot::SetImGuiContext(m_imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // the editor saves the layout itself (saveSettings())
    io.LogFilename = nullptr;
}

UiContext::~UiContext()
{
    makeCurrent();
    ImPlot::DestroyContext(m_implot);
    ImGui::DestroyContext(m_imgui);
}

void UiContext::makeCurrent() const noexcept
{
    ImGui::SetCurrentContext(m_imgui);
    ImPlot::SetCurrentContext(m_implot);
}

std::string UiContext::saveSettings() const
{
    makeCurrent();
    size_t size = 0;
    const char* settings = ImGui::SaveIniSettingsToMemory(&size);
    return std::string(settings, size);
}

void UiContext::loadSettings(std::string_view settings) const
{
    makeCurrent();
    ImGui::LoadIniSettingsFromMemory(settings.data(), settings.size());
}

u64 UiContext::allocationCount() noexcept
{
    return t_allocations;
}

} // namespace illusion::editor::ui

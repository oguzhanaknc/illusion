// The Rendering panel: the render quality of the editor's viewports (a preference, `preferences.set`), the
// GPU the editor renders with and what each viewport drew in its last frame.

#include "Panels.hpp"

#include <illusion/editor/ui/Icons.hpp>

#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

// Sends {"viewportQuality": {field: value}} to `preferences.set`, merging the steps of one drag.
void setQuality(Impl& shell, std::string_view field, std::string_view valueJson,
                std::string_view mergeKey = {})
{
    const std::string params =
        "{\"value\":{\"viewportQuality\":{" + jsonQuote(field) + ":" + std::string(valueJson) + "}}}";
    (void)shell.run("preferences.set", params, "rendering", {}, mergeKey);
}

void checkbox(Impl& shell, text::TextKey label, std::string_view field, bool value)
{
    ui::FrameText& text = shell.text;
    if (ImGui::Checkbox(text.label(label, text.concat("rendering.", field)), &value))
    {
        setQuality(shell, field, value ? "true" : "false");
    }
}

void sliderU32(Impl& shell, text::TextKey label, std::string_view field, u32 value, u32 min, u32 max)
{
    ui::FrameText& text = shell.text;
    int edited = static_cast<int>(value);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt(text.label(label, text.concat("rendering.", field)), &edited, static_cast<int>(min),
                         static_cast<int>(max)))
    {
        char json[16];
        std::snprintf(json, sizeof(json), "%d", edited);
        setQuality(shell, field, json, text.concat("rendering.drag.", field));
    }
}

void sliderF32(Impl& shell, text::TextKey label, std::string_view field, f32 value, f32 min, f32 max,
               const char* format)
{
    ui::FrameText& text = shell.text;
    f32 edited = value;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat(text.label(label, text.concat("rendering.", field)), &edited, min, max, format))
    {
        char json[32];
        std::snprintf(json, sizeof(json), "%.6g", static_cast<f64>(edited));
        setQuality(shell, field, json, text.concat("rendering.drag.", field));
    }
}

// A row of a two-column table: a label and a value.
void row(ui::FrameText& text, text::TextKey label, const char* value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", text.format(label));
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value);
}

} // namespace

void RenderingPanel::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const ViewportQuality& quality = shell.editor.preferences().viewportQuality;

    ui::sectionHeader(kit, text.format(s::kRenderingQuality));
    ImGui::TextDisabled("%s", text.format(s::kRenderingQualityHint));
    checkbox(shell, s::kRenderingShadows, "shadows", quality.shadows);
    ImGui::BeginDisabled(!quality.shadows);
    sliderU32(shell, s::kRenderingShadowCascades, "shadowCascades", quality.shadowCascades, 1, 4);
    sliderU32(shell, s::kRenderingShadowResolution, "shadowResolution", quality.shadowResolution, 512, 4096);
    sliderF32(shell, s::kRenderingShadowDistance, "shadowDistance", quality.shadowDistance, 5.0f, 1000.0f,
              "%.0f m");
    ImGui::EndDisabled();
    checkbox(shell, s::kRenderingAmbientOcclusion, "ambientOcclusion", quality.ambientOcclusion);
    checkbox(shell, s::kRenderingBloom, "bloom", quality.bloom);
    ImGui::BeginDisabled(!quality.bloom);
    sliderF32(shell, s::kRenderingBloomIntensity, "bloomIntensity", quality.bloomIntensity, 0.0f, 1.0f,
              "%.3f");
    ImGui::EndDisabled();
    checkbox(shell, s::kRenderingTemporalAntiAliasing, "temporalAntiAliasing", quality.temporalAntiAliasing);
    checkbox(shell, s::kRenderingOcclusionCulling, "occlusionCulling", quality.occlusionCulling);

    ui::sectionHeader(kit, text.format(s::kRenderingGpu));
    ViewportHost* host = shell.editor.viewports();
    if (host == nullptr)
    {
        ImGui::TextDisabled("%s", text.format(s::kProfilerGpuNoneHint));
        return;
    }
    const ViewportGpuInfo gpu = host->gpuInfo();
    if (ImGui::BeginTable(text.hidden("rendering.gpu"), 2, ImGuiTableFlags_SizingStretchProp))
    {
        setupStretchColumns({1.0f, 2.0f});
        row(text, s::kRenderingGpuDevice, text.copy(gpu.name));
        row(text, s::kRenderingGpuDriver, text.copy(gpu.driver));
        row(text, s::kRenderingGpuVulkan, text.copy(gpu.apiVersion));
        row(text, s::kRenderingGpuValidationErrors,
            text.format(s::kRenderingCount, {{"count", u64{gpu.validationErrors}}}));
        ImGui::EndTable();
    }

    ui::sectionHeader(kit, text.format(s::kRenderingLastFrame));
    if (host->viewportsVersion() != m_viewportsVersion)
    {
        m_viewportsVersion = host->viewportsVersion();
        m_viewports.clear();
        for (const ViewportInfo& info : host->viewports())
        {
            m_viewports.push_back(info.id);
        }
    }
    for (const std::string& id : m_viewports)
    {
        const Expected<ViewportRenderStats, CommandError> stats = host->renderStats(id);
        if (!stats ||
            !ImGui::BeginTable(text.concat("rendering.stats.", id), 2, ImGuiTableFlags_SizingStretchProp))
        {
            continue;
        }
        setupStretchColumns({1.0f, 2.0f});
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(text.copy(id));
        row(text, s::kRenderingDrawCalls,
            text.format(s::kRenderingCount, {{"count", u64{stats->drawCalls}}}));
        row(text, s::kRenderingTriangles,
            text.format(s::kRenderingCount, {{"count", u64{stats->triangles}}}));
        row(text, s::kRenderingVisible,
            text.format(s::kRenderingVisibleOf,
                        {{"visible", u64{stats->visible}}, {"candidates", u64{stats->candidates}}}));
        row(text, s::kRenderingShadowCasters,
            text.format(s::kRenderingCount, {{"count", u64{stats->shadowCasters}}}));
        ImGui::EndTable();
    }
}

} // namespace illusion::editor::shell::detail

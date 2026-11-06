// The Physics and Audio Mixer windows: the physics and audio of the running play session (PlayHost), the
// collision shapes in the Scene view, and a mixer whose changes last for the session (`play.set_mix`).

#include "Panels.hpp"

#include <illusion/editor/ui/Icons.hpp>

#include <array>
#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr std::array<std::string_view, kPlayAudioBuses> kBusIds{"sfx", "music", "ambience", "dialogue", "ui"};

// A row of a two-column table: a label and a count.
void countRow(ui::FrameText& text, text::TextKey label, u64 value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", text.format(label));
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(text.format(s::kRenderingCount, {{"count", value}}));
}

// The session's diagnostics, or nullopt (with an empty state drawn) when no session runs.
std::optional<PlayDiagnostics> running(Impl& shell, const ui::Icon& icon)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const PlayHost* play = shell.editor.play();
    const PlayDiagnostics diagnostics = play != nullptr ? play->diagnostics() : PlayDiagnostics{};
    if (!diagnostics.running)
    {
        ui::emptyState(kit, icon, text.format(s::kGameDebugStopped), text.format(s::kGameDebugStoppedHint));
        return std::nullopt;
    }
    return diagnostics;
}

} // namespace

void drawPhysicsWindow(Impl& shell)
{
    ui::FrameText& text = shell.text;
    // The collision shapes of every entity in the Scene view, also while stopped.
    if (ImGui::Button(text.iconLabel(ui::icons::kVisibility, s::kGameDebugShowShapes, "physics.show_shapes")))
    {
        (void)shell.run(
            "viewport.set_overlays",
            R"({"visualisers":{"colliders":true,"characters":true,"joints":true},"visualiserScope":"all"})",
            "physics");
    }
    ImGui::Separator();
    const std::optional<PlayDiagnostics> diagnostics = running(shell, ui::icons::kDeployedCode);
    if (!diagnostics)
    {
        return;
    }
    if (ImGui::BeginTable(text.hidden("physics.stats"), 2, ImGuiTableFlags_SizingStretchProp))
    {
        setupStretchColumns({2.0f, 1.0f});
        countRow(text, s::kGameDebugBodies, diagnostics->bodies);
        countRow(text, s::kGameDebugCharacters, diagnostics->characters);
        countRow(text, s::kGameDebugJoints, diagnostics->joints);
        countRow(text, s::kGameDebugContacts, diagnostics->contacts);
        countRow(text, s::kGameDebugSteps, diagnostics->physicsSteps);
        ImGui::EndTable();
    }
}

void drawAudioMixerWindow(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    const std::optional<PlayDiagnostics> diagnostics = running(shell, ui::icons::kGraphicEq);
    if (!diagnostics)
    {
        return;
    }
    ImGui::TextDisabled("%s", text.format(s::kGameDebugMixHint));
    char params[96];
    f32 master = diagnostics->masterVolume;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat(text.label(s::kGameDebugMaster, "mixer.master"), &master, 0.0f, 2.0f, "%.2f"))
    {
        std::snprintf(params, sizeof(params), R"({"masterVolume":%.4g})", static_cast<f64>(master));
        (void)shell.run("play.set_mix", params, "mixer");
    }
    const std::array<text::TextKey, kPlayAudioBuses> names = {
        s::kProjectSettingsBusSfx, s::kProjectSettingsBusMusic, s::kProjectSettingsBusAmbience,
        s::kProjectSettingsBusDialogue, s::kProjectSettingsBusUi};
    for (u32 bus = 0; bus < kPlayAudioBuses; ++bus)
    {
        const char* id = text.concat("mixer.bus.", kBusIds[bus]);
        const bool paused = diagnostics->busPaused[bus];
        if (ui::iconButton(kit, paused ? ui::icons::kPlayArrow : ui::icons::kPause, text.concat(id, ".pause"),
                           text.format(paused ? s::kGameDebugResumeBus : s::kGameDebugPauseBus)))
        {
            std::snprintf(params, sizeof(params), R"({"bus":"%s","paused":%s})", kBusIds[bus].data(),
                          paused ? "false" : "true");
            (void)shell.run("play.set_mix", params, "mixer");
        }
        ImGui::SameLine();
        f32 volume = diagnostics->busVolumes[bus];
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat(text.label(names[bus], id), &volume, 0.0f, 2.0f, "%.2f"))
        {
            std::snprintf(params, sizeof(params), R"({"bus":"%s","volume":%.4g})", kBusIds[bus].data(),
                          static_cast<f64>(volume));
            (void)shell.run("play.set_mix", params, "mixer");
        }
    }
    ui::sectionHeader(kit, text.format(s::kGameDebugVoices));
    if (ImGui::BeginTable(text.hidden("mixer.stats"), 2, ImGuiTableFlags_SizingStretchProp))
    {
        setupStretchColumns({2.0f, 1.0f});
        countRow(text, s::kGameDebugActiveVoices, diagnostics->activeVoices);
        countRow(text, s::kGameDebugDroppedSounds, diagnostics->droppedSounds);
        countRow(text, s::kGameDebugStolenVoices, diagnostics->stolenVoices);
        countRow(text, s::kGameDebugClips, diagnostics->clips);
        countRow(text, s::kGameDebugLoadingClips, diagnostics->loadingClips);
        countRow(text, s::kGameDebugClipKilobytes, (diagnostics->clipBytes + 1023) / 1024);
        ImGui::EndTable();
    }
}

} // namespace illusion::editor::shell::detail

// The Gamepad Tester window: the live state of a connected gamepad (buttons, sticks, triggers, touchpad,
// motion sensors) read from the editor's GamepadHost, and its outputs (rumble, light, DualSense adaptive
// triggers) driven through the `gamepad.*` commands.

#include "Panels.hpp"

#include <illusion/editor/ui/Icons.hpp>

#include <algorithm>
#include <array>
#include <cstdio>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr std::array<std::string_view, 4> kModeIds{"off", "resistance", "weapon", "vibration"};
constexpr u32 kTestRumbleMs = 500;

ImU32 color(Impl& shell, ui::ThemeToken token)
{
    return ImGui::ColorConvertFloat4ToU32(imvec(shell.theme.color(token)));
}

// A square with a stick's position as a dot inside a circle (+y up).
void drawStick(Impl& shell, const char* id, f32 x, f32 y, f32 size)
{
    ImGui::InvisibleButton(id, ImVec2(size, size));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 centre(min.x + size * 0.5f, min.y + size * 0.5f);
    const f32 radius = size * 0.5f - 2.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(centre, radius, color(shell, ui::ThemeToken::BgInput));
    drawList->AddCircle(centre, radius, color(shell, ui::ThemeToken::Border));
    drawList->AddLine(ImVec2(centre.x - radius, centre.y), ImVec2(centre.x + radius, centre.y),
                      color(shell, ui::ThemeToken::Separator));
    drawList->AddLine(ImVec2(centre.x, centre.y - radius), ImVec2(centre.x, centre.y + radius),
                      color(shell, ui::ThemeToken::Separator));
    const ImVec2 dot(centre.x + std::clamp(x, -1.0f, 1.0f) * radius,
                     centre.y - std::clamp(y, -1.0f, 1.0f) * radius);
    drawList->AddCircleFilled(dot, std::max(3.0f, size * 0.06f), color(shell, ui::ThemeToken::Accent));
}

// The touchpad as a rectangle with a dot per finger.
void drawTouchpad(Impl& shell, const GamepadSnapshot& pad, f32 width)
{
    const f32 height = width * 0.45f;
    ImGui::InvisibleButton("###gamepad_tester.touchpad.area", ImVec2(width, height));
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, color(shell, ui::ThemeToken::BgInput), 4.0f);
    drawList->AddRect(min, max, color(shell, ui::ThemeToken::Border), 4.0f);
    for (const GamepadFingerState& finger : pad.fingers)
    {
        if (finger.down)
        {
            const ImVec2 at(min.x + std::clamp(finger.position.x, 0.0f, 1.0f) * width,
                            min.y + std::clamp(finger.position.y, 0.0f, 1.0f) * height);
            drawList->AddCircleFilled(at, 4.0f + 6.0f * finger.pressure,
                                      color(shell, ui::ThemeToken::Accent));
        }
    }
}

void vectorRow(ui::FrameText& text, text::TextKey label, const Vec3& value)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", text.format(label));
    ImGui::TableNextColumn();
    ImGui::Text("%+.2f  %+.2f  %+.2f", static_cast<f64>(value.x), static_cast<f64>(value.y),
                static_cast<f64>(value.z));
}

} // namespace

struct GamepadTesterWindow::State
{
    std::vector<GamepadSnapshot> pads;
    u32 selected = 0; // id of the shown gamepad (0: the first)
    f32 low = 0.5f;
    f32 high = 0.5f;
    f32 leftTrigger = 0.0f;
    f32 rightTrigger = 0.0f;
    std::array<f32, 3> led{0.0f, 0.4f, 1.0f};
    int trigger = 1; // 0 left, 1 right
    int mode = 2;    // weapon
    f32 start = 0.3f;
    f32 end = 0.6f;
    f32 strength = 1.0f;
    f32 amplitude = 1.0f;
    int frequency = 30;
};

GamepadTesterWindow::GamepadTesterWindow()
    : m_state(std::make_unique<State>())
{
}

GamepadTesterWindow::~GamepadTesterWindow() = default;

void GamepadTesterWindow::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    State& state = *m_state;
    if (const GamepadHost* host = shell.editor.gamepads())
    {
        host->gamepads(state.pads);
    }
    else
    {
        state.pads.clear();
    }
    if (state.pads.empty())
    {
        ui::emptyState(kit, ui::icons::kSportsEsports, text.format(s::kGamepadTesterNone),
                       text.format(s::kGamepadTesterNoneHint));
        return;
    }
    const auto found = std::find_if(state.pads.begin(), state.pads.end(),
                                    [&](const GamepadSnapshot& pad) { return pad.id == state.selected; });
    const GamepadSnapshot& pad = found != state.pads.end() ? *found : state.pads.front();
    state.selected = pad.id;

    // Which gamepad, its model and player slot.
    if (state.pads.size() > 1)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(text.label(s::kGamepadTesterGamepad, "gamepad_tester.gamepad"),
                              text.copy(pad.name)))
        {
            for (const GamepadSnapshot& other : state.pads)
            {
                char id[48];
                std::snprintf(id, sizeof(id), "gamepad_tester.gamepad.%u", other.id);
                if (ImGui::Selectable(text.verbatim(other.name, id), other.id == pad.id))
                {
                    state.selected = other.id;
                }
            }
            ImGui::EndCombo();
        }
    }
    else
    {
        ImGui::TextUnformatted(text.copy(pad.name));
    }
    ui::badge(kit, text.copy(pad.type), ui::ThemeToken::TextSecondary);
    ImGui::SameLine();
    ui::badge(kit, text.format(s::kGamepadTesterPlayer, {{"player", u64{pad.player} + 1}}),
              ui::ThemeToken::TextSecondary);

    // What the gamepad reads on the left, what it can be sent on the right.
    if (!ImGui::BeginTable(text.hidden("gamepad_tester.layout"), 2, ImGuiTableFlags_SizingStretchSame))
    {
        return;
    }
    ImGui::TableNextColumn();

    // Buttons, lit while held.
    ui::sectionHeader(kit, text.format(s::kGamepadTesterButtons));
    if (ImGui::BeginTable(text.hidden("gamepad_tester.buttons"), 3, ImGuiTableFlags_SizingStretchSame))
    {
        for (usize b = 0; b < kGamepadButtons; ++b)
        {
            ImGui::TableNextColumn();
            if (pad.buttons[b])
            {
                ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Accent)), "%s",
                                   text.copy(gamepadButtonId(b)));
            }
            else
            {
                ImGui::TextDisabled("%s", text.copy(gamepadButtonId(b)));
            }
        }
        ImGui::EndTable();
    }

    // Sticks and triggers.
    ui::sectionHeader(kit, text.format(s::kGamepadTesterSticks));
    const f32 stickSize = 96.0f * kit.scale;
    drawStick(shell, "###gamepad_tester.stick.left", pad.axes[0], pad.axes[1], stickSize);
    ImGui::SameLine();
    drawStick(shell, "###gamepad_tester.stick.right", pad.axes[2], pad.axes[3], stickSize);
    for (const auto& [axis, label] : {std::pair{usize{4}, s::kGamepadTesterLeftTrigger},
                                      std::pair{usize{5}, s::kGamepadTesterRightTrigger}})
    {
        ImGui::ProgressBar(pad.axes[axis], ImVec2(-FLT_MIN, 0.0f), text.format(label));
    }

    if (pad.features.touchpad)
    {
        ui::sectionHeader(kit, text.format(s::kGamepadTesterTouchpad));
        drawTouchpad(shell, pad, std::min(ImGui::GetContentRegionAvail().x, 320.0f * kit.scale));
    }

    if (pad.features.motionSensors && pad.motionEnabled)
    {
        ui::sectionHeader(kit, text.format(s::kGamepadTesterMotion));
        if (ImGui::BeginTable(text.hidden("gamepad_tester.motion.values"), 2,
                              ImGuiTableFlags_SizingStretchProp))
        {
            setupStretchColumns({1.0f, 1.0f});
            vectorRow(text, s::kGamepadTesterGyroscope, pad.gyroscope);
            vectorRow(text, s::kGamepadTesterAccelerometer, pad.accelerometer);
            ImGui::EndTable();
        }
    }

    ImGui::TableNextColumn();
    char params[256];
    if (pad.features.motionSensors)
    {
        bool motion = pad.motionEnabled;
        if (ImGui::Checkbox(text.label(s::kGamepadTesterMotion, "gamepad_tester.motion"), &motion))
        {
            std::snprintf(params, sizeof(params), R"({"gamepad":%u,"enabled":%s})", pad.id,
                          motion ? "true" : "false");
            (void)shell.run("gamepad.set_motion", params, "gamepad_tester");
        }
    }

    if (pad.features.rumble || pad.features.triggerRumble)
    {
        ui::sectionHeader(kit, text.format(s::kGamepadTesterRumble));
        if (pad.features.rumble)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat(text.label(s::kGamepadTesterLow, "gamepad_tester.rumble.low"), &state.low,
                               0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat(text.label(s::kGamepadTesterHigh, "gamepad_tester.rumble.high"), &state.high,
                               0.0f, 1.0f, "%.2f");
        }
        if (pad.features.triggerRumble)
        {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat(text.label(s::kGamepadTesterLeftTrigger, "gamepad_tester.rumble.left"),
                               &state.leftTrigger, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat(text.label(s::kGamepadTesterRightTrigger, "gamepad_tester.rumble.right"),
                               &state.rightTrigger, 0.0f, 1.0f, "%.2f");
        }
        if (ImGui::Button(text.iconLabel(ui::icons::kVibration, s::kGamepadTesterTestRumble,
                                         "gamepad_tester.rumble.test")))
        {
            if (pad.features.rumble && pad.features.triggerRumble)
            {
                std::snprintf(
                    params, sizeof(params),
                    R"({"gamepad":%u,"low":%.3g,"high":%.3g,"left":%.3g,"right":%.3g,"durationMs":%u})",
                    pad.id, static_cast<f64>(state.low), static_cast<f64>(state.high),
                    static_cast<f64>(state.leftTrigger), static_cast<f64>(state.rightTrigger), kTestRumbleMs);
            }
            else if (pad.features.rumble)
            {
                std::snprintf(params, sizeof(params),
                              R"({"gamepad":%u,"low":%.3g,"high":%.3g,"durationMs":%u})", pad.id,
                              static_cast<f64>(state.low), static_cast<f64>(state.high), kTestRumbleMs);
            }
            else
            {
                std::snprintf(params, sizeof(params),
                              R"({"gamepad":%u,"left":%.3g,"right":%.3g,"durationMs":%u})", pad.id,
                              static_cast<f64>(state.leftTrigger), static_cast<f64>(state.rightTrigger),
                              kTestRumbleMs);
            }
            (void)shell.run("gamepad.rumble", params, "gamepad_tester");
        }
    }

    if (pad.features.led)
    {
        ui::sectionHeader(kit, text.format(s::kGamepadTesterLight));
        if (ImGui::ColorEdit3(text.label(s::kGamepadTesterLight, "gamepad_tester.led"), state.led.data(),
                              ImGuiColorEditFlags_NoInputs))
        {
            std::snprintf(params, sizeof(params), R"({"gamepad":%u,"color":[%.3g,%.3g,%.3g]})", pad.id,
                          static_cast<f64>(state.led[0]), static_cast<f64>(state.led[1]),
                          static_cast<f64>(state.led[2]));
            (void)shell.run("gamepad.set_led", params, "gamepad_tester");
        }
    }

    if (pad.features.adaptiveTriggers)
    {
        ui::sectionHeader(kit, text.format(s::kGamepadTesterAdaptive));
        const std::array<text::TextKey, 2> triggers{s::kGamepadTesterLeftTrigger,
                                                    s::kGamepadTesterRightTrigger};
        const std::array<text::TextKey, 4> modes{
            s::kGamepadTesterEffectOff, s::kGamepadTesterEffectResistance, s::kGamepadTesterEffectWeapon,
            s::kGamepadTesterEffectVibration};
        for (int t = 0; t < 2; ++t)
        {
            if (t != 0)
            {
                ImGui::SameLine();
            }
            if (ImGui::RadioButton(
                    text.label(triggers[static_cast<usize>(t)],
                               t == 0 ? "gamepad_tester.trigger.left" : "gamepad_tester.trigger.right"),
                    state.trigger == t))
            {
                state.trigger = t;
            }
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(text.label(s::kGamepadTesterEffect, "gamepad_tester.effect"),
                              text.format(modes[static_cast<usize>(state.mode)])))
        {
            for (usize m = 0; m < modes.size(); ++m)
            {
                if (ImGui::Selectable(
                        text.label(modes[m], text.concat("gamepad_tester.effect.", kModeIds[m])),
                        static_cast<usize>(state.mode) == m))
                {
                    state.mode = static_cast<int>(m);
                }
            }
            ImGui::EndCombo();
        }
        const auto slider = [&](text::TextKey label, const char* id, f32* value) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderFloat(text.label(label, id), value, 0.0f, 1.0f, "%.2f");
        };
        if (state.mode != 0)
        {
            slider(s::kGamepadTesterStart, "gamepad_tester.effect.start", &state.start);
        }
        if (state.mode == 2)
        {
            slider(s::kGamepadTesterEnd, "gamepad_tester.effect.end", &state.end);
        }
        if (state.mode == 1 || state.mode == 2)
        {
            slider(s::kGamepadTesterStrength, "gamepad_tester.effect.strength", &state.strength);
        }
        if (state.mode == 3)
        {
            slider(s::kGamepadTesterAmplitude, "gamepad_tester.effect.amplitude", &state.amplitude);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::SliderInt(text.label(s::kGamepadTesterFrequency, "gamepad_tester.effect.frequency"),
                             &state.frequency, 1, 255);
        }
        ImGui::BeginDisabled(state.mode == 2 && state.end <= state.start);
        if (ImGui::Button(
                text.iconLabel(ui::icons::kTune, s::kGamepadTesterApply, "gamepad_tester.effect.apply")))
        {
            std::snprintf(
                params, sizeof(params),
                R"({"gamepad":%u,"trigger":"%s","mode":"%s","start":%.3g,"end":%.3g,"strength":%.3g,)"
                R"("amplitude":%.3g,"frequency":%d})",
                pad.id, state.trigger == 0 ? "left" : "right",
                kModeIds[static_cast<usize>(state.mode)].data(), static_cast<f64>(state.start),
                static_cast<f64>(state.end), static_cast<f64>(state.strength),
                static_cast<f64>(state.amplitude), state.frequency);
            (void)shell.run("gamepad.set_trigger_effect", params, "gamepad_tester");
        }
        ImGui::EndDisabled();
    }
    ImGui::EndTable();
}

} // namespace illusion::editor::shell::detail

// The Input Actions window: the actions of an input actions asset (.input) with their bindings. Actions are
// added, removed and rebound through the `input.*` commands; a binding is captured from the next key press
// or typed (for gamepads and axes, e.g. "Gamepad.South" or "Stick.Left").

#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/platform/Input.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr std::string_view kDefaultAsset = "assets:/input/game.input";
constexpr std::array<std::string_view, 4> kTypes{"Button", "Axis", "Vector2", "Delta"};

} // namespace

struct InputActionsWindow::State
{
    struct Action
    {
        std::string name;
        std::string type;
        std::vector<std::string> bindings;
    };

    std::array<char, 256> asset{};
    std::string readAsset;
    usize readPosition = ~usize{0};
    usize readSteps = ~usize{0};
    bool exists = false;
    std::vector<Action> actions;
    std::optional<usize> capturing; // the action whose next key press becomes a binding
    std::array<char, 64> typed{};   // a binding typed for the action being captured
    std::array<char, 64> newName{};
    int newType = 0;

    State() { std::memcpy(asset.data(), kDefaultAsset.data(), kDefaultAsset.size()); }

    // The first input actions asset of the project, or the default path.
    void pickAsset(Impl& shell)
    {
        for (const auto& [path, record] : shell.editor.assetCatalog().records())
        {
            if (path.ends_with(".input") && path.size() < asset.size())
            {
                std::fill(asset.begin(), asset.end(), '\0');
                std::memcpy(asset.data(), path.data(), path.size());
                return;
            }
        }
    }

    // Reads the actions (`input.actions`) when the asset or the undo history changed.
    void refresh(Impl& shell)
    {
        const UndoStack& history = shell.editor.history();
        const std::string_view path(asset.data());
        if (path == readAsset && readPosition == history.position() && readSteps == history.steps().size())
        {
            return;
        }
        readAsset = std::string(path);
        readPosition = history.position();
        readSteps = history.steps().size();
        actions.clear();
        capturing.reset();
        exists = shell.editor.files().exists(readAsset);
        if (!exists)
        {
            return;
        }
        const std::optional<std::string> result =
            shell.run("input.actions", "{\"asset\":" + jsonQuote(readAsset) + "}", "input_actions");
        if (!result)
        {
            return;
        }
        const Expected<JsonDocument, JsonError> json = JsonDocument::parse(*result);
        if (!json)
        {
            return;
        }
        for (const JsonValue action : json->root().find("actions").elements())
        {
            Action& added = actions.emplace_back();
            added.name = std::string(action.find("name").asString().value_or(""));
            added.type = std::string(action.find("type").asString().value_or(""));
            for (const JsonValue binding : action.find("bindings").elements())
            {
                added.bindings.emplace_back(binding.asString().value_or(""));
            }
        }
    }

    void bind(Impl& shell, const Action& action, std::string_view binding)
    {
        (void)shell.run("input.bind",
                        "{\"asset\":" + jsonQuote(readAsset) + ",\"action\":" + jsonQuote(action.name) +
                            ",\"binding\":" + jsonQuote(binding) + "}",
                        "input_actions");
        capturing.reset();
    }
};

InputActionsWindow::InputActionsWindow()
    : m_state(std::make_unique<State>())
{
}

InputActionsWindow::~InputActionsWindow() = default;

void InputActionsWindow::draw(Impl& shell)
{
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    State& state = *m_state;
    if (shell.editor.project() == nullptr)
    {
        ui::emptyState(kit, ui::icons::kSportsEsports, text.format(s::kProjectSettingsNone),
                       text.format(s::kProjectSettingsNoneHint));
        return;
    }
    if (state.readAsset.empty())
    {
        state.pickAsset(shell);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText(text.label(s::kInputActionsAsset, "input_actions.asset"), state.asset.data(),
                     state.asset.size());
    state.refresh(shell);

    // The next key press becomes a binding of the action being captured; Escape cancels.
    if (state.capturing && *state.capturing < state.actions.size())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            state.capturing.reset();
        }
        else
        {
            for (u16 k = 1; k < static_cast<u16>(Key::Count); ++k)
            {
                const Key key = static_cast<Key>(k);
                const int imgui = ui::imguiKey(key);
                if (imgui != ImGuiKey_None && !ImGui::GetIO().WantTextInput &&
                    ImGui::IsKeyPressed(static_cast<ImGuiKey>(imgui), false))
                {
                    state.bind(shell, state.actions[*state.capturing],
                               std::string("Key.") + std::string(keyName(key)));
                    break;
                }
            }
        }
    }

    if (!state.exists || state.actions.empty())
    {
        ImGui::TextDisabled("%s", text.format(s::kInputActionsEmpty));
    }
    char id[96];
    for (usize i = 0; i < state.actions.size(); ++i)
    {
        const State::Action& action = state.actions[i];
        const char* actionId = text.concat("input_actions.action.", action.name);
        ImGui::PushID(static_cast<int>(i));
        ImGui::Separator();
        ImGui::TextUnformatted(text.copy(action.name));
        ImGui::SameLine();
        ui::badge(kit, text.copy(action.type), ui::ThemeToken::TextSecondary);
        ImGui::SameLine();
        if (ui::iconButton(kit, ui::icons::kDelete, text.concat(actionId, ".remove"),
                           text.format(s::kInputActionsRemove)))
        {
            (void)shell.run("input.action_remove",
                            "{\"asset\":" + jsonQuote(state.readAsset) +
                                ",\"name\":" + jsonQuote(action.name) + "}",
                            "input_actions");
        }
        for (usize b = 0; b < action.bindings.size(); ++b)
        {
            std::snprintf(id, sizeof(id), ".binding.%zu", b);
            if (ImGui::SmallButton(
                    text.verbatim(text.concat(action.bindings[b], "  x"), text.concat(actionId, id))))
            {
                (void)shell.run("input.unbind",
                                "{\"asset\":" + jsonQuote(state.readAsset) +
                                    ",\"action\":" + jsonQuote(action.name) +
                                    ",\"binding\":" + jsonQuote(action.bindings[b]) + "}",
                                "input_actions");
            }
            ui::richTooltip(kit, text.format(s::kInputActionsUnbind), nullptr, nullptr);
            ImGui::SameLine();
        }
        if (state.capturing == i)
        {
            ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Accent)), "%s",
                               text.format(s::kInputActionsPressKey));
            ImGui::SetNextItemWidth(160.0f * kit.scale);
            if (ImGui::InputTextWithHint(text.hidden(text.concat(actionId, ".typed")),
                                         text.format(s::kInputActionsTypeHint), state.typed.data(),
                                         state.typed.size(), ImGuiInputTextFlags_EnterReturnsTrue) &&
                state.typed[0] != '\0')
            {
                state.bind(shell, action, state.typed.data());
                state.typed.fill('\0');
            }
        }
        else if (ui::iconButton(kit, ui::icons::kAdd, text.concat(actionId, ".bind"),
                                text.format(s::kInputActionsBind)))
        {
            state.capturing = i;
            state.typed.fill('\0');
        }
        ImGui::PopID();
    }

    // A new action.
    ImGui::Separator();
    ImGui::SetNextItemWidth(160.0f * kit.scale);
    ImGui::InputTextWithHint(text.hidden("input_actions.new.name"), text.format(s::kInputActionsNewName),
                             state.newName.data(), state.newName.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f * kit.scale);
    if (ImGui::BeginCombo(text.hidden("input_actions.new.type"),
                          text.copy(kTypes[static_cast<usize>(state.newType)])))
    {
        for (usize t = 0; t < kTypes.size(); ++t)
        {
            if (ImGui::Selectable(text.verbatim(kTypes[t], text.concat("input_actions.new.type.", kTypes[t])),
                                  static_cast<usize>(state.newType) == t))
            {
                state.newType = static_cast<int>(t);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.newName[0] == '\0');
    if (ImGui::Button(text.iconLabel(ui::icons::kAddCircle, s::kInputActionsAdd, "input_actions.new.add")))
    {
        if (shell.run("input.action_define",
                      "{\"asset\":" + jsonQuote(std::string_view(state.asset.data())) +
                          ",\"name\":" + jsonQuote(std::string_view(state.newName.data())) +
                          ",\"type\":" + jsonQuote(kTypes[static_cast<usize>(state.newType)]) + "}",
                      "input_actions"))
        {
            state.newName.fill('\0');
        }
    }
    ImGui::EndDisabled();
}

} // namespace illusion::editor::shell::detail

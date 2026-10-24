// The Preferences window: appearance, language, keyboard (presets, conflicts, rebinding), AI agents,
// autosave and external tools. Every change goes through `preferences.set`.

#include "Panels.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/editor/core/Preferences.hpp>
#include <illusion/editor/ui/Icons.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/text/LocaleId.hpp>
#include <illusion/text/Localization.hpp>

#include <cstdio>
#include <cstring>

namespace illusion::editor::shell::detail
{
namespace
{

namespace s = strings;

constexpr text::TextKey kTabTitles[] = {s::kPrefsAppearance, s::kPrefsLanguage, s::kPrefsKeyboard,
                                        s::kPrefsAgents,     s::kPrefsAutosave, s::kPrefsExternalTools};

int indexOf(std::span<const std::string_view> names, std::string_view value) noexcept
{
    for (usize i = 0; i < names.size(); ++i)
    {
        if (names[i] == value)
        {
            return static_cast<int>(i);
        }
    }
    return 0;
}

bool containsFolded(std::string_view text, std::string_view needle) noexcept
{
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (usize i = 0; i + needle.size() <= text.size(); ++i)
    {
        usize j = 0;
        while (j < needle.size() && lower(text[i + j]) == lower(needle[j]))
        {
            ++j;
        }
        if (j == needle.size())
        {
            return true;
        }
    }
    return needle.empty();
}

bool isModifier(Key key) noexcept
{
    return key == Key::LeftShift || key == Key::RightShift || key == Key::LeftControl ||
           key == Key::RightControl || key == Key::LeftAlt || key == Key::RightAlt || key == Key::LeftSuper ||
           key == Key::RightSuper;
}

// The patch {"shortcuts": [...]} with @p action bound to @p chord (std::nullopt: back to the preset).
std::string shortcutsPatch(const Preferences& prefs, std::string_view action,
                           std::optional<std::string_view> chord)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    JsonMutValue list = root.addArray("shortcuts");
    for (const ShortcutBinding& binding : prefs.shortcuts)
    {
        if (binding.action == action)
        {
            continue;
        }
        JsonMutValue item = list.pushObject();
        item.add("action", doc.makeString(binding.action));
        item.add("chord", doc.makeString(binding.chord));
    }
    if (chord)
    {
        JsonMutValue item = list.pushObject();
        item.add("action", doc.makeString(action));
        item.add("chord", doc.makeString(*chord));
    }
    doc.setRoot(root);
    std::string out;
    return doc.write(out, JsonWriteOptions{.pretty = false}) ? out : std::string("{}");
}

} // namespace

text::TextKey preferencesTabTitle(PreferencesTab tab) noexcept
{
    return kTabTitles[static_cast<usize>(tab)];
}

struct PreferencesPage::State
{
    PreferencesTab tab = PreferencesTab::Appearance;
    std::optional<usize> capture; // action whose new chord is being captured
    std::array<char, 128> search{};
    std::vector<std::string> locales; // tags, read once
    // Fields edited until the item is deactivated, then saved.
    u64 version = 0;
    f32 uiScale = 1.0f;
    f64 autosaveInterval = 60.0;
    int autosaveCopies = 3;
    std::array<char, 512> codeEditor{};

    void sync(const EditorContext& editor)
    {
        if (version == editor.preferencesVersion())
        {
            return;
        }
        version = editor.preferencesVersion();
        const Preferences& prefs = editor.preferences();
        uiScale = prefs.uiScale;
        autosaveInterval = prefs.autosaveInterval;
        autosaveCopies = static_cast<int>(prefs.autosaveCopies);
        const usize length = std::min(prefs.codeEditor.size(), codeEditor.size() - 1);
        std::memcpy(codeEditor.data(), prefs.codeEditor.data(), length);
        codeEditor[length] = '\0';
    }

    void appearance(Impl& shell, ui::UiKit& kit);
    void language(Impl& shell);
    void keyboard(Impl& shell, ui::UiKit& kit);
    void agents(Impl& shell, ui::UiKit& kit);
    void autosave(Impl& shell, ui::UiKit& kit);
    void externalTools(Impl& shell, ui::UiKit& kit);
};

void PreferencesPage::State::appearance(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const Preferences& prefs = shell.editor.preferences();
    if (ui::beginPropertyTable(kit, "prefs.appearance"))
    {
        ui::propertyLabel(kit, text.format(s::kPrefsTheme));
        const char* const themes[] = {text.format(s::kPrefsThemeDark), text.format(s::kPrefsThemeLight),
                                      text.format(s::kPrefsThemeHighContrast)};
        int theme = indexOf(preferenceThemes(), prefs.theme);
        if (ui::segmentedControl(kit, "prefs.theme", themes, theme))
        {
            shell.setPreferences("{\"theme\":" + jsonQuote(preferenceThemes()[static_cast<usize>(theme)]) +
                                 "}");
        }
        ui::propertyLabel(kit, text.format(s::kPrefsAccent));
        const char* const accents[] = {text.format(s::kPrefsAccentAzure), text.format(s::kPrefsAccentViolet),
                                       text.format(s::kPrefsAccentTeal), text.format(s::kPrefsAccentAmber)};
        int accent = indexOf(preferenceAccents(), prefs.accent);
        if (ui::segmentedControl(kit, "prefs.accent", accents, accent))
        {
            shell.setPreferences("{\"accent\":" + jsonQuote(preferenceAccents()[static_cast<usize>(accent)]) +
                                 "}");
        }
        ui::propertyLabel(kit, text.format(s::kPrefsUiScale));
        ImGui::SliderFloat(text.hidden("prefs.ui_scale"), &uiScale, 0.75f, 2.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            char patch[48];
            std::snprintf(patch, sizeof(patch), R"({"uiScale":%.2f})", static_cast<f64>(uiScale));
            shell.setPreferences(patch);
        }
        ui::propertyLabel(kit, "");
        bool reduceMotion = prefs.reduceMotion;
        if (ui::toggleSwitch(kit, text.label(s::kPrefsReduceMotion, "prefs.reduce_motion"), reduceMotion))
        {
            shell.setPreferences(reduceMotion ? R"({"reduceMotion":true})" : R"({"reduceMotion":false})");
        }
        ui::propertyLabel(kit, "");
        bool colorBlind = prefs.colorBlindAxes;
        if (ui::toggleSwitch(kit, text.label(s::kPrefsColorBlind, "prefs.color_blind"), colorBlind))
        {
            shell.setPreferences(colorBlind ? R"({"colorBlindAxes":true})" : R"({"colorBlindAxes":false})");
        }
        ui::endPropertyTable();
    }
}

void PreferencesPage::State::language(Impl& shell)
{
    ui::FrameText& text = shell.text;
    if (locales.empty())
    {
        for (const text::LocaleId& locale : shell.editor.localization().availableLocales())
        {
            locales.emplace_back(locale.tag());
        }
    }
    const std::string& current = shell.editor.preferences().locale;
    for (const std::string& tag : locales)
    {
        const char* id = text.concat("prefs.locale.", tag);
        const char* label = tag == "en-XA" ? text.label(s::kPrefsLanguagePseudo, id) : text.verbatim(tag, id);
        if (ImGui::RadioButton(label, current == tag) && current != tag)
        {
            shell.setPreferences("{\"locale\":" + jsonQuote(tag) + "}");
        }
    }
}

void PreferencesPage::State::keyboard(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const Preferences& prefs = shell.editor.preferences();
    const std::span<const UiAction> actions = uiActions();

    const std::span<const std::string_view> presets = keymapPresets();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text.format(s::kPrefsKeyboardPreset));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * kit.scale);
    if (ImGui::BeginCombo(text.hidden("prefs.keymap"), text.copy(prefs.keymap)))
    {
        for (const std::string_view preset : presets)
        {
            if (ImGui::Selectable(text.verbatim(preset, text.concat("prefs.keymap.", preset)),
                                  prefs.keymap == preset))
            {
                shell.setPreferences("{\"keymap\":" + jsonQuote(preset) + "}");
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const usize conflicts = shell.keymap.conflicts().size();
    ImGui::TextColored(
        imvec(shell.theme.color(conflicts > 0 ? ui::ThemeToken::Warning : ui::ThemeToken::TextSecondary)),
        "%s", text.format(s::kPrefsKeyboardConflicts, {{"count", static_cast<u64>(conflicts)}}));
    ui::searchField(kit, "prefs.keyboard.search", search, text.format(s::kPrefsKeyboardSearch));

    // A chord being captured: the next key with its modifiers; Escape cancels.
    if (capture)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            capture.reset();
        }
        else
        {
            for (u16 k = 1; k < static_cast<u16>(Key::Count); ++k)
            {
                const Key key = static_cast<Key>(k);
                const int imgui = ui::imguiKey(key);
                if (isModifier(key) || imgui == ImGuiKey_None ||
                    !ImGui::IsKeyPressed(static_cast<ImGuiKey>(imgui), false))
                {
                    continue;
                }
                Chord chord;
                chord.key = key;
                chord.modifiers =
                    static_cast<u8>((io.KeyCtrl ? kChordCtrl : 0) | (io.KeyShift ? kChordShift : 0) |
                                    (io.KeyAlt ? kChordAlt : 0) | (io.KeySuper ? kChordSuper : 0));
                const std::string formatted = formatChord(chord);
                shell.setPreferences(
                    shortcutsPatch(prefs, actions[*capture].id, std::string_view(formatted)));
                capture.reset();
                break;
            }
        }
    }

    if (!ImGui::BeginTable(text.hidden("prefs.keyboard.table"), 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp))
    {
        return;
    }
    const std::string_view needle(search.data());
    for (usize i = 0; i < actions.size(); ++i)
    {
        const char* title = text.format(shell.actionTitle(i));
        if (!containsFolded(title, needle) && !containsFolded(actions[i].id, needle))
        {
            continue;
        }
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
        ImGui::TableNextColumn();
        const char* rowId = text.concat("prefs.shortcut.", actions[i].id);
        const std::string_view chord = shell.keymap.text(i);
        const char* label = capture == i
                                ? text.label(s::kPrefsKeyboardPress, rowId)
                                : text.verbatim(chord.empty() ? std::string_view("-") : chord, rowId);
        if (ImGui::Button(label, ImVec2(160.0f * kit.scale, 0.0f)))
        {
            capture = i;
        }
        for (const KeyConflict& conflict : shell.keymap.conflicts())
        {
            if (conflict.first == i || conflict.second == i)
            {
                const usize other = conflict.first == i ? conflict.second : conflict.first;
                ImGui::SameLine();
                ImGui::TextColored(imvec(shell.theme.color(ui::ThemeToken::Warning)), "%s",
                                   ui::icons::kWarning.c_str());
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", text.format(s::kPrefsKeyboardConflict,
                                                        {{"action", text.format(shell.actionTitle(other))}}));
                }
            }
        }
        ImGui::TableNextColumn();
        if (ui::iconButton(kit, ui::icons::kRestartAlt, text.concat(rowId, ".reset"),
                           text.format(s::kPrefsKeyboardReset)))
        {
            shell.setPreferences(shortcutsPatch(prefs, actions[i].id, std::nullopt));
        }
        ImGui::SameLine();
        if (ui::iconButton(kit, ui::icons::kBackspace, text.concat(rowId, ".remove"),
                           text.format(s::kPrefsKeyboardRemove)))
        {
            shell.setPreferences(shortcutsPatch(prefs, actions[i].id, std::string_view()));
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void PreferencesPage::State::agents(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    const Preferences& prefs = shell.editor.preferences();
    if (ui::beginPropertyTable(kit, "prefs.agents"))
    {
        ui::propertyLabel(kit, text.format(s::kPrefsAgentsPermissions));
        const char* const modes[] = {text.format(s::kPrefsPermissionReadOnly),
                                     text.format(s::kPrefsPermissionAsk),
                                     text.format(s::kPrefsPermissionAuto)};
        constexpr std::string_view kModes[] = {"read-only", "ask", "auto"};
        int mode = indexOf(kModes, prefs.agentPermissions);
        if (ui::segmentedControl(kit, "prefs.agent_permissions", modes, mode))
        {
            shell.setPreferences("{\"agentPermissions\":" + jsonQuote(kModes[mode]) + "}");
        }
        ui::propertyLabel(kit, "");
        bool popups = prefs.approvalPopups;
        if (ui::toggleSwitch(kit, text.label(s::kPrefsAgentsApprovalPopups, "prefs.approval_popups"), popups))
        {
            shell.setPreferences(popups ? R"({"approvalPopups":true})" : R"({"approvalPopups":false})");
        }
        ui::endPropertyTable();
    }
}

void PreferencesPage::State::autosave(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    if (ui::beginPropertyTable(kit, "prefs.autosave"))
    {
        ui::propertyLabel(kit, text.format(s::kPrefsAutosaveInterval));
        ImGui::InputDouble(text.hidden("prefs.autosave_interval"), &autosaveInterval, 10.0, 60.0, "%.0f");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            char patch[64];
            std::snprintf(patch, sizeof(patch), R"({"autosaveInterval":%.0f})",
                          std::clamp(autosaveInterval, 0.0, 3600.0));
            shell.setPreferences(patch);
        }
        ui::propertyLabel(kit, text.format(s::kPrefsAutosaveCopies));
        ImGui::InputInt(text.hidden("prefs.autosave_copies"), &autosaveCopies);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            char patch[48];
            std::snprintf(patch, sizeof(patch), R"({"autosaveCopies":%d})",
                          std::clamp(autosaveCopies, 1, 20));
            shell.setPreferences(patch);
        }
        ui::endPropertyTable();
    }
}

void PreferencesPage::State::externalTools(Impl& shell, ui::UiKit& kit)
{
    ui::FrameText& text = shell.text;
    if (ui::beginPropertyTable(kit, "prefs.external_tools"))
    {
        ui::propertyLabel(kit, text.format(s::kPrefsExternalToolsCodeEditor));
        ImGui::InputTextWithHint(text.hidden("prefs.code_editor"),
                                 text.format(s::kPrefsExternalToolsCodeEditorHint), codeEditor.data(),
                                 codeEditor.size());
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            shell.setPreferences("{\"codeEditor\":" + jsonQuote(codeEditor.data()) + "}");
        }
        ui::endPropertyTable();
    }
}

PreferencesPage::PreferencesPage()
    : m_state(std::make_unique<State>())
{
}

PreferencesPage::~PreferencesPage() = default;

void PreferencesPage::select(PreferencesTab tab) noexcept
{
    m_state->tab = tab;
}

bool PreferencesPage::capturing() const noexcept
{
    return m_state->capture.has_value();
}

void PreferencesPage::draw(Impl& shell)
{
    State& state = *m_state;
    state.sync(shell.editor);
    ui::UiKit kit = shell.kit();
    ui::FrameText& text = shell.text;
    if (ImGui::BeginChild(text.hidden("prefs.tabs"), ImVec2(180.0f * kit.scale, 0.0f)))
    {
        for (usize i = 0; i < static_cast<usize>(PreferencesTab::Count); ++i)
        {
            const PreferencesTab tab = static_cast<PreferencesTab>(i);
            char id[32];
            std::snprintf(id, sizeof(id), "prefs.tab.%zu", i);
            if (ImGui::Selectable(text.label(kTabTitles[i], id), state.tab == tab))
            {
                state.tab = tab;
                state.capture.reset();
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild(text.hidden("prefs.page")))
    {
        ui::sectionHeader(kit, text.format(kTabTitles[static_cast<usize>(state.tab)]));
        switch (state.tab)
        {
        case PreferencesTab::Appearance:
            state.appearance(shell, kit);
            break;
        case PreferencesTab::Language:
            state.language(shell);
            break;
        case PreferencesTab::Keyboard:
            state.keyboard(shell, kit);
            break;
        case PreferencesTab::Agents:
            state.agents(shell, kit);
            break;
        case PreferencesTab::Autosave:
            state.autosave(shell, kit);
            break;
        case PreferencesTab::ExternalTools:
        case PreferencesTab::Count:
            state.externalTools(shell, kit);
            break;
        }
    }
    ImGui::EndChild();
}

} // namespace illusion::editor::shell::detail

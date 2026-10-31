#include <illusion/editor/EditorStrings.hpp>
#include <illusion/editor/core/CommandRegistry.hpp>
#include <illusion/editor/core/Preferences.hpp>
#include <illusion/editor/core/UiActions.hpp>

#include <array>
#include <charconv>

namespace illusion::editor
{
namespace
{

namespace s = strings;

// clang-format off
// Every menu item, toolbar button and shortcut. Actions of one command take its default shortcut only when
// they are the first action of that command (see Keymap::build()).
constexpr UiAction kActions[] = {
    // File
    {.id = "file.new_scene", .command = "scene.new", .menu = UiMenu::File},
    {.id = "file.open_scene", .command = "scene.open", .menu = UiMenu::File},
    {.id = "file.save", .command = "scene.save", .menu = UiMenu::File},
    {.id = "file.save_as", .command = "scene.save_as", .menu = UiMenu::File},
    {.id = "file.projects", .command = "ui.show_panel", .params = R"({"panel":"window.welcome"})",
     .menu = UiMenu::File, .group = 1, .title = s::kActionFileProjects, .icon = "folder_open"},
    {.id = "file.preferences", .command = "ui.show_panel", .params = R"({"panel":"window.preferences"})",
     .menu = UiMenu::File, .group = 2, .title = s::kActionFilePreferences, .icon = "settings",
     .shortcut = "Ctrl+Comma"},
    {.id = "file.exit", .command = "ui.quit", .menu = UiMenu::File, .group = 3},
    // Edit
    {.id = "edit.undo", .command = "history.undo", .menu = UiMenu::Edit},
    {.id = "edit.redo", .command = "history.redo", .menu = UiMenu::Edit},
    {.id = "edit.cut", .command = "clipboard.cut", .params = R"({"entities":["$selection"]})",
     .menu = UiMenu::Edit, .group = 1},
    {.id = "edit.copy", .command = "clipboard.copy", .params = R"({"entities":["$selection"]})",
     .menu = UiMenu::Edit, .group = 1},
    {.id = "edit.paste", .command = "clipboard.paste", .menu = UiMenu::Edit, .group = 1},
    {.id = "edit.duplicate", .command = "entity.duplicate", .params = R"({"entities":["$selection"]})",
     .menu = UiMenu::Edit, .group = 1},
    {.id = "edit.delete", .command = "entity.destroy", .params = R"({"entities":["$selection"]})",
     .menu = UiMenu::Edit, .group = 1},
    {.id = "edit.clear_selection", .command = "selection.clear", .menu = UiMenu::Edit, .group = 2},
    {.id = "edit.palette", .command = "ui.palette", .menu = UiMenu::Edit, .group = 3,
     .shortcut = "Ctrl+Shift+P"},
    {.id = "edit.go_to", .command = "ui.palette", .params = R"({"mode":"entities"})", .menu = UiMenu::Edit,
     .group = 3, .title = s::kActionEditGoTo, .icon = "travel_explore", .shortcut = "Ctrl+P"},
    {.id = "edit.search", .command = "ui.palette", .title = s::kActionEditSearch, .icon = "search",
     .shortcut = "Ctrl+K"},
    // View
    {.id = "view.tool_select", .command = "gizmo.set_tool", .params = R"({"tool":"select"})", .menu = UiMenu::View,
     .title = s::kActionViewToolSelect, .icon = "arrow_selector_tool", .shortcut = "Q"},
    {.id = "view.tool_move", .command = "gizmo.set_tool", .params = R"({"tool":"translate"})", .menu = UiMenu::View,
     .title = s::kToolMove, .icon = "open_with", .shortcut = "W"},
    {.id = "view.tool_rotate", .command = "gizmo.set_tool", .params = R"({"tool":"rotate"})", .menu = UiMenu::View,
     .title = s::kToolRotate, .icon = "rotate_right", .shortcut = "E"},
    {.id = "view.tool_scale", .command = "gizmo.set_tool", .params = R"({"tool":"scale"})", .menu = UiMenu::View,
     .title = s::kToolScale, .icon = "open_in_full", .shortcut = "R"},
    {.id = "view.tool_universal", .command = "gizmo.set_tool", .params = R"({"tool":"universal"})", .menu = UiMenu::View,
     .title = s::kActionViewToolUniversal, .icon = "deployed_code", .shortcut = "T"},
    {.id = "view.tool_measure", .command = "gizmo.set_tool", .params = R"({"tool":"measure"})", .menu = UiMenu::View,
     .title = s::kActionViewToolMeasure, .icon = "straighten", .shortcut = "M"},
    {.id = "view.modal_move", .command = "gizmo.modal", .params = R"({"operation":"translate"})", .menu = UiMenu::View,
     .title = s::kActionViewModalMove, .icon = "drag_pan", .shortcut = "Shift+W"},
    {.id = "view.modal_rotate", .command = "gizmo.modal", .params = R"({"operation":"rotate"})", .menu = UiMenu::View,
     .title = s::kActionViewModalRotate, .icon = "rotate_90_degrees_ccw", .shortcut = "Shift+E"},
    {.id = "view.modal_scale", .command = "gizmo.modal", .params = R"({"operation":"scale"})", .menu = UiMenu::View,
     .title = s::kActionViewModalScale, .icon = "zoom_out_map", .shortcut = "Shift+R"},
    {.id = "view.frame_selected", .command = "viewport.frame", .menu = UiMenu::View, .group = 1,
     .title = s::kActionViewFrameSelected, .icon = "center_focus_strong", .shortcut = "F"},
    {.id = "view.frame_all", .command = "viewport.frame", .params = R"({"entities":[]})", .menu = UiMenu::View,
     .group = 1, .title = s::kActionViewFrameAll, .icon = "fit_screen", .shortcut = "Shift+F"},
    // Entity
    {.id = "entity.create_empty", .command = "entity.create", .params = R"({"name":"Entity"})",
     .menu = UiMenu::Entity},
    {.id = "entity.rename", .command = "entity.rename", .params = R"({"entity":"$selection"})",
     .menu = UiMenu::Entity, .group = 1},
    {.id = "entity.enable", .command = "entity.set_enabled", .params = R"({"entity":"$selection","enabled":true})",
     .menu = UiMenu::Entity, .group = 1, .title = s::kActionEntityEnable, .icon = "visibility"},
    {.id = "entity.disable", .command = "entity.set_enabled",
     .params = R"({"entity":"$selection","enabled":false})", .menu = UiMenu::Entity, .group = 1,
     .title = s::kActionEntityDisable, .icon = "visibility_off"},
    {.id = "entity.unparent", .command = "hierarchy.set_parent", .params = R"({"entity":"$selection"})",
     .menu = UiMenu::Entity, .group = 1, .title = s::kActionEntityUnparent, .icon = "move_up"},
    {.id = "entity.drop_to_ground", .command = "transform.drop_to_ground", .params = R"({"entities":["$selection"]})",
     .menu = UiMenu::Entity, .group = 2, .shortcut = "End"},
    {.id = "entity.align_to_view", .command = "transform.align_to_view", .params = R"({"entities":["$selection"]})",
     .menu = UiMenu::Entity, .group = 2, .shortcut = "Ctrl+Shift+F"},
    // Assets
    {.id = "assets.new_folder", .command = "asset.create_folder", .menu = UiMenu::Assets},
    {.id = "assets.new_material", .command = "material.create", .menu = UiMenu::Assets},
    {.id = "assets.import", .command = "asset.import", .menu = UiMenu::Assets, .group = 1,
     .title = s::kActionAssetsImport, .icon = "download", .milestone = "M7u"},
    // Play
    {.id = "play.play", .command = "play.start", .menu = UiMenu::Play, .title = s::kToolPlay,
     .icon = "play_arrow", .shortcut = "F5"},
    {.id = "play.pause", .command = "play.pause", .menu = UiMenu::Play, .title = s::kToolPause,
     .icon = "pause", .shortcut = "F6"},
    {.id = "play.step", .command = "play.step", .menu = UiMenu::Play, .title = s::kToolStep,
     .icon = "skip_next", .shortcut = "F7"},
    {.id = "play.simulate", .command = "play.start", .params = R"({"mode":"simulate"})", .menu = UiMenu::Play,
     .title = s::kActionPlaySimulate, .icon = "science", .shortcut = "Shift+F5"},
    // Modules
    {.id = "modules.build", .command = "module.build", .menu = UiMenu::Modules, .title = s::kActionModulesBuild,
     .icon = "build", .shortcut = "Ctrl+B", .milestone = "M7s"},
    {.id = "modules.reload", .command = "module.reload", .menu = UiMenu::Modules,
     .title = s::kActionModulesReload, .icon = "refresh", .shortcut = "Ctrl+R", .milestone = "M7s"},
    // Tools
    {.id = "tools.screenshot", .command = "editor.screenshot", .menu = UiMenu::Tools},
    {.id = "tools.clear_log", .command = "log.clear", .menu = UiMenu::Tools},
    {.id = "tools.recover", .command = "project.recover", .menu = UiMenu::Tools, .group = 1},
    // Window: panels
    {.id = "window.outliner", .command = "ui.show_panel", .params = R"({"panel":"panel.outliner"})",
     .menu = UiMenu::Window, .title = s::kPanelOutliner, .icon = "account_tree"},
    {.id = "window.inspector", .command = "ui.show_panel", .params = R"({"panel":"panel.inspector"})",
     .menu = UiMenu::Window, .title = s::kPanelInspector, .icon = "tune"},
    {.id = "window.scene", .command = "ui.show_panel", .params = R"({"panel":"panel.scene"})",
     .menu = UiMenu::Window, .title = s::kPanelScene, .icon = "view_in_ar"},
    {.id = "window.game", .command = "ui.show_panel", .params = R"({"panel":"panel.game"})",
     .menu = UiMenu::Window, .title = s::kPanelGame, .icon = "sports_esports"},
    {.id = "window.assets", .command = "ui.show_panel", .params = R"({"panel":"panel.assets"})",
     .menu = UiMenu::Window, .title = s::kPanelAssets, .icon = "folder"},
    {.id = "window.console", .command = "ui.show_panel", .params = R"({"panel":"panel.console"})",
     .menu = UiMenu::Window, .title = s::kPanelConsole, .icon = "terminal", .shortcut = "Ctrl+GraveAccent"},
    {.id = "window.history", .command = "ui.show_panel", .params = R"({"panel":"panel.history"})",
     .menu = UiMenu::Window, .title = s::kPanelHistory, .icon = "history"},
    {.id = "window.ai_activity", .command = "ui.show_panel", .params = R"({"panel":"panel.ai_activity"})",
     .menu = UiMenu::Window, .title = s::kPanelAiActivity, .icon = "smart_toy"},
    {.id = "window.profiler", .command = "ui.show_panel", .params = R"({"panel":"panel.profiler"})",
     .menu = UiMenu::Window, .title = s::kPanelProfiler, .icon = "speed"},
    {.id = "window.rendering", .command = "ui.show_panel", .params = R"({"panel":"panel.rendering"})",
     .menu = UiMenu::Window, .title = s::kPanelRendering, .icon = "brightness_6"},
    {.id = "window.project_settings", .command = "ui.show_panel",
     .params = R"({"panel":"panel.project_settings"})", .menu = UiMenu::Window,
     .title = s::kPanelProjectSettings, .icon = "settings_applications"},
    // Window: layouts
    {.id = "window.layout_default", .command = "ui.layout_apply", .params = R"({"name":"default"})",
     .menu = UiMenu::Window, .group = 1, .title = s::kLayoutDefault, .icon = "dashboard", .submenu = "layouts"},
    {.id = "window.layout_level_design", .command = "ui.layout_apply", .params = R"({"name":"level-design"})",
     .menu = UiMenu::Window, .group = 1, .title = s::kLayoutLevelDesign, .icon = "dashboard",
     .submenu = "layouts"},
    {.id = "window.layout_debug", .command = "ui.layout_apply", .params = R"({"name":"debug"})",
     .menu = UiMenu::Window, .group = 1, .title = s::kLayoutDebug, .icon = "dashboard", .submenu = "layouts"},
    {.id = "window.layout_profiling", .command = "ui.layout_apply", .params = R"({"name":"profiling"})",
     .menu = UiMenu::Window, .group = 1, .title = s::kLayoutProfiling, .icon = "dashboard",
     .submenu = "layouts"},
    {.id = "window.layout_agent", .command = "ui.layout_apply", .params = R"({"name":"agent"})",
     .menu = UiMenu::Window, .group = 1, .title = s::kLayoutAgent, .icon = "dashboard", .submenu = "layouts"},
    {.id = "window.layout_save", .command = "ui.layout_save", .menu = UiMenu::Window, .group = 2,
     .submenu = "layouts"},
    {.id = "window.layout_reset", .command = "ui.layout_reset", .menu = UiMenu::Window, .group = 2,
     .submenu = "layouts"},
    {.id = "window.notifications", .command = "ui.show_panel", .params = R"({"panel":"window.notifications"})",
     .menu = UiMenu::Window, .group = 3, .title = s::kWindowNotifications, .icon = "notifications"},
    // Help
    {.id = "help.welcome", .command = "ui.show_panel", .params = R"({"panel":"window.welcome"})",
     .menu = UiMenu::Help, .title = s::kWindowWelcome, .icon = "waving_hand"},
    {.id = "help.licenses", .command = "ui.show_panel", .params = R"({"panel":"window.licenses"})",
     .menu = UiMenu::Help, .group = 1, .title = s::kWindowLicenses, .icon = "gavel"},
    {.id = "help.about", .command = "ui.show_panel", .params = R"({"panel":"window.about"})",
     .menu = UiMenu::Help, .group = 1, .title = s::kWindowAbout, .icon = "info"},
};
// clang-format on

// A chord a preset gives an action.
struct PresetBinding
{
    std::string_view action;
    std::string_view chord; // "" removes the default
};

constexpr PresetBinding kUnity[] = {
    {"play.play", "Ctrl+P"},          {"play.pause", "Ctrl+Shift+P"}, {"play.step", "Ctrl+Alt+P"},
    {"edit.palette", "Ctrl+Shift+K"}, {"edit.go_to", "Ctrl+K"},       {"edit.search", ""},
};

constexpr PresetBinding kUnreal[] = {
    {"play.play", "Alt+P"},      {"play.simulate", "Alt+S"},        {"play.pause", "Pause"},
    {"view.tool_universal", ""}, {"window.console", "GraveAccent"},
};

constexpr PresetBinding kBlender[] = {
    {"edit.duplicate", "Shift+D"},
    {"edit.delete", "X"},
    {"edit.redo", "Ctrl+Shift+Z"},
    {"edit.palette", "F3"},
    {"view.tool_select", "W"},
    {"view.tool_move", ""},
    {"view.tool_rotate", ""},
    {"view.tool_scale", ""},
    {"view.modal_move", "G"},
    {"view.modal_rotate", "R"},
    {"view.modal_scale", "S"},
    {"view.tool_universal", ""},
    {"view.frame_selected", "KeypadDecimal"},
    {"play.play", "Shift+Space"},
    {"entity.create_empty", "Shift+A"},
};

constexpr std::array<std::string_view, 4> kPresets = {"illusion", "unity", "unreal", "blender"};

std::span<const PresetBinding> presetBindings(std::string_view preset) noexcept
{
    if (preset == "unity")
    {
        return kUnity;
    }
    if (preset == "unreal")
    {
        return kUnreal;
    }
    if (preset == "blender")
    {
        return kBlender;
    }
    return {};
}

struct ModifierName
{
    std::string_view name;
    u8 bit;
};

constexpr ModifierName kModifiers[] = {
    {"Ctrl", kChordCtrl}, {"Cmd", kChordCtrl},    {"Shift", kChordShift},
    {"Alt", kChordAlt},   {"Super", kChordSuper},
};

bool isModifierKey(Key key) noexcept
{
    switch (key)
    {
    case Key::LeftShift:
    case Key::RightShift:
    case Key::LeftControl:
    case Key::RightControl:
    case Key::LeftAlt:
    case Key::RightAlt:
    case Key::LeftSuper:
    case Key::RightSuper:
        return true;
    default:
        return false;
    }
}

} // namespace

std::string_view uiMenuName(UiMenu menu) noexcept
{
    constexpr std::array<std::string_view, kUiMenuCount + 1> kNames = {
        "", "file", "edit", "view", "entity", "assets", "play", "modules", "tools", "window", "help"};
    return kNames[static_cast<usize>(menu)];
}

std::span<const UiAction> uiActions() noexcept
{
    return kActions;
}

const UiAction* findUiAction(std::string_view id) noexcept
{
    for (const UiAction& action : kActions)
    {
        if (action.id == id)
        {
            return &action;
        }
    }
    return nullptr;
}

std::optional<Chord> parseChord(std::string_view text)
{
    Chord chord;
    while (!text.empty())
    {
        const usize plus = text.find('+', 1);
        const std::string_view part = text.substr(0, plus);
        if (plus == std::string_view::npos)
        {
            std::optional<Key> key = keyFromName(part);
            if (!key && part.size() == 1 && part[0] >= '0' && part[0] <= '9')
            {
                key = static_cast<Key>(static_cast<u16>(Key::Num0) + static_cast<u16>(part[0] - '0'));
            }
            if (!key || *key == Key::Unknown || isModifierKey(*key))
            {
                return std::nullopt;
            }
            chord.key = *key;
            return chord;
        }
        const auto modifier = std::find_if(std::begin(kModifiers), std::end(kModifiers),
                                           [&](const ModifierName& name) { return name.name == part; });
        if (modifier == std::end(kModifiers) || (chord.modifiers & modifier->bit) != 0)
        {
            return std::nullopt;
        }
        chord.modifiers = static_cast<u8>(chord.modifiers | modifier->bit);
        text.remove_prefix(plus + 1);
    }
    return std::nullopt;
}

std::string formatChord(Chord chord)
{
    if (!chord.valid())
    {
        return {};
    }
    std::string out;
    for (const ModifierName& modifier : kModifiers)
    {
        // "Cmd" is an alias of "Ctrl" when reading.
        if ((chord.modifiers & modifier.bit) != 0 && modifier.name != "Cmd")
        {
            out += modifier.name;
            out += '+';
        }
    }
    const u16 key = static_cast<u16>(chord.key);
    if (key >= static_cast<u16>(Key::Num0) && key <= static_cast<u16>(Key::Num9))
    {
        out += static_cast<char>('0' + (key - static_cast<u16>(Key::Num0)));
    }
    else
    {
        out += keyName(chord.key);
    }
    return out;
}

std::span<const std::string_view> keymapPresets() noexcept
{
    return kPresets;
}

Keymap Keymap::build(std::string_view preset, std::span<const ShortcutBinding> overrides,
                     const CommandRegistry* commands)
{
    Keymap keymap;
    keymap.m_preset = std::find(kPresets.begin(), kPresets.end(), preset) != kPresets.end()
                          ? std::string(preset)
                          : std::string(kPresets[0]);
    const std::span<const UiAction> actions = uiActions();
    keymap.m_chords.resize(actions.size());
    keymap.m_texts.resize(actions.size());
    const std::span<const PresetBinding> bindings = presetBindings(keymap.m_preset);
    for (usize i = 0; i < actions.size(); ++i)
    {
        const UiAction& action = actions[i];
        std::string_view chord = action.shortcut;
        if (chord.empty() && commands != nullptr)
        {
            const bool first =
                std::none_of(actions.begin(), actions.begin() + static_cast<std::ptrdiff_t>(i),
                             [&](const UiAction& other) { return other.command == action.command; });
            const CommandDescriptor* command = commands->find(action.command);
            chord = first && command != nullptr ? command->info.defaultShortcut : std::string_view();
        }
        for (const PresetBinding& binding : bindings)
        {
            if (binding.action == action.id)
            {
                chord = binding.chord;
            }
        }
        for (const ShortcutBinding& binding : overrides)
        {
            if (binding.action == action.id && (binding.chord.empty() || parseChord(binding.chord)))
            {
                chord = binding.chord;
            }
        }
        if (const std::optional<Chord> parsed = parseChord(chord))
        {
            keymap.m_chords[i] = *parsed;
            keymap.m_texts[i] = formatChord(*parsed);
        }
    }
    for (usize i = 0; i < actions.size(); ++i)
    {
        for (usize j = i + 1; j < actions.size() && keymap.m_chords[i].valid(); ++j)
        {
            if (keymap.m_chords[i] == keymap.m_chords[j])
            {
                keymap.m_conflicts.push_back(KeyConflict{keymap.m_chords[i], i, j});
            }
        }
    }
    return keymap;
}

Chord Keymap::chord(usize index) const noexcept
{
    return index < m_chords.size() ? m_chords[index] : Chord{};
}

std::string_view Keymap::text(usize index) const noexcept
{
    return index < m_texts.size() ? std::string_view(m_texts[index]) : std::string_view();
}

std::optional<usize> Keymap::find(Chord chord) const noexcept
{
    if (!chord.valid())
    {
        return std::nullopt;
    }
    for (usize i = 0; i < m_chords.size(); ++i)
    {
        if (m_chords[i] == chord)
        {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace illusion::editor

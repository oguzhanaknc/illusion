#pragma once
/// @file
/// The UI actions of the editor: every menu item, toolbar button and shortcut is an action that runs one
/// command with fixed params, so people and agents use the same API. Keymaps bind chords to actions.
///
/// ```cpp
/// for (const UiAction& action : uiActions())
///     if (action.menu == UiMenu::Edit && ImGui::MenuItem(title(action)))
///         editor.executeJson(action.command, parsed(action.params), {.origin = "menu"});
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/platform/Input.hpp>
#include <illusion/text/StringTable.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

class CommandRegistry;
struct ShortcutBinding;

/// The menus of the menu bar, in order.
enum class UiMenu : u8
{
    None, ///< In no menu (toolbar or shortcut only).
    File,
    Edit,
    View,
    Entity,
    Assets,
    Play,
    Modules,
    Tools,
    Window,
    Help,
};

/// Number of UiMenu values with a menu (all but None).
inline constexpr usize kUiMenuCount = 10;

/// Stable lower-case name of @p menu, e.g. "file" ("" for None).
[[nodiscard]] std::string_view uiMenuName(UiMenu menu) noexcept;

/// One action.
struct UiAction
{
    std::string_view id;        ///< Stable id, e.g. "edit.undo" (menu items get "menu.<id>").
    std::string_view command;   ///< The command it runs, e.g. "history.undo".
    std::string_view params{};  ///< Params as a JSON object ("" for none).
    UiMenu menu = UiMenu::None; ///< Menu it appears in.
    /// Group within the menu: a separator is drawn where the group changes.
    u8 group = 0;
    /// Title, or a default key (table nullptr) for the command's title.
    text::TextKey title{};
    std::string_view icon{};     ///< Material Symbols icon name ("" for the command's icon).
    std::string_view shortcut{}; ///< Default chord in the illusion keymap ("" for the command's shortcut).
    /// Milestone that brings the command, e.g. "M7r": until then the command does not exist and the item is
    /// disabled. Empty for actions whose command exists.
    std::string_view milestone{};
    /// Submenu of the menu, e.g. "layouts" ("" for the menu itself).
    std::string_view submenu{};
};

/// Every action, in menu order.
[[nodiscard]] std::span<const UiAction> uiActions() noexcept;

/// The action with @p id, or nullptr.
[[nodiscard]] const UiAction* findUiAction(std::string_view id) noexcept;

/// Modifier bits of a Chord.
enum ChordModifier : u8
{
    kChordCtrl = 1,  ///< Control (Command on macOS).
    kChordShift = 2, ///< Shift.
    kChordAlt = 4,   ///< Alt (Option on macOS).
    kChordSuper = 8, ///< Windows key (Control on macOS).
};

/// A key with modifiers, e.g. Ctrl+Shift+P.
struct Chord
{
    Key key = Key::Unknown; ///< The key (Unknown: no chord).
    u8 modifiers = 0;       ///< ChordModifier bits.

    /// True when it names a key.
    [[nodiscard]] constexpr bool valid() const noexcept { return key != Key::Unknown; }

    /// Chords are equal when key and modifiers match.
    friend constexpr bool operator==(const Chord&, const Chord&) noexcept = default;
};

/// Parses "Ctrl+Shift+P", "F2", "Delete" or "Alt+1": modifiers Ctrl (or Cmd), Shift, Alt and Super, then one
/// key named as keyName() names it (digits may be written as "1"). Returns std::nullopt for anything else,
/// including modifier keys alone.
[[nodiscard]] std::optional<Chord> parseChord(std::string_view text);

/// The text of @p chord in the form parseChord() reads ("" for an invalid chord).
[[nodiscard]] std::string formatChord(Chord chord);

/// The keymap presets: "illusion" (the default), "unity", "unreal" and "blender".
[[nodiscard]] std::span<const std::string_view> keymapPresets() noexcept;

/// Two actions bound to the same chord.
struct KeyConflict
{
    Chord chord;      ///< The chord.
    usize first = 0;  ///< Index of the first action in uiActions().
    usize second = 0; ///< Index of the other action.
};

/// The chord of every action: a preset plus the user's changes. Build it again when they change.
class Keymap
{
public:
    /// An empty keymap (no shortcuts).
    Keymap() = default;

    /// Resolves every action of uiActions(): the user's @p overrides win, then @p preset's chord, then the
    /// action's default, then the default shortcut of its command in @p commands (may be null). Unknown
    /// presets fall back to "illusion"; overrides of unknown actions and unparsable chords are ignored.
    [[nodiscard]] static Keymap build(std::string_view preset, std::span<const ShortcutBinding> overrides,
                                      const CommandRegistry* commands);

    /// The chord of action @p index (an invalid Chord for none).
    [[nodiscard]] Chord chord(usize index) const noexcept;

    /// The chord of action @p index as text ("" for none), valid as long as the keymap.
    [[nodiscard]] std::string_view text(usize index) const noexcept;

    /// Index of the first action bound to @p chord.
    [[nodiscard]] std::optional<usize> find(Chord chord) const noexcept;

    /// Chords bound to more than one action, in action order.
    [[nodiscard]] std::span<const KeyConflict> conflicts() const noexcept { return m_conflicts; }

    /// The preset it was built from.
    [[nodiscard]] std::string_view preset() const noexcept { return m_preset; }

private:
    std::string m_preset;
    std::vector<Chord> m_chords;
    std::vector<std::string> m_texts;
    std::vector<KeyConflict> m_conflicts;
};

} // namespace illusion::editor

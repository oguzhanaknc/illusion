#pragma once
/// @file
/// The editor preferences of the current user: appearance, language, keyboard, AI agents, autosave, external
/// tools and recent projects. They are saved as `preferences.json` in the user directory
/// (EditorContextOptions::userDirectory) and changed through the `preferences.set` command.
///
/// ```json
/// {"format": "illusion.preferences", "version": 1,
///  "data": {"theme": "light", "uiScale": 1.25, "keymap": "blender",
///           "shortcuts": [{"action": "edit.duplicate", "chord": "Shift+D"}]}}
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{
class StructRegistry;
struct TypeReflection;
} // namespace illusion

namespace illusion::editor
{

/// The `format` value of preferences files.
inline constexpr std::string_view kPreferencesFormat = "illusion.preferences";

/// Recent projects kept in Preferences::recentProjects.
inline constexpr usize kMaxRecentProjects = 10;

/// A shortcut that replaces the keymap preset's chord of one UI action.
struct ShortcutBinding
{
    std::string action; ///< UI action id, e.g. "edit.undo" (see uiActions()).
    std::string chord;  ///< Chord such as "Ctrl+Shift+Z" (see parseChord()); "" removes the shortcut.
};

/// Everything a user can set in Preferences.
struct Preferences
{
    // Appearance
    std::string theme = "dark";   ///< "dark", "light" or "high-contrast".
    std::string accent = "azure"; ///< Accent preset: "azure", "violet", "teal" or "amber".
    f32 uiScale = 1.0f;           ///< UI scale, 0.75 to 2 (multiplied by the monitor's content scale).
    bool reduceMotion = false;    ///< Turns animations off.
    bool colorBlindAxes = false;  ///< Okabe-Ito colours for the X, Y and Z axes.
    // Language
    std::string locale = "en"; ///< Language of the UI: "en", "en-XA" or a locale with a file.
    // Keyboard
    std::string keymap = "illusion";        ///< Keymap preset: "illusion", "unity", "unreal" or "blender".
    std::vector<ShortcutBinding> shortcuts; ///< Changes to the preset, at most one per action.
    /// Mouse navigation of the Scene views: "keymap" (the keymap preset's), "illusion", "unity", "unreal" or
    /// "blender".
    std::string viewportNavigation = "keymap";
    // AI agents
    std::string agentPermissions = "ask"; ///< Permission mode of agents in the window: read-only, ask, auto.
    bool approvalPopups = true;           ///< Show a pop-up when an agent asks for approval.
    bool followAgent = false;             ///< The Scene view frames what an agent selects.
    // Autosave
    f64 autosaveInterval = 60.0; ///< Seconds between autosaves of a dirty scene, 0 (never) to 3,600.
    u32 autosaveCopies = 3;      ///< Autosave files kept per scene, 1 to 20.
    // External tools
    std::string codeEditor; ///< Command that opens a source file ("" for the system default).
    // Projects and layout
    std::vector<std::string> recentProjects; ///< Project directories (UTF-8), the most recent first.
    std::string layout = "default";          ///< The layout used last (see `ui.layout_apply`).
};

/// Reflects ShortcutBinding and Preferences as "editor.ShortcutBinding" and "editor.Preferences" in
/// @p structs (once; later calls do nothing).
void reflectPreferences(StructRegistry& structs);

/// The reflection of Preferences in the component registry's struct registry (reflected on first use).
[[nodiscard]] const TypeReflection& preferencesReflection();

/// Checks every value of @p preferences (names, ranges, chords, duplicate shortcuts). The error names the
/// first invalid field as a JSON Pointer.
[[nodiscard]] Expected<void, std::string> validatePreferences(const Preferences& preferences);

/// @p preferences as the text of a preferences file.
[[nodiscard]] std::string writePreferences(const Preferences& preferences);

/// Reads a preferences file. Missing fields keep their defaults; the error says what is wrong.
[[nodiscard]] Expected<Preferences, std::string> readPreferences(std::string_view json);

/// Loads `<directory>/preferences.json`. A missing file gives the defaults; an unreadable or invalid one
/// gives the defaults and the reason in @p warning.
[[nodiscard]] Preferences loadPreferences(const std::filesystem::path& directory, std::string& warning);

/// Writes @p preferences to `<directory>/preferences.json` (creating the directory), replacing the file
/// atomically. Returns why it failed.
[[nodiscard]] Expected<void, std::string> savePreferences(const std::filesystem::path& directory,
                                                          const Preferences& preferences);

/// Moves @p project (a UTF-8 path) to the front of @p recent, removing duplicates and keeping at most
/// kMaxRecentProjects entries.
void addRecentProject(std::vector<std::string>& recent, std::string_view project);

/// The theme names preferences accept.
[[nodiscard]] std::span<const std::string_view> preferenceThemes() noexcept;

/// The accent preset names preferences accept.
[[nodiscard]] std::span<const std::string_view> preferenceAccents() noexcept;

/// The viewport navigation names preferences accept: "keymap" and the keymap preset names.
[[nodiscard]] std::span<const std::string_view> viewportNavigationPresets() noexcept;

/// The navigation preset @p preferences give the Scene views ("keymap" resolved to the keymap preset).
[[nodiscard]] std::string_view viewportNavigationOf(const Preferences& preferences) noexcept;

} // namespace illusion::editor

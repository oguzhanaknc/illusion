#pragma once

/// @file
/// The settings types the editor edits: reflected structs saved in the "settings" section of every scene, or
/// in one file of the project's `settings:` mount (written with writeSettingsFile(), SettingsFile.hpp).
///
/// @code
/// illusion::reflectStruct<game::Difficulty>("game.Difficulty").field("enemies", &game::Difficulty::enemies);
/// editor.settings().add("game.Difficulty", illusion::editor::SettingsScope::Scene);
/// editor.settings().add("game.Controls", illusion::editor::SettingsScope::Project,
/// "settings:/controls.json");
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/SettingsFile.hpp>
#include <illusion/ecs/StructRegistry.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// Where a settings type is saved.
enum class SettingsScope : u8
{
    Scene,   ///< In the "settings" section of each scene file.
    Project, ///< In its own file on the project's `settings:` mount.
};

/// Stable lower-case name of @p scope: "scene" or "project".
[[nodiscard]] std::string_view settingsScopeName(SettingsScope scope) noexcept;

/// A registered settings type.
struct SettingsType
{
    const StructReflection* type = nullptr;     ///< The reflected struct.
    SettingsScope scope = SettingsScope::Scene; ///< Where it is saved.
    std::string file{}; ///< Project settings: its file, e.g. "settings:/controls.json".
};

/// The settings types of an editor, by scope and stable name.
class SettingsCatalog : NonCopyable
{
public:
    /// Registers the struct reflected as @p type in @p structs as settings of @p scope. Project settings need
    /// @p file, a `settings:/` path ending in ".json" that no other type uses, and cannot hold Entity fields
    /// (they belong to no scene). Asserts on unknown types, duplicates and bad files (programmer errors).
    void add(std::string_view type, SettingsScope scope, std::string_view file = {},
             const StructRegistry& structs = ComponentRegistry::instance().structs());

    /// The type named @p type in @p scope, or nullptr.
    [[nodiscard]] const SettingsType* find(std::string_view type, SettingsScope scope) const noexcept;

    /// Every type, sorted by scope, then by name.
    [[nodiscard]] std::span<const SettingsType> types() const noexcept { return m_types; }

    /// Up to three names of @p scope close to @p type, best first.
    [[nodiscard]] std::vector<std::string> suggest(std::string_view type, SettingsScope scope) const;

private:
    std::vector<SettingsType> m_types;
};

} // namespace illusion::editor

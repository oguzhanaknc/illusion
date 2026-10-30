#pragma once

/// @file
/// The input actions asset: a `.input` JSON file that defines a game's actions, their types and their
/// default bindings, so designers and the editor change them without code.
///
/// ```json
/// {
///   "format": "illusion.input",
///   "version": 1,
///   "actions": [
///     {"name": "Jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]},
///     {"name": "Move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]}
///   ]
/// }
/// ```
///
/// ```cpp
/// illusion::InputActions actions;
/// if (auto loaded = illusion::loadInputActions(files, "assets:/game.input", actions); !loaded)
/// {
///     ILL_LOG_ERROR("input", "{}", loaded.error());
/// }
/// ```

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/input/InputActions.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace illusion
{

class VirtualFileSystem;

/// Value of the "format" member of an input actions asset.
inline constexpr std::string_view kInputActionsFormat = "illusion.input";

/// Version of the input actions asset format that writeInputActions() writes.
inline constexpr u32 kInputActionsVersion = 1;

/// File extension of input actions assets.
inline constexpr std::string_view kInputActionsExtension = ".input";

/// Name of @p type in input actions assets: "Button", "Axis" or "Vector2".
[[nodiscard]] std::string_view actionTypeName(ActionType type) noexcept;

/// The ActionType named @p name (actionTypeName()), or nullopt.
[[nodiscard]] std::optional<ActionType> actionTypeFromName(std::string_view name) noexcept;

/// Writes the actions of @p actions, in id order with their bindings (bindingToString()), as an input
/// actions asset ending in a line break. The same actions always give the same text.
[[nodiscard]] std::string writeInputActions(const InputActions& actions);

/// Replaces the actions of @p actions with those of the input actions asset @p json. On failure returns
/// why, with the JSON pointer of the bad value, and leaves @p actions unchanged. Fails on invalid JSON, a
/// wrong format or a newer version, a missing or invalid name, a name used twice, an unknown type, and a
/// binding that bindingFromString() rejects.
[[nodiscard]] Expected<void, std::string> readInputActions(std::string_view json, InputActions& actions);

/// Reads the input actions asset at @p virtualPath on @p files into @p actions (readInputActions()). On
/// failure returns why, starting with the path, and leaves @p actions unchanged.
[[nodiscard]] Expected<void, std::string>
loadInputActions(const VirtualFileSystem& files, std::string_view virtualPath, InputActions& actions);

} // namespace illusion

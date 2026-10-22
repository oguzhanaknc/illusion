#pragma once

/// @file
/// The Name Component: a label for an Entity, shown by the editor and used to find entities by name.
///
/// @code
/// const illusion::Entity player = world.create(illusion::Name{"Player"}, illusion::Transform{});
/// if (!illusion::isValidEntityName(text))
/// {
///     ILL_LOG_WARN("Game", "invalid name: {}", illusion::entityNameError(text));
/// }
/// @endcode

#include <illusion/core/Base.hpp>

#include <string>
#include <string_view>

namespace illusion
{

/// Largest valid entity name, in bytes of UTF-8.
inline constexpr usize kMaxEntityNameBytes = 256;

/// Name of an Entity (reflected as "illusion.Name"). Names need not be unique. Reading a Name from JSON
/// (scene files, editor commands) rejects invalid names; see isValidEntityName().
struct Name
{
    std::string value; ///< The name, UTF-8.

    /// Names are equal when their text is.
    friend bool operator==(const Name&, const Name&) = default;
};

/// Returns why @p name is not a valid entity name, or an empty string if it is valid. Valid names have at
/// most kMaxEntityNameBytes bytes and contain no '/' (reserved for paths of entities) and no control
/// characters (U+0000 to U+001F and U+007F). The empty name is valid.
[[nodiscard]] std::string_view entityNameError(std::string_view name) noexcept;

/// True if @p name is a valid entity name (see entityNameError()).
[[nodiscard]] inline bool isValidEntityName(std::string_view name) noexcept
{
    return entityNameError(name).empty();
}

} // namespace illusion

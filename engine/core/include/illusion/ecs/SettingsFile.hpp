#pragma once

/// @file
/// Settings files: the value of a reflected struct (StructRegistry.hpp) saved as JSON on its own, with its
/// type name and data version, for example the project settings under `settings:/`.
///
/// ```json
/// {"format": "illusion.settings", "type": "game.Controls", "version": 1, "value": {...}}
/// ```
///
/// @code
/// const illusion::StructRegistry& structs = illusion::ComponentRegistry::instance().structs();
/// game::Controls controls;
/// const illusion::StructReflection* type = structs.reflectionOf(illusion::typeHash<game::Controls>());
/// if (auto read = illusion::readSettingsFile(*type, json, &controls); !read)
/// {
///     ILL_LOG_ERROR("settings", "{}", read.error());
/// }
/// @endcode

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <string>
#include <string_view>

namespace illusion
{

/// The `format` value of settings files.
inline constexpr std::string_view kSettingsFormat = "illusion.settings";

/// A settings file: the value of @p type as JSON text with its format, type and data version, ending in a
/// line break. Fails, saying why, if a value cannot be written (for example an Entity field).
[[nodiscard]] Expected<std::string, std::string> writeSettingsFile(const StructReflection& type,
                                                                   const void* value);

/// Reads the settings file @p json of @p type into @p value (a default value of the type, so that missing
/// fields keep their defaults). Older data versions are migrated; the error says what is wrong.
[[nodiscard]] Expected<void, std::string> readSettingsFile(const StructReflection& type,
                                                           std::string_view json, void* value);

} // namespace illusion

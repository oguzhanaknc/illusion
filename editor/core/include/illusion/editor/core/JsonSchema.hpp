#pragma once

/// @file
/// JSON Schema 2020-12 generated from reflected types, and a validator for the schemas it generates.

#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/editor/core/CommandRegistry.hpp>

#include <string_view>
#include <vector>

namespace illusion::editor
{

/// The `$schema` of generated schemas.
inline constexpr std::string_view kJsonSchemaDialect = "https://json-schema.org/draft/2020-12/schema";

/// Writes the schema of the JSON encoding of @p type (see writeValue() and readValue()): an object with one
/// property per field, `additionalProperties: false`, and every field required except std::optional ones.
/// Units, ranges and descriptions from the field metadata become `minimum`, `maximum` and `description`.
[[nodiscard]] JsonMutValue writeTypeSchema(JsonMutDocument& doc, const TypeReflection& type);

/// Writes the schema of one value of @p type; @p meta (may be null) adds ranges and alternatives such as
/// `{"deg": n}` for angles and `"#rrggbb"` for colours.
[[nodiscard]] JsonMutValue writeValueSchema(JsonMutDocument& doc, const ValueType& type,
                                            const FieldMeta* meta);

/// Writes the input schema of @p command, with `$schema`. Commands that are not read-only also accept the
/// options `dryRun`, `mergeKey` and `expectRevision`.
[[nodiscard]] JsonMutValue writeCommandParamsSchema(JsonMutDocument& doc, const CommandDescriptor& command);

/// Writes the output schema of @p command, with `$schema`: its result struct plus the `_editor` member that
/// EditorContext::executeJson() adds.
[[nodiscard]] JsonMutValue writeCommandResultSchema(JsonMutDocument& doc, const CommandDescriptor& command);

/// Checks @p value against @p schema and returns the problems (empty if it is valid). Supports the keywords
/// that the writers above produce: type, properties, required, additionalProperties, items, prefixItems,
/// minItems, maxItems, enum, minimum, maximum, pattern (only `^[0-9]+$`) and anyOf.
[[nodiscard]] std::vector<ValueError> validateJson(JsonValue schema, JsonValue value);

} // namespace illusion::editor

#pragma once

/// @file
/// JSON form of reflected values: Components, structs and single fields.
///
/// Every reflected type (see Reflection.hpp) can be written to and read from JSON by field name. Scene
/// files, the editor's commands and tools use the same encoding:
///
/// | Field type | JSON |
/// | --- | --- |
/// | Bool | `true` / `false` |
/// | I8 ... I64, U8 ... U32 | numbers |
/// | U64 | a decimal string such as `"18446744073709551615"` (numbers are accepted when reading) |
/// | F32, F64 | numbers; with a FieldMeta::unit also `{"deg": 60}` (see unitFromSymbol()) |
/// | Vector2/3/4 | `[x, y, z, w]`; colours (FieldHint::Color, HdrColor) also `"#rrggbb"` or `"#rrggbbaa"` in
/// sRGB | | Quaternion | `[x, y, z, w]` | | String | a string | | Entity | through an EntityResolver; without
/// one the process-local handle (EntityResolver docs) | | Enum | the value's name, or a number for values
/// without a name | | Flags | an array of value names (and a number for bits without a name) | | Struct | an
/// object keyed by field name | | FixedArray, Array | an array | | Optional | `null` when empty, otherwise
/// the value | | Json | the JSON value itself |
///
/// Reading reports every problem with a JSON Pointer (RFC 6901) to the value, for example `/position/1`.
/// Nothing asserts on malformed input.
///
/// @code
/// illusion::JsonMutDocument doc;
/// const auto written = illusion::writeValue(doc, reflection, &transform);
/// if (written)
/// {
///     doc.setRoot(*written);
/// }
///
/// const illusion::ValueReadResult read =
///     illusion::readValue(reflection, &transform, json, {.mode = illusion::JsonReadMode::Merge});
/// for (const illusion::ValueError& error : read.errors)
/// {
///     ILL_LOG_ERROR("Tools", "{}: {}", error.pointer, error.message);
/// }
/// @endcode

#include <illusion/core/Expected.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace illusion
{

/// A problem with one value, found while reading or writing JSON.
struct ValueError
{
    std::string pointer; ///< JSON Pointer to the value, relative to the object read or written ("" for it).
    std::string message; ///< What is wrong, e.g. "must be a number" (English, starts in lower case).
};

/// Converts Entity fields to and from JSON. Scene files store entities as indices into the scene, the
/// editor (M7f) as persistent ids.
class EntityResolver
{
public:
    virtual ~EntityResolver() = default;

    /// Returns the JSON for @p entity (which may be null), built in @p doc.
    [[nodiscard]] virtual JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const = 0;

    /// Returns the entity that @p value refers to, or a message such as "must be null or the index of an
    /// entity in this scene".
    [[nodiscard]] virtual Expected<Entity, std::string> readEntity(JsonValue value) const = 0;
};

/// Options of writeValue() and getPath().
struct ValueWriteOptions
{
    /// Converts Entity fields. Without one, a null entity is written as `null` and any other as the
    /// decimal string of Entity::toBits(), which is meaningful only inside the process that wrote it.
    const EntityResolver* entities = nullptr;
    /// Write U64 values as decimal strings, so that readers which keep numbers as doubles stay exact.
    /// Scene files of version 1 write numbers.
    bool u64AsString = true;
};

/// How readValue() treats fields that the JSON does not mention.
enum class JsonReadMode : u8
{
    Replace, ///< They get their default values: the result is a default value with the JSON applied.
    Merge,   ///< They keep their current values.
};

/// Options of readValue() and setPath().
struct ValueReadOptions
{
    JsonReadMode mode = JsonReadMode::Replace; ///< Fields missing from the JSON.
    /// Converts Entity fields. Without one, `null`, an integer or a decimal string is read as the bits of a
    /// process-local handle (see ValueWriteOptions::entities).
    const EntityResolver* entities = nullptr;
    /// Version the JSON was saved with (nullopt: the current version). Older data is first renamed
    /// (FieldMeta::renamedFrom) and migrated (see TypeReflection::migrations); newer data is read as far as
    /// the fields are known, with a warning. Only the object itself is migrated, not the structs in it.
    std::optional<u32> savedVersion{};
    /// Leave the object unchanged if reading fails. Without it, a failed read may leave some fields read;
    /// loaders that discard the whole object on failure skip the copy this costs.
    bool atomic = true;
};

/// Result of readValue().
struct ValueReadResult
{
    std::vector<ValueError> errors;       ///< Problems that made reading fail; empty on success.
    std::vector<std::string> unknownKeys; ///< Pointers to members that match no field (skipped).
    std::vector<std::string> warnings;    ///< Other remarks, such as data newer than the code.

    /// True if the value was read.
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

/// Writes the reflected @p object of @p type (a Component or a struct) as a JSON object built in @p doc.
/// Fails for floating-point values that are not finite, which JSON cannot store.
[[nodiscard]] Expected<JsonMutValue, ValueError> writeValue(JsonMutDocument& doc, const TypeReflection& type,
                                                            const void* object,
                                                            const ValueWriteOptions& options = {});

/// Reads the JSON object @p json into the reflected @p object of @p type. Members that match no field are
/// skipped and listed in ValueReadResult::unknownKeys. @p object may be null only for tags (types without
/// fields).
[[nodiscard]] ValueReadResult readValue(const TypeReflection& type, void* object, JsonValue json,
                                        const ValueReadOptions& options = {});

/// Writes the value at @p path in @p object: field names and array indices separated by '.', for example
/// "bloom.intensity" or "points.2". An empty path is the whole object. Optional values are looked through
/// when they hold a value.
[[nodiscard]] Expected<JsonMutValue, ValueError> getPath(JsonMutDocument& doc, const TypeReflection& type,
                                                         const void* object, std::string_view path,
                                                         const ValueWriteOptions& options = {});

/// Reads @p json into the value at @p path (see getPath()). Only that value changes, and nothing changes if
/// reading fails; ValueError::pointer is relative to @p object.
[[nodiscard]] Expected<void, ValueError> setPath(const TypeReflection& type, void* object,
                                                 std::string_view path, JsonValue json,
                                                 const ValueReadOptions& options = {});

} // namespace illusion

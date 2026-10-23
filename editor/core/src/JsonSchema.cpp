#include <illusion/editor/core/JsonSchema.hpp>

#include <cmath>
#include <limits>
#include <string>

namespace illusion::editor
{
namespace
{

void addRange(JsonMutDocument& doc, JsonMutValue& schema, f64 low, f64 high, const FieldMeta* meta)
{
    if (meta != nullptr && meta->min)
    {
        low = std::max(low, *meta->min);
    }
    if (meta != nullptr && meta->max)
    {
        high = std::min(high, *meta->max);
    }
    if (std::isfinite(low))
    {
        schema.add("minimum", low == std::floor(low) && std::abs(low) < 9.0e15
                                  ? doc.makeI64(static_cast<i64>(low))
                                  : doc.makeF64(low));
    }
    if (std::isfinite(high))
    {
        schema.add("maximum", high == std::floor(high) && std::abs(high) < 9.0e15
                                  ? doc.makeI64(static_cast<i64>(high))
                                  : doc.makeF64(high));
    }
}

JsonMutValue integerSchema(JsonMutDocument& doc, f64 low, f64 high, const FieldMeta* meta)
{
    JsonMutValue schema = doc.makeObject();
    schema.add("type", doc.makeString("integer"));
    addRange(doc, schema, low, high, meta);
    return schema;
}

JsonMutValue numberArray(JsonMutDocument& doc, u32 count)
{
    JsonMutValue schema = doc.makeObject();
    schema.add("type", doc.makeString("array"));
    schema.addObject("items").add("type", doc.makeString("number"));
    schema.add("minItems", doc.makeU64(count));
    schema.add("maxItems", doc.makeU64(count));
    return schema;
}

JsonMutValue typeOrNull(JsonMutDocument& doc, std::string_view type)
{
    JsonMutValue schema = doc.makeObject();
    JsonMutValue types = schema.addArray("type");
    types.push(doc.makeString(type));
    types.push(doc.makeString("null"));
    return schema;
}

JsonMutValue anyOf(JsonMutDocument& doc, JsonMutValue a, JsonMutValue b)
{
    JsonMutValue schema = doc.makeObject();
    JsonMutValue options = schema.addArray("anyOf");
    options.push(a);
    options.push(b);
    return schema;
}

JsonMutValue enumSchema(JsonMutDocument& doc, const ValueType& type)
{
    JsonMutValue schema = doc.makeObject();
    schema.add("type", doc.makeString("string"));
    JsonMutValue names = schema.addArray("enum");
    for (const EnumValue& value : type.enumValues)
    {
        names.push(doc.makeString(value.name));
    }
    return schema;
}

std::string escapePointer(std::string_view key)
{
    std::string out;
    for (const char c : key)
    {
        if (c == '~')
        {
            out += "~0";
        }
        else if (c == '/')
        {
            out += "~1";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

bool hasType(JsonValue value, std::string_view type)
{
    if (type == "null")
    {
        return value.isNull();
    }
    if (type == "boolean")
    {
        return value.isBool();
    }
    if (type == "integer")
    {
        if (value.isInteger())
        {
            return true;
        }
        const std::optional<f64> number = value.isReal() ? value.asF64() : std::nullopt;
        return number && *number == std::floor(*number);
    }
    if (type == "number")
    {
        return value.isNumber();
    }
    if (type == "string")
    {
        return value.isString();
    }
    if (type == "array")
    {
        return value.isArray();
    }
    if (type == "object")
    {
        return value.isObject();
    }
    return false;
}

std::string describeType(JsonValue type)
{
    if (const std::optional<std::string_view> single = type.asString())
    {
        return std::string(*single);
    }
    std::string out;
    for (const JsonValue entry : type.elements())
    {
        out += out.empty() ? "" : " or ";
        out += entry.asString().value_or("?");
    }
    return out;
}

void validate(JsonValue schema, JsonValue value, const std::string& pointer, std::vector<ValueError>& errors)
{
    if (!schema.isObject())
    {
        return; // `true` and other non-object schemas accept anything
    }
    if (const JsonValue options = schema.find("anyOf"); options.isArray())
    {
        for (const JsonValue option : options.elements())
        {
            std::vector<ValueError> optionErrors;
            validate(option, value, pointer, optionErrors);
            if (optionErrors.empty())
            {
                return;
            }
        }
        errors.push_back(ValueError{pointer, "does not match any of the allowed forms"});
        return;
    }
    if (const JsonValue type = schema.find("type"); !type.isAbsent())
    {
        bool matches = false;
        if (const std::optional<std::string_view> single = type.asString())
        {
            matches = hasType(value, *single);
        }
        for (const JsonValue entry : type.elements())
        {
            matches = matches || hasType(value, entry.asString().value_or(""));
        }
        if (!matches)
        {
            errors.push_back(ValueError{pointer, "must be of type " + describeType(type) + ", not " +
                                                     std::string(jsonTypeName(value.type()))});
            return;
        }
    }
    if (const JsonValue names = schema.find("enum"); names.isArray())
    {
        bool found = false;
        for (const JsonValue name : names.elements())
        {
            found = found || (name.asString() && name.asString() == value.asString());
        }
        if (!found)
        {
            errors.push_back(ValueError{pointer, "must be one of the allowed names"});
        }
    }
    if (const std::optional<f64> number = value.asF64())
    {
        if (const std::optional<f64> minimum = schema.find("minimum").asF64(); minimum && *number < *minimum)
        {
            errors.push_back(ValueError{pointer, "must be at least " + schema.find("minimum").dump()});
        }
        if (const std::optional<f64> maximum = schema.find("maximum").asF64(); maximum && *number > *maximum)
        {
            errors.push_back(ValueError{pointer, "must be at most " + schema.find("maximum").dump()});
        }
    }
    if (const std::optional<std::string_view> pattern = schema.find("pattern").asString())
    {
        const std::string_view text = value.asString().value_or("");
        const bool digits = !text.empty() && text.find_first_not_of("0123456789") == std::string_view::npos;
        if (*pattern == "^[0-9]+$" && !digits)
        {
            errors.push_back(ValueError{pointer, "must be a decimal number"});
        }
    }
    if (value.isArray())
    {
        const usize size = value.size();
        if (const std::optional<u64> least = schema.find("minItems").asU64(); least && size < *least)
        {
            errors.push_back(
                ValueError{pointer, "must have at least " + std::to_string(*least) + " elements"});
        }
        if (const std::optional<u64> most = schema.find("maxItems").asU64(); most && size > *most)
        {
            errors.push_back(ValueError{pointer, "must have at most " + std::to_string(*most) + " elements"});
        }
        if (const JsonValue items = schema.find("items"); !items.isAbsent())
        {
            usize index = 0;
            for (const JsonValue element : value.elements())
            {
                validate(items, element, pointer + "/" + std::to_string(index++), errors);
            }
        }
    }
    if (value.isObject())
    {
        const JsonValue properties = schema.find("properties");
        const JsonValue additional = schema.find("additionalProperties");
        for (const JsonMember member : value.members())
        {
            const std::string memberPointer = pointer + "/" + escapePointer(member.key);
            if (const JsonValue property = properties.find(member.key); !property.isAbsent())
            {
                validate(property, member.value, memberPointer, errors);
            }
            else if (additional.asBool() == false)
            {
                errors.push_back(ValueError{memberPointer, "is not a known member"});
            }
            else if (additional.isObject())
            {
                validate(additional, member.value, memberPointer, errors);
            }
        }
        for (const JsonValue required : schema.find("required").elements())
        {
            const std::string_view key = required.asString().value_or("");
            if (value.find(key).isAbsent())
            {
                errors.push_back(ValueError{pointer + "/" + escapePointer(key), "is required"});
            }
        }
    }
}

} // namespace

JsonMutValue writeValueSchema(JsonMutDocument& doc, const ValueType& type, const FieldMeta* meta)
{
    constexpr f64 kInfinity = std::numeric_limits<f64>::infinity();
    JsonMutValue schema;
    switch (type.type)
    {
    case FieldType::Bool:
        schema = doc.makeObject();
        schema.add("type", doc.makeString("boolean"));
        break;
    case FieldType::I8:
        schema = integerSchema(doc, -128.0, 127.0, meta);
        break;
    case FieldType::I16:
        schema = integerSchema(doc, -32768.0, 32767.0, meta);
        break;
    case FieldType::I32:
        schema = integerSchema(doc, -2147483648.0, 2147483647.0, meta);
        break;
    case FieldType::I64:
        schema = integerSchema(doc, -kInfinity, kInfinity, meta);
        break;
    case FieldType::U8:
        schema = integerSchema(doc, 0.0, 255.0, meta);
        break;
    case FieldType::U16:
        schema = integerSchema(doc, 0.0, 65535.0, meta);
        break;
    case FieldType::U32:
        schema = integerSchema(doc, 0.0, 4294967295.0, meta);
        break;
    case FieldType::U64:
    {
        // Written as decimal strings (exact beyond 2^53); integers are accepted too.
        JsonMutValue text = doc.makeObject();
        text.add("type", doc.makeString("string"));
        text.add("pattern", doc.makeString("^[0-9]+$"));
        schema = anyOf(doc, text, integerSchema(doc, 0.0, kInfinity, nullptr));
        break;
    }
    case FieldType::F32:
    case FieldType::F64:
    {
        JsonMutValue number = doc.makeObject();
        number.add("type", doc.makeString("number"));
        addRange(doc, number, -kInfinity, kInfinity, meta);
        if (meta != nullptr && meta->unit != Unit::None)
        {
            // A unit-tagged number such as {"deg": 90}.
            JsonMutValue tagged = doc.makeObject();
            tagged.add("type", doc.makeString("object"));
            tagged.addObject("additionalProperties").add("type", doc.makeString("number"));
            schema = anyOf(doc, number, tagged);
        }
        else
        {
            schema = number;
        }
        break;
    }
    case FieldType::Vector2:
        schema = numberArray(doc, 2);
        break;
    case FieldType::Vector3:
    case FieldType::Vector4:
    {
        JsonMutValue array = numberArray(doc, type.type == FieldType::Vector3 ? 3 : 4);
        if (meta != nullptr && (meta->hint == FieldHint::Color || meta->hint == FieldHint::HdrColor))
        {
            JsonMutValue hex = doc.makeObject();
            hex.add("type", doc.makeString("string"));
            hex.add("description", doc.makeString("\"#rrggbb\" (or \"#rrggbbaa\" for 4 components), sRGB"));
            schema = anyOf(doc, array, hex);
        }
        else
        {
            schema = array;
        }
        break;
    }
    case FieldType::Quaternion:
        schema = numberArray(doc, 4);
        break;
    case FieldType::String:
        schema = doc.makeObject();
        schema.add("type", doc.makeString("string"));
        break;
    case FieldType::Entity:
        schema = typeOrNull(doc, "string");
        break;
    case FieldType::Enum:
        schema = enumSchema(doc, type);
        break;
    case FieldType::Flags:
        schema = doc.makeObject();
        schema.add("type", doc.makeString("array"));
        schema.add("items", enumSchema(doc, type));
        break;
    case FieldType::Struct:
        schema = writeTypeSchema(doc, *type.structType);
        break;
    case FieldType::FixedArray:
        schema = doc.makeObject();
        schema.add("type", doc.makeString("array"));
        schema.add("items", writeValueSchema(doc, *type.element, nullptr));
        schema.add("minItems", doc.makeU64(type.count));
        schema.add("maxItems", doc.makeU64(type.count));
        break;
    case FieldType::Array:
        schema = doc.makeObject();
        schema.add("type", doc.makeString("array"));
        schema.add("items", writeValueSchema(doc, *type.element, nullptr));
        break;
    case FieldType::Optional:
    {
        JsonMutValue none = doc.makeObject();
        none.add("type", doc.makeString("null"));
        schema = anyOf(doc, writeValueSchema(doc, *type.element, meta), none);
        break;
    }
    case FieldType::Json:
        schema = doc.makeObject(); // any JSON value
        break;
    case FieldType::Asset:
        schema = typeOrNull(doc, "string");
        break;
    }
    if (schema.isAbsent())
    {
        schema = doc.makeObject();
    }
    if (meta != nullptr && !meta->description.empty() && type.type != FieldType::Optional)
    {
        schema.add("description", doc.makeString(meta->description));
    }
    return schema;
}

namespace
{

// Writes an object schema of @p type (nullptr: an object without members) into @p schema.
void fillTypeSchema(JsonMutDocument& doc, JsonMutValue& schema, const TypeReflection* type)
{
    schema.add("type", doc.makeString("object"));
    JsonMutValue properties = schema.addObject("properties");
    JsonMutValue required = doc.makeArray();
    if (type != nullptr)
    {
        for (const FieldInfo& field : type->fields)
        {
            properties.add(field.name, writeValueSchema(doc, field, &field.meta));
            if (field.type != FieldType::Optional)
            {
                required.push(doc.makeString(field.name));
            }
        }
    }
    if (required.size() > 0)
    {
        schema.add("required", required);
    }
    schema.add("additionalProperties", doc.makeBool(false));
}

} // namespace

JsonMutValue writeTypeSchema(JsonMutDocument& doc, const TypeReflection& type)
{
    JsonMutValue schema = doc.makeObject();
    fillTypeSchema(doc, schema, &type);
    return schema;
}

JsonMutValue writeCommandParamsSchema(JsonMutDocument& doc, const CommandDescriptor& command)
{
    JsonMutValue schema = doc.makeObject();
    schema.add("$schema", doc.makeString(kJsonSchemaDialect));
    fillTypeSchema(doc, schema, command.params);
    if (!command.info.flags.readOnly)
    {
        JsonMutValue properties = schema.find("properties");
        properties.addObject("dryRun").add("type", doc.makeString("boolean"));
        properties.addObject("mergeKey").add("type", doc.makeString("string"));
        properties.addObject("clientToken").add("type", doc.makeString("string"));
        properties.add("expectRevision",
                       writeValueSchema(doc, illusion::detail::valueTypeOf<u64>(), nullptr));
    }
    return schema;
}

JsonMutValue writeCommandResultSchema(JsonMutDocument& doc, const CommandDescriptor& command)
{
    JsonMutValue schema = doc.makeObject();
    schema.add("$schema", doc.makeString(kJsonSchemaDialect));
    fillTypeSchema(doc, schema, command.result);
    JsonMutValue editor = schema.find("properties").addObject("_editor");
    editor.add("type", doc.makeString("object"));
    JsonMutValue properties = editor.addObject("properties");
    properties.add("revision", integerSchema(doc, 0.0, std::numeric_limits<f64>::infinity(), nullptr));
    properties.add("transaction", integerSchema(doc, 0.0, std::numeric_limits<f64>::infinity(), nullptr));
    properties.addObject("dryRun").add("type", doc.makeString("boolean"));
    properties.add("operations", integerSchema(doc, 0.0, std::numeric_limits<f64>::infinity(), nullptr));
    JsonMutValue warnings = properties.addObject("warnings");
    warnings.add("type", doc.makeString("array"));
    warnings.addObject("items").add("type", doc.makeString("string"));
    JsonMutValue required = editor.addArray("required");
    for (const std::string_view name : {"revision", "transaction", "dryRun", "operations", "warnings"})
    {
        required.push(doc.makeString(name));
    }
    editor.add("additionalProperties", doc.makeBool(false));
    JsonMutValue topRequired = schema.find("required");
    if (topRequired.isAbsent())
    {
        topRequired = schema.addArray("required");
    }
    topRequired.push(doc.makeString("_editor"));
    return schema;
}

std::vector<ValueError> validateJson(JsonValue schema, JsonValue value)
{
    std::vector<ValueError> errors;
    validate(schema, value, "", errors);
    return errors;
}

} // namespace illusion::editor

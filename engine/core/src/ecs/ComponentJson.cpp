#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/ComponentJson.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <new>

namespace illusion
{
namespace
{

// The position of the value being read or written, kept as views so that the pointer text is built only
// when there is a problem to report.
class ValuePath
{
public:
    static constexpr usize kMaxDepth = 64;

    [[nodiscard]] bool push(std::string_view name) noexcept
    {
        if (m_size == kMaxDepth)
        {
            return false;
        }
        m_segments[m_size++] = Segment{name, 0, false};
        return true;
    }

    [[nodiscard]] bool push(usize index) noexcept
    {
        if (m_size == kMaxDepth)
        {
            return false;
        }
        m_segments[m_size++] = Segment{{}, index, true};
        return true;
    }

    void pop() noexcept { --m_size; }

    // RFC 6901: '~' is written "~0" and '/' "~1".
    [[nodiscard]] std::string pointer() const
    {
        std::string text;
        for (usize i = 0; i < m_size; ++i)
        {
            text.push_back('/');
            const Segment& segment = m_segments[i];
            if (segment.isIndex)
            {
                text.append(std::to_string(segment.index));
                continue;
            }
            for (const char c : segment.name)
            {
                if (c == '~')
                {
                    text.append("~0");
                }
                else if (c == '/')
                {
                    text.append("~1");
                }
                else
                {
                    text.push_back(c);
                }
            }
        }
        return text;
    }

private:
    // Trivial, so that the array below is not initialized: only pushed segments are read.
    struct Segment
    {
        std::string_view name;
        usize index;
        bool isIndex;
    };

    std::array<Segment, kMaxDepth> m_segments;
    usize m_size = 0;
};

constexpr std::string_view kTooDeep = "is nested too deeply";

const FieldMeta kNoMeta{};

bool isFinite(std::span<const f32> values) noexcept
{
    for (const f32 value : values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

usize vectorSize(FieldType type) noexcept
{
    return type == FieldType::Vector2 ? 2 : type == FieldType::Vector3 ? 3 : 4;
}

template <class T>
const T& as(const void* value) noexcept
{
    return *static_cast<const T*>(value);
}

template <class T>
T& as(void* value) noexcept
{
    return *static_cast<T*>(value);
}

std::string decimal(u64 value)
{
    std::array<char, 24> buffer{};
    const auto end = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value).ptr;
    return std::string(buffer.data(), end);
}

// --- Writing -------------------------------------------------------------------------------------------

class Writer
{
public:
    Writer(JsonMutDocument& doc, const ValueWriteOptions& options) noexcept
        : m_doc(doc)
        , m_options(options)
    {
    }

    // Returns an absent value after recording the first problem.
    JsonMutValue object(const TypeReflection& type, const void* object)
    {
        JsonMutValue result = m_doc.makeObject();
        if (type.size == 0)
        {
            return result; // a tag
        }
        for (const FieldInfo& field : type.fields)
        {
            if (!m_path.push(field.name))
            {
                return fail(kTooDeep);
            }
            const JsonMutValue value = this->value(field, fieldData(object, field));
            if (value.isAbsent())
            {
                return {};
            }
            m_path.pop();
            result.add(field.name, value);
        }
        return result;
    }

    JsonMutValue value(const ValueType& type, const void* value)
    {
        switch (type.type)
        {
        case FieldType::Bool:
            return m_doc.makeBool(as<bool>(value));
        case FieldType::I8:
            return m_doc.makeI64(as<i8>(value));
        case FieldType::I16:
            return m_doc.makeI64(as<i16>(value));
        case FieldType::I32:
            return m_doc.makeI64(as<i32>(value));
        case FieldType::I64:
            return m_doc.makeI64(as<i64>(value));
        case FieldType::U8:
            return m_doc.makeU64(as<u8>(value));
        case FieldType::U16:
            return m_doc.makeU64(as<u16>(value));
        case FieldType::U32:
            return m_doc.makeU64(as<u32>(value));
        case FieldType::U64:
            return u64Value(as<u64>(value));
        case FieldType::F32:
        {
            const f32 number = as<f32>(value);
            return std::isfinite(number) ? m_doc.makeF32(number) : fail("is not finite");
        }
        case FieldType::F64:
        {
            const f64 number = as<f64>(value);
            return std::isfinite(number) ? m_doc.makeF64(number) : fail("is not finite");
        }
        case FieldType::Vector2:
        case FieldType::Vector3:
        case FieldType::Vector4:
            return floats(std::span(static_cast<const f32*>(value), vectorSize(type.type)));
        case FieldType::Quaternion:
        {
            // Written as [x, y, z, w], the order used by glTF.
            const Quat& q = as<Quat>(value);
            const f32 values[4] = {q.x, q.y, q.z, q.w};
            return floats(values);
        }
        case FieldType::String:
            return m_doc.makeString(as<std::string>(value));
        case FieldType::Entity:
            return entity(as<Entity>(value));
        case FieldType::Enum:
        {
            const i64 number = readEnumValue(value, type);
            for (const EnumValue& named : type.enumValues)
            {
                if (named.value == number)
                {
                    return m_doc.makeString(named.name);
                }
            }
            return m_doc.makeI64(number); // a value without a name is kept as a number
        }
        case FieldType::Flags:
            return flags(type, value);
        case FieldType::Struct:
            return object(*type.structType, value);
        case FieldType::FixedArray:
        {
            JsonMutValue array = m_doc.makeArray();
            for (u32 i = 0; i < type.count; ++i)
            {
                if (!element(
                        array, *type.element,
                        static_cast<const std::byte*>(value) + static_cast<usize>(i) * type.element->size, i))
                {
                    return {};
                }
            }
            return array;
        }
        case FieldType::Array:
        {
            JsonMutValue array = m_doc.makeArray();
            const usize size = type.container->size(value);
            for (usize i = 0; i < size; ++i)
            {
                if (!element(array, *type.element, type.container->constElement(value, i), i))
                {
                    return {};
                }
            }
            return array;
        }
        case FieldType::Optional:
            return type.container->size(value) == 0
                       ? m_doc.makeNull()
                       : this->value(*type.element, type.container->constElement(value, 0));
        case FieldType::Json:
        {
            const auto parsed = JsonDocument::parse(as<JsonText>(value).text);
            return parsed ? m_doc.copy(parsed->root()) : fail("holds text that is not valid JSON");
        }
        }
        return fail("has an unsupported type");
    }

    [[nodiscard]] ValueError takeError() noexcept { return std::move(m_error); }

private:
    JsonMutValue fail(std::string_view message)
    {
        if (!m_failed)
        {
            m_failed = true;
            m_error.pointer = m_path.pointer();
            m_error.message = message;
        }
        return {};
    }

    bool element(JsonMutValue& array, const ValueType& type, const void* value, usize index)
    {
        if (!m_path.push(index))
        {
            fail(kTooDeep);
            return false;
        }
        const JsonMutValue written = this->value(type, value);
        if (written.isAbsent())
        {
            return false;
        }
        m_path.pop();
        array.push(written);
        return true;
    }

    JsonMutValue floats(std::span<const f32> values)
    {
        if (!isFinite(values))
        {
            return fail("is not finite");
        }
        JsonMutValue array = m_doc.makeArray();
        for (const f32 value : values)
        {
            array.push(m_doc.makeF32(value));
        }
        return array;
    }

    JsonMutValue u64Value(u64 value)
    {
        return m_options.u64AsString ? m_doc.makeString(decimal(value)) : m_doc.makeU64(value);
    }

    JsonMutValue entity(Entity entity)
    {
        if (m_options.entities != nullptr)
        {
            return m_options.entities->writeEntity(m_doc, entity);
        }
        return entity.isNull() ? m_doc.makeNull() : u64Value(entity.toBits());
    }

    JsonMutValue flags(const ValueType& type, const void* value)
    {
        const u64 mask = type.enumSize >= 8 ? ~u64{0} : (u64{1} << (type.enumSize * 8)) - 1;
        u64 remaining = static_cast<u64>(readEnumValue(value, type)) & mask;
        JsonMutValue array = m_doc.makeArray();
        for (const EnumValue& named : type.enumValues)
        {
            const u64 bits = static_cast<u64>(named.value) & mask;
            if (bits != 0 && (remaining & bits) == bits)
            {
                array.push(m_doc.makeString(named.name));
                remaining &= ~bits;
            }
        }
        if (remaining != 0)
        {
            array.push(m_doc.makeU64(remaining)); // bits without a name are kept as a number
        }
        return array;
    }

    JsonMutDocument& m_doc;
    const ValueWriteOptions& m_options;
    ValuePath m_path;
    ValueError m_error;
    bool m_failed = false;
};

// --- Reading -------------------------------------------------------------------------------------------

bool hexDigit(char c, u32& out) noexcept
{
    if (c >= '0' && c <= '9')
    {
        out = static_cast<u32>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f')
    {
        out = static_cast<u32>(c - 'a' + 10);
        return true;
    }
    if (c >= 'A' && c <= 'F')
    {
        out = static_cast<u32>(c - 'A' + 10);
        return true;
    }
    return false;
}

f32 srgbToLinear(u32 byte) noexcept
{
    const f64 c = static_cast<f64>(byte) / 255.0;
    return static_cast<f32>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
}

// Parses "#rrggbb" (alpha 1) or "#rrggbbaa" (alpha linear) into @p out; @p count is 3 or 4.
bool parseColor(std::string_view text, f32* out, usize count) noexcept
{
    const bool withAlpha = text.size() == 9;
    if (text.empty() || text[0] != '#' || (text.size() != 7 && !withAlpha) || (withAlpha && count == 3))
    {
        return false;
    }
    u32 bytes[4] = {0, 0, 0, 255};
    for (usize i = 0; i < (text.size() - 1) / 2; ++i)
    {
        u32 high = 0;
        u32 low = 0;
        if (!hexDigit(text[1 + i * 2], high) || !hexDigit(text[2 + i * 2], low))
        {
            return false;
        }
        bytes[i] = high * 16 + low;
    }
    for (usize i = 0; i < 3; ++i)
    {
        out[i] = srgbToLinear(bytes[i]);
    }
    if (count == 4)
    {
        out[3] = static_cast<f32>(bytes[3]) / 255.0f;
    }
    return true;
}

class Reader
{
public:
    Reader(const ValueReadOptions& options, ValueReadResult& result) noexcept
        : m_options(options)
        , m_result(result)
    {
    }

    // Reads the members of @p json into @p object, which already holds a value (merging).
    bool object(const TypeReflection& type, void* object, JsonValue json)
    {
        if (!json.isObject())
        {
            return fail("must be an object");
        }
        bool ok = true;
        for (const JsonMember member : json.members())
        {
            const FieldInfo* field = type.fieldOrRenamed(member.key);
            if (!m_path.push(member.key))
            {
                return fail(std::string(kTooDeep));
            }
            if (field == nullptr)
            {
                m_result.unknownKeys.push_back(m_path.pointer());
            }
            else
            {
                ok = value(*field, fieldData(object, *field), member.value, field->meta) && ok;
            }
            m_path.pop();
        }
        return ok;
    }

    // Reads @p json into @p value, which already holds a value of @p type (merging into structs).
    bool value(const ValueType& type, void* value, JsonValue json, const FieldMeta& meta)
    {
        switch (type.type)
        {
        case FieldType::Bool:
        {
            const std::optional<bool> flag = json.asBool();
            if (!flag)
            {
                return fail("must be true or false");
            }
            as<bool>(value) = *flag;
            return true;
        }
        case FieldType::I8:
            return integer<i8>(json, value, meta);
        case FieldType::I16:
            return integer<i16>(json, value, meta);
        case FieldType::I32:
            return integer<i32>(json, value, meta);
        case FieldType::I64:
            return integer<i64>(json, value, meta);
        case FieldType::U8:
            return integer<u8>(json, value, meta);
        case FieldType::U16:
            return integer<u16>(json, value, meta);
        case FieldType::U32:
            return integer<u32>(json, value, meta);
        case FieldType::U64:
            return integer<u64>(json, value, meta);
        case FieldType::F32:
        {
            f64 number = 0.0;
            if (!scalar(json, meta, number) || !floatRange(number) || !metaRange(number, meta, true))
            {
                return false;
            }
            as<f32>(value) = static_cast<f32>(number);
            return true;
        }
        case FieldType::F64:
        {
            f64 number = 0.0;
            if (!scalar(json, meta, number) || !metaRange(number, meta))
            {
                return false;
            }
            as<f64>(value) = number;
            return true;
        }
        case FieldType::Vector2:
        case FieldType::Vector3:
        case FieldType::Vector4:
            return vector(json, static_cast<f32*>(value), vectorSize(type.type), meta);
        case FieldType::Quaternion:
        {
            f32 values[4] = {};
            if (!floats(json, values, 4, meta))
            {
                return false;
            }
            as<Quat>(value) = Quat(values[3], values[0], values[1], values[2]);
            return true;
        }
        case FieldType::String:
        {
            const std::optional<std::string_view> text = json.asString();
            if (!text)
            {
                return fail("must be a string");
            }
            as<std::string>(value).assign(*text);
            return true;
        }
        case FieldType::Entity:
            return entity(json, as<Entity>(value));
        case FieldType::Enum:
            return enumeration(type, value, json);
        case FieldType::Flags:
            return flags(type, value, json);
        case FieldType::Struct:
            return object(*type.structType, value, json);
        case FieldType::FixedArray:
        {
            if (!json.isArray() || json.size() != type.count)
            {
                return fail(std::format("must be an array of {} values", type.count));
            }
            bool ok = true;
            usize index = 0;
            for (const JsonValue element : json.elements())
            {
                ok = this->element(*type.element, static_cast<std::byte*>(value) + index * type.element->size,
                                   element, index, meta) &&
                     ok;
                ++index;
            }
            return ok;
        }
        case FieldType::Array:
        {
            if (!json.isArray())
            {
                return fail("must be an array");
            }
            type.container->resize(value, json.size());
            bool ok = true;
            usize index = 0;
            for (const JsonValue element : json.elements())
            {
                ok = this->element(*type.element, type.container->element(value, index), element, index,
                                   meta) &&
                     ok;
                ++index;
            }
            return ok;
        }
        case FieldType::Optional:
            if (json.isNull())
            {
                type.container->resize(value, 0);
                return true;
            }
            type.container->resize(value, 1);
            return this->value(*type.element, type.container->element(value, 0), json, meta);
        case FieldType::Json:
            if (!json.write(as<JsonText>(value).text, JsonWriteOptions{.pretty = false}))
            {
                return fail("must be a JSON value");
            }
            return true;
        }
        return fail("has an unsupported type");
    }

    // Reports a problem with the current value; always returns false.
    bool fail(std::string message)
    {
        m_result.errors.push_back(ValueError{m_path.pointer(), std::move(message)});
        return false;
    }

    ValuePath& path() noexcept { return m_path; }

private:
    bool element(const ValueType& type, void* value, JsonValue json, usize index, const FieldMeta& meta)
    {
        if (!m_path.push(index))
        {
            return fail(std::string(kTooDeep));
        }
        const bool ok = this->value(type, value, json, meta);
        m_path.pop();
        return ok;
    }

    template <class T>
    bool integer(JsonValue json, void* out, const FieldMeta& meta)
    {
        T number = 0;
        if (json.isString() && std::is_same_v<T, u64>)
        {
            // U64 values are written as decimal strings.
            const std::string_view text = *json.asString();
            u64 parsed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (text.empty() || error != std::errc() || end != text.data() + text.size())
            {
                return fail("must be an integer or a decimal string");
            }
            number = static_cast<T>(parsed);
        }
        else if (!json.isInteger())
        {
            return fail("must be an integer");
        }
        else if (const std::optional<i64> signedNumber = json.asI64())
        {
            if constexpr (std::is_unsigned_v<T>)
            {
                if (*signedNumber < 0)
                {
                    return fail("must not be negative");
                }
                if (static_cast<u64>(*signedNumber) > static_cast<u64>(std::numeric_limits<T>::max()))
                {
                    return fail("is out of range");
                }
            }
            else if (*signedNumber < static_cast<i64>(std::numeric_limits<T>::min()) ||
                     *signedNumber > static_cast<i64>(std::numeric_limits<T>::max()))
            {
                return fail("is out of range");
            }
            number = static_cast<T>(*signedNumber);
        }
        else
        {
            // Above the range of i64.
            if constexpr (!std::is_same_v<T, u64>)
            {
                return fail("is out of range");
            }
            else
            {
                number = *json.asU64();
            }
        }
        if (!metaRange(static_cast<f64>(number), meta))
        {
            return false;
        }
        *static_cast<T*>(out) = number;
        return true;
    }

    // A number, or a unit-tagged number such as {"deg": 60} converted to the field's unit.
    bool scalar(JsonValue json, const FieldMeta& meta, f64& out)
    {
        if (const std::optional<f64> number = json.asF64())
        {
            out = *number;
            return true;
        }
        if (!json.isObject() || json.size() != 1)
        {
            return fail("must be a number");
        }
        const JsonMember member = *json.members().begin();
        const std::optional<Unit> unit = unitFromSymbol(member.key);
        const std::optional<f64> number = member.value.asF64();
        if (!unit || !number)
        {
            return fail("must be a number or a unit-tagged number such as {\"deg\": 60}");
        }
        if (meta.unit == Unit::None)
        {
            return fail("has no unit; write a plain number");
        }
        const std::optional<f64> converted = convertUnit(*number, *unit, meta.unit);
        if (!converted)
        {
            return fail(std::format("cannot be given in {} (its unit is {})", unitSymbol(*unit),
                                    unitSymbol(meta.unit)));
        }
        out = *converted;
        return true;
    }

    // Converting a double outside the f32 range would be undefined behaviour.
    bool floatRange(f64 number)
    {
        if (std::abs(number) > static_cast<f64>(std::numeric_limits<f32>::max()))
        {
            return fail("is out of range");
        }
        return true;
    }

    // f32 values are compared with the bounds rounded to f32, so that a bound written as an f32 constant
    // accepts that constant after a round trip through JSON.
    bool metaRange(f64 number, const FieldMeta& meta, bool asF32 = false)
    {
        const auto bound = [asF32](f64 value) {
            return asF32 ? static_cast<f64>(static_cast<f32>(value)) : value;
        };
        const f64 value = asF32 ? static_cast<f64>(static_cast<f32>(number)) : number;
        if (meta.min && value < bound(*meta.min))
        {
            return fail(std::format("must be at least {}", *meta.min));
        }
        if (meta.max && value > bound(*meta.max))
        {
            return fail(std::format("must be at most {}", *meta.max));
        }
        return true;
    }

    bool floats(JsonValue json, f32* out, usize count, const FieldMeta& meta)
    {
        const auto wrongShape = [&] {
            return fail(std::format("must be an array of {} numbers", count));
        };
        if (!json.isArray() || json.size() != count)
        {
            return wrongShape();
        }
        usize index = 0;
        for (const JsonValue element : json.elements())
        {
            if (!m_path.push(index))
            {
                return fail(std::string(kTooDeep));
            }
            const std::optional<f64> number = element.asF64();
            const bool ok = number.has_value() ? floatRange(*number) && metaRange(*number, meta, true)
                                               : fail("must be a number");
            m_path.pop();
            if (!ok)
            {
                return false;
            }
            out[index++] = static_cast<f32>(*number);
        }
        return true;
    }

    bool vector(JsonValue json, f32* out, usize count, const FieldMeta& meta)
    {
        const bool color = meta.hint == FieldHint::Color || meta.hint == FieldHint::HdrColor;
        if (color && json.isString())
        {
            f32 values[4] = {};
            if (count == 2 || !parseColor(*json.asString(), values, count))
            {
                return fail(count == 4 ? "must be a colour such as \"#ff8000\" or \"#ff800080\""
                                       : "must be a colour such as \"#ff8000\"");
            }
            for (usize i = 0; i < count; ++i)
            {
                if (!metaRange(static_cast<f64>(values[i]), meta, true))
                {
                    return false;
                }
            }
            std::copy_n(values, count, out);
            return true;
        }
        f32 values[4] = {};
        if (!floats(json, values, count, meta))
        {
            return false;
        }
        std::copy_n(values, count, out);
        return true;
    }

    bool entity(JsonValue json, Entity& out)
    {
        if (m_options.entities != nullptr)
        {
            Expected<Entity, std::string> resolved = m_options.entities->readEntity(json);
            if (!resolved)
            {
                return fail(std::move(resolved.error()));
            }
            out = *resolved;
            return true;
        }
        if (json.isNull())
        {
            out = kNullEntity;
            return true;
        }
        u64 bits = 0;
        if (json.isString())
        {
            const std::string_view text = *json.asString();
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), bits);
            if (text.empty() || error != std::errc() || end != text.data() + text.size())
            {
                return fail("must be null or an entity handle");
            }
        }
        else if (const std::optional<u64> number = json.asU64())
        {
            bits = *number;
        }
        else
        {
            return fail("must be null or an entity handle");
        }
        out = Entity::fromBits(bits);
        return true;
    }

    bool enumeration(const ValueType& type, void* value, JsonValue json)
    {
        if (const std::optional<std::string_view> name = json.asString())
        {
            for (const EnumValue& named : type.enumValues)
            {
                if (named.name == *name)
                {
                    writeEnumValue(value, type, named.value);
                    return true;
                }
            }
            return fail(std::format("has unknown value '{}'", *name));
        }
        if (const std::optional<i64> number = json.asI64())
        {
            writeEnumValue(value, type, *number);
            return true;
        }
        if (const std::optional<u64> number = json.asU64())
        {
            writeEnumValue(value, type, static_cast<i64>(*number));
            return true;
        }
        return fail("must be a value name");
    }

    bool flags(const ValueType& type, void* value, JsonValue json)
    {
        if (const std::optional<u64> number = json.asU64())
        {
            writeEnumValue(value, type, static_cast<i64>(*number));
            return true;
        }
        if (!json.isArray())
        {
            return fail("must be an array of value names");
        }
        u64 bits = 0;
        usize index = 0;
        for (const JsonValue element : json.elements())
        {
            if (const std::optional<u64> number = element.asU64())
            {
                bits |= *number;
            }
            else if (const std::optional<std::string_view> name = element.asString())
            {
                const EnumValue* found = nullptr;
                for (const EnumValue& named : type.enumValues)
                {
                    if (named.name == *name)
                    {
                        found = &named;
                        break;
                    }
                }
                if (found == nullptr)
                {
                    const bool pushed = m_path.push(index);
                    fail(std::format("has unknown value '{}'", *name));
                    if (pushed)
                    {
                        m_path.pop();
                    }
                    return false;
                }
                bits |= static_cast<u64>(found->value);
            }
            else
            {
                return fail("must be an array of value names");
            }
            ++index;
        }
        writeEnumValue(value, type, static_cast<i64>(bits));
        return true;
    }

    const ValueReadOptions& m_options;
    ValueReadResult& m_result;
    ValuePath m_path;
};

// Storage for a temporary value of a reflected type.
class ScratchValue
{
public:
    ScratchValue(u32 size, u32 alignment)
        : m_alignment(alignment)
        , m_data(::operator new(size, std::align_val_t{alignment}))
    {
    }

    ~ScratchValue() { ::operator delete(m_data, std::align_val_t{m_alignment}); }

    ScratchValue(const ScratchValue&) = delete;
    ScratchValue& operator=(const ScratchValue&) = delete;

    [[nodiscard]] void* data() const noexcept { return m_data; }

private:
    u32 m_alignment;
    void* m_data;
};

bool needsMigration(const TypeReflection& type, u32 savedVersion) noexcept
{
    for (const Migration& migration : type.migrations)
    {
        if (migration.fromVersion >= savedVersion)
        {
            return true;
        }
    }
    for (const FieldInfo& field : type.fields)
    {
        if (!field.meta.renamedFrom.empty())
        {
            return true;
        }
    }
    return false;
}

// Renames fields (FieldMeta::renamedFrom) and runs the migration steps from @p savedVersion on, in order.
Expected<JsonDocument, JsonError> migrate(const TypeReflection& type, JsonValue json, u32 savedVersion)
{
    JsonMutDocument doc;
    JsonMutValue object = doc.copy(json);
    doc.setRoot(object);
    for (const FieldInfo& field : type.fields)
    {
        const std::string_view oldName = field.meta.renamedFrom;
        if (!oldName.empty() && !json.find(oldName).isAbsent() && json.find(field.name).isAbsent())
        {
            object.remove(oldName);
            object.add(field.name, doc.copy(json.find(oldName)));
        }
    }
    for (u32 version = savedVersion; version < type.version; ++version)
    {
        for (const Migration& migration : type.migrations)
        {
            if (migration.fromVersion == version)
            {
                migration.migrate(doc, object);
            }
        }
    }
    std::string text;
    if (auto written = doc.write(text, JsonWriteOptions{.pretty = false}); !written)
    {
        return unexpected(written.error());
    }
    return JsonDocument::parse(text);
}

struct Location
{
    const ValueType* type = nullptr; // null: the whole object
    void* data = nullptr;
    const FieldMeta* meta = &kNoMeta;
};

// Walks @p path from the object at @p object; pushes the segments it passes to @p walked.
Expected<Location, ValueError> resolve(const TypeReflection& root, void* object, std::string_view path,
                                       ValuePath& walked)
{
    Location location{nullptr, object, &kNoMeta};
    const auto error = [&](std::string message) {
        return Expected<Location, ValueError>(unexpected(ValueError{walked.pointer(), std::move(message)}));
    };
    usize start = 0;
    while (start <= path.size() && !path.empty())
    {
        const usize dot = path.find('.', start);
        const std::string_view segment =
            path.substr(start, dot == std::string_view::npos ? path.size() - start : dot - start);
        start = dot == std::string_view::npos ? path.size() + 1 : dot + 1;

        // Optional values are looked through.
        while (location.type != nullptr && location.type->type == FieldType::Optional)
        {
            if (location.type->container->size(location.data) == 0)
            {
                return error("is empty");
            }
            location.data = location.type->container->element(location.data, 0);
            location.type = location.type->element;
        }

        const TypeReflection* structType = location.type == nullptr ? &root
                                           : location.type->type == FieldType::Struct
                                               ? location.type->structType
                                               : nullptr;
        if (segment.empty())
        {
            return error("has an empty path segment");
        }
        if (structType != nullptr)
        {
            const FieldInfo* field = structType->field(segment);
            if (field == nullptr || !walked.push(segment))
            {
                return error(std::format("has no field '{}'", segment));
            }
            location = Location{field, fieldData(location.data, *field), &field->meta};
            continue;
        }
        const FieldType kind = location.type->type;
        if (kind != FieldType::FixedArray && kind != FieldType::Array)
        {
            return error(std::format("has no field '{}'", segment));
        }
        usize index = 0;
        const auto [end, parseError] =
            std::from_chars(segment.data(), segment.data() + segment.size(), index);
        if (parseError != std::errc() || end != segment.data() + segment.size())
        {
            return error(std::format("is an array; '{}' is not an index", segment));
        }
        const usize size = kind == FieldType::FixedArray ? location.type->count
                                                         : location.type->container->size(location.data);
        if (index >= size || !walked.push(index))
        {
            return error(std::format("has no element {} (size {})", index, size));
        }
        location.data = kind == FieldType::FixedArray
                            ? static_cast<std::byte*>(location.data) + index * location.type->element->size
                            : location.type->container->element(location.data, index);
        location.type = location.type->element;
    }
    return location;
}

} // namespace

Expected<JsonMutValue, ValueError> writeValue(JsonMutDocument& doc, const TypeReflection& type,
                                              const void* object, const ValueWriteOptions& options)
{
    ILL_PROFILE_SCOPE();
    Writer writer(doc, options);
    const JsonMutValue value = writer.object(type, object);
    if (value.isAbsent())
    {
        return unexpected(writer.takeError());
    }
    return value;
}

ValueReadResult readValue(const TypeReflection& type, void* object, JsonValue json,
                          const ValueReadOptions& options)
{
    ILL_PROFILE_SCOPE();
    ValueReadResult result;
    Reader reader(options, result);
    if (options.savedVersion && *options.savedVersion > type.version)
    {
        result.warnings.push_back(std::format("'{}' was saved with version {}, newer than version {}; fields "
                                              "this version does not know are skipped",
                                              type.name, *options.savedVersion, type.version));
    }

    JsonDocument migrated;
    if (options.savedVersion && *options.savedVersion < type.version && json.isObject() &&
        needsMigration(type, *options.savedVersion))
    {
        Expected<JsonDocument, JsonError> converted = migrate(type, json, *options.savedVersion);
        if (!converted)
        {
            reader.fail(std::format("could not be migrated: {}", converted.error().message));
            return result;
        }
        migrated = std::move(*converted);
        json = migrated.root();
    }

    if (type.size == 0)
    {
        reader.object(type, nullptr, json); // a tag: only checks the shape
        return result;
    }
    if (!options.atomic)
    {
        if (options.mode == JsonReadMode::Replace)
        {
            type.ops->destroy(object);
            type.ops->construct(object);
        }
        reader.object(type, object, json);
        return result;
    }

    const ScratchValue scratch(type.size, type.alignment);
    if (options.mode == JsonReadMode::Replace)
    {
        type.ops->construct(scratch.data());
    }
    else
    {
        type.ops->copy(scratch.data(), object);
    }
    if (reader.object(type, scratch.data(), json))
    {
        type.ops->assign(object, scratch.data());
    }
    type.ops->destroy(scratch.data());
    return result;
}

Expected<JsonMutValue, ValueError> getPath(JsonMutDocument& doc, const TypeReflection& type,
                                           const void* object, std::string_view path,
                                           const ValueWriteOptions& options)
{
    ValuePath walked;
    // resolve() does not modify the object; it needs a mutable pointer only to share code with setPath().
    Expected<Location, ValueError> location = resolve(type, const_cast<void*>(object), path, walked);
    if (!location)
    {
        return unexpected(std::move(location.error()));
    }
    if (location->type == nullptr)
    {
        return writeValue(doc, type, object, options);
    }
    Writer writer(doc, options);
    const JsonMutValue value = writer.value(*location->type, location->data);
    if (value.isAbsent())
    {
        ValueError error = writer.takeError();
        error.pointer = walked.pointer() + error.pointer;
        return unexpected(std::move(error));
    }
    return value;
}

Expected<void, ValueError> setPath(const TypeReflection& type, void* object, std::string_view path,
                                   JsonValue json, const ValueReadOptions& options)
{
    ValuePath walked;
    Expected<Location, ValueError> location = resolve(type, object, path, walked);
    if (!location)
    {
        return unexpected(std::move(location.error()));
    }
    if (location->type == nullptr)
    {
        ValueReadResult result = readValue(type, object, json, options);
        if (!result.ok())
        {
            return unexpected(std::move(result.errors.front()));
        }
        return {};
    }
    const ValueType& valueType = *location->type;
    FieldValue scratch =
        options.mode == JsonReadMode::Replace ? FieldValue(valueType) : FieldValue(valueType, location->data);
    ValueReadResult result;
    Reader reader(options, result);
    if (!reader.value(valueType, scratch.data(), json, *location->meta))
    {
        ValueError error = std::move(result.errors.front());
        error.pointer = walked.pointer() + error.pointer;
        return unexpected(std::move(error));
    }
    valueType.ops->assign(location->data, scratch.data());
    return {};
}

} // namespace illusion

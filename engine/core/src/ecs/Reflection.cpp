#include <illusion/core/Assert.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>

#include <cstring>
#include <numbers>

namespace illusion
{

std::string_view fieldTypeName(FieldType type) noexcept
{
    switch (type)
    {
    case FieldType::Bool:
        return "Bool";
    case FieldType::I32:
        return "I32";
    case FieldType::U32:
        return "U32";
    case FieldType::I64:
        return "I64";
    case FieldType::U64:
        return "U64";
    case FieldType::F32:
        return "F32";
    case FieldType::F64:
        return "F64";
    case FieldType::Vector2:
        return "Vector2";
    case FieldType::Vector3:
        return "Vector3";
    case FieldType::Vector4:
        return "Vector4";
    case FieldType::Quaternion:
        return "Quaternion";
    case FieldType::String:
        return "String";
    case FieldType::Entity:
        return "Entity";
    case FieldType::Enum:
        return "Enum";
    case FieldType::I8:
        return "I8";
    case FieldType::I16:
        return "I16";
    case FieldType::U8:
        return "U8";
    case FieldType::U16:
        return "U16";
    case FieldType::Struct:
        return "Struct";
    case FieldType::FixedArray:
        return "FixedArray";
    case FieldType::Array:
        return "Array";
    case FieldType::Flags:
        return "Flags";
    case FieldType::Optional:
        return "Optional";
    case FieldType::Json:
        return "Json";
    }
    return "Unknown";
}

namespace
{

constexpr std::string_view kUnitSymbols[] = {"",   "m", "rad", "deg",   "s", "kg",
                                             "Hz", "N", "m/s", "ratio", "%", "dB"};

// Quantities: units convert only within one. Each unit's factor turns it into the first unit listed.
enum class Quantity : u8
{
    None,
    Other,
    Angle,
    Ratio,
};

Quantity quantityOf(Unit unit) noexcept
{
    switch (unit)
    {
    case Unit::None:
        return Quantity::None;
    case Unit::Radians:
    case Unit::Degrees:
        return Quantity::Angle;
    case Unit::Ratio:
    case Unit::Percent:
        return Quantity::Ratio;
    default:
        return Quantity::Other;
    }
}

f64 factorOf(Unit unit) noexcept
{
    switch (unit)
    {
    case Unit::Degrees:
        return std::numbers::pi / 180.0;
    case Unit::Percent:
        return 0.01;
    default:
        return 1.0;
    }
}

} // namespace

std::string_view unitSymbol(Unit unit) noexcept
{
    const auto index = static_cast<usize>(unit);
    return index < std::size(kUnitSymbols) ? kUnitSymbols[index] : std::string_view();
}

std::optional<Unit> unitFromSymbol(std::string_view symbol) noexcept
{
    for (usize i = 1; i < std::size(kUnitSymbols); ++i)
    {
        if (kUnitSymbols[i] == symbol)
        {
            return static_cast<Unit>(i);
        }
    }
    return std::nullopt;
}

std::optional<f64> convertUnit(f64 value, Unit from, Unit to) noexcept
{
    if (from == to)
    {
        return value;
    }
    const Quantity quantity = quantityOf(from);
    if (quantity == Quantity::None || quantity == Quantity::Other || quantity != quantityOf(to))
    {
        return std::nullopt;
    }
    return value * factorOf(from) / factorOf(to);
}

std::string_view fieldHintName(FieldHint hint) noexcept
{
    switch (hint)
    {
    case FieldHint::None:
        return "None";
    case FieldHint::Color:
        return "Color";
    case FieldHint::HdrColor:
        return "HdrColor";
    case FieldHint::Angle:
        return "Angle";
    case FieldHint::Slider:
        return "Slider";
    case FieldHint::LogSlider:
        return "LogSlider";
    case FieldHint::Multiline:
        return "Multiline";
    case FieldHint::Layer:
        return "Layer";
    case FieldHint::LayerMask:
        return "LayerMask";
    }
    return "Unknown";
}

const FieldInfo* TypeReflection::field(std::string_view fieldName) const noexcept
{
    for (const FieldInfo& info : fields)
    {
        if (info.name == fieldName)
        {
            return &info;
        }
    }
    return nullptr;
}

const FieldInfo* TypeReflection::fieldOrRenamed(std::string_view fieldName) const noexcept
{
    if (const FieldInfo* info = field(fieldName))
    {
        return info;
    }
    for (const FieldInfo& info : fields)
    {
        if (!info.meta.renamedFrom.empty() && info.meta.renamedFrom == fieldName)
        {
            return &info;
        }
    }
    return nullptr;
}

namespace
{

// "castShadows" -> "Cast shadows", "fovY" -> "Fov Y", "maxSlope2" -> "Max slope 2".
std::string splitWords(std::string_view name)
{
    std::string words;
    words.reserve(name.size() + 4);
    for (usize i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        const bool upper = c >= 'A' && c <= 'Z';
        const bool digit = c >= '0' && c <= '9';
        const char previous = i > 0 ? name[i - 1] : '\0';
        const bool previousLower = previous >= 'a' && previous <= 'z';
        const bool previousDigit = previous >= '0' && previous <= '9';
        if (c == '_')
        {
            if (!words.empty() && words.back() != ' ')
            {
                words.push_back(' ');
            }
            continue;
        }
        if (!words.empty() && words.back() != ' ' &&
            ((upper && (previousLower || previousDigit)) || (digit && !previousDigit)))
        {
            words.push_back(' ');
        }
        if (words.empty())
        {
            words.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
        }
        else
        {
            // Keep acronyms ("HDR") and single capitals that end the name ("fovY") as they are.
            const char next = i + 1 < name.size() ? name[i + 1] : '\0';
            const bool nextLower = next >= 'a' && next <= 'z';
            const bool lowerIt = upper && previousLower && nextLower;
            words.push_back(lowerIt ? static_cast<char>(c - 'A' + 'a') : c);
        }
    }
    return words;
}

} // namespace

std::string displayNameOf(const FieldInfo& field)
{
    return field.meta.displayName.empty() ? splitWords(field.name) : std::string(field.meta.displayName);
}

std::string displayNameOf(const TypeReflection& type)
{
    if (!type.meta.displayName.empty())
    {
        return std::string(type.meta.displayName);
    }
    const usize dot = type.name.rfind('.');
    return splitWords(dot == std::string_view::npos ? type.name : type.name.substr(dot + 1));
}

i64 readEnumValue(const void* value, const ValueType& type) noexcept
{
    switch (type.enumSize)
    {
    case 1:
    {
        u8 number = 0;
        std::memcpy(&number, value, 1);
        return type.enumSigned ? static_cast<i64>(static_cast<i8>(number)) : static_cast<i64>(number);
    }
    case 2:
    {
        u16 number = 0;
        std::memcpy(&number, value, 2);
        return type.enumSigned ? static_cast<i64>(static_cast<i16>(number)) : static_cast<i64>(number);
    }
    case 4:
    {
        u32 number = 0;
        std::memcpy(&number, value, 4);
        return type.enumSigned ? static_cast<i64>(static_cast<i32>(number)) : static_cast<i64>(number);
    }
    default:
    {
        i64 number = 0;
        std::memcpy(&number, value, 8);
        return number;
    }
    }
}

void writeEnumValue(void* value, const ValueType& type, i64 number) noexcept
{
    switch (type.enumSize)
    {
    case 1:
    {
        const auto narrow = static_cast<u8>(number);
        std::memcpy(value, &narrow, 1);
        break;
    }
    case 2:
    {
        const auto narrow = static_cast<u16>(number);
        std::memcpy(value, &narrow, 2);
        break;
    }
    case 4:
    {
        const auto narrow = static_cast<u32>(number);
        std::memcpy(value, &narrow, 4);
        break;
    }
    default:
        std::memcpy(value, &number, 8);
        break;
    }
}

i64 readEnumField(const void* object, const FieldInfo& field) noexcept
{
    return readEnumValue(fieldData(object, field), field);
}

void writeEnumField(void* object, const FieldInfo& field, i64 value) noexcept
{
    writeEnumValue(fieldData(object, field), field, value);
}

namespace
{

template <class T>
bool equalAs(const void* a, const void* b) noexcept
{
    return *static_cast<const T*>(a) == *static_cast<const T*>(b);
}

} // namespace

bool valuesEqual(const ValueType& type, const void* a, const void* b) noexcept
{
    switch (type.type)
    {
    case FieldType::Bool:
        return equalAs<bool>(a, b);
    case FieldType::I8:
        return equalAs<i8>(a, b);
    case FieldType::I16:
        return equalAs<i16>(a, b);
    case FieldType::I32:
        return equalAs<i32>(a, b);
    case FieldType::I64:
        return equalAs<i64>(a, b);
    case FieldType::U8:
        return equalAs<u8>(a, b);
    case FieldType::U16:
        return equalAs<u16>(a, b);
    case FieldType::U32:
        return equalAs<u32>(a, b);
    case FieldType::U64:
        return equalAs<u64>(a, b);
    case FieldType::F32:
        return equalAs<f32>(a, b);
    case FieldType::F64:
        return equalAs<f64>(a, b);
    case FieldType::Vector2:
        return equalAs<Vec2>(a, b);
    case FieldType::Vector3:
        return equalAs<Vec3>(a, b);
    case FieldType::Vector4:
        return equalAs<Vec4>(a, b);
    case FieldType::Quaternion:
        return equalAs<Quat>(a, b);
    case FieldType::String:
        return equalAs<std::string>(a, b);
    case FieldType::Entity:
        return equalAs<Entity>(a, b);
    case FieldType::Json:
        return equalAs<JsonText>(a, b);
    case FieldType::Enum:
    case FieldType::Flags:
        return readEnumValue(a, type) == readEnumValue(b, type);
    case FieldType::Struct:
        for (const FieldInfo& field : type.structType->fields)
        {
            if (!valuesEqual(field, fieldData(a, field), fieldData(b, field)))
            {
                return false;
            }
        }
        return true;
    case FieldType::FixedArray:
        for (u32 i = 0; i < type.count; ++i)
        {
            const usize offset = static_cast<usize>(i) * type.element->size;
            if (!valuesEqual(*type.element, static_cast<const std::byte*>(a) + offset,
                             static_cast<const std::byte*>(b) + offset))
            {
                return false;
            }
        }
        return true;
    case FieldType::Array:
    case FieldType::Optional:
    {
        const usize size = type.container->size(a);
        if (size != type.container->size(b))
        {
            return false;
        }
        for (usize i = 0; i < size; ++i)
        {
            if (!valuesEqual(*type.element, type.container->constElement(a, i),
                             type.container->constElement(b, i)))
            {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

FieldValue::FieldValue(const ValueType& type)
{
    type.ops->construct(allocate(type));
    m_type = &type;
}

FieldValue::FieldValue(const ValueType& type, const void* source)
{
    type.ops->copy(allocate(type), source);
    m_type = &type;
}

FieldValue::FieldValue(const FieldValue& other)
{
    if (other.m_type != nullptr)
    {
        other.m_type->ops->copy(allocate(*other.m_type), other.m_data);
        m_type = other.m_type;
    }
}

FieldValue::FieldValue(FieldValue&& other) noexcept
{
    takeFrom(other);
}

FieldValue& FieldValue::operator=(const FieldValue& other)
{
    if (this == &other)
    {
        return *this;
    }
    if (m_type != nullptr && other.m_type != nullptr && m_type->typeHash == other.m_type->typeHash)
    {
        m_type->ops->assign(m_data, other.m_data);
        return *this;
    }
    FieldValue copy(other);
    reset();
    takeFrom(copy);
    return *this;
}

FieldValue& FieldValue::operator=(FieldValue&& other) noexcept
{
    if (this != &other)
    {
        reset();
        takeFrom(other);
    }
    return *this;
}

FieldValue::~FieldValue()
{
    reset();
}

void* FieldValue::allocate(const ValueType& type)
{
    if (type.size <= kInlineSize && type.alignment <= kInlineAlignment)
    {
        m_data = m_inline;
    }
    else
    {
        m_data = ::operator new(type.size, std::align_val_t{type.alignment});
    }
    return m_data;
}

void FieldValue::reset() noexcept
{
    if (m_type == nullptr)
    {
        return;
    }
    m_type->ops->destroy(m_data);
    if (m_data != m_inline)
    {
        ::operator delete(m_data, std::align_val_t{m_type->alignment});
    }
    m_type = nullptr;
    m_data = nullptr;
}

void FieldValue::takeFrom(FieldValue& other) noexcept
{
    if (other.m_type == nullptr)
    {
        return;
    }
    if (other.m_data == other.m_inline)
    {
        m_data = m_inline;
        other.m_type->ops->relocate(m_data, other.m_data);
    }
    else
    {
        m_data = other.m_data; // take the heap block
    }
    m_type = other.m_type;
    other.m_type = nullptr;
    other.m_data = nullptr;
}

bool operator==(const FieldValue& a, const FieldValue& b) noexcept
{
    if (a.m_type == nullptr || b.m_type == nullptr)
    {
        return a.m_type == b.m_type;
    }
    return a.m_type->typeHash == b.m_type->typeHash && valuesEqual(*a.m_type, a.m_data, b.m_data);
}

FieldValue readField(const void* object, const FieldInfo& field)
{
    return FieldValue(field, fieldData(object, field));
}

void writeField(void* object, const FieldInfo& field, const FieldValue& value)
{
    ILL_ASSERT(value.type() != nullptr && value.type()->typeHash == field.typeHash,
               "writeField() of '{}' with a value of another type", field.name);
    field.ops->assign(fieldData(object, field), value.data());
}

bool fieldsEqual(const void* a, const void* b, const FieldInfo& field) noexcept
{
    return valuesEqual(field, fieldData(a, field), fieldData(b, field));
}

FieldValue defaultValue(const TypeReflection& type, const FieldInfo& field)
{
    ILL_ASSERT(type.size > 0 && type.ops != nullptr, "defaultValue() of a field of the tag '{}'", type.name);
    void* object = ::operator new(type.size, std::align_val_t{type.alignment});
    type.ops->construct(object);
    FieldValue value(field, fieldData(object, field));
    type.ops->destroy(object);
    ::operator delete(object, std::align_val_t{type.alignment});
    return value;
}

namespace
{

std::string_view validateEntityName(const void* value) noexcept
{
    return entityNameError(*static_cast<const std::string*>(value));
}

constexpr std::string_view kRequiresHierarchy[] = {"illusion.Hierarchy"};

} // namespace

void reflectCoreComponents(ComponentRegistry& registry)
{
    reflect<Transform>("illusion.Transform", 1, registry)
        .meta({.displayName = "Transform",
               .category = "Core",
               .icon = "open_with",
               .description = "Position, rotation and scale of the Entity in the world.",
               .docsAnchor = "manual/ecs.md#components"})
        .field("position", &Transform::position, {.unit = Unit::Meters})
        .field("rotation", &Transform::rotation)
        .field("scale", &Transform::scale, {.step = 0.01});
    reflect<EntityGuid>("illusion.EntityGuid", 1, registry)
        .meta({.displayName = "Entity id",
               .category = "Core",
               .icon = "fingerprint",
               .description = "Persistent id of the Entity, saved as its id in scene files.",
               .docsAnchor = "manual/scenes.md#entity-ids"})
        .field("value", &EntityGuid::value, {.readOnly = true});
    reflect<Name>("illusion.Name", 1, registry)
        .meta({.displayName = "Name",
               .category = "Core",
               .icon = "label",
               .description = "Name of the Entity, shown in the editor.",
               .docsAnchor = "manual/scenes.md#names"})
        .field("value", &Name::value, {.validate = validateEntityName});
    reflect<Hierarchy>("illusion.Hierarchy", 1, registry)
        .meta({.displayName = "Hierarchy",
               .category = "Core",
               .icon = "account_tree",
               .description = "Parent of the Entity and its position among its siblings.",
               .docsAnchor = "manual/ecs.md#transform-hierarchy"})
        .field("parent", &Hierarchy::parent, {.readOnly = true})
        .field("order", &Hierarchy::order, {.readOnly = true});
    reflect<LocalTransform>("illusion.LocalTransform", 1, registry)
        .meta({.displayName = "Local transform",
               .category = "Core",
               .icon = "control_camera",
               .description = "Position, rotation and scale of the Entity relative to its parent.",
               .docsAnchor = "manual/ecs.md#transform-hierarchy",
               .requiredComponents = kRequiresHierarchy})
        .field("position", &LocalTransform::position, {.unit = Unit::Meters})
        .field("rotation", &LocalTransform::rotation)
        .field("scale", &LocalTransform::scale, {.step = 0.01});
    reflect<Disabled>("illusion.Disabled", 1, registry)
        .meta({.displayName = "Disabled",
               .category = "Core",
               .icon = "visibility_off",
               .description = "Switches the Entity and its descendants off.",
               .docsAnchor = "manual/ecs.md#disabled-and-inactive"});
    reflectRuntime<Inactive>(
        "illusion.Inactive",
        {.displayName = "Inactive",
         .category = "Core",
         .icon = "block",
         .description = "The Entity or one of its ancestors is Disabled (runtime state).",
         .docsAnchor = "manual/ecs.md#disabled-and-inactive"},
        registry);
    reflectRuntime<WorldSpaceAuthority>(
        "illusion.WorldSpaceAuthority",
        {.displayName = "World-space authority",
         .category = "Core",
         .icon = "public",
         .description =
             "A System writes the Transform in world space; the LocalTransform follows it (runtime state).",
         .docsAnchor = "manual/ecs.md#transform-hierarchy"},
        registry);
    reflectRuntime<UnknownComponents>("illusion.UnknownComponents",
                                      {.displayName = "Unknown components",
                                       .category = "Core",
                                       .icon = "help",
                                       .description = "Components of types that were not registered when the "
                                                      "scene was loaded, saved again unchanged.",
                                       .docsAnchor = "manual/scenes.md#unknown-types"},
                                      registry);
}

} // namespace illusion

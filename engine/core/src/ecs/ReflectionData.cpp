#include "ReflectionData.hpp"

#include <illusion/core/Assert.hpp>

namespace illusion::detail
{
namespace
{

bool isValidFieldName(std::string_view name) noexcept
{
    if (name.empty() || (name[0] >= '0' && name[0] <= '9'))
    {
        return false;
    }
    for (const char c : name)
    {
        const bool valid =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!valid)
        {
            return false;
        }
    }
    return true;
}

bool isInteger(FieldType type) noexcept
{
    switch (type)
    {
    case FieldType::I8:
    case FieldType::I16:
    case FieldType::I32:
    case FieldType::I64:
    case FieldType::U8:
    case FieldType::U16:
    case FieldType::U32:
    case FieldType::U64:
        return true;
    default:
        return false;
    }
}

bool isUnsigned(FieldType type) noexcept
{
    return type == FieldType::U8 || type == FieldType::U16 || type == FieldType::U32 ||
           type == FieldType::U64;
}

// The type that metadata such as units and ranges describes: arrays and optionals pass it to their
// elements.
const ValueType& leafOf(const ValueType& type) noexcept
{
    const ValueType* leaf = &type;
    while (leaf->type == FieldType::FixedArray || leaf->type == FieldType::Array ||
           leaf->type == FieldType::Optional)
    {
        leaf = leaf->element;
    }
    return *leaf;
}

} // namespace

bool isValidStableName(std::string_view name) noexcept
{
    if (name.empty())
    {
        return false;
    }
    for (const char c : name)
    {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                           c == '_' || c == '.';
        if (!valid)
        {
            return false;
        }
    }
    return true;
}

ReflectionData::ReflectionData(std::string_view name, u32 version, const TypeLayout& layout)
    : m_name(name)
{
    m_view.name = m_name;
    m_view.version = version;
    m_view.size = layout.size;
    m_view.alignment = layout.alignment;
    m_view.typeHash = layout.typeHash;
    m_view.ops = layout.ops;
}

std::string_view ReflectionData::intern(std::string_view text)
{
    return text.empty() ? std::string_view() : std::string_view(m_strings.emplace_back(text));
}

ValueType ReflectionData::copyType(const ValueType& type, StructResolver resolve, const void* context)
{
    ValueType copy = type;
    if (type.type == FieldType::Enum || type.type == FieldType::Flags)
    {
        std::vector<EnumValue>& values = m_enums.emplace_back();
        values.reserve(type.enumValues.size());
        for (const EnumValue& value : type.enumValues)
        {
            values.push_back(EnumValue{intern(value.name), value.value});
        }
        copy.enumValues = values;
    }
    if (type.type == FieldType::Struct)
    {
        copy.structType = resolve(context, type.typeHash);
        ILL_CHECK(copy.structType != nullptr,
                  "a field of '{}' has a struct type that is not reflected; call reflectStruct() first",
                  m_name);
    }
    if (type.element != nullptr)
    {
        copy.element = &m_types.emplace_back(copyType(*type.element, resolve, context));
    }
    return copy;
}

void ReflectionData::checkField(const FieldInfo& field) const
{
    ILL_ASSERT(isValidFieldName(field.name), "invalid field name '{}' in '{}'", field.name, m_name);
    ILL_ASSERT(m_view.fieldOrRenamed(field.name) == nullptr,
               "field '{}' reflected twice in '{}' (or another field was renamed from it)", field.name,
               m_name);
    ILL_ASSERT(m_view.size > 0 && field.offset + field.size <= m_view.size, "field '{}' lies outside '{}'",
               field.name, m_name);

    const FieldMeta& meta = field.meta;
    if (!meta.renamedFrom.empty())
    {
        ILL_ASSERT(isValidFieldName(meta.renamedFrom) && meta.renamedFrom != field.name,
                   "field '{}' of '{}' has an invalid renamedFrom '{}'", field.name, m_name,
                   meta.renamedFrom);
        ILL_ASSERT(m_view.fieldOrRenamed(meta.renamedFrom) == nullptr,
                   "renamedFrom '{}' of field '{}' in '{}' is the name of another field", meta.renamedFrom,
                   field.name, m_name);
    }
    ILL_ASSERT(!meta.min || !meta.max || *meta.min <= *meta.max, "field '{}' of '{}' has min > max",
               field.name, m_name);
    ILL_ASSERT(!meta.softMin || !meta.softMax || *meta.softMin <= *meta.softMax,
               "field '{}' of '{}' has softMin > softMax", field.name, m_name);
    ILL_ASSERT(!meta.step || *meta.step > 0.0, "field '{}' of '{}' has a step that is not positive",
               field.name, m_name);
    ILL_ASSERT(meta.displayUnit == Unit::None || convertUnit(1.0, meta.unit, meta.displayUnit).has_value(),
               "field '{}' of '{}' cannot show {} as {}", field.name, m_name, unitSymbol(meta.unit),
               unitSymbol(meta.displayUnit));

    const ValueType& leaf = leafOf(field);
    const bool numeric = isInteger(leaf.type) || leaf.type == FieldType::F32 || leaf.type == FieldType::F64;
    const bool vector =
        leaf.type == FieldType::Vector2 || leaf.type == FieldType::Vector3 || leaf.type == FieldType::Vector4;
    ILL_ASSERT(
        meta.unit == Unit::None || leaf.type == FieldType::F32 || leaf.type == FieldType::F64 || vector,
        "field '{}' of '{}' has a unit but is not a floating-point number or vector", field.name, m_name);
    ILL_ASSERT((!meta.min && !meta.max && !meta.softMin && !meta.softMax && !meta.step) || numeric || vector,
               "field '{}' of '{}' has a range but is not numeric", field.name, m_name);
    switch (meta.hint)
    {
    case FieldHint::None:
        break;
    case FieldHint::Color:
    case FieldHint::HdrColor:
        ILL_ASSERT(leaf.type == FieldType::Vector3 || leaf.type == FieldType::Vector4,
                   "colour field '{}' of '{}' must be a Vector3 or Vector4", field.name, m_name);
        break;
    case FieldHint::Angle:
        ILL_ASSERT(meta.unit == Unit::Radians || meta.unit == Unit::Degrees,
                   "angle field '{}' of '{}' needs the unit Radians or Degrees", field.name, m_name);
        break;
    case FieldHint::Slider:
    case FieldHint::LogSlider:
        ILL_ASSERT(numeric, "slider field '{}' of '{}' must be a number", field.name, m_name);
        break;
    case FieldHint::Multiline:
        ILL_ASSERT(leaf.type == FieldType::String, "multi-line field '{}' of '{}' must be a String",
                   field.name, m_name);
        break;
    case FieldHint::Layer:
        ILL_ASSERT(isInteger(leaf.type), "layer field '{}' of '{}' must be an integer", field.name, m_name);
        break;
    case FieldHint::LayerMask:
        ILL_ASSERT(isUnsigned(leaf.type), "layer mask field '{}' of '{}' must be an unsigned integer",
                   field.name, m_name);
        break;
    }
}

void ReflectionData::addField(const FieldInfo& field, StructResolver resolve, const void* context)
{
    checkField(field);
    FieldInfo copy;
    static_cast<ValueType&>(copy) = copyType(field, resolve, context);
    copy.name = intern(field.name);
    copy.offset = field.offset;
    copy.meta = field.meta;
    copy.meta.displayName = intern(field.meta.displayName);
    copy.meta.description = intern(field.meta.description);
    copy.meta.category = intern(field.meta.category);
    copy.meta.renamedFrom = intern(field.meta.renamedFrom);
    m_fields.push_back(copy);
    m_view.fields = m_fields;
}

void ReflectionData::setMeta(const TypeMeta& meta)
{
    for (const std::string_view required : meta.requiredComponents)
    {
        ILL_ASSERT(isValidStableName(required) && required != m_name,
                   "'{}' has an invalid required Component '{}'", m_name, required);
    }
    std::vector<std::string_view>& requiredList = m_required.emplace_back();
    for (const std::string_view required : meta.requiredComponents)
    {
        requiredList.push_back(intern(required));
    }
    TypeMeta copy = meta;
    copy.displayName = intern(meta.displayName);
    copy.category = intern(meta.category);
    copy.icon = intern(meta.icon);
    copy.description = intern(meta.description);
    copy.docsAnchor = intern(meta.docsAnchor);
    copy.requiredComponents = requiredList;
    m_view.meta = copy;
}

void ReflectionData::addMigration(const Migration& migration)
{
    ILL_ASSERT(migration.migrate != nullptr, "null migration for '{}'", m_name);
    ILL_ASSERT(migration.fromVersion < m_view.version,
               "migration of '{}' from version {} does not lead to the current version {}", m_name,
               migration.fromVersion, m_view.version);
    for (const Migration& existing : m_migrations)
    {
        ILL_ASSERT(existing.fromVersion != migration.fromVersion, "two migrations of '{}' from version {}",
                   m_name, migration.fromVersion);
    }
    m_migrations.push_back(migration);
    m_view.migrations = m_migrations;
}

} // namespace illusion::detail

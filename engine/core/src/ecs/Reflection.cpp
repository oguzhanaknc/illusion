#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>

#include <cstring>

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
    case FieldType::Vec2:
        return "Vec2";
    case FieldType::Vec3:
        return "Vec3";
    case FieldType::Vec4:
        return "Vec4";
    case FieldType::Quat:
        return "Quat";
    case FieldType::String:
        return "String";
    case FieldType::Entity:
        return "Entity";
    case FieldType::Enum:
        return "Enum";
    }
    return "Unknown";
}

const FieldInfo* ComponentReflection::field(std::string_view fieldName) const noexcept
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

i64 readEnumField(const void* component, const FieldInfo& field) noexcept
{
    const auto* data = static_cast<const std::byte*>(component) + field.offset;
    switch (field.enumSize)
    {
    case 1:
    {
        u8 value = 0;
        std::memcpy(&value, data, 1);
        return field.enumSigned ? static_cast<i64>(static_cast<i8>(value)) : static_cast<i64>(value);
    }
    case 2:
    {
        u16 value = 0;
        std::memcpy(&value, data, 2);
        return field.enumSigned ? static_cast<i64>(static_cast<i16>(value)) : static_cast<i64>(value);
    }
    case 4:
    {
        u32 value = 0;
        std::memcpy(&value, data, 4);
        return field.enumSigned ? static_cast<i64>(static_cast<i32>(value)) : static_cast<i64>(value);
    }
    default:
    {
        i64 value = 0;
        std::memcpy(&value, data, 8);
        return value;
    }
    }
}

void writeEnumField(void* component, const FieldInfo& field, i64 value) noexcept
{
    auto* data = static_cast<std::byte*>(component) + field.offset;
    switch (field.enumSize)
    {
    case 1:
    {
        const auto narrow = static_cast<u8>(value);
        std::memcpy(data, &narrow, 1);
        break;
    }
    case 2:
    {
        const auto narrow = static_cast<u16>(value);
        std::memcpy(data, &narrow, 2);
        break;
    }
    case 4:
    {
        const auto narrow = static_cast<u32>(value);
        std::memcpy(data, &narrow, 4);
        break;
    }
    default:
        std::memcpy(data, &value, 8);
        break;
    }
}

void reflectCoreComponents(ComponentRegistry& registry)
{
    reflect<Transform>("illusion.Transform", 1, registry)
        .field("position", &Transform::position)
        .field("rotation", &Transform::rotation)
        .field("scale", &Transform::scale);
}

} // namespace illusion

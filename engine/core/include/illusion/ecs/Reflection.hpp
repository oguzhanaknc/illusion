#pragma once

/// @file
/// Component reflection: stable names and field descriptions.
///
/// A reflected component type has an explicit, stable name (for example "illusion.Transform") and a
/// list of named fields. Scene files, the editor and tools use reflection instead of C++ type names,
/// which differ between compilers, so renaming a C++ type or moving it to another namespace does not
/// break saved data.
///
/// @code
/// illusion::reflect<game::Spinner>("game.Spinner")
///     .field("axis", &game::Spinner::axis)
///     .field("speed", &game::Spinner::speed);
/// @endcode

#include <illusion/core/Math.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace illusion
{

/// Type of a reflected field.
enum class FieldType : u8
{
    Bool,   ///< bool
    I32,    ///< i32
    U32,    ///< u32
    I64,    ///< i64
    U64,    ///< u64
    F32,    ///< f32
    F64,    ///< f64
    Vec2,   ///< Vec2
    Vec3,   ///< Vec3
    Vec4,   ///< Vec4
    Quat,   ///< Quat
    String, ///< std::string
    Entity, ///< Entity (a reference to another entity)
    Enum,   ///< An enumeration with an EnumReflection specialization
};

/// Returns the name of @p type, e.g. "Vec3".
[[nodiscard]] std::string_view fieldTypeName(FieldType type) noexcept;

/// A named value of a reflected enumeration.
struct EnumValue
{
    std::string_view name; ///< Stable name written to files.
    i64 value = 0;         ///< Numeric value.
};

/// Specialize for every enumeration used as a reflected field, with a constexpr array of values:
/// @code
/// template <>
/// struct illusion::EnumReflection<game::Team>
/// {
///     static constexpr std::array kValues{illusion::EnumValue{"Red", 0}, illusion::EnumValue{"Blue", 1}};
/// };
/// @endcode
template <class E>
struct EnumReflection;

/// An enumeration with an EnumReflection specialization.
template <class E>
concept ReflectedEnum = std::is_enum_v<E> && requires { EnumReflection<E>::kValues; };

/// Description of one reflected field.
struct FieldInfo
{
    std::string_view name;                 ///< Stable field name (letters, digits and '_').
    FieldType type = FieldType::Bool;      ///< Value type.
    u32 offset = 0;                        ///< Byte offset within the component.
    u32 enumSize = 0;                      ///< Enum: size of the underlying type in bytes.
    bool enumSigned = false;               ///< Enum: true if the underlying type is signed.
    std::span<const EnumValue> enumValues; ///< Enum: the named values.
};

/// Reflection of a component type. Owned by the ComponentRegistry.
struct ComponentReflection
{
    std::string_view name;             ///< Stable type name.
    u32 version = 1;                   ///< Data version, written to files for future migrations.
    std::span<const FieldInfo> fields; ///< Fields in reflection order.

    /// Returns the field named @p fieldName, or nullptr.
    [[nodiscard]] const FieldInfo* field(std::string_view fieldName) const noexcept;
};

/// Reads an Enum field of the component at @p component as a 64-bit value.
[[nodiscard]] i64 readEnumField(const void* component, const FieldInfo& field) noexcept;

/// Writes @p value to an Enum field of the component at @p component (truncated to the field's size).
void writeEnumField(void* component, const FieldInfo& field, i64 value) noexcept;

namespace detail
{

template <class>
inline constexpr bool kAlwaysFalse = false;

template <class M>
constexpr FieldType fieldTypeOf() noexcept
{
    if constexpr (std::is_same_v<M, bool>)
    {
        return FieldType::Bool;
    }
    else if constexpr (std::is_same_v<M, i32>)
    {
        return FieldType::I32;
    }
    else if constexpr (std::is_same_v<M, u32>)
    {
        return FieldType::U32;
    }
    else if constexpr (std::is_same_v<M, i64>)
    {
        return FieldType::I64;
    }
    else if constexpr (std::is_same_v<M, u64>)
    {
        return FieldType::U64;
    }
    else if constexpr (std::is_same_v<M, f32>)
    {
        return FieldType::F32;
    }
    else if constexpr (std::is_same_v<M, f64>)
    {
        return FieldType::F64;
    }
    else if constexpr (std::is_same_v<M, Vec2>)
    {
        return FieldType::Vec2;
    }
    else if constexpr (std::is_same_v<M, Vec3>)
    {
        return FieldType::Vec3;
    }
    else if constexpr (std::is_same_v<M, Vec4>)
    {
        return FieldType::Vec4;
    }
    else if constexpr (std::is_same_v<M, Quat>)
    {
        return FieldType::Quat;
    }
    else if constexpr (std::is_same_v<M, std::string>)
    {
        return FieldType::String;
    }
    else if constexpr (std::is_same_v<M, Entity>)
    {
        return FieldType::Entity;
    }
    else if constexpr (ReflectedEnum<M>)
    {
        return FieldType::Enum;
    }
    else
    {
        static_assert(kAlwaysFalse<M>,
                      "unsupported field type (enums need an EnumReflection specialization)");
        return FieldType::Bool;
    }
}

} // namespace detail

/// Adds fields to a reflected type. Returned by reflect().
template <ComponentType T>
class ReflectionBuilder
{
public:
    /// Creates a builder for @p id in @p registry. Use reflect() instead.
    ReflectionBuilder(ComponentRegistry& registry, ComponentId id) noexcept
        : m_registry(&registry)
        , m_id(id)
    {
    }

    /// Reflects the data member @p member under @p name. Names must be unique within the type.
    template <class M>
    ReflectionBuilder& field(std::string_view name, M T::* member)
    {
        FieldInfo info;
        info.name = name;
        info.type = detail::fieldTypeOf<M>();
        info.offset = offsetOf(member);
        if constexpr (std::is_enum_v<M>)
        {
            info.enumSize = static_cast<u32>(sizeof(M));
            info.enumSigned = std::is_signed_v<std::underlying_type_t<M>>;
            info.enumValues = EnumReflection<M>::kValues;
        }
        m_registry->addReflectedField(m_id, info);
        return *this;
    }

    /// The reflected component's id.
    [[nodiscard]] ComponentId id() const noexcept { return m_id; }

private:
    template <class M>
    static u32 offsetOf(M T::* member) noexcept
    {
        const T object{};
        const auto* base = reinterpret_cast<const std::byte*>(std::addressof(object));
        const auto* field = reinterpret_cast<const std::byte*>(std::addressof(object.*member));
        return static_cast<u32>(field - base);
    }

    ComponentRegistry* m_registry;
    ComponentId m_id;
};

/// Reflects component @p T under @p stableName (letters, digits, '_' and '.', e.g. "game.Spinner"),
/// replacing any earlier reflection of @p T. Increase @p version when the meaning of saved data changes.
template <ComponentType T>
ReflectionBuilder<T> reflect(std::string_view stableName, u32 version = 1,
                             ComponentRegistry& registry = ComponentRegistry::instance())
{
    static_assert(std::is_default_constructible_v<T>, "reflected components must be default constructible");
    const ComponentId id = registry.registerType<T>();
    registry.beginReflection(id, stableName, version);
    return ReflectionBuilder<T>(registry, id);
}

/// Reflects the core components (Transform as "illusion.Transform").
void reflectCoreComponents(ComponentRegistry& registry = ComponentRegistry::instance());

} // namespace illusion

#pragma once

/// @file
/// Reflection: stable names, field descriptions and metadata of Components and plain structs.
///
/// A reflected type has an explicit, stable name (for example "illusion.Transform") and a list of named
/// fields. Scene files, the editor and tools use reflection instead of C++ type names, which differ between
/// compilers, so renaming a C++ type or moving it to another namespace does not break saved data. Metadata
/// (FieldMeta, TypeMeta) tells the editor how to show and edit each field.
///
/// @code
/// illusion::reflect<game::Spinner>("game.Spinner")
///     .meta({.displayName = "Spinner", .category = "Gameplay", .icon = "autorenew"})
///     .field("axis", &game::Spinner::axis)
///     .field("speed", &game::Spinner::speed,
///            {.unit = illusion::Unit::Radians, .displayUnit = illusion::Unit::Degrees,
///             .hint = illusion::FieldHint::Angle});
/// @endcode
///
/// Plain structs (settings, descriptions) are reflected the same way with reflectStruct() (see
/// StructRegistry.hpp) and can then be fields of other reflected types. The JSON form of reflected values
/// is in ComponentJson.hpp.
///
/// Reflection data is owned by the registries: names, metadata strings and enumeration values are copied,
/// so they outlive the module that registered them. Function tables (ValueOps, ContainerOps, migrations)
/// stay in the registering module's code and may be used only while it is loaded, like ComponentInfo.
/// Reflecting a type again replaces its reflection, but the old one stays valid until the registry is
/// destroyed, so pointers to TypeReflection and FieldInfo never dangle.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/core/TypeName.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Entity.hpp>

#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace illusion
{

class JsonMutDocument;
class JsonMutValue;
struct TypeReflection;

/// Type of a reflected field. The numbering is stable.
enum class FieldType : u8
{
    Bool,       ///< bool
    I32,        ///< i32
    U32,        ///< u32
    I64,        ///< i64
    U64,        ///< u64
    F32,        ///< f32
    F64,        ///< f64
    Vector2,    ///< Vec2
    Vector3,    ///< Vec3
    Vector4,    ///< Vec4
    Quaternion, ///< Quat
    String,     ///< std::string
    Entity,     ///< Entity (a reference to another entity)
    Enum,       ///< An enumeration with an EnumReflection specialization
    I8,         ///< i8
    I16,        ///< i16
    U8,         ///< u8
    U16,        ///< u16
    Struct,     ///< A struct reflected with reflectStruct()
    FixedArray, ///< std::array<T, N> of a reflectable T
    Array,      ///< std::vector<T> of a reflectable T (not bool)
    Flags,      ///< An enumeration whose EnumReflection sets kFlags: a set of bits
    Optional,   ///< std::optional<T> of a reflectable T
    Json,       ///< JsonText: any JSON value
};

/// Returns the name of @p type, e.g. "Vector3".
[[nodiscard]] std::string_view fieldTypeName(FieldType type) noexcept;

/// Physical unit of a numeric field.
enum class Unit : u8
{
    None,            ///< No unit.
    Meters,          ///< m
    Radians,         ///< rad
    Degrees,         ///< deg
    Seconds,         ///< s
    Kilograms,       ///< kg
    Hertz,           ///< Hz
    Newtons,         ///< N
    MetersPerSecond, ///< m/s
    Ratio,           ///< A fraction where 1 is the whole (written "ratio").
    Percent,         ///< % (100 is the whole).
    Decibels,        ///< dB
};

/// Returns the symbol of @p unit used in unit-tagged JSON and the editor, e.g. "deg" ("" for None).
[[nodiscard]] std::string_view unitSymbol(Unit unit) noexcept;

/// Returns the unit whose symbol is @p symbol, or nullopt ("" is not a symbol).
[[nodiscard]] std::optional<Unit> unitFromSymbol(std::string_view symbol) noexcept;

/// Converts @p value from @p from to @p to. Only units of the same quantity convert (Radians and Degrees,
/// Ratio and Percent, any unit to itself); otherwise returns nullopt.
[[nodiscard]] std::optional<f64> convertUnit(f64 value, Unit from, Unit to) noexcept;

/// How the editor shows a field.
enum class FieldHint : u8
{
    None,      ///< The default widget for the field type.
    Color,     ///< Vector3/Vector4 holding a linear colour in [0, 1].
    HdrColor,  ///< Vector3/Vector4 holding a linear colour that may exceed 1.
    Angle,     ///< A rotation angle (with Unit::Radians or Unit::Degrees).
    Slider,    ///< A slider between FieldMeta::min/softMin and max/softMax.
    LogSlider, ///< A logarithmic slider.
    Multiline, ///< A multi-line text box for a String.
    Layer,     ///< One layer index.
    LayerMask, ///< A bit mask of layers.
};

/// Returns the name of @p hint, e.g. "HdrColor".
[[nodiscard]] std::string_view fieldHintName(FieldHint hint) noexcept;

/// Checks a value read from JSON (see FieldMeta::validate). Returns an empty string if @p value is valid,
/// otherwise why it is not, in English and lower case, e.g. "must not contain '/'" (static storage).
using ValidateFn = std::string_view (*)(const void* value) noexcept;

/// Metadata of a reflected field, given to TypeReflectionBuilder::field(). Strings are copied. Designated
/// initializers must follow the member order below.
struct FieldMeta
{
    std::string_view displayName{}; ///< Label in English (default: derived from the field name).
    std::string_view description{}; ///< Tooltip in English.
    std::string_view category{};    ///< Group within the type, e.g. "Limits".
    Unit unit = Unit::None;         ///< Unit of the stored value.
    Unit displayUnit = Unit::None;  ///< Unit shown to the user (None: the stored unit).
    /// Smallest valid value, in the stored unit. Reading JSON rejects smaller values. Applies to numbers,
    /// to every component of vectors, and through arrays and optionals.
    std::optional<f64> min{};
    std::optional<f64> max{};         ///< Largest valid value (see min).
    std::optional<f64> softMin{};     ///< Lower end of sliders; values below it are still valid.
    std::optional<f64> softMax{};     ///< Upper end of sliders.
    std::optional<f64> step{};        ///< Increment of drags and sliders.
    FieldHint hint = FieldHint::None; ///< Widget.
    bool readOnly = false;            ///< Shown but not editable in the editor.
    bool hidden = false;              ///< Not shown in the editor (still saved).
    bool advanced = false;            ///< Shown only when advanced fields are expanded.
    /// Name the field had in older data: reading JSON accepts it, and renames it before migrations run.
    std::string_view renamedFrom{};
    /// Rule that reading JSON checks after a value was read: a value it rejects fails the read with its
    /// message. Like min and max it applies through arrays and optionals, to every element. The function
    /// stays in the registering module's code (see the file documentation).
    ValidateFn validate = nullptr;
};

/// Metadata of a reflected type, given to TypeReflectionBuilder::meta(). Strings are copied. Designated
/// initializers must follow the member order below.
struct TypeMeta
{
    std::string_view displayName{}; ///< Name in English (default: derived from the stable name).
    std::string_view category{};    ///< Path in menus, '/'-separated, e.g. "Physics/Bodies".
    std::string_view icon{};        ///< Material Symbols icon name, e.g. "videocam".
    std::string_view description{}; ///< One sentence in English.
    std::string_view docsAnchor{};  ///< Manual page and anchor, e.g. "manual/physics.md#rigid-bodies".
    /// Stable names of Components that an Entity with this Component must also have.
    std::span<const std::string_view> requiredComponents{};
    bool singleton = false; ///< At most one Entity per World should have it.
    /// Runtime state: never saved, copied or edited. Scene files skip it.
    bool transient = false;
};

/// A named value of a reflected enumeration.
struct EnumValue
{
    std::string_view name; ///< Stable name written to files.
    i64 value = 0;         ///< Numeric value.
};

/// Specialize for every enumeration used as a reflected field, with a constexpr array of values. Add
/// `static constexpr bool kFlags = true;` for an enumeration of bits, which becomes FieldType::Flags:
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

/// A reflected enumeration of bits (EnumReflection<E>::kFlags is true).
template <class E>
concept ReflectedFlags = ReflectedEnum<E> && requires {
    { EnumReflection<E>::kFlags } -> std::convertible_to<bool>;
} && EnumReflection<E>::kFlags;

/// Any JSON value stored as compact text: the C++ type of FieldType::Json fields.
struct JsonText
{
    std::string text = "null"; ///< Compact JSON text.

    /// Compares the text.
    friend bool operator==(const JsonText&, const JsonText&) = default;
};

/// Lifetime functions of a reflected value type, generated for its C++ type.
struct ValueOps
{
    void (*construct)(void* destination) noexcept = nullptr;                  ///< Default-constructs.
    void (*copy)(void* destination, const void* source) noexcept = nullptr;   ///< Copy-constructs.
    void (*relocate)(void* destination, void* source) noexcept = nullptr;     ///< Moves, then destroys.
    void (*assign)(void* destination, const void* source) noexcept = nullptr; ///< Copy-assigns.
    void (*destroy)(void* value) noexcept = nullptr;                          ///< Destroys.
};

/// Element access of Array (std::vector) and Optional (std::optional, 0 or 1 elements) values.
struct ContainerOps
{
    usize (*size)(const void* container) noexcept = nullptr;           ///< Number of elements.
    void (*resize)(void* container, usize size) noexcept = nullptr;    ///< New elements are defaults.
    void* (*element)(void* container, usize index) noexcept = nullptr; ///< Element @p index.
    const void* (*constElement)(const void* container, usize index) noexcept = nullptr; ///< Read-only.
};

/// Description of the type of a reflected value. Owned by a registry.
struct ValueType
{
    FieldType type = FieldType::Bool;           ///< Kind of value.
    u32 size = 0;                               ///< sizeof the C++ type.
    u32 alignment = 1;                          ///< alignof the C++ type.
    u64 typeHash = 0;                           ///< typeHash() of the C++ type.
    const ValueOps* ops = nullptr;              ///< Lifetime functions.
    u32 enumSize = 0;                           ///< Enum, Flags: size of the underlying type in bytes.
    bool enumSigned = false;                    ///< Enum, Flags: true if the underlying type is signed.
    std::span<const EnumValue> enumValues;      ///< Enum, Flags: the named values.
    const TypeReflection* structType = nullptr; ///< Struct: its reflection.
    const ValueType* element = nullptr;         ///< FixedArray, Array, Optional: the element type.
    u32 count = 0;                              ///< FixedArray: number of elements.
    const ContainerOps* container = nullptr;    ///< Array, Optional: element access.
};

/// Description of one reflected field.
struct FieldInfo : ValueType
{
    std::string_view name; ///< Stable field name (letters, digits and '_').
    u32 offset = 0;        ///< Byte offset within the object.
    FieldMeta meta;        ///< Metadata (strings owned by the registry).
};

/// Migrates the JSON object of a type saved with one version to the next version, in place. Build new
/// values with @p doc.
using MigrationFn = void (*)(JsonMutDocument& doc, JsonMutValue object);

/// A migration step registered with TypeReflectionBuilder::migrate().
struct Migration
{
    u32 fromVersion = 0;           ///< Version of the data it accepts; it produces fromVersion + 1.
    MigrationFn migrate = nullptr; ///< The step.
};

/// Reflection of a Component or struct type. Owned by a registry.
struct TypeReflection
{
    std::string_view name;                 ///< Stable type name.
    u32 version = 1;                       ///< Data version, written to files for migrations.
    std::span<const FieldInfo> fields;     ///< Fields in reflection order.
    TypeMeta meta;                         ///< Metadata (strings owned by the registry).
    std::span<const Migration> migrations; ///< Migration steps in registration order.
    u32 size = 0;                          ///< sizeof the C++ type, or 0 for tags.
    u32 alignment = 1;                     ///< alignof the C++ type.
    u64 typeHash = 0;                      ///< typeHash() of the C++ type.
    const ValueOps* ops = nullptr;         ///< Lifetime functions of the whole type.

    /// Returns the field named @p fieldName, or nullptr.
    [[nodiscard]] const FieldInfo* field(std::string_view fieldName) const noexcept;

    /// Returns the field named @p fieldName or renamed from it (FieldMeta::renamedFrom), or nullptr.
    [[nodiscard]] const FieldInfo* fieldOrRenamed(std::string_view fieldName) const noexcept;
};

/// Reflection of a Component type.
using ComponentReflection = TypeReflection;

/// Reflection of a struct type.
using StructReflection = TypeReflection;

/// Returns the label of @p field: FieldMeta::displayName, or the field name split into words
/// ("castShadows" becomes "Cast shadows").
[[nodiscard]] std::string displayNameOf(const FieldInfo& field);

/// Returns the name of @p type: TypeMeta::displayName, or the last part of the stable name split into
/// words ("illusion.PointLight" becomes "Point light").
[[nodiscard]] std::string displayNameOf(const TypeReflection& type);

/// Reads an Enum or Flags value at @p value as a 64-bit number.
[[nodiscard]] i64 readEnumValue(const void* value, const ValueType& type) noexcept;

/// Writes @p number to the Enum or Flags value at @p value (truncated to its size).
void writeEnumValue(void* value, const ValueType& type, i64 number) noexcept;

/// Reads an Enum or Flags field of the object at @p object as a 64-bit value.
[[nodiscard]] i64 readEnumField(const void* object, const FieldInfo& field) noexcept;

/// Writes @p value to an Enum or Flags field of the object at @p object (truncated to the field's size).
void writeEnumField(void* object, const FieldInfo& field, i64 value) noexcept;

/// Returns the address of @p field in the object at @p object.
[[nodiscard]] inline void* fieldData(void* object, const FieldInfo& field) noexcept
{
    return static_cast<std::byte*>(object) + field.offset;
}

/// Returns the address of @p field in the object at @p object.
[[nodiscard]] inline const void* fieldData(const void* object, const FieldInfo& field) noexcept
{
    return static_cast<const std::byte*>(object) + field.offset;
}

/// Compares two values of @p type: structs field by reflected field, containers element by element,
/// floating-point numbers with `==`.
[[nodiscard]] bool valuesEqual(const ValueType& type, const void* a, const void* b) noexcept;

/// An owned copy of one reflected value of any type, for undo, multi-edit and generic tools. Values of up
/// to 48 bytes are stored inline. A FieldValue refers to its ValueType, which its registry keeps alive;
/// values of a module's types must be destroyed before the module is unloaded.
class FieldValue
{
public:
    /// Creates an empty value.
    FieldValue() noexcept = default;

    /// Creates a default-constructed value of @p type.
    explicit FieldValue(const ValueType& type);

    /// Creates a copy of the value of @p type at @p source.
    FieldValue(const ValueType& type, const void* source);

    /// Copies the value of @p other.
    FieldValue(const FieldValue& other);

    /// Takes the value of @p other, which becomes empty.
    FieldValue(FieldValue&& other) noexcept;

    /// Replaces this value with a copy of @p other.
    FieldValue& operator=(const FieldValue& other);

    /// Replaces this value with the value of @p other, which becomes empty.
    FieldValue& operator=(FieldValue&& other) noexcept;

    ~FieldValue();

    /// The type of the value, or nullptr if empty.
    [[nodiscard]] const ValueType* type() const noexcept { return m_type; }

    /// True if there is no value.
    [[nodiscard]] bool empty() const noexcept { return m_type == nullptr; }

    /// The value (nullptr if empty).
    [[nodiscard]] void* data() noexcept { return m_data; }

    /// The value (nullptr if empty).
    [[nodiscard]] const void* data() const noexcept { return m_data; }

    /// True if the value has the C++ type @p T.
    template <class T>
    [[nodiscard]] bool holds() const noexcept
    {
        return m_type != nullptr && m_type->typeHash == typeHash<T>();
    }

    /// The value as @p T, or nullptr if it has another type.
    template <class T>
    [[nodiscard]] T* get() noexcept
    {
        return holds<T>() ? static_cast<T*>(m_data) : nullptr;
    }

    /// The value as @p T, or nullptr if it has another type.
    template <class T>
    [[nodiscard]] const T* get() const noexcept
    {
        return holds<T>() ? static_cast<const T*>(m_data) : nullptr;
    }

    /// True if both are empty, or have the same type and equal values (see valuesEqual()).
    friend bool operator==(const FieldValue& a, const FieldValue& b) noexcept;

private:
    static constexpr usize kInlineSize = 48;
    static constexpr usize kInlineAlignment = 16;

    void* allocate(const ValueType& type);
    void reset() noexcept;
    void takeFrom(FieldValue& other) noexcept;

    const ValueType* m_type = nullptr;
    void* m_data = nullptr;
    alignas(kInlineAlignment) std::byte m_inline[kInlineSize];
};

/// Returns a copy of @p field of the object at @p object.
[[nodiscard]] FieldValue readField(const void* object, const FieldInfo& field);

/// Assigns @p value to @p field of the object at @p object. Asserts that @p value has the field's type.
void writeField(void* object, const FieldInfo& field, const FieldValue& value);

/// True if @p field has equal values in the objects at @p a and @p b (see valuesEqual()).
[[nodiscard]] bool fieldsEqual(const void* a, const void* b, const FieldInfo& field) noexcept;

/// Returns the value of @p field in a default-constructed object of @p type.
[[nodiscard]] FieldValue defaultValue(const TypeReflection& type, const FieldInfo& field);

/// Size and lifetime functions of a type being reflected. Built by reflect() and reflectStruct().
struct TypeLayout
{
    u64 typeHash = 0;              ///< typeHash() of the C++ type.
    u32 size = 0;                  ///< sizeof the type, or 0 for tags.
    u32 alignment = 1;             ///< alignof the type.
    const ValueOps* ops = nullptr; ///< Lifetime functions.
};

namespace detail
{

template <class>
inline constexpr bool kAlwaysFalse = false;

template <class T>
struct VectorTraits : std::false_type
{
};

template <class T, class A>
struct VectorTraits<std::vector<T, A>> : std::true_type
{
    using Element = T;
};

template <class T>
struct ArrayTraits : std::false_type
{
};

template <class T, usize N>
struct ArrayTraits<std::array<T, N>> : std::true_type
{
    using Element = T;
    static constexpr usize kCount = N;
};

template <class T>
struct OptionalTraits : std::false_type
{
};

template <class T>
struct OptionalTraits<std::optional<T>> : std::true_type
{
    using Element = T;
};

template <class M>
constexpr FieldType fieldTypeOf() noexcept
{
    if constexpr (std::is_same_v<M, bool>)
    {
        return FieldType::Bool;
    }
    else if constexpr (std::is_same_v<M, i8>)
    {
        return FieldType::I8;
    }
    else if constexpr (std::is_same_v<M, i16>)
    {
        return FieldType::I16;
    }
    else if constexpr (std::is_same_v<M, i32>)
    {
        return FieldType::I32;
    }
    else if constexpr (std::is_same_v<M, i64>)
    {
        return FieldType::I64;
    }
    else if constexpr (std::is_same_v<M, u8>)
    {
        return FieldType::U8;
    }
    else if constexpr (std::is_same_v<M, u16>)
    {
        return FieldType::U16;
    }
    else if constexpr (std::is_same_v<M, u32>)
    {
        return FieldType::U32;
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
        return FieldType::Vector2;
    }
    else if constexpr (std::is_same_v<M, Vec3>)
    {
        return FieldType::Vector3;
    }
    else if constexpr (std::is_same_v<M, Vec4>)
    {
        return FieldType::Vector4;
    }
    else if constexpr (std::is_same_v<M, Quat>)
    {
        return FieldType::Quaternion;
    }
    else if constexpr (std::is_same_v<M, std::string>)
    {
        return FieldType::String;
    }
    else if constexpr (std::is_same_v<M, Entity>)
    {
        return FieldType::Entity;
    }
    else if constexpr (std::is_same_v<M, JsonText>)
    {
        return FieldType::Json;
    }
    else if constexpr (ReflectedFlags<M>)
    {
        return FieldType::Flags;
    }
    else if constexpr (ReflectedEnum<M>)
    {
        return FieldType::Enum;
    }
    else if constexpr (ArrayTraits<M>::value)
    {
        return FieldType::FixedArray;
    }
    else if constexpr (VectorTraits<M>::value)
    {
        static_assert(!std::is_same_v<typename VectorTraits<M>::Element, bool>,
                      "std::vector<bool> cannot be reflected; use std::vector<u8>");
        return FieldType::Array;
    }
    else if constexpr (OptionalTraits<M>::value)
    {
        return FieldType::Optional;
    }
    else if constexpr (std::is_class_v<M> && std::is_default_constructible_v<M> &&
                       std::is_copy_constructible_v<M>)
    {
        return FieldType::Struct; // must be reflected with reflectStruct() first (checked at runtime)
    }
    else
    {
        static_assert(kAlwaysFalse<M>, "unsupported field type (enums need an EnumReflection specialization; "
                                       "structs must be default constructible and copyable)");
        return FieldType::Bool;
    }
}

template <class M>
inline constexpr ValueOps kValueOps{
    [](void* destination) noexcept { ::new (destination) M(); },
    [](void* destination, const void* source) noexcept {
        ::new (destination) M(*static_cast<const M*>(source));
    },
    [](void* destination, void* source) noexcept {
        M* from = static_cast<M*>(source);
        ::new (destination) M(std::move(*from));
        std::destroy_at(from);
    },
    [](void* destination, const void* source) noexcept {
        *static_cast<M*>(destination) = *static_cast<const M*>(source);
    },
    [](void* value) noexcept { std::destroy_at(static_cast<M*>(value)); },
};

template <class V>
inline constexpr ContainerOps kVectorOps{
    [](const void* container) noexcept -> usize { return static_cast<const V*>(container)->size(); },
    [](void* container, usize size) noexcept { static_cast<V*>(container)->resize(size); },
    [](void* container, usize index) noexcept -> void* { return static_cast<V*>(container)->data() + index; },
    [](const void* container, usize index) noexcept -> const void* {
        return static_cast<const V*>(container)->data() + index;
    },
};

template <class O>
inline constexpr ContainerOps kOptionalOps{
    [](const void* container) noexcept -> usize {
        return static_cast<const O*>(container)->has_value() ? 1 : 0;
    },
    [](void* container, usize size) noexcept {
        O& optional = *static_cast<O*>(container);
        if (size == 0)
        {
            optional.reset();
        }
        else if (!optional.has_value())
        {
            optional.emplace();
        }
    },
    [](void* container, usize) noexcept -> void* { return std::addressof(**static_cast<O*>(container)); },
    [](const void* container, usize) noexcept -> const void* {
        return std::addressof(**static_cast<const O*>(container));
    },
};

template <class M>
const ValueType& valueTypeOf() noexcept;

template <class M>
ValueType makeValueType() noexcept
{
    ValueType type;
    type.type = fieldTypeOf<M>();
    type.size = static_cast<u32>(sizeof(M));
    type.alignment = static_cast<u32>(alignof(M));
    type.typeHash = typeHash<M>();
    type.ops = &kValueOps<M>;
    if constexpr (std::is_enum_v<M>)
    {
        type.enumSize = static_cast<u32>(sizeof(M));
        type.enumSigned = std::is_signed_v<std::underlying_type_t<M>>;
        type.enumValues = EnumReflection<M>::kValues;
    }
    else if constexpr (ArrayTraits<M>::value)
    {
        type.element = &valueTypeOf<typename ArrayTraits<M>::Element>();
        type.count = static_cast<u32>(ArrayTraits<M>::kCount);
    }
    else if constexpr (VectorTraits<M>::value)
    {
        type.element = &valueTypeOf<typename VectorTraits<M>::Element>();
        type.container = &kVectorOps<M>;
    }
    else if constexpr (OptionalTraits<M>::value)
    {
        type.element = &valueTypeOf<typename OptionalTraits<M>::Element>();
        type.container = &kOptionalOps<M>;
    }
    return type;
}

// The description of M in the calling module. Registries copy it, so it may be unloaded with the module.
template <class M>
const ValueType& valueTypeOf() noexcept
{
    static const ValueType type = makeValueType<M>();
    return type;
}

template <class T>
TypeLayout typeLayoutOf() noexcept
{
    TypeLayout layout;
    layout.typeHash = typeHash<T>();
    layout.size = std::is_empty_v<T> ? 0u : static_cast<u32>(sizeof(T));
    layout.alignment = static_cast<u32>(alignof(T));
    layout.ops = &kValueOps<T>;
    return layout;
}

} // namespace detail

/// Adds fields, metadata and migrations to a reflected type. Returned by reflect() and reflectStruct().
template <class T, class Registry, class Id>
class TypeReflectionBuilder
{
public:
    /// Creates a builder for @p id in @p registry. Use reflect() or reflectStruct() instead.
    TypeReflectionBuilder(Registry& registry, Id id) noexcept
        : m_registry(&registry)
        , m_id(id)
    {
    }

    /// Reflects the data member @p member under @p name with @p meta. Names must be unique within the type.
    /// A struct member's type must be reflected with reflectStruct() first.
    template <class M>
    TypeReflectionBuilder& field(std::string_view name, M T::* member, const FieldMeta& meta = {})
    {
        FieldInfo info;
        static_cast<ValueType&>(info) = detail::valueTypeOf<M>();
        info.name = name;
        info.offset = offsetOf(member);
        info.meta = meta;
        m_registry->addReflectedField(m_id, info);
        return *this;
    }

    /// Sets the metadata of the type.
    TypeReflectionBuilder& meta(const TypeMeta& meta)
    {
        m_registry->setReflectionMeta(m_id, meta);
        return *this;
    }

    /// Registers @p step to convert JSON saved with @p fromVersion to @p fromVersion + 1. Data older than
    /// the type's version runs every step from its version on, in order of versions.
    TypeReflectionBuilder& migrate(u32 fromVersion, MigrationFn step)
    {
        m_registry->addReflectionMigration(m_id, Migration{fromVersion, step});
        return *this;
    }

    /// The reflected type's id.
    [[nodiscard]] Id id() const noexcept { return m_id; }

private:
    template <class M>
    static u32 offsetOf(M T::* member) noexcept
    {
        const T object{};
        const auto* base = reinterpret_cast<const std::byte*>(std::addressof(object));
        const auto* field = reinterpret_cast<const std::byte*>(std::addressof(object.*member));
        return static_cast<u32>(field - base);
    }

    Registry* m_registry;
    Id m_id;
};

/// Builder of a reflected Component.
template <ComponentType T>
using ReflectionBuilder = TypeReflectionBuilder<T, ComponentRegistry, ComponentId>;

/// Reflects component @p T under @p stableName (letters, digits, '_' and '.', e.g. "game.Spinner"),
/// replacing any earlier reflection of @p T. Increase @p version when the meaning of saved data changes,
/// and add a migration.
template <ComponentType T>
ReflectionBuilder<T> reflect(std::string_view stableName, u32 version = 1,
                             ComponentRegistry& registry = ComponentRegistry::instance())
{
    static_assert(std::is_default_constructible_v<T>, "reflected components must be default constructible");
    static_assert(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
                  "reflected components must be copyable");
    const ComponentId id = registry.registerType<T>();
    registry.beginReflection(id, stableName, version, detail::typeLayoutOf<T>());
    return ReflectionBuilder<T>(registry, id);
}

/// Reflects runtime state @p T under @p stableName with TypeMeta::transient set, so that the editor can
/// name it while scene files skip it.
template <ComponentType T>
ReflectionBuilder<T> reflectRuntime(std::string_view stableName, TypeMeta meta = {},
                                    ComponentRegistry& registry = ComponentRegistry::instance())
{
    meta.transient = true;
    ReflectionBuilder<T> builder = reflect<T>(stableName, 1, registry);
    builder.meta(meta);
    return builder;
}

/// Reflects the core components (Transform as "illusion.Transform").
void reflectCoreComponents(ComponentRegistry& registry = ComponentRegistry::instance());

} // namespace illusion

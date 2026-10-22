#pragma once

/// @file
/// Reflection of plain structs: settings, descriptions and other data that is not a Component.
///
/// A reflected struct can be a field of a Component or of another struct, an element of an array, and
/// can be read and written as JSON on its own (ComponentJson.hpp), for example as a settings file.
///
/// @code
/// illusion::reflectStruct<game::Difficulty>("game.Difficulty")
///     .field("enemyCount", &game::Difficulty::enemyCount, {.min = 0})
///     .field("spawnDelay", &game::Difficulty::spawnDelay, {.unit = illusion::Unit::Seconds});
/// illusion::reflect<game::Level>("game.Level").field("difficulty", &game::Level::difficulty);
/// @endcode
///
/// Every ComponentRegistry owns a StructRegistry (ComponentRegistry::structs()), so modules that share the
/// host's component registry share its structs too.

#include <illusion/core/Base.hpp>
#include <illusion/ecs/Component.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <memory>
#include <string_view>
#include <type_traits>

namespace illusion
{

/// Dense identifier of a reflected struct within a StructRegistry.
using StructId = u16;

/// Value returned for unknown structs.
inline constexpr StructId kInvalidStructId = 0xFFFF;

/// Table of reflected structs. Registration is thread-safe; reflect types during startup, before other
/// threads read their reflection.
class StructRegistry : NonCopyable
{
public:
    StructRegistry();
    ~StructRegistry();

    /// Returns the id of the struct whose C++ type has @p typeHash, or kInvalidStructId.
    [[nodiscard]] StructId find(u64 typeHash) const;

    /// Returns the id of the struct reflected under @p stableName, or kInvalidStructId.
    [[nodiscard]] StructId findByStableName(std::string_view stableName) const;

    /// Returns the reflection of @p id, or nullptr if @p id is not a registered id.
    [[nodiscard]] const StructReflection* reflection(StructId id) const;

    /// Number of reflected structs.
    [[nodiscard]] u32 count() const;

    /// @name Reflection building
    /// Used by reflectStruct() and TypeReflectionBuilder.
    /// @{

    /// Starts the reflection of the type described by @p layout under @p stableName and returns its id,
    /// replacing any earlier reflection of the same type (which keeps its id). Stable names must be unique
    /// among structs; reusing one for another type is a fatal error.
    StructId beginReflection(const TypeLayout& layout, std::string_view stableName, u32 version);

    /// Appends a field to the reflection of @p id. Strings, enumeration values and element types are copied.
    void addReflectedField(StructId id, const FieldInfo& field);

    /// Sets the metadata of @p id (strings are copied).
    void setReflectionMeta(StructId id, const TypeMeta& meta);

    /// Appends a migration step to @p id.
    void addReflectionMigration(StructId id, const Migration& migration);

    /// @}

    /// Returns the current reflection of the struct whose C++ type has @p typeHash, or nullptr. Used while
    /// reflecting fields; the registry's lock must not be held by the caller.
    [[nodiscard]] const StructReflection* reflectionOf(u64 typeHash) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Builder of a reflected struct.
template <class T>
using StructReflectionBuilder = TypeReflectionBuilder<T, StructRegistry, StructId>;

/// Reflects struct @p T under @p stableName (letters, digits, '_' and '.', e.g. "game.Difficulty"),
/// replacing any earlier reflection of @p T. Reflect a struct before the types that use it; types reflected
/// earlier keep the struct's previous reflection until they are reflected again.
template <class T>
StructReflectionBuilder<T> reflectStruct(std::string_view stableName, u32 version = 1,
                                         StructRegistry& registry = ComponentRegistry::instance().structs())
{
    static_assert(std::is_class_v<T> && !std::is_empty_v<T>, "reflected structs must be non-empty classes");
    static_assert(std::is_default_constructible_v<T>, "reflected structs must be default constructible");
    static_assert(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
                  "reflected structs must be copyable");
    static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>,
                  "reflected structs must be nothrow movable and destructible");
    const StructId id = registry.beginReflection(detail::typeLayoutOf<T>(), stableName, version);
    return StructReflectionBuilder<T>(registry, id);
}

} // namespace illusion

#pragma once

// Storage of one reflected type, shared by ComponentRegistry and StructRegistry. Not a public header.

#include <illusion/ecs/Reflection.hpp>

#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::detail
{

// Returns true for stable type names: letters, digits, '_' and '.'.
[[nodiscard]] bool isValidStableName(std::string_view name) noexcept;

// Maps the type hash of a struct field to its current reflection, or nullptr if it is not reflected.
using StructResolver = const TypeReflection* (*)(const void* context, u64 typeHash);

// Owns everything a TypeReflection view points into. Deques keep element addresses stable while fields
// are appended, so views and FieldInfo pointers stay valid.
class ReflectionData
{
public:
    ReflectionData(std::string_view name, u32 version, const TypeLayout& layout);

    ReflectionData(const ReflectionData&) = delete;
    ReflectionData& operator=(const ReflectionData&) = delete;

    [[nodiscard]] const TypeReflection& view() const noexcept { return m_view; }

    // Copies @p field, its strings, enumeration values and element types; resolves struct types through
    // @p resolve. Asserts that the field is valid.
    void addField(const FieldInfo& field, StructResolver resolve, const void* context);

    void setMeta(const TypeMeta& meta);

    void addMigration(const Migration& migration);

private:
    std::string_view intern(std::string_view text);
    [[nodiscard]] ValueType copyType(const ValueType& type, StructResolver resolve, const void* context);
    void checkField(const FieldInfo& field) const;

    std::string m_name;
    std::deque<std::string> m_strings;
    std::deque<std::vector<EnumValue>> m_enums;
    std::deque<ValueType> m_types;
    std::vector<FieldInfo> m_fields;
    std::vector<Migration> m_migrations;
    std::deque<std::vector<std::string_view>> m_required; // one list per setMeta() call
    TypeReflection m_view;
};

} // namespace illusion::detail

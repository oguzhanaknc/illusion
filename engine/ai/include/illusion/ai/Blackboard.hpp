#pragma once

/// @file
/// Blackboards (docs/adr/0062-navigation-and-ai.md): the typed keys a behaviour tree declares and the
/// fixed-size values an agent keeps for them.

#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <string>
#include <string_view>

namespace illusion
{

/// Most keys a behaviour tree declares.
inline constexpr u32 kMaxBlackboardKeys = 32;

/// Type of a blackboard key.
enum class BlackboardType : u8
{
    Bool,    ///< On or off.
    Int,     ///< A whole number.
    Float,   ///< A number.
    Vector3, ///< A point or direction in world space.
    Entity,  ///< Another Entity; a destroyed one counts as not set.
};

/// Stable name of @p type, e.g. "Vector3".
[[nodiscard]] constexpr std::string_view blackboardTypeName(BlackboardType type) noexcept
{
    switch (type)
    {
    case BlackboardType::Bool:
        return "Bool";
    case BlackboardType::Int:
        return "Int";
    case BlackboardType::Float:
        return "Float";
    case BlackboardType::Vector3:
        return "Vector3";
    case BlackboardType::Entity:
        return "Entity";
    }
    return "Bool";
}

/// The value of one key. Only the field of the key's type is meaningful.
struct BlackboardValue
{
    BlackboardType type = BlackboardType::Bool; ///< The key's type.
    bool set = false;                           ///< Has a value; Bool, Int and Float keys are always set.
    bool flag = false;                          ///< Bool.
    i32 integer = 0;                            ///< Int.
    f32 number = 0.0f;                          ///< Float.
    Vec3 vector{0.0f};                          ///< Vector3.
    Entity entity;                              ///< Entity.
};

/// A key declared by a behaviour tree.
struct BlackboardKey
{
    std::string name;                           ///< Unique in the tree.
    BlackboardType type = BlackboardType::Bool; ///< Its type.
    BlackboardValue defaultValue; ///< Value when the tree starts (Vector3 and Entity unset unless given).
};

/// How a condition compares a key with a value.
enum class BlackboardCompare : u8
{
    IsSet,        ///< Vector3, Entity: has a value (an alive Entity).
    IsNotSet,     ///< Vector3, Entity: has none.
    Equal,        ///< Bool, Int, Float.
    NotEqual,     ///< Bool, Int, Float.
    Greater,      ///< Int, Float.
    Less,         ///< Int, Float.
    GreaterEqual, ///< Int.
    LessEqual,    ///< Int.
};

/// True when @p value compares with @p operand as @p compare says; @p alive is the liveness of an Entity
/// value (the caller looks it up). Comparisons a type does not support (see BlackboardCompare) are false.
[[nodiscard]] inline bool compareBlackboard(const BlackboardValue& value, BlackboardCompare compare,
                                            const BlackboardValue& operand, bool alive) noexcept
{
    const bool set = value.type == BlackboardType::Entity ? value.set && alive : value.set;
    if (compare == BlackboardCompare::IsSet)
    {
        return set;
    }
    if (compare == BlackboardCompare::IsNotSet)
    {
        return !set;
    }
    switch (value.type)
    {
    case BlackboardType::Bool:
        if (compare == BlackboardCompare::Equal)
        {
            return value.flag == operand.flag;
        }
        return compare == BlackboardCompare::NotEqual && value.flag != operand.flag;
    case BlackboardType::Int:
        switch (compare)
        {
        case BlackboardCompare::Equal:
            return value.integer == operand.integer;
        case BlackboardCompare::NotEqual:
            return value.integer != operand.integer;
        case BlackboardCompare::Greater:
            return value.integer > operand.integer;
        case BlackboardCompare::Less:
            return value.integer < operand.integer;
        case BlackboardCompare::GreaterEqual:
            return value.integer >= operand.integer;
        case BlackboardCompare::LessEqual:
            return value.integer <= operand.integer;
        default:
            return false;
        }
    case BlackboardType::Float:
        switch (compare)
        {
        case BlackboardCompare::Equal:
            return value.number == operand.number;
        case BlackboardCompare::NotEqual:
            return value.number != operand.number;
        case BlackboardCompare::Greater:
            return value.number > operand.number;
        case BlackboardCompare::Less:
            return value.number < operand.number;
        default:
            return false;
        }
    case BlackboardType::Vector3:
    case BlackboardType::Entity:
        return false;
    }
    return false;
}

} // namespace illusion

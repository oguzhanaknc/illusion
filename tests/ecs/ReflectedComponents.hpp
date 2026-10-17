#pragma once

// Reflected component types shared by the reflection and scene tests. Component types must have unique
// fully qualified names, so they live in a named namespace.

#include <illusion/ecs/Reflection.hpp>

#include <array>
#include <string>

namespace reflection_test
{

enum class Team : illusion::i16
{
    Red = -1,
    Neutral = 0,
    Blue = 7,
};

enum class Flags : illusion::u8
{
    None = 0,
    Visible = 1,
};

} // namespace reflection_test

// Enum specializations must be declared before the enums are used as reflected fields.
template <>
struct illusion::EnumReflection<reflection_test::Team>
{
    static constexpr std::array kValues{illusion::EnumValue{"Red", -1}, illusion::EnumValue{"Neutral", 0},
                                        illusion::EnumValue{"Blue", 7}};
};

template <>
struct illusion::EnumReflection<reflection_test::Flags>
{
    static constexpr std::array kValues{illusion::EnumValue{"None", 0}, illusion::EnumValue{"Visible", 1}};
};

namespace reflection_test
{

// Every field type.
struct Everything
{
    bool flag = false;
    illusion::i32 i32Value = -5;
    illusion::u32 u32Value = 5;
    illusion::i64 i64Value = -(illusion::i64{1} << 40);
    illusion::u64 u64Value = illusion::u64{1} << 63;
    illusion::f32 f32Value = 0.1f;
    illusion::f64 f64Value = 0.1;
    illusion::Vec2 vec2{1.0f, 2.0f};
    illusion::Vec3 vec3{1.0f, 2.0f, 3.0f};
    illusion::Vec4 vec4{1.0f, 2.0f, 3.0f, 4.0f};
    illusion::Quat quat{1.0f, 0.0f, 0.0f, 0.0f};
    std::string text = "default";
    illusion::Entity target;
    Team team = Team::Neutral;
    Flags flags = Flags::None;
};

// References another entity.
struct Follow
{
    illusion::Entity leader;
    illusion::f32 distance = 2.0f;
};

// A reflected tag.
struct Selected
{
};

// A component that is never reflected.
struct Unreflected
{
    int value = 0;
};

// Reflects the types above (again; reflection replaces earlier reflections of the same type).
inline void reflectTestComponents()
{
    illusion::reflect<Everything>("test.Everything", 3)
        .field("flag", &Everything::flag)
        .field("i32Value", &Everything::i32Value)
        .field("u32Value", &Everything::u32Value)
        .field("i64Value", &Everything::i64Value)
        .field("u64Value", &Everything::u64Value)
        .field("f32Value", &Everything::f32Value)
        .field("f64Value", &Everything::f64Value)
        .field("vec2", &Everything::vec2)
        .field("vec3", &Everything::vec3)
        .field("vec4", &Everything::vec4)
        .field("quat", &Everything::quat)
        .field("text", &Everything::text)
        .field("target", &Everything::target)
        .field("team", &Everything::team)
        .field("flags", &Everything::flags);
    illusion::reflect<Follow>("test.Follow")
        .field("leader", &Follow::leader)
        .field("distance", &Follow::distance);
    illusion::reflect<Selected>("test.Selected");
}

} // namespace reflection_test

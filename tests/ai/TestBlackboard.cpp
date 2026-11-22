#include <illusion/ai/Blackboard.hpp>

#include <doctest/doctest.h>

using namespace illusion;

namespace
{

BlackboardValue integer(i32 value)
{
    BlackboardValue v;
    v.type = BlackboardType::Int;
    v.set = true;
    v.integer = value;
    return v;
}

BlackboardValue number(f32 value)
{
    BlackboardValue v;
    v.type = BlackboardType::Float;
    v.set = true;
    v.number = value;
    return v;
}

BlackboardValue flag(bool value)
{
    BlackboardValue v;
    v.type = BlackboardType::Bool;
    v.set = true;
    v.flag = value;
    return v;
}

} // namespace

TEST_CASE("Int keys support every ordering comparison")
{
    const BlackboardValue three = integer(3);
    CHECK(compareBlackboard(three, BlackboardCompare::Equal, integer(3), true));
    CHECK_FALSE(compareBlackboard(three, BlackboardCompare::NotEqual, integer(3), true));
    CHECK(compareBlackboard(three, BlackboardCompare::Greater, integer(2), true));
    CHECK_FALSE(compareBlackboard(three, BlackboardCompare::Greater, integer(3), true));
    CHECK(compareBlackboard(three, BlackboardCompare::Less, integer(4), true));
    CHECK(compareBlackboard(three, BlackboardCompare::GreaterEqual, integer(3), true));
    CHECK(compareBlackboard(three, BlackboardCompare::LessEqual, integer(3), true));
    CHECK_FALSE(compareBlackboard(three, BlackboardCompare::LessEqual, integer(2), true));
    CHECK(compareBlackboard(three, BlackboardCompare::IsSet, integer(0), true));
}

TEST_CASE("Float and Bool keys compare by value")
{
    CHECK(compareBlackboard(number(1.5f), BlackboardCompare::Greater, number(1.0f), true));
    CHECK(compareBlackboard(number(1.5f), BlackboardCompare::Less, number(2.0f), true));
    CHECK(compareBlackboard(number(1.5f), BlackboardCompare::Equal, number(1.5f), true));
    CHECK(compareBlackboard(number(1.5f), BlackboardCompare::NotEqual, number(-1.5f), true));
    CHECK_FALSE(compareBlackboard(number(1.5f), BlackboardCompare::GreaterEqual, number(1.0f), true));
    CHECK(compareBlackboard(flag(true), BlackboardCompare::Equal, flag(true), true));
    CHECK(compareBlackboard(flag(true), BlackboardCompare::NotEqual, flag(false), true));
    CHECK_FALSE(compareBlackboard(flag(true), BlackboardCompare::Greater, flag(false), true));
}

TEST_CASE("Vector3 and Entity keys are set or not, and a dead Entity is not set")
{
    BlackboardValue point;
    point.type = BlackboardType::Vector3;
    CHECK(compareBlackboard(point, BlackboardCompare::IsNotSet, point, true));
    point.set = true;
    CHECK(compareBlackboard(point, BlackboardCompare::IsSet, point, true));
    CHECK_FALSE(compareBlackboard(point, BlackboardCompare::Equal, point, true));

    BlackboardValue entity;
    entity.type = BlackboardType::Entity;
    entity.set = true;
    entity.entity = Entity{.index = 4, .generation = 1};
    CHECK(compareBlackboard(entity, BlackboardCompare::IsSet, entity, true));
    CHECK_FALSE(compareBlackboard(entity, BlackboardCompare::IsSet, entity, false));
    CHECK(compareBlackboard(entity, BlackboardCompare::IsNotSet, entity, false));
    entity.set = false;
    CHECK_FALSE(compareBlackboard(entity, BlackboardCompare::IsSet, entity, true));
}

TEST_CASE("blackboard type names are stable")
{
    CHECK(blackboardTypeName(BlackboardType::Bool) == "Bool");
    CHECK(blackboardTypeName(BlackboardType::Vector3) == "Vector3");
    CHECK(blackboardTypeName(BlackboardType::Entity) == "Entity");
}

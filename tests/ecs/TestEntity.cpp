#include <illusion/ecs/Entity.hpp>

#include <doctest/doctest.h>

#include <unordered_set>

using namespace illusion;

TEST_CASE("Default Entity is null")
{
    constexpr Entity entity;
    static_assert(entity.isNull());
    CHECK(entity == kNullEntity);
    CHECK_FALSE(Entity{0, 0}.isNull());
}

TEST_CASE("Entity packs to and from 64 bits")
{
    const Entity entity{123456, 789};
    CHECK(entity.toBits() == ((u64{789} << 32) | 123456));
    CHECK(Entity::fromBits(entity.toBits()) == entity);
}

TEST_CASE("Entity equality and hashing consider index and generation")
{
    CHECK(Entity{1, 0} != Entity{1, 1});
    CHECK(Entity{1, 0} != Entity{2, 0});
    std::unordered_set<Entity> set{Entity{1, 0}, Entity{1, 1}, Entity{1, 0}};
    CHECK(set.size() == 2);
}

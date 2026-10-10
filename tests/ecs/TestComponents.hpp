#pragma once

// Component types shared by the ECS tests. Component types must have unique fully qualified names,
// so they live in a named namespace (never an anonymous one).

#include <illusion/core/Base.hpp>

#include <string>

namespace ecs_test
{

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Velocity
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Health
{
    int value = 100;
};

struct Name
{
    std::string value;
};

struct alignas(64) Aligned64
{
    float data[4] = {};
};

struct Big
{
    illusion::u8 bytes[1000] = {};
};

struct PlayerTag
{
};

struct EnemyTag
{
};

} // namespace ecs_test

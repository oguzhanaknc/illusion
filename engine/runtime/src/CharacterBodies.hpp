#pragma once

// Which physics bodies belong to a character, for the casts that must pass through them (foot placement, the
// camera's spring arm).

#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

namespace illusion::detail
{

// True if @p entity is @p character, one of its ancestors or one of its descendants: casts pass through
// the character's own bodies.
[[nodiscard]] inline bool partOf(const World& world, Entity entity, Entity character)
{
    constexpr int kMaxDepth = 64;
    const auto below = [&world](Entity child, Entity ancestor) {
        Entity current = child;
        for (int depth = 0; depth < kMaxDepth && world.isAlive(current); ++depth)
        {
            if (current == ancestor)
            {
                return true;
            }
            const Hierarchy* hierarchy = world.tryGet<Hierarchy>(current);
            if (hierarchy == nullptr)
            {
                return false;
            }
            current = hierarchy->parent;
        }
        return false;
    };
    return below(entity, character) || below(character, entity);
}

// The body that carries @p character: the nearest of it and its ancestors with a CharacterController or a
// RigidBody, else the Entity itself.
[[nodiscard]] inline Entity carrierOf(const World& world, Entity character)
{
    constexpr int kMaxDepth = 64;
    Entity current = character;
    for (int depth = 0; depth < kMaxDepth && world.isAlive(current); ++depth)
    {
        if (world.has<CharacterController>(current) || world.has<RigidBody>(current))
        {
            return current;
        }
        const Hierarchy* hierarchy = world.tryGet<Hierarchy>(current);
        if (hierarchy == nullptr)
        {
            break;
        }
        current = hierarchy->parent;
    }
    return character;
}

} // namespace illusion::detail

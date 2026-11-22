#pragma once

/// @file
/// The Components of AI (docs/adr/0062-navigation-and-ai.md): the saved BehaviorAgent, AiPerception and
/// AiStimulus, and the runtime AiAgentState that AiWorld adds.

#include <illusion/ai/BehaviorTreeState.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <string>
#include <vector>

namespace illusion
{

class ComponentRegistry;

/// Teams a stimulus can belong to (AiStimulus::team, AiPerception::detectTeams).
inline constexpr u32 kMaxAiTeams = 32;

/// A starting value of a blackboard key, saved with the agent. The key's type says which field is read:
/// Bool and Int from number (non-zero, rounded), Float from number, Vector3 from vector, Entity from entity.
struct BlackboardOverride
{
    std::string key;   ///< The key's name in the tree.
    f32 number = 0.0f; ///< Bool, Int, Float.
    Vec3 vector{0.0f}; ///< Vector3.
    Entity entity;     ///< Entity.
};

/// Runs a behaviour tree (`.behavior`) for the Entity, usually with a NavAgent to move it.
struct BehaviorAgent
{
    AssetRef tree;                              ///< The `.behavior` file.
    bool enabled = true;                        ///< Tick it while the game runs.
    f32 tickInterval = 0.0f;                    ///< Seconds between ticks; 0 ticks every update.
    std::vector<BlackboardOverride> blackboard; ///< Starting values that replace the tree's defaults.
};

/// Lets the Entity see and hear AiStimulus Entities and noises. It looks along its +z axis, the way a
/// NavAgent with faceMovement faces.
struct AiPerception
{
    f32 sightRadius = 15.0f;        ///< In m; 0 is blind.
    f32 sightAngle = 2.0943951f;    ///< Full cone angle about the Entity's +z axis in radians (120°).
    f32 eyeHeight = 1.6f;           ///< Height of the eye above the Entity's position in m.
    u32 occluderMask = 0xFFFFFFFFu; ///< Physics layers that block sight (bit i = layer i).
    f32 hearing = 1.0f;             ///< Multiplier of the radius of noises; 0 is deaf.
    f32 memorySeconds = 5.0f;       ///< How long it remembers what it no longer senses.
    u32 detectTeams = 0xFFFFFFFFu;  ///< Teams it notices (bit i = team i).
    std::string targetKey;   ///< Entity key of its tree set to the chosen target (cleared when forgotten).
    std::string positionKey; ///< Vector3 key of its tree set to the target's last known position.
};

/// Makes the Entity something AiPerception sees.
struct AiStimulus
{
    u32 team = 0;                  ///< In [0, kMaxAiTeams).
    Vec3 offset{0.0f, 1.0f, 0.0f}; ///< Point looked at, relative to the Entity's position in m.
    bool visible = true;           ///< Can be seen now (hiding, invisibility).
};

/// The AI state of an Entity, added and kept by AiWorld (runtime only, never saved).
struct AiAgentState
{
    u32 slot = 0;                                 ///< The Entity's slot in its AiWorld.
    BehaviorStatus status = BehaviorStatus::Idle; ///< The tree's status after the last tick.
    i32 runningNode = -1;                         ///< runningNode() after the last tick.
    Entity target;                                ///< The perception's chosen target, or null.
};

/// Registers BehaviorAgent, AiPerception and AiStimulus (saved, prefixed "illusion.") and AiAgentState
/// (runtime) with @p registry.
void registerAiComponents(ComponentRegistry& registry);

} // namespace illusion

#pragma once

/// @file
/// The AI part of the scripting API (docs/adr/0062-navigation-and-ai.md): game modules write and read the
/// blackboards of behaviour trees, carry out their actions, report noises and read what agents perceive.
///
/// @code
/// if (illusion::AiService* ai = context.service<illusion::AiService>())
/// {
///     std::array<illusion::AiActionRequest, 16> actions{};
///     const illusion::u32 count = ai->pollActions(actions);
///     for (illusion::u32 i = 0; i < count && i < actions.size(); ++i)
///     {
///         if (actions[i].phase == illusion::AiActionPhase::Started && actions[i].name == "bite")
///         {
///             static_cast<void>(ai->finishAction(actions[i].entity, actions[i].action,
///             bite(actions[i].entity)));
///         }
///     }
///     if (jumped)
///     {
///         static_cast<void>(ai->reportNoise({.position = playerPosition, .radius = 8.0f, .instigator =
///         player}));
///     }
/// }
/// @endcode

#include <illusion/ai/Blackboard.hpp>
#include <illusion/ai/Perception.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <span>
#include <string_view>

namespace illusion
{

/// Why an AiService call failed.
enum class AiError : u8
{
    NoAgent,       ///< The Entity has no BehaviorAgent or AiPerception (or is not alive).
    NoTree,        ///< The agent's tree has not loaded (or failed to load).
    UnknownKey,    ///< The tree has no key of that name.
    TypeMismatch,  ///< The key has another type.
    UnknownAction, ///< No running action of that id on the Entity.
    InvalidValue,  ///< A value is not finite or out of range (a negative radius, a team out of range).
    Full,          ///< The noises of this update are full.
};

/// Stable name of @p error, e.g. "UnknownKey".
[[nodiscard]] constexpr std::string_view aiErrorName(AiError error) noexcept
{
    switch (error)
    {
    case AiError::NoAgent:
        return "NoAgent";
    case AiError::NoTree:
        return "NoTree";
    case AiError::UnknownKey:
        return "UnknownKey";
    case AiError::TypeMismatch:
        return "TypeMismatch";
    case AiError::UnknownAction:
        return "UnknownAction";
    case AiError::InvalidValue:
        return "InvalidValue";
    case AiError::Full:
        return "Full";
    }
    return "InvalidValue";
}

/// Whether an action started or was aborted.
enum class AiActionPhase : u8
{
    Started, ///< The Action node started; finish it with AiService::finishAction().
    Aborted, ///< A guard or a parent ended it; do not finish it.
};

/// An action of a behaviour tree that the last update started or aborted.
struct AiActionRequest
{
    Entity entity;                                ///< The agent.
    u32 action = 0;                               ///< Id of the action, unique per agent while it runs.
    std::string_view name{};                      ///< The Action node's name for game code.
    AiActionPhase phase = AiActionPhase::Started; ///< Started or aborted.
};

/// AI of the running game, implemented by the host (AiWorldService over an AiWorld) and found by modules
/// with `context.service<AiService>()`. Blackboard writes are seen by the next AI step; reads return the
/// values after the last one. String views it returns stay valid until the next update. Adding a function
/// changes the module API version.
class AiService : NonCopyable
{
public:
    /// Sets the Bool key @p key of @p entity's blackboard.
    virtual Expected<void, AiError> setBool(Entity entity, std::string_view key, bool value) = 0;
    /// Sets the Int key @p key.
    virtual Expected<void, AiError> setInt(Entity entity, std::string_view key, i32 value) = 0;
    /// Sets the Float key @p key.
    virtual Expected<void, AiError> setFloat(Entity entity, std::string_view key, f32 value) = 0;
    /// Sets the Vector3 key @p key.
    virtual Expected<void, AiError> setVector(Entity entity, std::string_view key, const Vec3& value) = 0;
    /// Sets the Entity key @p key; a null @p value clears it.
    virtual Expected<void, AiError> setEntity(Entity entity, std::string_view key, Entity value) = 0;
    /// Clears the Vector3 or Entity key @p key.
    virtual Expected<void, AiError> clearValue(Entity entity, std::string_view key) = 0;

    /// The value of @p key of @p entity's blackboard.
    [[nodiscard]] virtual Expected<BlackboardValue, AiError> value(Entity entity,
                                                                   std::string_view key) const = 0;

    /// Writes the actions the last update started or aborted into @p out, in agent order, and returns how
    /// many there are; every call in an update returns the same actions.
    virtual u32 pollActions(std::span<AiActionRequest> out) = 0;

    /// Finishes the running action @p action of @p entity; its node succeeds or fails at the next AI step.
    virtual Expected<void, AiError> finishAction(Entity entity, u32 action, bool success) = 0;

    /// Reports a noise that perceivers hear in the next AI step.
    virtual Expected<void, AiError> reportNoise(const AiNoise& noise) = 0;

    /// Writes what @p entity's AiPerception senses or remembers into @p out, nearest first, and returns how
    /// many there are.
    virtual Expected<u32, AiError> perceived(Entity entity, std::span<AiPerceived> out) const = 0;

    /// The name of the deepest running node of @p entity's tree (empty when none runs).
    [[nodiscard]] virtual Expected<std::string_view, AiError> runningNode(Entity entity) const = 0;

    /// Starts @p entity's tree again from its root with the starting blackboard; running actions abort.
    virtual Expected<void, AiError> restart(Entity entity) = 0;

protected:
    AiService() = default;
    virtual ~AiService() = default;
};

} // namespace illusion

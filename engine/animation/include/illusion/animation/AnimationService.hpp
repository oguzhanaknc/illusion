#pragma once

/// @file
/// The animation part of the scripting API (docs/adr/0060-animation-graphs.md): game modules drive the
/// animation graphs of their Animators with parameters, cross-fade Animators without a graph, read the state
/// events and joint placements of the last update, and aim look-at and IK
/// (docs/adr/0061-sockets-ik-and-ragdolls.md) and aim (docs/adr/0065-retargeting-and-aim-layers.md).
///
/// @code
/// if (illusion::AnimationService* animation = context.service<illusion::AnimationService>())
/// {
///     static_cast<void>(animation->setFloat(hero, "speed", glm::length(input)));
///     if (jumpPressed)
///     {
///         static_cast<void>(animation->setTrigger(hero, "jump"));
///     }
///     std::array<illusion::AnimationEvent, 16> events{};
///     const illusion::u32 count = animation->pollEvents(events);
///     for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
///     {
///         if (events[i].name == "footstep")
///         {
///             playFootstep(events[i].entity);
///         }
///     }
/// }
/// @endcode

#include <illusion/animation/Aim.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>

#include <span>
#include <string_view>

namespace illusion
{

/// Why an AnimationService call failed.
enum class AnimationError : u8
{
    NoAnimator, ///< The Entity has no Animator (or is not alive).
    NoGraph, ///< The call needs a graph (jointTransform(): a pose) that the Animator lacks or has not loaded.
    HasGraph,     ///< play() on an Animator whose graph controls it.
    UnknownName,  ///< No parameter, layer, clip or joint of that name, or no LookAt, TwoBoneIk or Aim for it.
    TypeMismatch, ///< The parameter has another type (setFloat() on a Trigger, ...).
    InvalidValue, ///< A value is not finite or out of range (a negative fade).
};

/// Stable name of @p error, e.g. "UnknownName".
[[nodiscard]] constexpr std::string_view animationErrorName(AnimationError error) noexcept
{
    switch (error)
    {
    case AnimationError::NoAnimator:
        return "NoAnimator";
    case AnimationError::NoGraph:
        return "NoGraph";
    case AnimationError::HasGraph:
        return "HasGraph";
    case AnimationError::UnknownName:
        return "UnknownName";
    case AnimationError::TypeMismatch:
        return "TypeMismatch";
    case AnimationError::InvalidValue:
        return "InvalidValue";
    }
    return "InvalidValue";
}

/// A state event that the last update crossed.
struct AnimationEvent
{
    Entity entity;            ///< The Entity of the Animator.
    std::string_view name{};  ///< The event's name in the graph.
    std::string_view layer{}; ///< The layer of the state.
    std::string_view state{}; ///< The state.
};

/// Skeletal animation of the running game (ADR 0059, ADR 0060), implemented by the host
/// (AnimationWorldService over an AnimationWorld) and found by modules with
/// `context.service<AnimationService>()`. Parameter writes take effect at the next Animation step; reads
/// return the values the graph uses. String views it returns stay valid until the next update. Adding a
/// function changes the module API version.
/// @ingroup scripting_api
class AnimationService : NonCopyable
{
public:
    /// Sets the Float parameter @p name of @p entity's graph.
    virtual Expected<void, AnimationError> setFloat(Entity entity, std::string_view name, f32 value) = 0;
    /// Sets the Int parameter @p name of @p entity's graph.
    virtual Expected<void, AnimationError> setInt(Entity entity, std::string_view name, i32 value) = 0;
    /// Sets the Bool parameter @p name of @p entity's graph.
    virtual Expected<void, AnimationError> setBool(Entity entity, std::string_view name, bool value) = 0;
    /// Sets the Trigger @p name of @p entity's graph until a transition consumes it or the next step ends.
    virtual Expected<void, AnimationError> setTrigger(Entity entity, std::string_view name) = 0;

    /// The value of the Float, Int or Bool parameter @p name (Int and Bool as numbers).
    [[nodiscard]] virtual Expected<f32, AnimationError> parameter(Entity entity,
                                                                  std::string_view name) const = 0;

    /// The current state of the layer @p layer (the target of a running transition).
    [[nodiscard]] virtual Expected<std::string_view, AnimationError>
    currentState(Entity entity, std::string_view layer) const = 0;

    /// The normalized time of the current state of @p layer (may exceed 1 for a finished non-looping state).
    [[nodiscard]] virtual Expected<f32, AnimationError> normalizedTime(Entity entity,
                                                                       std::string_view layer) const = 0;

    /// Cross-fades an Animator without a graph from its clip to the clip @p clip
    /// (`model.glb#animation/<name>`) over @p fadeSeconds, restarting at time 0; sets Animator::clip, so the
    /// change is saved.
    virtual Expected<void, AnimationError> play(Entity entity, std::string_view clip, f32 fadeSeconds) = 0;

    /// Writes the events of the last update into @p out, in update order, and returns how many there are;
    /// every call in an update returns the same events.
    virtual u32 pollEvents(std::span<AnimationEvent> out) = 0;

    /// The world-space transform of the joint @p joint of @p entity's skeleton from the last update (where a
    /// socket would place a child); UnknownName for a joint the skeleton lacks, NoGraph while it loads.
    [[nodiscard]] virtual Expected<Mat4, AnimationError> jointTransform(Entity entity,
                                                                        std::string_view joint) const = 0;

    /// Sets the world-space point the LookAt of @p entity looks at (and clears its target Entity).
    virtual Expected<void, AnimationError> setLookAtTarget(Entity entity, const Vec3& point) = 0;
    /// Sets the world-space target of the TwoBoneIk of @p entity whose end joint is @p joint (clears its
    /// target Entity).
    virtual Expected<void, AnimationError> setIkTarget(Entity entity, std::string_view joint,
                                                       const Vec3& point) = 0;
    /// Sets the weight of the TwoBoneIk of @p entity whose end joint is @p joint, clamped to [0, 1] (0 turns
    /// it off).
    virtual Expected<void, AnimationError> setIkWeight(Entity entity, std::string_view joint, f32 weight) = 0;

    /// Turns the Ragdoll of @p entity on (the body falls from its current pose and velocity) or off (the pose
    /// blends back to the animation); NoAnimator without an Animator, UnknownName without a Ragdoll.
    virtual Expected<void, AnimationError> setRagdoll(Entity entity, bool active) = 0;

    /// Sets the world-space point the Aim of @p entity aims at (and clears its target Entity).
    virtual Expected<void, AnimationError> setAimTarget(Entity entity, const Vec3& point) = 0;
    /// Sets the weight of the Aim of @p entity's procedural turn, clamped to [0, 1].
    virtual Expected<void, AnimationError> setAimWeight(Entity entity, f32 weight) = 0;
    /// The yaw and pitch the Aim of @p entity aimed at in the last update (smoothed and clamped, radians).
    [[nodiscard]] virtual Expected<AimAngles, AnimationError> aimAngles(Entity entity) const = 0;

protected:
    AnimationService() = default;
    virtual ~AnimationService() = default;
};

} // namespace illusion

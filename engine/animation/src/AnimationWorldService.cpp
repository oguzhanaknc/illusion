#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationGraph.hpp>
#include <illusion/animation/AnimationWorldService.hpp>
#include <illusion/core/AssetId.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace illusion
{
namespace
{

// Int parameters are kept as f32, which holds whole numbers exactly up to 2^24.
constexpr i32 kMaxIntParameter = 1 << 24;

struct GraphTarget
{
    u32 slot = 0;
    const AnimationGraph* graph = nullptr;
    const AnimationGraphState* state = nullptr;
};

[[nodiscard]] const Animator* animatorOf(const AnimationWorld& animation, Entity entity) noexcept
{
    const World& world = animation.world();
    return world.isAlive(entity) ? world.tryGet<Animator>(entity) : nullptr;
}

[[nodiscard]] Expected<GraphTarget, AnimationError> graphOf(const AnimationWorld& animation, Entity entity)
{
    const Animator* animator = animatorOf(animation, entity);
    if (animator == nullptr)
    {
        return unexpected(AnimationError::NoAnimator);
    }
    const std::optional<u32> slot = animation.findSlot(entity);
    const AnimationGraph* graph = animator->graph && slot ? animation.graph(*slot) : nullptr;
    if (graph == nullptr)
    {
        return unexpected(AnimationError::NoGraph);
    }
    return GraphTarget{*slot, graph, animation.graphState(*slot)};
}

// The parameter @p name of @p target's graph, which must have the type @p type.
[[nodiscard]] Expected<i32, AnimationError> parameterOf(const GraphTarget& target, std::string_view name,
                                                        AnimationParameterType type)
{
    const i32 index = target.graph->findParameter(name);
    if (index < 0)
    {
        return unexpected(AnimationError::UnknownName);
    }
    if (target.graph->parameters[static_cast<usize>(index)].type != type)
    {
        return unexpected(AnimationError::TypeMismatch);
    }
    return index;
}

[[nodiscard]] Expected<const GraphLayerState*, AnimationError> layerOf(const GraphTarget& target,
                                                                       std::string_view layer)
{
    const i32 index = target.graph->findLayer(layer);
    if (index < 0 || static_cast<usize>(index) >= target.state->layers.size())
    {
        return unexpected(AnimationError::UnknownName);
    }
    return &target.state->layers[static_cast<usize>(index)];
}

} // namespace

AnimationWorldService::~AnimationWorldService() = default;

Expected<void, AnimationError> AnimationWorldService::setFloat(Entity entity, std::string_view name,
                                                               f32 value)
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const Expected<i32, AnimationError> index = parameterOf(*target, name, AnimationParameterType::Float);
    if (!index)
    {
        return unexpected(index.error());
    }
    if (!std::isfinite(value))
    {
        return unexpected(AnimationError::InvalidValue);
    }
    m_animation.setParameter(target->slot, *index, value);
    return {};
}

Expected<void, AnimationError> AnimationWorldService::setInt(Entity entity, std::string_view name, i32 value)
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const Expected<i32, AnimationError> index = parameterOf(*target, name, AnimationParameterType::Int);
    if (!index)
    {
        return unexpected(index.error());
    }
    if (value > kMaxIntParameter || value < -kMaxIntParameter)
    {
        return unexpected(AnimationError::InvalidValue);
    }
    m_animation.setParameter(target->slot, *index, static_cast<f32>(value));
    return {};
}

Expected<void, AnimationError> AnimationWorldService::setBool(Entity entity, std::string_view name,
                                                              bool value)
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const Expected<i32, AnimationError> index = parameterOf(*target, name, AnimationParameterType::Bool);
    if (!index)
    {
        return unexpected(index.error());
    }
    m_animation.setParameter(target->slot, *index, value ? 1.0f : 0.0f);
    return {};
}

Expected<void, AnimationError> AnimationWorldService::setTrigger(Entity entity, std::string_view name)
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const Expected<i32, AnimationError> index = parameterOf(*target, name, AnimationParameterType::Trigger);
    if (!index)
    {
        return unexpected(index.error());
    }
    m_animation.setTrigger(target->slot, *index);
    return {};
}

Expected<f32, AnimationError> AnimationWorldService::parameter(Entity entity, std::string_view name) const
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const i32 index = target->graph->findParameter(name);
    if (index < 0 || static_cast<usize>(index) >= target->state->parameters.size())
    {
        return unexpected(AnimationError::UnknownName);
    }
    if (target->graph->parameters[static_cast<usize>(index)].type == AnimationParameterType::Trigger)
    {
        return unexpected(AnimationError::TypeMismatch);
    }
    return target->state->parameters[static_cast<usize>(index)];
}

Expected<std::string_view, AnimationError> AnimationWorldService::currentState(Entity entity,
                                                                               std::string_view layer) const
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const Expected<const GraphLayerState*, AnimationError> machine = layerOf(*target, layer);
    if (!machine)
    {
        return unexpected(machine.error());
    }
    const auto index = static_cast<usize>(target->graph->findLayer(layer));
    return std::string_view(target->graph->layers[index].states[(*machine)->state].name);
}

Expected<f32, AnimationError> AnimationWorldService::normalizedTime(Entity entity,
                                                                    std::string_view layer) const
{
    const Expected<GraphTarget, AnimationError> target = graphOf(m_animation, entity);
    if (!target)
    {
        return unexpected(target.error());
    }
    const Expected<const GraphLayerState*, AnimationError> machine = layerOf(*target, layer);
    if (!machine)
    {
        return unexpected(machine.error());
    }
    return (*machine)->time;
}

Expected<void, AnimationError> AnimationWorldService::play(Entity entity, std::string_view clip,
                                                           f32 fadeSeconds)
{
    const Animator* animator = animatorOf(m_animation, entity);
    if (animator == nullptr)
    {
        return unexpected(AnimationError::NoAnimator);
    }
    if (animator->graph)
    {
        return unexpected(AnimationError::HasGraph);
    }
    const std::optional<AssetPathParts> parts = splitAssetPath(clip);
    if (!parts || parts->fragment != AssetFragment::Animation)
    {
        return unexpected(AnimationError::UnknownName);
    }
    if (!std::isfinite(fadeSeconds) || fadeSeconds < 0.0f)
    {
        return unexpected(AnimationError::InvalidValue);
    }
    const AssetRef ref(clip);
    if (!ref)
    {
        return unexpected(AnimationError::UnknownName);
    }
    m_animation.crossFade(entity, ref, fadeSeconds);
    return {};
}

u32 AnimationWorldService::pollEvents(std::span<AnimationEvent> out)
{
    const std::span<const AnimationEvent> events = m_animation.events();
    std::copy_n(events.begin(), std::min(events.size(), out.size()), out.begin());
    return static_cast<u32>(events.size());
}

} // namespace illusion

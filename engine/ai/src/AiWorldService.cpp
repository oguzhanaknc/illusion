#include <illusion/ai/AiComponents.hpp>
#include <illusion/ai/AiWorldService.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>

namespace illusion
{

AiWorldService::~AiWorldService() = default;

Expected<void, AiError> AiWorldService::setBool(Entity entity, std::string_view key, bool value)
{
    BlackboardValue entry;
    entry.type = BlackboardType::Bool;
    entry.flag = value;
    return m_ai.setValue(entity, key, entry);
}

Expected<void, AiError> AiWorldService::setInt(Entity entity, std::string_view key, i32 value)
{
    BlackboardValue entry;
    entry.type = BlackboardType::Int;
    entry.integer = value;
    return m_ai.setValue(entity, key, entry);
}

Expected<void, AiError> AiWorldService::setFloat(Entity entity, std::string_view key, f32 value)
{
    BlackboardValue entry;
    entry.type = BlackboardType::Float;
    entry.number = value;
    return m_ai.setValue(entity, key, entry);
}

Expected<void, AiError> AiWorldService::setVector(Entity entity, std::string_view key, const Vec3& value)
{
    BlackboardValue entry;
    entry.type = BlackboardType::Vector3;
    entry.set = true;
    entry.vector = value;
    return m_ai.setValue(entity, key, entry);
}

Expected<void, AiError> AiWorldService::setEntity(Entity entity, std::string_view key, Entity value)
{
    BlackboardValue entry;
    entry.type = BlackboardType::Entity;
    entry.set = value != kNullEntity;
    entry.entity = value;
    return m_ai.setValue(entity, key, entry);
}

Expected<void, AiError> AiWorldService::clearValue(Entity entity, std::string_view key)
{
    const Expected<BlackboardValue, AiError> current = m_ai.value(entity, key);
    if (!current)
    {
        return unexpected(current.error());
    }
    if (current->type != BlackboardType::Vector3 && current->type != BlackboardType::Entity)
    {
        return unexpected(AiError::TypeMismatch);
    }
    BlackboardValue entry;
    entry.type = current->type;
    return m_ai.setValue(entity, key, entry);
}

Expected<BlackboardValue, AiError> AiWorldService::value(Entity entity, std::string_view key) const
{
    return m_ai.value(entity, key);
}

u32 AiWorldService::pollActions(std::span<AiActionRequest> out)
{
    const std::span<const AiActionRequest> actions = m_ai.actions();
    std::copy_n(actions.begin(), std::min(out.size(), actions.size()), out.begin());
    return static_cast<u32>(actions.size());
}

Expected<void, AiError> AiWorldService::finishAction(Entity entity, u32 action, bool success)
{
    return m_ai.finishAction(entity, action, success);
}

Expected<void, AiError> AiWorldService::reportNoise(const AiNoise& noise)
{
    return m_ai.reportNoise(noise);
}

Expected<u32, AiError> AiWorldService::perceived(Entity entity, std::span<AiPerceived> out) const
{
    return m_ai.perceived(entity, out);
}

Expected<std::string_view, AiError> AiWorldService::runningNode(Entity entity) const
{
    const World& world = m_ai.world();
    if (!world.isAlive(entity) || (!world.has<BehaviorAgent>(entity) && !world.has<AiPerception>(entity)))
    {
        return unexpected(AiError::NoAgent);
    }
    const std::optional<u32> slot = m_ai.findSlot(entity);
    const BehaviorTree* tree = slot ? m_ai.tree(*slot) : nullptr;
    if (tree == nullptr)
    {
        return unexpected(AiError::NoTree);
    }
    const i32 node = illusion::runningNode(*tree, *m_ai.treeState(*slot));
    if (node < 0)
    {
        return std::string_view{};
    }
    return std::string_view(tree->nodes[static_cast<usize>(node)].name);
}

Expected<void, AiError> AiWorldService::restart(Entity entity)
{
    return m_ai.restart(entity);
}

} // namespace illusion

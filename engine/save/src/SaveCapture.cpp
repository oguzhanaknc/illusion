#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/save/SaveCapture.hpp>
#include <illusion/save/SaveComponents.hpp>

#include <algorithm>
#include <format>
#include <unordered_set>
#include <utility>

namespace illusion
{

void collectSavedEntities(const World& world, std::vector<Entity>& out)
{
    ILL_PROFILE_SCOPE();
    out.clear();
    ComponentMask tagged;
    tagged.set(componentId<Saved>());
    world.forEachMatching(tagged, {}, [&](const RawChunk& chunk) {
        const std::span<const Entity> entities = chunk.entities();
        out.insert(out.end(), entities.begin(), entities.end());
    });
    if (out.empty())
    {
        return;
    }
    // Every parent link, sorted by parent, to find children by binary search.
    std::vector<std::pair<u64, Entity>> links;
    ComponentMask children;
    children.set(componentId<Hierarchy>());
    world.forEachMatching(children, {}, [&](const RawChunk& chunk) {
        const auto* hierarchies = static_cast<const Hierarchy*>(chunk.column(componentId<Hierarchy>()));
        const std::span<const Entity> entities = chunk.entities();
        for (u32 i = 0; i < chunk.size(); ++i)
        {
            if (!hierarchies[i].parent.isNull())
            {
                links.emplace_back(hierarchies[i].parent.toBits(), entities[i]);
            }
        }
    });
    std::sort(links.begin(), links.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::unordered_set<Entity> seen(out.begin(), out.end());
    for (usize i = 0; i < out.size(); ++i)
    {
        const u64 key = out[i].toBits();
        auto it = std::lower_bound(links.begin(), links.end(), key,
                                   [](const auto& link, u64 value) { return link.first < value; });
        for (; it != links.end() && it->first == key; ++it)
        {
            if (seen.insert(it->second).second)
            {
                out.push_back(it->second);
            }
        }
    }
}

Expected<SaveCapture, SaveFailure> captureSave(World& world, const SaveCaptureOptions& options)
{
    ILL_PROFILE_SCOPE();
    std::vector<Entity> saved;
    collectSavedEntities(world, saved);
    assignMissingGuids(world, options.guids != nullptr ? *options.guids : GuidGenerator::shared());
    SceneSaveOptions save;
    save.subset = std::span<const Entity>(saved);
    save.order = SceneOrder::Hierarchy;
    save.prefabMode = PrefabSaveMode::Expanded;
    save.externalReferences = true;
    SceneSaveResult result = saveScene(world, save);
    if (!result.ok())
    {
        return unexpected(SaveFailure{SaveError::InvalidScene, std::move(result.error)});
    }
    return SaveCapture{std::move(result.json), result.entities};
}

Expected<SaveApplied, SaveFailure> applySave(World& world, std::string_view json)
{
    ILL_PROFILE_SCOPE();
    std::vector<Entity> saved;
    collectSavedEntities(world, saved);
    SceneLoadOptions load;
    load.ids = IdPolicy::Keep;
    load.resolveExternal = true;
    load.replace = saved;
    SceneLoadResult result = loadScene(world, json, load);
    if (!result.ok())
    {
        return unexpected(SaveFailure{SaveError::InvalidScene, std::move(result.error)});
    }
    SaveApplied applied;
    applied.replaced = static_cast<u32>(saved.size());
    for (std::string& warning : result.warnings)
    {
        if (!warning.starts_with("unknown component type '") || !warning.ends_with("' preserved"))
        {
            applied.warnings.push_back(std::move(warning));
        }
    }
    // Types the game no longer has are dropped rather than kept as UnknownComponents (ADR 0058).
    for (const Entity entity : result.entities)
    {
        if (world.has<UnknownComponents>(entity))
        {
            for (const PreservedValue& value : world.get<UnknownComponents>(entity).components)
            {
                applied.warnings.push_back(std::format("component type '{}' of entity {} dropped", value.type,
                                                       toString(world.get<EntityGuid>(entity))));
            }
            static_cast<void>(world.remove<UnknownComponents>(entity));
        }
    }
    for (const std::string& warning : applied.warnings)
    {
        ILL_LOG_WARN("Save", "{}", warning);
    }
    applied.entities = std::move(result.entities);
    return applied;
}

} // namespace illusion

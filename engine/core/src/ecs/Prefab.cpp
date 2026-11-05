#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>

namespace illusion
{
namespace
{

// splitmix64's finalizer: a bijection on 64-bit values with good avalanche.
constexpr u64 mix(u64 value) noexcept
{
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

} // namespace

EntityGuid derivePrefabGuid(EntityGuid instance, EntityGuid local) noexcept
{
    const u64 value = mix(instance.value + 0x9e3779b97f4a7c15ull * mix(local.value ^ 0x632be59bd9b4e019ull));
    return EntityGuid{value != 0 ? value : 1};
}

namespace detail
{

std::string instanceRecordScene(std::string_view prefab, EntityGuid instance)
{
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("format", doc.makeString("illusion.scene")).add("version", doc.makeU64(kSceneFormatVersion));
    const std::array<char, kGuidTextLength> id = formatGuid(instance);
    root.addArray("entities")
        .pushObject()
        .add("prefab", doc.makeString(prefab))
        .add("id", doc.makeString(std::string_view(id.data(), id.size())));
    std::string text;
    (void)doc.write(text, JsonWriteOptions{.pretty = false});
    return text;
}

} // namespace detail

PrefabInstantiateResult instantiatePrefab(World& world, const PrefabData& prefab, Entity parent,
                                          EntityGuid instance, const PrefabSource& nested)
{
    ILL_PROFILE_SCOPE();
    PrefabInstantiateResult result;
    SceneLoadOptions options;
    options.prefabs = [&](std::string_view path) -> std::optional<std::string> {
        if (path == prefab.path)
        {
            return prefab.json;
        }
        return nested ? nested(path) : std::nullopt;
    };
    SceneLoadResult loaded = loadScene(world, detail::instanceRecordScene(prefab.path, instance), options);
    if (!loaded.ok())
    {
        result.error = std::move(loaded.error);
        return result;
    }
    if (loaded.entities.size() == 1 && world.has<UnresolvedPrefab>(loaded.entities.front()))
    {
        world.destroy(loaded.entities.front());
        result.error = loaded.warnings.empty() ? std::string("the prefab could not be loaded")
                                               : std::move(loaded.warnings.back());
        return result;
    }
    const auto root = std::find_if(loaded.entities.begin(), loaded.entities.end(), [&](Entity entity) {
        const PrefabInstance* placed = world.tryGet<PrefabInstance>(entity);
        return placed != nullptr && placed->instance == instance;
    });
    std::rotate(loaded.entities.begin(), root, root + 1);
    result.root = loaded.entities.front();
    // The prefab's root has no parent in its file: place it like a new child (or root) of the scene.
    if (parent.isNull())
    {
        (void)detach(world, result.root, ParentMode::KeepLocal);
    }
    else if (!setParent(world, result.root, parent, ParentMode::KeepLocal))
    {
        for (const Entity entity : loaded.entities)
        {
            world.destroy(entity);
        }
        result.root = Entity{};
        result.error = "the parent is not alive";
        return result;
    }
    result.entities = std::move(loaded.entities);
    result.warnings = std::move(loaded.warnings);
    return result;
}

} // namespace illusion

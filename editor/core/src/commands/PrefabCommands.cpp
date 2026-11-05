#include "CommandSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/WorldEditor.hpp>

#include <unordered_map>

namespace illusion::editor::detail
{
namespace
{

// Writes Entity fields as the EntityGuids of their World (for values copied between Worlds).
class GuidWriter final : public EntityResolver
{
public:
    explicit GuidWriter(const World& world) noexcept
        : m_world(world)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        const EntityGuid* guid = m_world.isAlive(entity) ? m_world.tryGet<EntityGuid>(entity)
                                                         : static_cast<const EntityGuid*>(nullptr);
        if (guid == nullptr)
        {
            return doc.makeNull();
        }
        const std::array<char, kGuidTextLength> text = formatGuid(*guid);
        return doc.makeString(std::string_view(text.data(), text.size()));
    }

    Expected<Entity, std::string> readEntity(JsonValue) const override
    {
        return unexpected(std::string("cannot be read by a guid writer"));
    }

private:
    const World& m_world;
};

Expected<void, CommandError> checkPrefabPath(CommandContext& context, std::string_view path)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return open;
    }
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || lowerExtension(path) != kPrefabExtension ||
        path.size() <= kAssetsRoot.size() + kPrefabExtension.size() ||
        context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            "/path", "must be a .prefab file in the assets: mount, e.g. \"assets:/props/crate.prefab\""));
    }
    return {};
}

// The text of the prefab file @p path.
Expected<std::string, CommandError> readPrefabText(CommandContext& context, std::string_view path)
{
    if (Expected<void, CommandError> valid = checkPrefabPath(context, path); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    std::string text;
    if (const FileError error = context.editor().files().readText(path, text); error != FileError::None)
    {
        return unexpected(
            error == FileError::NotFound
                ? CommandError::make(ErrorKind::NotFound, "'" + std::string(path) + "' does not exist")
                : CommandError::make(ErrorKind::IoError, "cannot read '" + std::string(path) + "' (" +
                                                             std::string(fileErrorName(error)) + ")"));
    }
    return text;
}

// Reads prefabs through the project, except @p path, whose text is @p text (not yet on disk in a dry run).
PrefabSource sourceWith(CommandContext& context, std::string path, std::string text)
{
    return [source = context.editor().prefabSource(), path = std::move(path),
            text = std::move(text)](std::string_view wanted) -> std::optional<std::string> {
        return wanted == path ? std::optional<std::string>(text) : source(wanted);
    };
}

// The root of the instance that @p entity (params member "/entity") belongs to.
Expected<Entity, CommandError> instanceRootOf(CommandContext& context, Entity entity)
{
    World& world = context.world();
    const PrefabMember* member = world.tryGet<PrefabMember>(entity);
    if (member == nullptr)
    {
        return unexpected(CommandError::invalid("/entity", "is not an entity of a prefab instance"));
    }
    const EntityGuid instance = member->instance;
    std::vector<Entity> all;
    world.entities(all);
    for (const Entity candidate : all)
    {
        const PrefabInstance* root = world.tryGet<PrefabInstance>(candidate);
        const PrefabMember* rootMember = world.tryGet<PrefabMember>(candidate);
        if (root != nullptr && rootMember != nullptr && root->instance == instance &&
            rootMember->instance == instance)
        {
            return candidate;
        }
    }
    return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                         "the instance of the entity has lost its root; unpack it"));
}

// The entities of the instance @p instance.
std::vector<Entity> membersOf(const World& world, EntityGuid instance)
{
    std::vector<Entity> all;
    world.entities(all);
    std::erase_if(all, [&](Entity entity) {
        const PrefabMember* member = world.tryGet<PrefabMember>(entity);
        return member == nullptr || member->instance != instance;
    });
    return all;
}

std::string guidString(EntityGuid guid)
{
    const std::array<char, kGuidTextLength> text = formatGuid(guid);
    return std::string(text.data(), text.size());
}

PrefabInstanceResult instanceResult(CommandContext& context, Entity root)
{
    const PrefabInstance& instance = context.world().get<PrefabInstance>(root);
    return PrefabInstanceResult{context.summary(root), guidString(instance.instance),
                                std::string(instance.prefab.path()),
                                static_cast<u32>(membersOf(context.world(), instance.instance).size())};
}

// Creates an instance of the prefab @p path (text @p text) under @p parent at @p index through the editor.
Expected<Entity, CommandError> placeInstance(CommandContext& context, const std::string& path,
                                             const std::string& text, Entity parent, std::optional<u32> index)
{
    const PrefabSource source = sourceWith(context, path, text);
    {
        // Check first: paste keeps a prefab that cannot be loaded as an unresolved record.
        World scratch;
        const PrefabInstantiateResult trial =
            instantiatePrefab(scratch, PrefabData{path, text}, Entity{}, EntityGuid{1}, source);
        if (!trial.ok())
        {
            return unexpected(CommandError::invalid("/path", "is not a valid prefab: " + trial.error));
        }
    }
    Expected<std::vector<Entity>, CommandError> roots = context.edit().paste(
        illusion::detail::instanceRecordScene(path, EntityGuid{1}), parent, index, source);
    if (!roots)
    {
        return unexpected(std::move(roots).error());
    }
    if (roots->size() != 1 || !context.world().has<PrefabInstance>(roots->front()))
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "the prefab instance was not created"));
    }
    return roots->front();
}

// The prefab text of the subtree of @p root, its root placed where it is in the world.
Expected<std::pair<std::string, u32>, CommandError> prefabText(CommandContext& context, Entity root)
{
    World& world = context.world();
    const HierarchyIndex& hierarchy = context.index().hierarchy();
    std::vector<Entity> subset;
    const std::span<const Entity> subtree = hierarchy.subtree(root);
    if (subtree.empty())
    {
        subset.push_back(root);
    }
    else
    {
        subset.assign(subtree.begin(), subtree.end());
    }
    const PrefabSource source = context.editor().prefabSource();
    SceneSaveOptions options;
    options.subset = std::span<const Entity>(subset);
    options.prefabFile = true;
    options.prefabs = source;
    const SceneSaveResult saved = saveScene(world, options);
    if (!saved.ok())
    {
        return unexpected(CommandError::invalid("/entity", "cannot be saved as a prefab: " + saved.error));
    }
    // The root keeps its world placement as its local one: a prefab's root has no parent.
    World copy;
    SceneLoadOptions load;
    load.prefabs = source;
    const SceneLoadResult loaded = loadScene(copy, saved.json, load);
    if (!loaded.ok())
    {
        return unexpected(
            CommandError::make(ErrorKind::Internal, "cannot read the prefab back: " + loaded.error));
    }
    const EntityGuid rootId = world.get<EntityGuid>(root);
    for (const Entity entity : loaded.entities)
    {
        if (copy.get<EntityGuid>(entity) != rootId)
        {
            continue;
        }
        const Transform placed = worldTransformOf(world, root);
        if (Transform* transform = copy.tryGet<Transform>(entity))
        {
            *transform = placed;
        }
        if (LocalTransform* local = copy.tryGet<LocalTransform>(entity))
        {
            *local = LocalTransform::from(placed);
        }
    }
    SceneSaveOptions again;
    again.prefabFile = true;
    again.prefabs = source;
    const SceneSaveResult text = saveScene(copy, again);
    if (!text.ok())
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot write the prefab: " + text.error));
    }
    return std::pair<std::string, u32>(text.json, saved.entities);
}

Expected<PrefabCreateResult, CommandError> prefabCreate(CommandContext& context,
                                                        const PrefabCreateParams& params)
{
    if (Expected<void, CommandError> valid = checkPrefabPath(context, params.path); !valid)
    {
        return unexpected(std::move(valid).error());
    }
    Expected<Entity, CommandError> root = context.entity(params.entity, "/entity");
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    if (!params.overwrite.value_or(false) && context.editor().files().exists(params.path))
    {
        return unexpected(
            CommandError::make(ErrorKind::AlreadyExists, "'" + params.path + "' exists already"));
    }
    Expected<std::pair<std::string, u32>, CommandError> text = prefabText(context, *root);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    Expected<bool, CommandError> created = context.edit().writeFile(params.path, text->first);
    if (!created)
    {
        return unexpected(std::move(created).error());
    }
    PrefabCreateResult result{params.path, *created, text->second, std::nullopt};
    if (!params.replace.value_or(true))
    {
        return result;
    }

    // Replace the entities with an instance at the same place, then point references at its entities.
    World& world = context.world();
    const HierarchyIndex& hierarchy = context.index().hierarchy();
    const Entity parent = hierarchy.parent(*root);
    const Hierarchy* placement = world.tryGet<Hierarchy>(*root);
    const std::optional<u32> index =
        placement != nullptr ? std::optional<u32>(placement->order) : std::nullopt;
    std::vector<Entity> originals(hierarchy.subtree(*root).begin(), hierarchy.subtree(*root).end());
    if (originals.empty())
    {
        originals.push_back(*root);
    }
    Expected<Entity, CommandError> instance = placeInstance(context, params.path, text->first, parent, index);
    if (!instance)
    {
        return unexpected(std::move(instance).error());
    }
    const EntityGuid instanceId = world.get<PrefabInstance>(*instance).instance;
    std::unordered_map<u64, Entity> byLocal;
    for (const Entity member : membersOf(world, instanceId))
    {
        byLocal.emplace(world.get<PrefabMember>(member).local.value, member);
    }
    std::unordered_map<u64, Entity> targets;
    for (const Entity original : originals)
    {
        // A prefab entity's id in the file is the id of the entity it was saved from.
        if (const auto it = byLocal.find(world.get<EntityGuid>(original).value); it != byLocal.end())
        {
            targets.emplace(original.toBits(), it->second);
        }
    }
    (void)context.edit().redirectReferences(targets);
    if (Expected<u32, CommandError> destroyed = context.edit().destroy(*root); !destroyed)
    {
        return unexpected(std::move(destroyed).error());
    }
    result.instance = context.summary(*instance);
    return result;
}

Expected<PrefabInstanceResult, CommandError> prefabInstantiate(CommandContext& context,
                                                               const PrefabInstantiateParams& params)
{
    Expected<std::string, CommandError> text = readPrefabText(context, params.path);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    Entity parent;
    if (params.parent)
    {
        Expected<Entity, CommandError> resolved = context.entity(*params.parent, "/parent");
        if (!resolved)
        {
            return unexpected(std::move(resolved).error());
        }
        parent = *resolved;
    }
    Expected<Entity, CommandError> root = placeInstance(context, params.path, *text, parent, params.index);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    return instanceResult(context, *root);
}

Expected<PrefabUnpackResult, CommandError> prefabUnpack(CommandContext& context,
                                                        const PrefabEntityParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<Entity, CommandError> root = instanceRootOf(context, *entity);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    World& world = context.world();
    const std::vector<Entity> members = membersOf(world, world.get<PrefabMember>(*root).instance);
    for (const Entity member : members)
    {
        // Nested instances are unpacked too: their roots' PrefabInstance goes with the link.
        if (world.has<PrefabInstance>(member))
        {
            if (Expected<void, CommandError> removed =
                    context.edit().removeComponent(member, componentId<PrefabInstance>());
                !removed)
            {
                return unexpected(std::move(removed).error());
            }
        }
        if (Expected<void, CommandError> removed =
                context.edit().removeComponent(member, componentId<PrefabMember>());
            !removed)
        {
            return unexpected(std::move(removed).error());
        }
    }
    return PrefabUnpackResult{static_cast<u32>(members.size())};
}

Expected<PrefabOverridesResult, CommandError> prefabOverrides(CommandContext& context,
                                                              const PrefabEntityParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<Entity, CommandError> root = instanceRootOf(context, *entity);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    const std::array subset{*root};
    SceneSaveOptions options;
    options.subset = std::span<const Entity>(subset);
    options.prefabs = context.editor().prefabSource();
    const SceneSaveResult saved = saveScene(context.world(), options);
    if (!saved.ok())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, saved.error));
    }
    const Expected<JsonDocument, JsonError> doc = JsonDocument::parse(saved.json);
    if (!doc)
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot read the saved instance"));
    }
    PrefabInstanceResult instance = instanceResult(context, *root);
    PrefabOverridesResult result{std::move(instance.root), instance.instance, instance.prefab,
                                 JsonText{"{}"}};
    for (const JsonValue record : doc->root().find("entities").elements())
    {
        const JsonValue overrides = record.find("overrides");
        if (record.find("id").asString() == std::string_view(result.instance) && overrides.isObject())
        {
            (void)overrides.write(result.overrides.text, JsonWriteOptions{.pretty = false});
        }
    }
    return result;
}

bool hasType(const World& world, Entity entity, ComponentId type)
{
    const std::span<const ComponentId> types = world.componentsOf(entity);
    return std::find(types.begin(), types.end(), type) != types.end();
}

// True if prefab.revert changes @p type of @p entity when no Component is named.
bool revertsByDefault(const World& world, Entity entity, ComponentId type)
{
    const TypeReflection* reflection = world.registry().reflection(type);
    if (reflection == nullptr || reflection->meta.transient || type == componentId<EntityGuid>() ||
        type == componentId<Hierarchy>())
    {
        return false;
    }
    // A Transform under a LocalTransform is derived from it.
    return type != componentId<Transform>() || !world.has<LocalTransform>(entity);
}

Expected<PrefabRevertResult, CommandError> prefabRevert(CommandContext& context,
                                                        const PrefabRevertParams& params)
{
    Expected<Entity, CommandError> entity = context.entity(params.entity, "/entity");
    if (!entity)
    {
        return unexpected(std::move(entity).error());
    }
    Expected<Entity, CommandError> root = instanceRootOf(context, *entity);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    World& world = context.world();
    std::optional<ComponentId> only;
    if (params.component)
    {
        Expected<ComponentId, CommandError> type = context.componentType(*params.component, "/component");
        if (!type)
        {
            return unexpected(std::move(type).error());
        }
        if (*type == componentId<Hierarchy>() || *type == componentId<EntityGuid>() ||
            world.registry().reflection(*type)->meta.transient)
        {
            return unexpected(CommandError::invalid(
                "/component", "cannot be reverted (move entities with hierarchy.set_parent)"));
        }
        only = *type;
    }

    // The prefab's entity, expanded with the same instance id: its id and its references match.
    const PrefabInstance& instance = world.get<PrefabInstance>(*root);
    World base;
    SceneLoadOptions options;
    options.prefabs = context.editor().prefabSource();
    const SceneLoadResult loaded = loadScene(
        base, illusion::detail::instanceRecordScene(instance.prefab.path(), instance.instance), options);
    if (!loaded.ok() || loaded.entities.empty() || base.has<UnresolvedPrefab>(loaded.entities.front()))
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the prefab '" + std::string(instance.prefab.path()) +
                                                 "' cannot be loaded" +
                                                 (loaded.ok() ? std::string() : ": " + loaded.error)));
    }
    const EntityGuid guid = world.get<EntityGuid>(*entity);
    Entity original;
    for (const Entity candidate : loaded.entities)
    {
        if (base.get<EntityGuid>(candidate) == guid)
        {
            original = candidate;
        }
    }
    if (original.isNull())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the prefab no longer has this entity; unpack the instance"));
    }

    std::vector<ComponentId> types;
    if (only)
    {
        types.push_back(*only);
    }
    else
    {
        for (const ComponentId type : base.componentsOf(original))
        {
            if (revertsByDefault(base, original, type))
            {
                types.push_back(type);
            }
        }
        for (const ComponentId type : world.componentsOf(*entity))
        {
            if (revertsByDefault(world, *entity, type) &&
                std::find(types.begin(), types.end(), type) == types.end())
            {
                types.push_back(type);
            }
        }
    }
    const GuidWriter writer(base);
    u32 reverted = 0;
    for (const ComponentId type : types)
    {
        const bool inPrefab = hasType(base, original, type);
        const bool inInstance = hasType(world, *entity, type);
        if (!inPrefab)
        {
            if (inInstance)
            {
                if (Expected<void, CommandError> removed = context.edit().removeComponent(*entity, type);
                    !removed)
                {
                    return unexpected(std::move(removed).error());
                }
                ++reverted;
            }
            continue;
        }
        const TypeReflection& reflection = *world.registry().reflection(type);
        const std::byte tag{};
        const void* data = reflection.size == 0 ? &tag : base.componentData(original, type);
        Expected<std::string, CommandError> value = writeReflected(reflection, data, &writer);
        if (!value)
        {
            return unexpected(std::move(value).error());
        }
        const Expected<JsonDocument, JsonError> json = JsonDocument::parse(*value);
        if (!json)
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "cannot read the prefab's value"));
        }
        if (!inInstance)
        {
            Expected<std::vector<std::string>, CommandError> added =
                context.edit().addComponent(*entity, type, json->root());
            if (!added)
            {
                return unexpected(std::move(added).error());
            }
            ++reverted;
            continue;
        }
        Expected<bool, CommandError> set =
            context.edit().setComponent(*entity, type, json->root(), JsonReadMode::Replace);
        if (!set)
        {
            return unexpected(std::move(set).error());
        }
        reverted += *set ? 1u : 0u;
    }
    return PrefabRevertResult{reverted};
}

} // namespace

void registerPrefabCommands(CommandRegistry& registry)
{
    registry.add<&prefabCreate>({
        .name = "prefab.create",
        .title = strings::kCommandPrefabCreateTitle,
        .description = strings::kCommandPrefabCreateDescription,
        .agentSummary =
            "Saves an entity and its descendants as a .prefab file and, unless replace is false, replaces "
            "them "
            "with an instance of it at the same place (references to them follow to the instance). Example: "
            "{\"entity\": \"/Crate\", \"path\": \"assets:/props/crate.prefab\"}.",
        .category = "Prefab",
        .icon = "widgets",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
        .palette = true,
    });
    registry.add<&prefabInstantiate>({
        .name = "prefab.instantiate",
        .title = strings::kCommandPrefabInstantiateTitle,
        .description = strings::kCommandPrefabInstantiateDescription,
        .agentSummary =
            "Creates an instance of a .prefab file, linked to it: later changes of the prefab reach it "
            "except for the values the scene overrides. Example: {\"path\": "
            "\"assets:/props/crate.prefab\", \"parent\": \"/Level\"}.",
        .category = "Prefab",
        .icon = "add_box",
        .flags = {.undoable = true},
        .palette = true,
    });
    registry.add<&prefabUnpack>({
        .name = "prefab.unpack",
        .title = strings::kCommandPrefabUnpackTitle,
        .description = strings::kCommandPrefabUnpackDescription,
        .agentSummary = "Turns the entities of a prefab instance (and of instances nested in it) into plain "
                        "entities with the same ids and values, no longer linked to the prefab. Example: "
                        "{\"entity\": \"/Crate\"}.",
        .category = "Prefab",
        .icon = "unarchive",
        .flags = {.undoable = true},
    });
    registry.add<&prefabOverrides>({
        .name = "prefab.overrides",
        .title = strings::kCommandPrefabOverridesTitle,
        .description = strings::kCommandPrefabOverridesDescription,
        .agentSummary =
            "Returns what a prefab instance changes from its prefab, as saved in its scene record: by "
            "the entity's id in the prefab file, Component patches (null for a removed Component) or "
            "null for a removed entity. Example: {\"entity\": \"/Crate/Lid\"}.",
        .category = "Prefab",
        .icon = "difference",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Any,
    });
    registry.add<&prefabRevert>({
        .name = "prefab.revert",
        .title = strings::kCommandPrefabRevertTitle,
        .description = strings::kCommandPrefabRevertDescription,
        .agentSummary =
            "Gives an entity of a prefab instance its prefab's values again: one Component, or every "
            "Component but its Hierarchy (Components the prefab lacks are removed, ones it has are "
            "added back). Example: {\"entity\": \"/Crate/Lid\", \"component\": \"illusion.Name\"}.",
        .category = "Prefab",
        .icon = "undo",
        .flags = {.undoable = true},
    });
}

} // namespace illusion::editor::detail

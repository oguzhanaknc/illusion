#include "CommandSupport.hpp"

#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/editor/core/WorldEditor.hpp>

#include <cmath>
#include <map>
#include <unordered_map>
#include <unordered_set>

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
    if (!context.dryRun())
    {
        context.editor().trackPrefab(path, text);
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
    if (params.position && !(std::isfinite(params.position->x) && std::isfinite(params.position->y) &&
                             std::isfinite(params.position->z)))
    {
        return unexpected(CommandError::invalid("/position", "must be finite"));
    }
    Expected<Entity, CommandError> root = placeInstance(context, params.path, *text, parent, params.index);
    if (!root)
    {
        return unexpected(std::move(root).error());
    }
    if (params.position)
    {
        // The root keeps its rotation and scale; its parent's placement maps the world position into it.
        World& world = context.world();
        Transform placed = worldTransformOf(world, *root);
        placed.position = *params.position;
        const Entity above = context.index().hierarchy().parent(*root);
        const Transform local = above.isNull() ? placed : relativeTo(placed, worldTransformOf(world, above));
        if (world.has<Transform>(*root))
        {
            if (Expected<bool, CommandError> set =
                    context.edit().assignComponent(*root, componentId<Transform>(), &placed);
                !set)
            {
                return unexpected(std::move(set).error());
            }
        }
        if (world.has<LocalTransform>(*root))
        {
            const LocalTransform value = LocalTransform::from(local);
            if (Expected<bool, CommandError> set =
                    context.edit().assignComponent(*root, componentId<LocalTransform>(), &value);
                !set)
            {
                return unexpected(std::move(set).error());
            }
        }
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

// Checks the optional field param of prefab.apply and prefab.revert: a top-level field of @p type.
Expected<void, CommandError> checkField(const std::optional<std::string>& field,
                                        const std::optional<std::string>& component,
                                        const TypeReflection* type)
{
    if (!field)
    {
        return {};
    }
    if (!component || type == nullptr)
    {
        return unexpected(CommandError::invalid("/field", "needs component"));
    }
    for (const FieldInfo& info : type->fields)
    {
        if (info.name == *field)
        {
            return {};
        }
    }
    return unexpected(CommandError::invalid("/field", "is not a field of " + std::string(type->name)));
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
    // A child's Transform is derived from its LocalTransform (a root's LocalTransform follows its Transform).
    const Hierarchy* hierarchy = world.tryGet<Hierarchy>(entity);
    return type != componentId<Transform>() || !world.has<LocalTransform>(entity) || hierarchy == nullptr ||
           hierarchy->parent.isNull();
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
    if (Expected<void, CommandError> valid =
            checkField(params.field, params.component, only ? world.registry().reflection(*only) : nullptr);
        !valid)
    {
        return unexpected(std::move(valid).error());
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
        if (params.field)
        {
            if (!inInstance)
            {
                return unexpected(
                    CommandError::invalid("/field", "belongs to a Component the entity does not have"));
            }
            Expected<bool, CommandError> set =
                context.edit().setField(*entity, type, *params.field, json->root().find(*params.field));
            if (!set)
            {
                return unexpected(std::move(set).error());
            }
            reverted += *set ? 1u : 0u;
            continue;
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

// Writes the Entity fields of an instance's Components as ids in its prefab file: its own entities by local
// id, others (which a prefab cannot refer to) as null.
class LocalWriter final : public EntityResolver
{
public:
    LocalWriter(const World& world, EntityGuid instance) noexcept
        : m_world(world)
        , m_instance(instance)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        const PrefabMember* member = m_world.isAlive(entity) ? m_world.tryGet<PrefabMember>(entity)
                                                             : static_cast<const PrefabMember*>(nullptr);
        if (member == nullptr || member->instance != m_instance)
        {
            return doc.makeNull();
        }
        return doc.makeString(guidString(member->local));
    }

    Expected<Entity, std::string> readEntity(JsonValue) const override
    {
        return unexpected(std::string("cannot be read by a local writer"));
    }

private:
    const World& m_world;
    EntityGuid m_instance;
};

// Reads Entity fields as the entities of a World with those ids.
class GuidReader final : public EntityResolver
{
public:
    explicit GuidReader(const std::unordered_map<u64, Entity>& entities) noexcept
        : m_entities(entities)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity) const override { return doc.makeNull(); }

    Expected<Entity, std::string> readEntity(JsonValue value) const override
    {
        if (value.isNull())
        {
            return Entity{};
        }
        const std::optional<std::string_view> text = value.asString();
        const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
        const auto it = guid ? m_entities.find(guid->value) : m_entities.end();
        if (it == m_entities.end())
        {
            return unexpected(std::string("must be null or the id of an entity of the prefab"));
        }
        return it->second;
    }

private:
    const std::unordered_map<u64, Entity>& m_entities;
};

std::unordered_map<u64, Entity> entitiesByGuid(const World& world)
{
    std::vector<Entity> all;
    world.entities(all);
    std::unordered_map<u64, Entity> out;
    out.reserve(all.size());
    for (const Entity entity : all)
    {
        if (const EntityGuid* guid = world.tryGet<EntityGuid>(entity))
        {
            out.emplace(guid->value, entity);
        }
    }
    return out;
}

// The roots of the outermost instances of the edited World.
std::vector<Entity> instanceRoots(const World& world)
{
    std::vector<Entity> all;
    world.entities(all);
    std::erase_if(all, [&](Entity entity) {
        const PrefabInstance* instance = world.tryGet<PrefabInstance>(entity);
        const PrefabMember* member = world.tryGet<PrefabMember>(entity);
        return instance == nullptr || member == nullptr || member->instance != instance->instance;
    });
    return all;
}

// The overrides of every instance of the edited World (by instance id, compact JSON), as saved now.
Expected<std::unordered_map<u64, std::string>, CommandError> allOverrides(CommandContext& context,
                                                                          const PrefabSource& prefabs)
{
    std::unordered_map<u64, std::string> out;
    const std::vector<Entity> roots = instanceRoots(context.world());
    if (roots.empty())
    {
        return out;
    }
    SceneSaveOptions options;
    options.subset = std::span<const Entity>(roots);
    options.prefabs = prefabs;
    const SceneSaveResult saved = saveScene(context.world(), options);
    if (!saved.ok())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, saved.error));
    }
    const Expected<JsonDocument, JsonError> doc = JsonDocument::parse(saved.json);
    if (!doc)
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot read the saved instances"));
    }
    for (const JsonValue record : doc->root().find("entities").elements())
    {
        const std::optional<std::string_view> id = record.find("id").asString();
        const std::optional<EntityGuid> guid = id ? parseGuid(*id) : std::nullopt;
        if (record.find("prefab").isAbsent() || !guid)
        {
            continue;
        }
        std::string text = "{}";
        if (const JsonValue overrides = record.find("overrides"); overrides.isObject())
        {
            text.clear();
            (void)overrides.write(text, JsonWriteOptions{.pretty = false});
        }
        out.emplace(guid->value, std::move(text));
    }
    return out;
}

// Brings the instance whose root is @p root up to date with its prefab as @p prefabs reads it, keeping the
// overrides @p overrides (compact JSON): its entities keep their handles, so references and added children
// stay. Returns the number of edits.
Expected<u32, CommandError> syncInstance(CommandContext& context, Entity root, std::string_view overrides,
                                         const PrefabSource& prefabs)
{
    World& world = context.world();
    const PrefabInstance instance = world.get<PrefabInstance>(root);
    const std::vector<Entity> members = membersOf(world, instance.instance);
    const std::unordered_set<Entity> memberSet(members.begin(), members.end());

    // The instance as it should be, next to stubs of the scene's other entities that overrides may name.
    JsonMutDocument doc;
    JsonMutValue top = doc.makeObject();
    doc.setRoot(top);
    top.add("format", doc.makeString("illusion.scene")).add("version", doc.makeU64(kSceneFormatVersion));
    JsonMutValue entries = top.addArray("entities");
    std::vector<Entity> all;
    world.entities(all);
    for (const Entity entity : all)
    {
        const EntityGuid* guid = world.tryGet<EntityGuid>(entity);
        if (guid != nullptr && !memberSet.contains(entity))
        {
            entries.pushObject().add("id", doc.makeString(guidString(*guid)));
        }
    }
    JsonMutValue record = entries.pushObject();
    record.add("prefab", doc.makeString(instance.prefab.path()))
        .add("id", doc.makeString(guidString(instance.instance)));
    const Expected<JsonDocument, JsonError> patch = JsonDocument::parse(overrides);
    if (patch && patch->root().isObject() && patch->root().size() > 0)
    {
        record.add("overrides", doc.copy(patch->root()));
    }
    std::string text;
    (void)doc.write(text, JsonWriteOptions{.pretty = false});
    World target;
    SceneLoadOptions options;
    options.prefabs = prefabs;
    const SceneLoadResult loaded = loadScene(target, text, options);
    if (!loaded.ok())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "cannot update the instance " + guidString(instance.instance) +
                                                 ": " + loaded.error));
    }
    const std::vector<Entity> wanted = membersOf(target, instance.instance);
    if (wanted.empty() || target.has<UnresolvedPrefab>(wanted.front()))
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed,
                               "the prefab '" + std::string(instance.prefab.path()) + "' cannot be loaded"));
    }

    EntityIndex& index = context.index();
    u32 edits = 0;
    // Entities the prefab gained: created first, so that values can refer to them.
    for (const Entity entity : wanted)
    {
        const EntityGuid guid = target.get<EntityGuid>(entity);
        if (index.find(guid).isNull())
        {
            CreateEntityOptions create;
            create.guid = guid;
            if (Expected<Entity, CommandError> created = context.edit().create(create); !created)
            {
                return unexpected(std::move(created).error());
            }
            ++edits;
        }
    }
    const GuidWriter writer(target);
    std::unordered_set<u64> kept;
    for (const Entity entity : wanted)
    {
        const EntityGuid guid = target.get<EntityGuid>(entity);
        kept.insert(guid.value);
        const Entity edited = index.find(guid);
        const bool isRoot = target.has<PrefabInstance>(entity) &&
                            target.get<PrefabInstance>(entity).instance == instance.instance;
        if (!isRoot)
        {
            const Hierarchy* hierarchy = target.tryGet<Hierarchy>(entity);
            const Entity parent = hierarchy != nullptr && !hierarchy->parent.isNull()
                                      ? index.find(target.get<EntityGuid>(hierarchy->parent))
                                      : Entity{};
            if (index.hierarchy().parent(edited) != parent)
            {
                if (Expected<void, CommandError> moved =
                        context.edit().setParent(edited, parent, ParentMode::KeepLocal);
                    !moved)
                {
                    return unexpected(std::move(moved).error());
                }
                ++edits;
            }
        }
        for (const ComponentId type : target.componentsOf(entity))
        {
            const bool link = type == componentId<PrefabMember>() || type == componentId<PrefabInstance>();
            if (!link && !revertsByDefault(target, entity, type))
            {
                continue;
            }
            const TypeReflection& reflection = *target.registry().reflection(type);
            const std::byte tag{};
            const void* data = reflection.size == 0 ? &tag : target.componentData(entity, type);
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
            if (hasType(world, edited, type))
            {
                Expected<bool, CommandError> set =
                    context.edit().setComponent(edited, type, json->root(), JsonReadMode::Replace);
                if (!set)
                {
                    return unexpected(std::move(set).error());
                }
                edits += *set ? 1u : 0u;
            }
            else
            {
                Expected<std::vector<std::string>, CommandError> added =
                    context.edit().addComponent(edited, type, json->root());
                if (!added)
                {
                    return unexpected(std::move(added).error());
                }
                ++edits;
            }
        }
        const std::span<const ComponentId> own = world.componentsOf(edited);
        const std::vector<ComponentId> types(own.begin(), own.end());
        for (const ComponentId type : types)
        {
            if (revertsByDefault(world, edited, type) && !hasType(target, entity, type))
            {
                if (Expected<void, CommandError> removed = context.edit().removeComponent(edited, type);
                    !removed)
                {
                    return unexpected(std::move(removed).error());
                }
                ++edits;
            }
        }
    }
    // Entities the prefab lost.
    for (const Entity entity : members)
    {
        if (world.isAlive(entity) && !kept.contains(world.get<EntityGuid>(entity).value))
        {
            if (Expected<u32, CommandError> destroyed = context.edit().destroy(entity); !destroyed)
            {
                return unexpected(std::move(destroyed).error());
            }
            ++edits;
        }
    }
    return edits;
}

// True if prefab.apply writes @p type of the instance entity @p entity into its prefab.
bool appliesByDefault(const World& world, Entity entity, ComponentId type, bool isRoot)
{
    const TypeReflection* reflection = world.registry().reflection(type);
    if (reflection == nullptr || reflection->meta.transient || type == componentId<EntityGuid>() ||
        type == componentId<Hierarchy>())
    {
        return false;
    }
    // The root's placement belongs to the scene; a child's Transform is derived from its LocalTransform.
    if (isRoot)
    {
        return type != componentId<Transform>() && type != componentId<LocalTransform>();
    }
    return type != componentId<Transform>() || !world.has<LocalTransform>(entity);
}

Expected<PrefabApplyResult, CommandError> prefabApply(CommandContext& context,
                                                      const PrefabApplyParams& params)
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
        if (params.all.value_or(false))
        {
            return unexpected(CommandError::invalid("/all", "cannot be combined with component"));
        }
        Expected<ComponentId, CommandError> type = context.componentType(*params.component, "/component");
        if (!type)
        {
            return unexpected(std::move(type).error());
        }
        if (!appliesByDefault(world, *entity, *type, *entity == *root))
        {
            return unexpected(CommandError::invalid(
                "/component",
                "cannot be applied (the hierarchy and the root's placement stay in the scene)"));
        }
        only = *type;
    }
    if (Expected<void, CommandError> valid =
            checkField(params.field, params.component, only ? world.registry().reflection(*only) : nullptr);
        !valid)
    {
        return unexpected(std::move(valid).error());
    }
    const PrefabInstance instance = world.get<PrefabInstance>(*root);
    const std::string path(instance.prefab.path());
    Expected<std::string, CommandError> before = readPrefabText(context, path);
    if (!before)
    {
        return unexpected(std::move(before).error());
    }
    // The other instances keep their overrides when the prefab changes under them.
    Expected<std::unordered_map<u64, std::string>, CommandError> overrides =
        allOverrides(context, context.editor().prefabSource());
    if (!overrides)
    {
        return unexpected(std::move(overrides).error());
    }

    // The prefab file as a World: its entity ids are the instance's local ids.
    World prefab;
    SceneLoadOptions load;
    load.prefabs = context.editor().prefabSource();
    const SceneLoadResult loaded = loadScene(prefab, *before, load);
    if (!loaded.ok())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "the prefab '" + path + "' cannot be loaded: " + loaded.error));
    }
    const std::unordered_map<u64, Entity> prefabEntities = entitiesByGuid(prefab);
    const LocalWriter localWriter(world, instance.instance);
    const GuidWriter prefabWriter(prefab);
    const GuidReader reader(prefabEntities);
    const std::vector<Entity> members =
        params.all.value_or(false) ? membersOf(world, instance.instance) : std::vector<Entity>{*entity};
    u32 applied = 0;
    for (const Entity member : members)
    {
        const auto found = prefabEntities.find(world.get<PrefabMember>(member).local.value);
        if (found == prefabEntities.end())
        {
            continue; // the prefab no longer has this entity
        }
        const Entity target = found->second;
        const bool isRoot = member == *root;
        std::vector<ComponentId> types;
        if (only)
        {
            types.push_back(*only);
        }
        else
        {
            for (const ComponentId type : world.componentsOf(member))
            {
                if (appliesByDefault(world, member, type, isRoot))
                {
                    types.push_back(type);
                }
            }
            for (const ComponentId type : prefab.componentsOf(target))
            {
                if (appliesByDefault(prefab, target, type, isRoot) &&
                    std::find(types.begin(), types.end(), type) == types.end())
                {
                    types.push_back(type);
                }
            }
        }
        for (const ComponentId type : types)
        {
            const TypeReflection& reflection = *world.registry().reflection(type);
            const bool inInstance = hasType(world, member, type);
            const bool inPrefab = hasType(prefab, target, type);
            if (!inInstance)
            {
                if (inPrefab)
                {
                    (void)prefab.remove(target, type);
                    ++applied;
                }
                continue;
            }
            const std::byte tag{};
            const void* data = reflection.size == 0 ? &tag : world.componentData(member, type);
            Expected<std::string, CommandError> value = writeReflected(reflection, data, &localWriter);
            if (!value)
            {
                return unexpected(std::move(value).error());
            }
            if (inPrefab)
            {
                const void* current = reflection.size == 0 ? &tag : prefab.componentData(target, type);
                Expected<std::string, CommandError> was = writeReflected(reflection, current, &prefabWriter);
                if (was && *was == *value)
                {
                    continue;
                }
            }
            Expected<JsonDocument, JsonError> json = JsonDocument::parse(*value);
            if (!json)
            {
                return unexpected(
                    CommandError::make(ErrorKind::Internal, "cannot read the instance's value"));
            }
            ValueReadOptions read;
            read.mode = JsonReadMode::Replace;
            read.entities = &reader;
            if (params.field)
            {
                if (!inPrefab)
                {
                    return unexpected(CommandError::invalid(
                        "/field", "belongs to a Component the prefab's entity does not have"));
                }
                // Only that field merges into the prefab's value.
                JsonMutDocument one;
                JsonMutValue object = one.makeObject();
                object.add(*params.field, one.copy(json->root().find(*params.field)));
                one.setRoot(object);
                std::string patch;
                (void)one.write(patch, JsonWriteOptions{.pretty = false});
                json = JsonDocument::parse(patch);
                if (!json)
                {
                    return unexpected(
                        CommandError::make(ErrorKind::Internal, "cannot read the field's value"));
                }
                read.mode = JsonReadMode::Merge;
            }
            void* into = inPrefab ? prefab.componentData(target, type) : prefab.emplaceDefault(target, type);
            if (const ValueReadResult result = readValue(reflection, into, json->root(), read); !result.ok())
            {
                return unexpected(
                    CommandError::make(ErrorKind::Internal, "cannot apply " + std::string(reflection.name) +
                                                                ": " + result.errors.front().message));
            }
            ++applied;
        }
    }
    if (params.all.value_or(false))
    {
        // Entities removed from the instance leave the prefab (with their descendants there).
        std::unordered_set<u64> present;
        for (const Entity member : membersOf(world, instance.instance))
        {
            present.insert(world.get<PrefabMember>(member).local.value);
        }
        for (const auto& [guid, target] : prefabEntities)
        {
            if (!present.contains(guid) && prefab.isAlive(target))
            {
                (void)destroyRecursive(prefab, target);
                ++applied;
            }
        }
    }
    PrefabApplyResult result{path, applied, 0};
    if (applied == 0)
    {
        return result;
    }
    SceneSaveOptions save;
    save.prefabFile = true;
    save.prefabs = context.editor().prefabSource();
    const SceneSaveResult written = saveScene(prefab, save);
    if (!written.ok())
    {
        return unexpected(
            CommandError::make(ErrorKind::ValidationFailed, "cannot write the prefab: " + written.error));
    }
    if (Expected<bool, CommandError> stored = context.edit().writeFile(path, written.json); !stored)
    {
        return unexpected(std::move(stored).error());
    }
    if (!context.dryRun())
    {
        context.editor().trackPrefab(path, written.json); // the open instances follow it below
    }
    // Every other instance follows the new prefab with its own overrides.
    const PrefabSource source = sourceWith(context, path, written.json);
    for (const Entity other : instanceRoots(world))
    {
        const EntityGuid id = world.get<PrefabInstance>(other).instance;
        if (id == instance.instance)
        {
            continue;
        }
        const auto kept = overrides->find(id.value);
        Expected<u32, CommandError> synced = syncInstance(
            context, other, kept != overrides->end() ? std::string_view(kept->second) : "{}", source);
        if (!synced)
        {
            return unexpected(std::move(synced).error());
        }
        result.instances += *synced > 0 ? 1u : 0u;
    }
    return result;
}

Expected<PrefabRefreshResult, CommandError> prefabRefresh(CommandContext& context,
                                                          const PrefabRefreshParams& params)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    if (params.path)
    {
        if (Expected<void, CommandError> valid = checkPrefabPath(context, *params.path); !valid)
        {
            return unexpected(std::move(valid).error());
        }
    }
    // The prefabs whose file differs from the text the instances were made from.
    const std::map<std::string, std::string, std::less<>> tracked = context.editor().trackedPrefabs();
    const PrefabSource disk = context.editor().prefabSource();
    PrefabRefreshResult result;
    for (const auto& [path, text] : tracked)
    {
        if (params.path && path != *params.path)
        {
            continue;
        }
        const std::optional<std::string> now = disk(path);
        if (now && *now != text)
        {
            result.prefabs.push_back(path);
        }
    }
    if (result.prefabs.empty())
    {
        return result;
    }
    // Overrides as the instances were made (old texts), applied to the files as they are now.
    const PrefabSource before = [&](std::string_view path) -> std::optional<std::string> {
        const auto it = tracked.find(path);
        return it != tracked.end() ? std::optional<std::string>(it->second) : disk(path);
    };
    Expected<std::unordered_map<u64, std::string>, CommandError> overrides = allOverrides(context, before);
    if (!overrides)
    {
        return unexpected(std::move(overrides).error());
    }
    const PrefabSource after = context.dryRun() ? disk : context.editor().trackedPrefabSource();
    for (const Entity root : instanceRoots(context.world()))
    {
        const auto kept = overrides->find(context.world().get<PrefabInstance>(root).instance.value);
        Expected<u32, CommandError> synced = syncInstance(
            context, root, kept != overrides->end() ? std::string_view(kept->second) : "{}", after);
        if (!synced)
        {
            return unexpected(std::move(synced).error());
        }
        result.instances += *synced > 0 ? 1u : 0u;
    }
    if (!context.dryRun())
    {
        for (const std::string& path : result.prefabs)
        {
            if (std::optional<std::string> now = disk(path))
            {
                context.editor().trackPrefab(path, std::move(*now));
            }
        }
    }
    return result;
}

Expected<void, CommandError> checkDiscard(CommandContext& context, const std::optional<bool>& discard)
{
    if (context.editor().dirty() && !discard.value_or(false))
    {
        return unexpected(CommandError::make(
            ErrorKind::PreconditionFailed, "the scene has unsaved changes; save it or pass discardChanges"));
    }
    return {};
}

Expected<SceneInfoResult, CommandError> prefabOpen(CommandContext& context, const PrefabOpenParams& params)
{
    Expected<std::string, CommandError> text = readPrefabText(context, params.path);
    if (!text)
    {
        return unexpected(std::move(text).error());
    }
    if (Expected<void, CommandError> allowed = checkDiscard(context, params.discardChanges); !allowed)
    {
        return unexpected(std::move(allowed).error());
    }
    EditorContext& editor = context.editor();
    const std::string from = editor.document().path();
    if (from == params.path)
    {
        return unexpected(CommandError::invalid("/path", "is the file being edited"));
    }
    if (context.dryRun())
    {
        return sceneInfo(editor);
    }
    std::vector<std::string> stack(editor.prefabEditStack().begin(), editor.prefabEditStack().end());
    std::vector<std::string> warnings;
    if (Expected<void, CommandError> opened = editor.openScene(params.path, &warnings); !opened)
    {
        return unexpected(std::move(opened).error());
    }
    stack.push_back(from);
    editor.setPrefabEditStack(std::move(stack));
    for (std::string& warning : warnings)
    {
        context.warn(std::move(warning));
    }
    return sceneInfo(editor);
}

Expected<SceneInfoResult, CommandError> prefabClose(CommandContext& context, const PrefabCloseParams& params)
{
    EditorContext& editor = context.editor();
    if (editor.prefabEditStack().empty())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no prefab is open for editing"));
    }
    if (Expected<void, CommandError> allowed = checkDiscard(context, params.discardChanges); !allowed)
    {
        return unexpected(std::move(allowed).error());
    }
    if (context.dryRun())
    {
        return sceneInfo(editor);
    }
    std::vector<std::string> stack(editor.prefabEditStack().begin(), editor.prefabEditStack().end());
    const std::string back = std::move(stack.back());
    stack.pop_back();
    if (back.empty())
    {
        editor.newScene();
    }
    else
    {
        std::vector<std::string> warnings;
        if (Expected<void, CommandError> opened = editor.openScene(back, &warnings); !opened)
        {
            return unexpected(std::move(opened).error());
        }
        for (std::string& warning : warnings)
        {
            context.warn(std::move(warning));
        }
    }
    editor.setPrefabEditStack(std::move(stack));
    return sceneInfo(editor);
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
    registry.add<&prefabRefresh>({
        .name = "prefab.refresh",
        .title = strings::kCommandPrefabRefreshTitle,
        .description = strings::kCommandPrefabRefreshDescription,
        .agentSummary = "Brings the open scene's instances up to date with prefab files that changed on "
                        "disk, keeping their "
                        "overrides; the editor runs it by itself when it sees a prefab change. Without path, "
                        "every changed "
                        "prefab. Example: {\"path\": \"assets:/props/crate.prefab\"}.",
        .category = "Prefab",
        .icon = "sync",
        .flags = {.undoable = true},
    });
    registry.add<&prefabOpen>({
        .name = "prefab.open",
        .title = strings::kCommandPrefabOpenTitle,
        .description = strings::kCommandPrefabOpenDescription,
        .agentSummary =
            "Opens a .prefab file for editing in place of the scene (unsaved changes need discardChanges); "
            "scene.save then writes the prefab, and prefab.close returns to the scene, whose instances show "
            "the "
            "changes. Example: {\"path\": \"assets:/props/crate.prefab\"}.",
        .category = "Prefab",
        .icon = "edit",
        .flags = {.destructive = true, .outsideTransactions = true},
        .palette = true,
    });
    registry.add<&prefabClose>({
        .name = "prefab.close",
        .title = strings::kCommandPrefabCloseTitle,
        .description = strings::kCommandPrefabCloseDescription,
        .agentSummary =
            "Leaves prefab edit mode and reopens the scene it was entered from (unsaved changes to the "
            "prefab need discardChanges). Example: {}.",
        .category = "Prefab",
        .icon = "arrow_back",
        .flags = {.destructive = true, .outsideTransactions = true},
        .palette = true,
    });
    registry.add<&prefabApply>({
        .name = "prefab.apply",
        .title = strings::kCommandPrefabApplyTitle,
        .description = strings::kCommandPrefabApplyDescription,
        .agentSummary =
            "Writes what an instance entity overrides into its prefab file: one Component, every Component "
            "of the "
            "entity, or (all) the whole instance including removed entities. The root's placement and the "
            "hierarchy stay in the scene; other instances in the open scene follow the new prefab and keep "
            "their "
            "own overrides. Example: {\"entity\": \"/Crate/Lid\", \"component\": \"illusion.Name\"}.",
        .category = "Prefab",
        .icon = "publish",
        .flags = {.undoable = true},
        .permission = PermissionClass::Files,
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

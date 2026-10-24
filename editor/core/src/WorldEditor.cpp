#include "PathText.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/ChangeJournal.hpp>
#include <illusion/editor/core/EntityIndex.hpp>
#include <illusion/editor/core/WorldEditor.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace illusion::editor
{
namespace
{

std::string guidText(EntityGuid guid)
{
    const auto text = formatGuid(guid);
    return std::string(text.data(), text.size());
}

// Entity fields as ids, through the index. With a remap, ids of copied entities resolve to their copies.
class IndexResolver final : public EntityResolver
{
public:
    explicit IndexResolver(EntityIndex& index,
                           const std::unordered_map<u64, Entity>* remap = nullptr) noexcept
        : m_index(&index)
        , m_remap(remap)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        const EntityGuid guid = m_index->guidOf(entity);
        return guid.isNull() ? doc.makeNull() : doc.makeString(guidText(guid));
    }

    Expected<Entity, std::string> readEntity(JsonValue value) const override
    {
        if (value.isNull())
        {
            return Entity{};
        }
        const std::optional<std::string_view> text = value.asString();
        const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
        if (!guid)
        {
            return unexpected(std::string("must be null or an entity id of 16 hex digits"));
        }
        if (m_remap != nullptr)
        {
            if (const auto it = m_remap->find(guid->value); it != m_remap->end())
            {
                return it->second;
            }
        }
        const Entity entity = m_index->find(*guid);
        if (entity.isNull())
        {
            return unexpected("refers to " + std::string(*text) + ", which does not exist");
        }
        return entity;
    }

private:
    EntityIndex* m_index;
    const std::unordered_map<u64, Entity>* m_remap;
};

bool hasComponent(const World& world, Entity entity, ComponentId type)
{
    const std::span<const ComponentId> components = world.componentsOf(entity);
    return std::find(components.begin(), components.end(), type) != components.end();
}

bool typeHasEntities(const ValueType& type) noexcept
{
    switch (type.type)
    {
    case FieldType::Entity:
        return true;
    case FieldType::Struct:
        return std::any_of(type.structType->fields.begin(), type.structType->fields.end(),
                           [](const FieldInfo& field) { return typeHasEntities(field); });
    case FieldType::FixedArray:
    case FieldType::Array:
    case FieldType::Optional:
        return typeHasEntities(*type.element);
    default:
        return false;
    }
}

// Calls fn(Entity&) for every Entity value inside a value of @p type.
template <class F>
void forEachEntity(const ValueType& type, void* data, F& fn)
{
    switch (type.type)
    {
    case FieldType::Entity:
        fn(*static_cast<Entity*>(data));
        break;
    case FieldType::Struct:
        for (const FieldInfo& field : type.structType->fields)
        {
            forEachEntity(field, fieldData(data, field), fn);
        }
        break;
    case FieldType::FixedArray:
        for (u32 i = 0; i < type.count; ++i)
        {
            forEachEntity(*type.element, static_cast<std::byte*>(data) + usize{i} * type.element->size, fn);
        }
        break;
    case FieldType::Array:
    case FieldType::Optional:
        for (usize i = 0, n = type.container->size(data); i < n; ++i)
        {
            forEachEntity(*type.element, type.container->element(data, i), fn);
        }
        break;
    default:
        break;
    }
}

template <class F>
void forEachEntityField(const TypeReflection& type, void* object, F& fn)
{
    for (const FieldInfo& field : type.fields)
    {
        forEachEntity(field, fieldData(object, field), fn);
    }
}

std::string compact(const JsonMutDocument& doc)
{
    std::string out;
    if (!doc.write(out, JsonWriteOptions{.pretty = false}))
    {
        out.clear();
    }
    return out;
}

CommandError readError(const ValueReadResult& result)
{
    CommandError error = CommandError::make(ErrorKind::ValidationFailed, "the value is invalid");
    for (const ValueError& problem : result.errors)
    {
        error.errors.push_back(problem);
    }
    if (error.errors.size() == 1)
    {
        error.message = "'" + error.errors.front().pointer + "' " + error.errors.front().message;
    }
    return error;
}

CommandError fileError(FileError error, std::string_view path)
{
    const std::string where(path);
    switch (error)
    {
    case FileError::None:
        break;
    case FileError::InvalidPath:
    case FileError::UnknownMount:
        return CommandError::invalid("", "is not a valid project path (" + std::string(fileErrorName(error)) +
                                             ")");
    case FileError::NotFound:
        return CommandError::make(ErrorKind::NotFound, "'" + where + "' does not exist");
    case FileError::AlreadyExists:
        return CommandError::make(ErrorKind::AlreadyExists, "'" + where + "' exists already");
    case FileError::ReadOnly:
        return CommandError::make(ErrorKind::PreconditionFailed, "'" + where + "' is on a read-only mount");
    case FileError::NotEmpty:
        return CommandError::make(ErrorKind::PreconditionFailed, "'" + where + "' is not empty");
    case FileError::Unsupported:
        return CommandError::make(ErrorKind::Unsupported, "'" + where + "' cannot be changed");
    case FileError::AccessDenied:
    case FileError::IoError:
        return CommandError::make(ErrorKind::IoError, "cannot change '" + where + "' (" +
                                                          std::string(fileErrorName(error)) + ")");
    }
    return CommandError::make(ErrorKind::Internal, "unexpected file error");
}

bool isGuarded(ComponentId type)
{
    return type == componentId<EntityGuid>() || type == componentId<Hierarchy>();
}

} // namespace

struct WorldEditor::Watch
{
    Entity entity;
    EntityGuid guid;
    ComponentId type = kInvalidComponentId;
    std::optional<std::string> before;
    bool always = false; // record the value even if it did not change
};

WorldEditor::WorldEditor(World& world, EntityIndex& index, GuidGenerator& guids, ChangeJournal& changes)
    : m_world(&world)
    , m_index(&index)
    , m_guids(&guids)
    , m_changes(&changes)
{
}

void WorldEditor::setFiles(VirtualFileSystem* files, std::filesystem::path blobs, std::string blobPrefix)
{
    m_files = files;
    m_blobs = std::move(blobs);
    m_blobPrefix = std::move(blobPrefix);
}

void WorldEditor::begin()
{
    ILL_ASSERT(!m_recording, "WorldEditor transactions do not nest");
    m_operations.clear();
    m_recording = true;
}

std::vector<Operation> WorldEditor::commit()
{
    ILL_ASSERT(m_recording, "commit() without begin()");
    m_recording = false;
    return std::move(m_operations);
}

void WorldEditor::rollback()
{
    ILL_ASSERT(m_recording, "rollback() without begin()");
    m_recording = false;
    std::vector<Operation> operations = std::move(m_operations);
    m_operations.clear();
    for (auto it = operations.rbegin(); it != operations.rend(); ++it)
    {
        if (Expected<void, CommandError> undone = apply(inverted(*it)); !undone)
        {
            ILL_LOG_ERROR("Editor", "Rolling back a transaction failed: {}", undone.error().message);
        }
    }
}

void WorldEditor::record(Operation operation)
{
    if (m_recording)
    {
        m_operations.push_back(std::move(operation));
    }
}

std::optional<std::string> WorldEditor::capture(Entity entity, ComponentId type) const
{
    if (!m_world->isAlive(entity) || !hasComponent(*m_world, entity, type))
    {
        return std::nullopt;
    }
    const TypeReflection* reflection = m_world->registry().reflection(type);
    if (reflection == nullptr)
    {
        return std::nullopt;
    }
    const std::byte tag{};
    const void* data = reflection->size == 0 ? &tag : m_world->componentData(entity, type);
    IndexResolver resolver(*m_index);
    JsonMutDocument doc;
    Expected<JsonMutValue, ValueError> value = writeValue(doc, *reflection, data, {.entities = &resolver});
    if (!value)
    {
        ILL_LOG_ERROR("Editor", "Cannot capture {}: {}", reflection->name, value.error().message);
        return std::nullopt;
    }
    doc.setRoot(*value);
    return compact(doc);
}

Expected<void, CommandError> WorldEditor::checkEditable(Entity entity, ComponentId type) const
{
    if (!m_world->isAlive(entity))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    const TypeReflection* reflection = m_world->registry().reflection(type);
    if (reflection == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the Component type is not reflected"));
    }
    if (type == componentId<EntityGuid>())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "entity ids cannot be changed"));
    }
    if (type == componentId<Hierarchy>())
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "illusion.Hierarchy changes through hierarchy.set_parent and "
                                             "hierarchy.reorder"));
    }
    return {};
}

void WorldEditor::watch(std::vector<Watch>& watches, Entity entity, ComponentId type) const
{
    const auto same = [&](const Watch& w) {
        return w.entity == entity && w.type == type;
    };
    if (entity.isNull() || std::any_of(watches.begin(), watches.end(), same))
    {
        return;
    }
    watches.push_back(Watch{entity, m_index->guidOf(entity), type, capture(entity, type)});
}

void WorldEditor::finish(std::vector<Watch>& watches)
{
    for (Watch& watched : watches)
    {
        std::optional<std::string> after = capture(watched.entity, watched.type);
        if (after == watched.before && !watched.always)
        {
            continue;
        }
        const std::string_view name = m_world->registry().reflection(watched.type)->name;
        m_world->markChanged(watched.type);
        m_changes->record(watched.guid, ChangeKind::Changed, name);
        record(ComponentChange{watched.guid, std::string(name), std::move(watched.before), std::move(after)});
    }
    watches.clear();
}

void WorldEditor::watchSiblings(std::vector<Watch>& watches, Entity parent)
{
    std::vector<Entity> siblings;
    m_index->children(parent, siblings);
    for (const Entity sibling : siblings)
    {
        if (m_world->has<Hierarchy>(sibling))
        {
            watch(watches, sibling, componentId<Hierarchy>());
        }
    }
}

void WorldEditor::subtreeOf(Entity root, std::vector<Entity>& out)
{
    out.clear();
    const HierarchyIndex& hierarchy = m_index->hierarchy();
    const std::span<const Entity> subtree = hierarchy.subtree(root);
    if (subtree.empty())
    {
        out.push_back(root);
        return;
    }
    out.assign(subtree.begin(), subtree.end());
}

bool WorldEditor::hasEntityFields(ComponentId type)
{
    if (type >= m_entityFieldCache.size())
    {
        m_entityFieldCache.resize(usize{type} + 1, -1);
    }
    if (m_entityFieldCache[type] < 0)
    {
        const TypeReflection* reflection = m_world->registry().reflection(type);
        const bool has = reflection != nullptr &&
                         std::any_of(reflection->fields.begin(), reflection->fields.end(),
                                     [](const FieldInfo& field) { return typeHasEntities(field); });
        m_entityFieldCache[type] = has ? i8{1} : i8{0};
    }
    return m_entityFieldCache[type] == 1;
}

EntityRecord WorldEditor::snapshot(Entity entity) const
{
    EntityRecord record;
    record.guid = m_index->guidOf(entity);
    IndexResolver resolver(*m_index);
    JsonMutDocument doc;
    JsonMutValue components = doc.makeObject();
    const std::byte tag{};
    for (const ComponentId type : m_world->componentsOf(entity))
    {
        const TypeReflection* reflection = m_world->registry().reflection(type);
        if (reflection == nullptr || type == componentId<EntityGuid>())
        {
            continue; // the id is the record's; unreflected Components cannot be restored
        }
        const void* data = reflection->size == 0 ? &tag : m_world->componentData(entity, type);
        Expected<JsonMutValue, ValueError> value =
            writeValue(doc, *reflection, data, {.entities = &resolver});
        if (value)
        {
            components.add(reflection->name, *value);
        }
    }
    doc.setRoot(components);
    record.components = compact(doc);
    return record;
}

Expected<Entity, CommandError> WorldEditor::create(const CreateEntityOptions& options)
{
    ILL_PROFILE_SCOPE();
    if (const std::string_view problem = entityNameError(options.name);
        !options.name.empty() && !problem.empty())
    {
        return unexpected(withPointerPrefix(CommandError::invalid("", std::string(problem)), "/name"));
    }
    if (!options.parent.isNull() && !m_world->isAlive(options.parent))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the parent does not exist"));
    }
    if (!options.guid.isNull() && !m_index->lookup(options.guid).isNull())
    {
        return unexpected(CommandError::make(ErrorKind::AlreadyExists,
                                             "an entity with the id " + guidText(options.guid) + " exists"));
    }
    const ComponentRegistry& registry = m_world->registry();
    std::vector<ComponentId> types;
    if (!options.components.isAbsent() && !options.components.isNull())
    {
        if (!options.components.isObject())
        {
            return unexpected(CommandError::invalid("", "must be an object of Component values"));
        }
        for (const JsonMember member : options.components.members())
        {
            const ComponentId type = registry.findByStableName(member.key);
            const std::string pointer = "/" + std::string(member.key);
            if (type == kInvalidComponentId || registry.reflection(type) == nullptr)
            {
                CommandError error = CommandError::make(
                    ErrorKind::NotFound, "there is no Component type '" + std::string(member.key) + "'");
                error.errors.push_back(ValueError{pointer, "is not a Component type"});
                return unexpected(std::move(error));
            }
            if (isGuarded(type))
            {
                return unexpected(
                    CommandError::invalid(pointer, "cannot be given; use the parent and index"));
            }
            types.push_back(type);
        }
    }
    const bool local = std::find(types.begin(), types.end(), componentId<LocalTransform>()) != types.end();
    if (local && options.parent.isNull() && !options.index)
    {
        return unexpected(CommandError::invalid("/illusion.LocalTransform", "needs a parent"));
    }

    const Entity entity = m_world->create();
    const EntityGuid guid = options.guid.isNull() ? m_guids->next() : options.guid;
    m_world->emplace<EntityGuid>(entity, guid);
    m_index->insert(guid, entity);
    if (!options.name.empty())
    {
        m_world->emplace<Name>(entity, Name{options.name});
    }
    IndexResolver resolver(*m_index);
    for (usize i = 0; i < types.size(); ++i)
    {
        const TypeReflection& reflection = *registry.reflection(types[i]);
        const JsonValue value = options.components.find(reflection.name);
        void* data = m_world->emplaceDefault(entity, types[i]);
        const ValueReadResult read =
            reflection.size == 0
                ? ValueReadResult{}
                : readValue(reflection, data, value, {.mode = JsonReadMode::Merge, .entities = &resolver});
        const bool tagWithFields = reflection.size == 0 && value.isObject() && value.size() > 0;
        if (!read.ok() || !read.unknownKeys.empty() || tagWithFields)
        {
            m_index->erase(guid);
            m_world->destroy(entity);
            CommandError error =
                tagWithFields ? CommandError::invalid("/" + std::string((*value.members().begin()).key),
                                                      "is not a field")
                : read.ok()   ? CommandError::invalid(read.unknownKeys.front(), "is not a field")
                              : readError(read);
            return unexpected(withPointerPrefix(std::move(error), "/" + std::string(reflection.name)));
        }
    }
    // Components the given ones require, with default values.
    for (usize i = 0; i < types.size(); ++i)
    {
        for (const std::string_view required : registry.reflection(types[i])->meta.requiredComponents)
        {
            const ComponentId type = registry.findByStableName(required);
            if (type != kInvalidComponentId && !isGuarded(type) && !hasComponent(*m_world, entity, type))
            {
                m_world->emplaceDefault(entity, type);
                types.push_back(type);
            }
        }
    }

    std::vector<Watch> siblings;
    if (!options.parent.isNull() || options.index)
    {
        watchSiblings(siblings, options.parent);
        watch(siblings, options.parent, componentId<Hierarchy>());
        const Expected<void, HierarchyError> parented = illusion::setParent(
            *m_world, entity, options.parent, local ? ParentMode::KeepLocal : ParentMode::KeepWorld,
            options.index.value_or(kLastSibling));
        ILL_ASSERT(parented.hasValue(), "a new entity cannot form a cycle");
    }
    for (const ComponentId type : types)
    {
        m_world->markChanged(type);
    }
    m_changes->record(guid, ChangeKind::Created);
    record(EntityLifetime{{snapshot(entity)}, true});
    finish(siblings);
    return entity;
}

Expected<u32, CommandError> WorldEditor::destroy(Entity root)
{
    ILL_PROFILE_SCOPE();
    if (!m_world->isAlive(root))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    std::vector<Entity> doomed;
    subtreeOf(root, doomed);
    std::unordered_set<u64> doomedBits;
    for (const Entity entity : doomed)
    {
        doomedBits.insert(entity.toBits());
    }

    // Entity fields elsewhere that refer into the subtree become null first.
    std::vector<Entity> all;
    m_world->entities(all);
    std::vector<Watch> references;
    const auto refersToDoomed = [&](Entity& value) {
        return doomedBits.contains(value.toBits());
    };
    for (const Entity entity : all)
    {
        if (doomedBits.contains(entity.toBits()))
        {
            continue;
        }
        for (const ComponentId type : m_world->componentsOf(entity))
        {
            if (!hasEntityFields(type))
            {
                continue;
            }
            const TypeReflection& reflection = *m_world->registry().reflection(type);
            void* data = m_world->componentData(entity, type);
            bool refers = false;
            auto check = [&](Entity& value) {
                refers = refers || refersToDoomed(value);
            };
            forEachEntityField(reflection, data, check);
            if (!refers)
            {
                continue;
            }
            watch(references, entity, type);
            auto clear = [&](Entity& value) {
                if (refersToDoomed(value))
                {
                    value = Entity{};
                }
            };
            forEachEntityField(reflection, data, clear);
        }
    }
    finish(references);

    const Hierarchy* hierarchy = m_world->tryGet<Hierarchy>(root);
    const Entity parent = hierarchy != nullptr ? parentOf(*m_world, root) : Entity{};
    std::vector<Watch> siblings;
    std::vector<Entity> remaining;
    if (hierarchy != nullptr)
    {
        m_index->children(parent, remaining);
        std::erase(remaining, root);
        std::erase_if(remaining, [&](Entity entity) { return !m_world->has<Hierarchy>(entity); });
        for (const Entity sibling : remaining)
        {
            watch(siblings, sibling, componentId<Hierarchy>());
        }
    }

    EntityLifetime lifetime;
    lifetime.spawn = false;
    for (const Entity entity : doomed)
    {
        lifetime.entities.push_back(snapshot(entity));
    }
    for (const EntityRecord& record : lifetime.entities)
    {
        m_changes->record(record.guid, ChangeKind::Destroyed);
        m_index->erase(record.guid);
    }
    for (const Entity entity : doomed)
    {
        m_world->destroy(entity);
    }
    for (usize i = 0; i < remaining.size(); ++i)
    {
        m_world->get<Hierarchy>(remaining[i]).order = static_cast<u32>(i);
    }
    m_world->markChanged<Hierarchy>();
    record(std::move(lifetime));
    finish(siblings);
    return static_cast<u32>(doomed.size());
}

Expected<Entity, CommandError> WorldEditor::duplicate(Entity root)
{
    ILL_PROFILE_SCOPE();
    if (!m_world->isAlive(root))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    std::vector<Entity> originals;
    subtreeOf(root, originals);
    std::unordered_map<u64, Entity> copies; // original Entity bits -> copy
    std::vector<Entity> created;
    for (const Entity original : originals)
    {
        const Entity copy = m_world->create();
        const EntityGuid guid = m_guids->next();
        m_world->emplace<EntityGuid>(copy, guid);
        m_index->insert(guid, copy);
        copies.emplace(original.toBits(), copy);
        created.push_back(copy);
    }
    const ComponentRegistry& registry = m_world->registry();
    for (usize i = 0; i < originals.size(); ++i)
    {
        // Copy the component list first: emplacing into the copy does not touch the original's archetype,
        // but keep the span out of the loop that changes the World.
        const std::span<const ComponentId> source = m_world->componentsOf(originals[i]);
        const std::vector<ComponentId> types(source.begin(), source.end());
        for (const ComponentId type : types)
        {
            const TypeReflection* reflection = registry.reflection(type);
            if (reflection == nullptr || type == componentId<EntityGuid>())
            {
                continue;
            }
            void* target = m_world->emplaceDefault(created[i], type);
            if (reflection->size == 0)
            {
                continue;
            }
            reflection->ops->assign(target, m_world->componentData(originals[i], type));
            auto remap = [&](Entity& value) {
                if (const auto it = copies.find(value.toBits()); it != copies.end())
                {
                    value = it->second;
                }
            };
            forEachEntityField(*reflection, target, remap);
            m_world->markChanged(type);
        }
    }
    const Entity copyRoot = created.front();
    std::vector<Watch> siblings;
    if (const Hierarchy* hierarchy = m_world->tryGet<Hierarchy>(root))
    {
        const u32 position = hierarchy->order;
        std::vector<Entity> others;
        m_index->children(parentOf(*m_world, root), others);
        for (const Entity sibling : others)
        {
            if (sibling != copyRoot && m_world->has<Hierarchy>(sibling))
            {
                watch(siblings, sibling, componentId<Hierarchy>());
            }
        }
        const Expected<void, HierarchyError> moved = illusion::reorder(*m_world, copyRoot, position + 1);
        ILL_ASSERT(moved.hasValue(), "reordering a new copy cannot fail");
    }
    EntityLifetime lifetime;
    for (const Entity copy : created)
    {
        lifetime.entities.push_back(snapshot(copy));
        m_changes->record(m_index->guidOf(copy), ChangeKind::Created);
    }
    record(std::move(lifetime));
    finish(siblings);
    return copyRoot;
}

Expected<void, CommandError> WorldEditor::rename(Entity entity, std::string_view name)
{
    if (!m_world->isAlive(entity))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    if (const std::string_view problem = entityNameError(name); !name.empty() && !problem.empty())
    {
        return unexpected(CommandError::invalid("", std::string(problem)));
    }
    std::vector<Watch> watches;
    watch(watches, entity, componentId<Name>());
    if (name.empty())
    {
        m_world->remove<Name>(entity);
    }
    else if (Name* current = m_world->tryGet<Name>(entity))
    {
        current->value = std::string(name);
    }
    else
    {
        m_world->emplace<Name>(entity, Name{std::string(name)});
    }
    finish(watches);
    return {};
}

Expected<void, CommandError> WorldEditor::setEnabled(Entity entity, bool enabled)
{
    if (!m_world->isAlive(entity))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    std::vector<Watch> watches;
    watch(watches, entity, componentId<Disabled>());
    illusion::setEnabled(*m_world, entity, enabled);
    finish(watches);
    return {};
}

Expected<std::vector<std::string>, CommandError> WorldEditor::addComponent(Entity entity, ComponentId type,
                                                                           JsonValue value)
{
    if (Expected<void, CommandError> editable = checkEditable(entity, type); !editable)
    {
        return unexpected(std::move(editable).error());
    }
    const ComponentRegistry& registry = m_world->registry();
    const TypeReflection& reflection = *registry.reflection(type);
    if (hasComponent(*m_world, entity, type))
    {
        return unexpected(CommandError::make(ErrorKind::AlreadyExists,
                                             "the entity has " + std::string(reflection.name) + " already"));
    }
    // Requirements first, depth first, each once.
    std::vector<ComponentId> order;
    std::vector<ComponentId> pending{type};
    while (!pending.empty())
    {
        const ComponentId next = pending.back();
        pending.pop_back();
        if (std::find(order.begin(), order.end(), next) != order.end() ||
            hasComponent(*m_world, entity, next))
        {
            continue;
        }
        if (next == componentId<Hierarchy>())
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 std::string(reflection.name) +
                                                     " needs a parent; use hierarchy.set_parent first"));
        }
        order.push_back(next);
        for (const std::string_view required : registry.reflection(next)->meta.requiredComponents)
        {
            const ComponentId requiredType = registry.findByStableName(required);
            if (requiredType != kInvalidComponentId && registry.reflection(requiredType) != nullptr)
            {
                pending.push_back(requiredType);
            }
        }
    }
    std::reverse(order.begin(), order.end());

    if (reflection.size == 0 && value.isObject() && value.size() > 0)
    {
        return unexpected(
            CommandError::invalid("/" + std::string((*value.members().begin()).key),
                                  "is not a field (" + std::string(reflection.name) + " is a tag)"));
    }
    std::vector<Watch> watches;
    for (const ComponentId added : order)
    {
        watch(watches, entity, added);
        m_world->emplaceDefault(entity, added);
    }
    if (!value.isAbsent() && !value.isNull() && reflection.size > 0)
    {
        IndexResolver resolver(*m_index);
        const ValueReadResult read = readValue(reflection, m_world->componentData(entity, type), value,
                                               {.mode = JsonReadMode::Merge, .entities = &resolver});
        if (!read.ok() || !read.unknownKeys.empty())
        {
            for (const ComponentId added : order)
            {
                m_world->remove(entity, added);
            }
            return unexpected(read.ok() ? CommandError::invalid(read.unknownKeys.front(), "is not a field")
                                        : readError(read));
        }
    }
    finish(watches);
    std::vector<std::string> names;
    for (const ComponentId added : order)
    {
        names.emplace_back(registry.reflection(added)->name);
    }
    return names;
}

Expected<void, CommandError> WorldEditor::removeComponent(Entity entity, ComponentId type)
{
    if (Expected<void, CommandError> editable = checkEditable(entity, type); !editable)
    {
        return unexpected(std::move(editable).error());
    }
    const ComponentRegistry& registry = m_world->registry();
    const std::string_view name = registry.reflection(type)->name;
    if (!hasComponent(*m_world, entity, type))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity has no " + std::string(name)));
    }
    for (const ComponentId other : m_world->componentsOf(entity))
    {
        const TypeReflection* reflection = registry.reflection(other);
        if (reflection != nullptr && other != type)
        {
            const auto& required = reflection->meta.requiredComponents;
            if (std::find(required.begin(), required.end(), name) != required.end())
            {
                return unexpected(
                    CommandError::make(ErrorKind::PreconditionFailed,
                                       std::string(reflection->name) + " requires " + std::string(name)));
            }
        }
    }
    std::vector<Watch> watches;
    watch(watches, entity, type);
    m_world->remove(entity, type);
    finish(watches);
    return {};
}

Expected<bool, CommandError> WorldEditor::setComponent(Entity entity, ComponentId type, JsonValue value,
                                                       JsonReadMode mode)
{
    ILL_PROFILE_SCOPE();
    if (Expected<void, CommandError> editable = checkEditable(entity, type); !editable)
    {
        return unexpected(std::move(editable).error());
    }
    const TypeReflection& reflection = *m_world->registry().reflection(type);
    if (!hasComponent(*m_world, entity, type))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "the entity has no " + std::string(reflection.name)));
    }
    if (!value.isObject())
    {
        return unexpected(CommandError::invalid("", "must be an object of field values"));
    }
    if (reflection.size == 0)
    {
        return false;
    }
    std::vector<Watch> watches;
    watch(watches, entity, type);
    IndexResolver resolver(*m_index);
    const ValueReadResult read = readValue(reflection, m_world->componentData(entity, type), value,
                                           {.mode = mode, .entities = &resolver});
    if (!read.ok())
    {
        return unexpected(readError(read));
    }
    if (!read.unknownKeys.empty())
    {
        // Undo the fields that were read, then report the unknown one with the closest field names.
        if (Expected<void, CommandError> restored = applyComponent(ComponentChange{
                watches.front().guid, std::string(reflection.name), std::nullopt, watches.front().before});
            !restored)
        {
            return unexpected(std::move(restored).error());
        }
        CommandError error = CommandError::invalid(read.unknownKeys.front(), "is not a field");
        std::vector<std::string_view> fields;
        for (const FieldInfo& field : reflection.fields)
        {
            fields.push_back(field.name);
        }
        const std::string_view key = std::string_view(read.unknownKeys.front()).substr(1);
        error.suggestions = closestMatches(key, fields);
        return unexpected(std::move(error));
    }
    const bool changed = capture(entity, type) != watches.front().before;
    finish(watches);
    return changed;
}

Expected<bool, CommandError> WorldEditor::setField(Entity entity, ComponentId type, std::string_view path,
                                                   JsonValue value)
{
    if (Expected<void, CommandError> editable = checkEditable(entity, type); !editable)
    {
        return unexpected(std::move(editable).error());
    }
    const TypeReflection& reflection = *m_world->registry().reflection(type);
    if (!hasComponent(*m_world, entity, type))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "the entity has no " + std::string(reflection.name)));
    }
    if (reflection.size == 0)
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, std::string(reflection.name) + " has no fields"));
    }
    std::vector<Watch> watches;
    watch(watches, entity, type);
    IndexResolver resolver(*m_index);
    if (Expected<void, ValueError> set = setPath(reflection, m_world->componentData(entity, type), path,
                                                 value, {.mode = JsonReadMode::Merge, .entities = &resolver});
        !set)
    {
        return unexpected(CommandError::invalid("", set.error().pointer.empty()
                                                        ? set.error().message
                                                        : set.error().pointer + " " + set.error().message));
    }
    const bool changed = capture(entity, type) != watches.front().before;
    finish(watches);
    return changed;
}

Expected<bool, CommandError> WorldEditor::resetComponent(Entity entity, ComponentId type)
{
    if (Expected<void, CommandError> editable = checkEditable(entity, type); !editable)
    {
        return unexpected(std::move(editable).error());
    }
    const TypeReflection& reflection = *m_world->registry().reflection(type);
    if (!hasComponent(*m_world, entity, type))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "the entity has no " + std::string(reflection.name)));
    }
    if (reflection.size == 0)
    {
        return false;
    }
    std::vector<Watch> watches;
    watch(watches, entity, type);
    static const JsonDocument empty = std::move(JsonDocument::parse("{}").value());
    const ValueReadResult read = readValue(reflection, m_world->componentData(entity, type), empty.root(),
                                           {.mode = JsonReadMode::Replace});
    ILL_ASSERT(read.ok(), "reading defaults cannot fail");
    const bool changed = capture(entity, type) != watches.front().before;
    finish(watches);
    return changed;
}

Expected<bool, CommandError> WorldEditor::assignComponent(Entity entity, ComponentId type, const void* value)
{
    if (Expected<void, CommandError> editable = checkEditable(entity, type); !editable)
    {
        return unexpected(std::move(editable).error());
    }
    const TypeReflection& reflection = *m_world->registry().reflection(type);
    if (!hasComponent(*m_world, entity, type))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "the entity has no " + std::string(reflection.name)));
    }
    if (reflection.size == 0)
    {
        return false;
    }
    std::vector<Watch> watches;
    watch(watches, entity, type);
    reflection.ops->assign(m_world->componentData(entity, type), value);
    const bool changed = capture(entity, type) != watches.front().before;
    finish(watches);
    return changed;
}

Expected<std::vector<Entity>, CommandError> WorldEditor::paste(std::string_view scene, Entity parent,
                                                               std::optional<u32> index)
{
    ILL_PROFILE_SCOPE();
    if (!parent.isNull() && !m_world->isAlive(parent))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the parent does not exist"));
    }
    SceneLoadOptions options;
    options.ids = IdPolicy::Regenerate;
    options.guids = m_guids;
    // loadScene() adds nothing when it fails.
    const SceneLoadResult loaded = loadScene(*m_world, scene, options);
    if (!loaded.ok())
    {
        return unexpected(CommandError::invalid("", "is not a scene: " + loaded.error));
    }
    std::unordered_set<u64> pasted;
    for (const Entity entity : loaded.entities)
    {
        pasted.insert(entity.toBits());
        m_index->insert(m_world->get<EntityGuid>(entity), entity);
    }
    std::vector<Entity> roots;
    for (const Entity entity : loaded.entities)
    {
        const Hierarchy* hierarchy = m_world->tryGet<Hierarchy>(entity);
        if (hierarchy == nullptr || !pasted.contains(hierarchy->parent.toBits()))
        {
            roots.push_back(entity);
        }
    }

    // The new roots join the siblings under the parent; the existing siblings are recorded.
    std::vector<Watch> siblings;
    std::vector<Entity> existing;
    m_index->children(parent, existing);
    for (const Entity sibling : existing)
    {
        if (!pasted.contains(sibling.toBits()) && m_world->has<Hierarchy>(sibling))
        {
            watch(siblings, sibling, componentId<Hierarchy>());
        }
    }
    watch(siblings, parent, componentId<Hierarchy>());
    u32 position = index.value_or(kLastSibling);
    for (const Entity root : roots)
    {
        if (parent.isNull() && !index && !m_world->has<Hierarchy>(root))
        {
            continue; // a root outside the hierarchy stays one
        }
        const Expected<void, HierarchyError> parented =
            illusion::setParent(*m_world, root, parent, ParentMode::KeepWorld, position);
        ILL_ASSERT(parented.hasValue(), "new entities cannot form a cycle with their parent");
        if (position != kLastSibling)
        {
            ++position;
        }
    }
    m_world->markChanged<Hierarchy>();
    // Scene text leaves out the derived Transforms of children: derive them before the snapshot, so that the
    // pasted entities (and their redo) have them.
    const HierarchyIndex& hierarchy = m_index->hierarchy();
    for (const Entity root : roots)
    {
        for (const Entity entity : hierarchy.subtree(root))
        {
            const LocalTransform* local = m_world->tryGet<LocalTransform>(entity);
            const Entity above = hierarchy.parent(entity);
            if (entity == root || local == nullptr || above.isNull() || m_world->has<Transform>(entity))
            {
                continue;
            }
            const Transform* parentTransform = m_world->tryGet<Transform>(above);
            const Transform placed =
                combine(parentTransform != nullptr ? *parentTransform : Transform{}, local->asTransform());
            m_world->emplace<Transform>(entity, placed);
        }
    }

    EntityLifetime lifetime;
    for (const Entity entity : loaded.entities)
    {
        for (const ComponentId type : m_world->componentsOf(entity))
        {
            m_world->markChanged(type);
        }
        lifetime.entities.push_back(snapshot(entity));
        m_changes->record(lifetime.entities.back().guid, ChangeKind::Created);
    }
    record(std::move(lifetime));
    finish(siblings);
    return roots;
}

void WorldEditor::resetSettings(std::vector<PreservedValue> settings)
{
    std::sort(settings.begin(), settings.end(),
              [](const PreservedValue& a, const PreservedValue& b) { return a.type < b.type; });
    m_settings = std::move(settings);
}

Expected<bool, CommandError> WorldEditor::setSetting(std::string_view type, std::optional<std::string> value,
                                                     std::optional<u32> version)
{
    SettingsChange change;
    change.type = std::string(type);
    const auto it = std::find_if(m_settings.begin(), m_settings.end(),
                                 [&](const PreservedValue& setting) { return setting.type == type; });
    if (it != m_settings.end())
    {
        change.before = it->value.text;
        change.beforeVersion = it->version;
    }
    change.after = std::move(value);
    change.afterVersion = change.after ? version : std::nullopt;
    if (change.before == change.after && change.beforeVersion == change.afterVersion)
    {
        return false;
    }
    if (Expected<void, CommandError> applied = applySettings(change); !applied)
    {
        return unexpected(std::move(applied).error());
    }
    record(std::move(change));
    return true;
}

Expected<void, CommandError> WorldEditor::setParent(Entity entity, Entity parent, ParentMode mode, u32 index)
{
    ILL_PROFILE_SCOPE();
    if (!m_world->isAlive(entity) || (!parent.isNull() && !m_world->isAlive(parent)))
    {
        return unexpected(
            CommandError::make(ErrorKind::NotFound, "the entity or its new parent does not exist"));
    }
    std::vector<Watch> watches;
    watch(watches, entity, componentId<Hierarchy>());
    watch(watches, entity, componentId<LocalTransform>());
    watch(watches, entity, componentId<Transform>());
    // While the entity is a child its Transform is derived from the parent after every change, without an
    // operation. Recording the Transform even when the move keeps it lets the reverse of this move restore
    // the value it had exactly, whatever the parent did in between (a transaction cancel or a multi-step undo
    // reverts several commands before anything is derived again).
    watches.back().always = m_world->has<Transform>(entity);
    watchSiblings(watches, m_world->has<Hierarchy>(entity) ? parentOf(*m_world, entity) : Entity{});
    watchSiblings(watches, parent);
    watch(watches, parent, componentId<Hierarchy>());
    if (const Expected<void, HierarchyError> moved =
            illusion::setParent(*m_world, entity, parent, mode, index);
        !moved)
    {
        return unexpected(
            moved.error() == HierarchyError::Cycle
                ? CommandError::make(ErrorKind::PreconditionFailed,
                                     "the new parent is the entity itself or one of its descendants")
                : CommandError::make(ErrorKind::NotFound, "the entity or its new parent does not exist"));
    }
    // A child's Transform is derived from its parent after every change. Derive it now, so that the operation
    // records the change: undo then restores the Transform the entity had as a root bit for bit, instead of
    // leaving the derived value (which rounding makes slightly different even when the world placement is
    // kept).
    if (!parent.isNull())
    {
        const LocalTransform* local = m_world->tryGet<LocalTransform>(entity);
        Transform* transform = m_world->tryGet<Transform>(entity);
        if (local != nullptr && transform != nullptr)
        {
            const Transform* parentTransform = m_world->tryGet<Transform>(parent);
            *transform =
                combine(parentTransform != nullptr ? *parentTransform : Transform{}, local->asTransform());
        }
    }
    finish(watches);
    return {};
}

Expected<void, CommandError> WorldEditor::reorder(Entity entity, u32 index)
{
    if (!m_world->isAlive(entity))
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    if (!m_world->has<Hierarchy>(entity))
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed,
                               "the entity is not in the hierarchy; use hierarchy.set_parent first"));
    }
    std::vector<Watch> watches;
    watchSiblings(watches, parentOf(*m_world, entity));
    watch(watches, entity, componentId<Hierarchy>());
    if (const Expected<void, HierarchyError> moved = illusion::reorder(*m_world, entity, index); !moved)
    {
        return unexpected(CommandError::make(ErrorKind::NotFound, "the entity does not exist"));
    }
    finish(watches);
    return {};
}

Expected<std::optional<std::string>, CommandError> WorldEditor::storeBlob(std::string_view path)
{
    std::filesystem::path source;
    if (const FileError error = m_files->resolve(path, source); error != FileError::None)
    {
        return unexpected(fileError(error, path));
    }
    std::error_code code;
    if (!std::filesystem::exists(source, code))
    {
        return std::optional<std::string>{};
    }
    std::string name = m_blobPrefix + "/" + std::to_string(m_blobCounter++);
    const std::filesystem::path target = m_blobs / utf8Path(name);
    std::filesystem::create_directories(target.parent_path(), code);
    std::filesystem::copy(source, target, std::filesystem::copy_options::recursive, code);
    if (code)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, "cannot keep a copy of '" +
                                                                     std::string(path) +
                                                                     "' in the trash: " + code.message()));
    }
    return std::optional<std::string>(std::move(name));
}

Expected<bool, CommandError> WorldEditor::writeFile(std::string_view path, std::string_view text)
{
    if (m_files == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    FileStat stat;
    const FileError statError = m_files->stat(path, stat);
    if (statError != FileError::None && statError != FileError::NotFound)
    {
        return unexpected(fileError(statError, path));
    }
    if (statError == FileError::None && stat.isDirectory)
    {
        return unexpected(
            CommandError::make(ErrorKind::PreconditionFailed, "'" + std::string(path) + "' is a directory"));
    }
    std::filesystem::path real;
    if (const FileError error = m_files->resolve(path, real); error != FileError::None)
    {
        return unexpected(fileError(error, path));
    }
    if (Expected<void, CommandError> parents = createParents(path); !parents)
    {
        return unexpected(std::move(parents).error());
    }
    Expected<std::optional<std::string>, CommandError> before = storeBlob(path);
    if (!before)
    {
        return unexpected(std::move(before).error());
    }
    if (const FileError error = m_files->writeText(path, text); error != FileError::None)
    {
        return unexpected(fileError(error, path));
    }
    Expected<std::optional<std::string>, CommandError> after = storeBlob(path);
    if (!after)
    {
        return unexpected(std::move(after).error());
    }
    record(FileChange{FileChange::Kind::Replace, std::string(path), {}, *before, *after});
    return !before->has_value();
}

Expected<void, CommandError> WorldEditor::moveFile(std::string_view from, std::string_view to)
{
    if (m_files == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    FileStat source;
    if (const FileError error = m_files->stat(from, source); error != FileError::None)
    {
        return unexpected(fileError(error, from));
    }
    std::filesystem::path target;
    if (const FileError error = m_files->resolve(to, target); error != FileError::None)
    {
        return unexpected(fileError(error, to));
    }
    if (m_files->exists(to))
    {
        return unexpected(fileError(FileError::AlreadyExists, to));
    }
    if (Expected<void, CommandError> parents = createParents(to); !parents)
    {
        return unexpected(std::move(parents).error());
    }
    if (const FileError error = m_files->rename(from, to); error != FileError::None)
    {
        return unexpected(fileError(error, error == FileError::AlreadyExists ? to : from));
    }
    record(FileChange{FileChange::Kind::Move, std::string(from), std::string(to), {}, {}});
    return {};
}

Expected<void, CommandError> WorldEditor::deleteFile(std::string_view path)
{
    if (m_files == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    std::filesystem::path real;
    if (const FileError error = m_files->resolve(path, real); error != FileError::None)
    {
        return unexpected(fileError(error, path));
    }
    if (!m_files->exists(path))
    {
        return unexpected(fileError(FileError::NotFound, path));
    }
    Expected<std::optional<std::string>, CommandError> before = storeBlob(path);
    if (!before)
    {
        return unexpected(std::move(before).error());
    }
    if (const FileError error = m_files->remove(path, true); error != FileError::None)
    {
        return unexpected(fileError(error, path));
    }
    record(FileChange{FileChange::Kind::Replace, std::string(path), {}, *before, std::nullopt});
    return {};
}

Expected<void, CommandError> WorldEditor::createParents(std::string_view path)
{
    const usize root = path.find(":/");
    const usize slash = path.rfind('/');
    if (root == std::string_view::npos || slash == std::string_view::npos || slash <= root + 1 ||
        m_files->exists(path.substr(0, slash)))
    {
        return {};
    }
    return createDirectory(path.substr(0, slash));
}

Expected<void, CommandError> WorldEditor::createDirectory(std::string_view path)
{
    if (m_files == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    // Missing parents are created too: the change is the topmost new directory with everything below it.
    std::string_view top = path;
    const usize root = path.find(":/");
    for (usize slash = top.rfind('/'); root != std::string_view::npos && slash != std::string_view::npos &&
                                       slash > root + 1 && !m_files->exists(top.substr(0, slash));
         slash = top.rfind('/'))
    {
        top = top.substr(0, slash);
    }
    if (const FileError error = m_files->createDirectory(path); error != FileError::None)
    {
        return unexpected(fileError(error, path));
    }
    Expected<std::optional<std::string>, CommandError> after = storeBlob(top);
    if (!after)
    {
        return unexpected(std::move(after).error());
    }
    record(FileChange{FileChange::Kind::Replace, std::string(top), {}, std::nullopt, *after});
    return {};
}

Expected<void, CommandError> WorldEditor::apply(const Operation& operation)
{
    Expected<void, CommandError> applied = std::visit(
        [this](const auto& value) -> Expected<void, CommandError> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ComponentChange>)
            {
                return applyComponent(value);
            }
            else if constexpr (std::is_same_v<T, EntityLifetime>)
            {
                return applyLifetime(value);
            }
            else if constexpr (std::is_same_v<T, SettingsChange>)
            {
                return applySettings(value);
            }
            else
            {
                return applyFile(value);
            }
        },
        operation);
    if (applied)
    {
        record(operation);
    }
    return applied;
}

Expected<void, CommandError> WorldEditor::applyComponent(const ComponentChange& change)
{
    const Entity entity = m_index->find(change.entity);
    const ComponentId type = m_world->registry().findByStableName(change.type);
    if (entity.isNull() || type == kInvalidComponentId || m_world->registry().reflection(type) == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::Internal, "cannot apply a change of " + change.type +
                                                                      " to " + guidText(change.entity)));
    }
    if (!change.after)
    {
        m_world->remove(entity, type);
    }
    else
    {
        const TypeReflection& reflection = *m_world->registry().reflection(type);
        void* data = hasComponent(*m_world, entity, type) ? m_world->componentData(entity, type)
                                                          : m_world->emplaceDefault(entity, type);
        if (reflection.size > 0)
        {
            Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(*change.after);
            IndexResolver resolver(*m_index);
            const ValueReadResult read =
                parsed ? readValue(reflection, data, parsed->root(),
                                   {.mode = JsonReadMode::Replace, .entities = &resolver})
                       : ValueReadResult{{ValueError{"", "is not valid JSON"}}, {}, {}};
            if (!read.ok())
            {
                return unexpected(
                    CommandError::make(ErrorKind::Internal,
                                       "cannot restore " + change.type + ": " + read.errors.front().message));
            }
        }
    }
    m_world->markChanged(type);
    m_changes->record(change.entity, ChangeKind::Changed, change.type);
    return {};
}

Expected<void, CommandError> WorldEditor::applyLifetime(const EntityLifetime& lifetime)
{
    if (!lifetime.spawn)
    {
        for (auto it = lifetime.entities.rbegin(); it != lifetime.entities.rend(); ++it)
        {
            const Entity entity = m_index->find(it->guid);
            if (entity.isNull())
            {
                return unexpected(CommandError::make(
                    ErrorKind::Internal, "cannot destroy " + guidText(it->guid) + ", which does not exist"));
            }
            m_index->erase(it->guid);
            m_world->destroy(entity);
            m_changes->record(it->guid, ChangeKind::Destroyed);
        }
        m_world->markChanged<Hierarchy>();
        return {};
    }
    // Create every entity first, so that entity fields between them resolve.
    std::vector<Entity> entities;
    for (const EntityRecord& record : lifetime.entities)
    {
        if (!m_index->lookup(record.guid).isNull())
        {
            return unexpected(CommandError::make(
                ErrorKind::Internal, "cannot create " + guidText(record.guid) + ", which exists"));
        }
        const Entity entity = m_world->create();
        m_world->emplace<EntityGuid>(entity, record.guid);
        m_index->insert(record.guid, entity);
        entities.push_back(entity);
    }
    const ComponentRegistry& registry = m_world->registry();
    IndexResolver resolver(*m_index);
    for (usize i = 0; i < entities.size(); ++i)
    {
        Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(lifetime.entities[i].components);
        if (!parsed || !parsed->root().isObject())
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "damaged entity record"));
        }
        for (const JsonMember member : parsed->root().members())
        {
            const ComponentId type = registry.findByStableName(member.key);
            const TypeReflection* reflection =
                type != kInvalidComponentId ? registry.reflection(type) : nullptr;
            if (reflection == nullptr)
            {
                ILL_LOG_WARN("Editor", "Restoring {}: Component type {} is not registered any more",
                             guidText(lifetime.entities[i].guid), member.key);
                continue;
            }
            void* data = m_world->emplaceDefault(entities[i], type);
            if (reflection->size > 0)
            {
                const ValueReadResult read = readValue(
                    *reflection, data, member.value, {.mode = JsonReadMode::Replace, .entities = &resolver});
                if (!read.ok())
                {
                    ILL_LOG_WARN("Editor", "Restoring {} of {}: {}", member.key,
                                 guidText(lifetime.entities[i].guid), read.errors.front().message);
                }
            }
            m_world->markChanged(type);
        }
        m_changes->record(lifetime.entities[i].guid, ChangeKind::Created);
    }
    m_world->markChanged<Hierarchy>();
    return {};
}

Expected<void, CommandError> WorldEditor::applyFile(const FileChange& change)
{
    if (m_files == nullptr)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed, "no project is open"));
    }
    if (change.kind == FileChange::Kind::Move)
    {
        if (const FileError error = m_files->rename(change.path, change.target); error != FileError::None)
        {
            return unexpected(fileError(error, change.path));
        }
        return {};
    }
    if (m_files->exists(change.path))
    {
        if (const FileError error = m_files->remove(change.path, true); error != FileError::None)
        {
            return unexpected(fileError(error, change.path));
        }
    }
    if (!change.after)
    {
        return {};
    }
    std::filesystem::path target;
    if (const FileError error = m_files->resolve(change.path, target); error != FileError::None)
    {
        return unexpected(fileError(error, change.path));
    }
    std::error_code code;
    std::filesystem::create_directories(target.parent_path(), code);
    std::filesystem::copy(m_blobs / utf8Path(*change.after), target, std::filesystem::copy_options::recursive,
                          code);
    if (code)
    {
        return unexpected(CommandError::make(ErrorKind::IoError, "cannot restore '" + change.path +
                                                                     "' from the trash: " + code.message()));
    }
    return {};
}

Expected<void, CommandError> WorldEditor::applySettings(const SettingsChange& change)
{
    const auto it = std::lower_bound(
        m_settings.begin(), m_settings.end(), change.type,
        [](const PreservedValue& setting, const std::string& type) { return setting.type < type; });
    const bool present = it != m_settings.end() && it->type == change.type;
    const std::optional<std::string> current =
        present ? std::optional<std::string>(it->value.text) : std::nullopt;
    if (current != change.before)
    {
        return unexpected(
            CommandError::make(ErrorKind::Internal, "the scene setting " + change.type + " does not match"));
    }
    if (!change.after)
    {
        if (present)
        {
            m_settings.erase(it);
        }
    }
    else if (present)
    {
        it->value.text = *change.after;
        it->version = change.afterVersion;
    }
    else
    {
        m_settings.insert(it, PreservedValue{change.type, change.afterVersion, JsonText{*change.after}});
    }
    m_changes->record(EntityGuid{}, ChangeKind::Settings, change.type);
    return {};
}

} // namespace illusion::editor

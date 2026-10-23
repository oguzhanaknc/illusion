#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/EntityIndex.hpp>
#include <illusion/editor/core/Selection.hpp>

#include <algorithm>
#include <array>

namespace illusion::editor
{
namespace
{

constexpr std::string_view kReferenceForms =
    "must be an entity reference: an id of 16 hex digits, a path such as \"/Game/Board\", \"name:Board\" or "
    "\"$selection\"";

std::string guidText(EntityGuid guid)
{
    const auto text = formatGuid(guid);
    return std::string(text.data(), text.size());
}

CommandError notFound(std::string_view pointer, std::string message)
{
    CommandError error = CommandError::make(ErrorKind::NotFound, message);
    error.errors.push_back(ValueError{std::string(pointer), std::move(message)});
    return error;
}

CommandError ambiguous(std::string_view pointer, std::string message)
{
    CommandError error = CommandError::make(ErrorKind::Ambiguous, message);
    error.errors.push_back(ValueError{std::string(pointer), std::move(message)});
    return error;
}

} // namespace

EntityIndex::EntityIndex(World& world)
    : m_world(&world)
{
    rebuild();
}

Entity EntityIndex::find(EntityGuid guid)
{
    if (guid.isNull())
    {
        return {};
    }
    const auto valid = [&](Entity entity) {
        const EntityGuid* current = m_world->isAlive(entity) ? m_world->tryGet<EntityGuid>(entity) : nullptr;
        return current != nullptr && *current == guid;
    };
    if (const auto it = m_entities.find(guid.value); it != m_entities.end() && valid(it->second))
    {
        return it->second;
    }
    rebuild();
    const auto it = m_entities.find(guid.value);
    return it != m_entities.end() ? it->second : Entity{};
}

Entity EntityIndex::lookup(EntityGuid guid) const noexcept
{
    const auto it = m_entities.find(guid.value);
    if (it == m_entities.end() || !m_world->isAlive(it->second))
    {
        return {};
    }
    const EntityGuid* current = m_world->tryGet<EntityGuid>(it->second);
    return current != nullptr && *current == guid ? it->second : Entity{};
}

EntityGuid EntityIndex::guidOf(Entity entity) const noexcept
{
    const EntityGuid* guid = m_world->isAlive(entity) ? m_world->tryGet<EntityGuid>(entity) : nullptr;
    return guid != nullptr ? *guid : EntityGuid{};
}

void EntityIndex::insert(EntityGuid guid, Entity entity)
{
    m_entities[guid.value] = entity;
}

void EntityIndex::erase(EntityGuid guid)
{
    m_entities.erase(guid.value);
}

void EntityIndex::rebuild()
{
    m_entities.clear();
    m_world->query<const EntityGuid>().each(
        [this](Entity entity, const EntityGuid& guid) { m_entities[guid.value] = entity; });
}

const HierarchyIndex& EntityIndex::hierarchy()
{
    m_hierarchy.update(*m_world);
    return m_hierarchy;
}

void EntityIndex::updateTransforms(bool addMissing)
{
    ILL_PROFILE_SCOPE();
    const auto versions = [this] {
        return std::array<u64, 3>{m_world->componentVersion<Transform>(),
                                  m_world->componentVersion<LocalTransform>(),
                                  m_world->componentVersion<Hierarchy>()};
    };
    if (!addMissing &&
        std::equal(std::begin(m_placementVersions), std::end(m_placementVersions), versions().begin()))
    {
        return;
    }
    const HierarchyIndex& index = hierarchy();
    // Parents come before their children in pre-order, so every parent's Transform is current when its
    // children are derived from it.
    for (const Entity entity : index.preOrder())
    {
        const Entity parent = index.parent(entity);
        const LocalTransform* local = parent.isNull() ? nullptr : m_world->tryGet<LocalTransform>(entity);
        if (local == nullptr)
        {
            continue;
        }
        const Transform* parentTransform = m_world->tryGet<Transform>(parent);
        const Transform placed =
            combine(parentTransform != nullptr ? *parentTransform : Transform{}, local->asTransform());
        if (Transform* transform = m_world->tryGet<Transform>(entity))
        {
            *transform = placed;
        }
        else if (addMissing)
        {
            m_world->emplace<Transform>(entity, placed);
        }
    }
    const std::array<u64, 3> current = versions();
    std::copy(current.begin(), current.end(), std::begin(m_placementVersions));
}

void EntityIndex::children(Entity parent, std::vector<Entity>& out)
{
    const HierarchyIndex& index = hierarchy();
    if (!parent.isNull())
    {
        const std::span<const Entity> children = index.children(parent);
        out.insert(out.end(), children.begin(), children.end());
        return;
    }
    const std::span<const Entity> roots = index.roots();
    out.insert(out.end(), roots.begin(), roots.end());
    m_scratch.clear();
    m_world->entities(m_scratch);
    const usize first = out.size();
    for (const Entity entity : m_scratch)
    {
        if (!index.contains(entity))
        {
            out.push_back(entity);
        }
    }
    std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
              [this](Entity a, Entity b) { return guidOf(a).value < guidOf(b).value; });
}

std::string EntityIndex::segmentOf(Entity entity) const
{
    const Name* name = m_world->tryGet<Name>(entity);
    return name != nullptr && !name->value.empty() ? name->value : "#" + guidText(guidOf(entity));
}

std::string EntityIndex::pathOf(Entity entity)
{
    if (!m_world->isAlive(entity))
    {
        return {};
    }
    std::vector<std::string> segments;
    for (Entity current = entity; !current.isNull() && segments.size() <= m_world->aliveCount();
         current = parentOf(*m_world, current))
    {
        segments.push_back(segmentOf(current));
    }
    std::string path;
    for (auto it = segments.rbegin(); it != segments.rend(); ++it)
    {
        path += '/';
        path += *it;
    }
    return path;
}

EntitySummary EntityIndex::summary(Entity entity)
{
    EntitySummary out;
    out.id = guidText(guidOf(entity));
    if (const Name* name = m_world->isAlive(entity) ? m_world->tryGet<Name>(entity) : nullptr)
    {
        out.name = name->value;
    }
    out.path = pathOf(entity);
    return out;
}

Expected<Entity, CommandError> EntityIndex::resolve(std::string_view reference, const Selection& selection,
                                                    std::string_view pointer)
{
    if (reference == "$selection")
    {
        if (selection.size() != 1)
        {
            return unexpected(selection.empty()
                                  ? CommandError::make(ErrorKind::PreconditionFailed, "nothing is selected")
                                  : ambiguous(pointer, std::to_string(selection.size()) +
                                                           " entities are selected; this needs exactly one"));
        }
        const Entity entity = find(selection.primary());
        if (entity.isNull())
        {
            return unexpected(notFound(pointer, "the selected entity does not exist any more"));
        }
        return entity;
    }
    if (reference.starts_with("name:"))
    {
        return resolveName(reference.substr(5), pointer);
    }
    if (reference.starts_with('/'))
    {
        return resolvePath(reference, pointer);
    }
    if (const std::optional<EntityGuid> guid =
            reference.size() == kGuidTextLength ? parseGuid(reference) : std::nullopt)
    {
        const Entity entity = find(*guid);
        if (entity.isNull())
        {
            return unexpected(notFound(pointer, "no entity has the id " + std::string(reference)));
        }
        return entity;
    }
    return unexpected(CommandError::invalid(std::string(pointer), std::string(kReferenceForms)));
}

Expected<void, CommandError> EntityIndex::resolveAll(std::span<const std::string> references,
                                                     const Selection& selection, std::string_view pointer,
                                                     std::vector<Entity>& out)
{
    const auto add = [&out](Entity entity) {
        if (std::find(out.begin(), out.end(), entity) == out.end())
        {
            out.push_back(entity);
        }
    };
    for (usize i = 0; i < references.size(); ++i)
    {
        if (references[i] == "$selection")
        {
            for (const EntityGuid guid : selection.items())
            {
                if (const Entity entity = find(guid); !entity.isNull())
                {
                    add(entity);
                }
            }
            continue;
        }
        const std::string elementPointer = std::string(pointer) + "/" + std::to_string(i);
        Expected<Entity, CommandError> entity = resolve(references[i], selection, elementPointer);
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        add(*entity);
    }
    return {};
}

Expected<Entity, CommandError> EntityIndex::resolveName(std::string_view name, std::string_view pointer)
{
    Entity found;
    u32 matches = 0;
    std::vector<std::string> names;
    m_world->query<const Name>().each([&](Entity entity, const Name& entityName) {
        if (entityName.value == name)
        {
            found = entity;
            ++matches;
        }
        names.push_back(entityName.value);
    });
    if (matches == 1)
    {
        return found;
    }
    if (matches > 1)
    {
        return unexpected(ambiguous(pointer, std::to_string(matches) + " entities are named '" +
                                                 std::string(name) + "'; use a path or an id"));
    }
    CommandError error = notFound(pointer, "no entity is named '" + std::string(name) + "'");
    error.suggestions = closestMatches(name, std::vector<std::string_view>(names.begin(), names.end()));
    for (std::string& suggestion : error.suggestions)
    {
        suggestion = "name:" + suggestion;
    }
    return unexpected(std::move(error));
}

Expected<Entity, CommandError> EntityIndex::resolvePath(std::string_view path, std::string_view pointer)
{
    Entity current;
    std::string walked;
    std::vector<Entity> candidates;
    std::string_view rest = path.substr(1);
    while (true)
    {
        const usize slash = rest.find('/');
        const std::string_view segment = rest.substr(0, slash);
        if (segment.empty())
        {
            return unexpected(CommandError::invalid(std::string(pointer), "has an empty path segment"));
        }
        candidates.clear();
        children(current, candidates);
        Entity match;
        u32 matches = 0;
        const std::optional<EntityGuid> guid =
            segment.starts_with('#') ? parseGuid(segment.substr(1)) : std::nullopt;
        for (const Entity candidate : candidates)
        {
            const Name* name = m_world->tryGet<Name>(candidate);
            if ((name != nullptr && name->value == segment) || (guid && guidOf(candidate) == *guid))
            {
                match = candidate;
                ++matches;
            }
        }
        if (matches > 1)
        {
            return unexpected(ambiguous(pointer, std::to_string(matches) + " entities under '" +
                                                     (walked.empty() ? std::string("/") : walked) +
                                                     "' are named '" + std::string(segment) +
                                                     "'; use an id"));
        }
        if (matches == 0)
        {
            CommandError error = notFound(pointer, "no entity '" + std::string(segment) + "' under '" +
                                                       (walked.empty() ? std::string("/") : walked) + "'");
            std::vector<std::string> names;
            for (const Entity candidate : candidates)
            {
                names.push_back(segmentOf(candidate));
            }
            for (std::string& suggestion :
                 closestMatches(segment, std::vector<std::string_view>(names.begin(), names.end())))
            {
                error.suggestions.push_back(walked + "/" + suggestion);
            }
            return unexpected(std::move(error));
        }
        current = match;
        walked += '/';
        walked += segment;
        if (slash == std::string_view::npos)
        {
            return current;
        }
        rest = rest.substr(slash + 1);
    }
}

} // namespace illusion::editor

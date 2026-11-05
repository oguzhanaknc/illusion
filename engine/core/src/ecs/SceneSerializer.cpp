#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Prefab.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/World.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.scene";
constexpr std::string_view kPrefabFormatName = "illusion.prefab";

std::string_view textOf(const std::array<char, kGuidTextLength>& text) noexcept
{
    return {text.data(), text.size()};
}

// Entity fields of a saved scene are the ids of saved entities.
class SceneGuidWriter final : public EntityResolver
{
public:
    explicit SceneGuidWriter(const std::unordered_map<Entity, u64>& ids) noexcept
        : m_ids(ids)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        const auto it = m_ids.find(entity);
        return it != m_ids.end() ? doc.makeString(textOf(formatGuid(EntityGuid{it->second})))
                                 : doc.makeNull();
    }

    Expected<Entity, std::string> readEntity(JsonValue) const override
    {
        return unexpected(std::string("cannot be read by a scene writer"));
    }

private:
    const std::unordered_map<Entity, u64>& m_ids;
};

// Version 2: Entity fields are ids of entities in the file. A lenient reader (for files with unresolved
// prefab instances, whose entities are not created) reads unknown ids as null and counts them.
class SceneGuidReader final : public EntityResolver
{
public:
    explicit SceneGuidReader(const std::unordered_map<u64, Entity>& entities, bool lenient = false) noexcept
        : m_entities(entities)
        , m_lenient(lenient)
    {
    }

    // Number of ids read as null because no entity has them (lenient readers only).
    [[nodiscard]] usize cleared() const noexcept { return m_cleared; }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity) const override { return doc.makeNull(); }

    Expected<Entity, std::string> readEntity(JsonValue value) const override
    {
        if (value.isNull())
        {
            return kNullEntity;
        }
        const std::optional<std::string_view> text = value.asString();
        const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
        const auto it = guid ? m_entities.find(guid->value) : m_entities.end();
        if (it == m_entities.end())
        {
            if (m_lenient && guid)
            {
                ++m_cleared;
                return kNullEntity;
            }
            return unexpected(std::string("must be null or the id of an entity in this scene"));
        }
        return it->second;
    }

private:
    const std::unordered_map<u64, Entity>& m_entities;
    bool m_lenient;
    mutable usize m_cleared = 0;
};

// Version 1: Entity fields are indices into the scene's entity array.
class SceneIndexReader final : public EntityResolver
{
public:
    explicit SceneIndexReader(std::span<const Entity> entities) noexcept
        : m_entities(entities)
    {
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity) const override { return doc.makeNull(); }

    Expected<Entity, std::string> readEntity(JsonValue value) const override
    {
        if (value.isNull())
        {
            return kNullEntity;
        }
        const std::optional<u64> index = value.asU64();
        if (!index || *index >= m_entities.size())
        {
            return unexpected(std::string("must be null or the index of an entity in this scene"));
        }
        return m_entities[static_cast<usize>(*index)];
    }

private:
    std::span<const Entity> m_entities;
};

// True if @p a and @p b hold the same JSON value (members in any order).
bool jsonEqual(JsonValue a, JsonValue b)
{
    if (a.type() != b.type())
    {
        return false;
    }
    switch (a.type())
    {
    case JsonType::Bool:
        return a.asBool() == b.asBool();
    case JsonType::Integer:
        return a.asI64() == b.asI64() && a.asU64() == b.asU64();
    case JsonType::Real:
        return a.asF64() == b.asF64();
    case JsonType::String:
        return a.asString() == b.asString();
    case JsonType::Array:
        if (a.size() != b.size())
        {
            return false;
        }
        for (usize i = 0; i < a.size(); ++i)
        {
            if (!jsonEqual(a[i], b[i]))
            {
                return false;
            }
        }
        return true;
    case JsonType::Object:
        if (a.size() != b.size())
        {
            return false;
        }
        for (const JsonMember member : a.members())
        {
            const JsonValue other = b.find(member.key);
            if (other.isAbsent() || !jsonEqual(member.value, other))
            {
                return false;
            }
        }
        return true;
    default:
        return true;
    }
}

// The override that turns @p base into @p value: objects compare field by field, other values as a whole;
// nullopt if they are equal.
std::optional<JsonMutValue> jsonPatch(JsonMutDocument& doc, JsonValue value, JsonValue base)
{
    if (value.isObject() && base.isObject())
    {
        JsonMutValue patch = doc.makeObject();
        bool changed = false;
        for (const JsonMember member : value.members())
        {
            const JsonValue other = base.find(member.key);
            if (other.isAbsent())
            {
                patch.add(member.key, doc.copy(member.value));
                changed = true;
            }
            else if (std::optional<JsonMutValue> nested = jsonPatch(doc, member.value, other))
            {
                patch.add(member.key, *nested);
                changed = true;
            }
        }
        return changed ? std::optional<JsonMutValue>(patch) : std::nullopt;
    }
    return jsonEqual(value, base) ? std::nullopt : std::optional<JsonMutValue>(doc.copy(value));
}

// "/vec3" -> "vec3"; "/points/2" -> "points".
std::string_view firstToken(std::string_view pointer) noexcept
{
    if (pointer.starts_with('/'))
    {
        pointer.remove_prefix(1);
    }
    return pointer.substr(0, pointer.find('/'));
}

// "<owner>: field 'vec3' of 'test.Everything' is not finite", with the full pointer for nested values.
std::string fieldMessage(std::string_view owner, std::string_view type, const ValueError& error)
{
    const std::string_view field = firstToken(error.pointer);
    std::string message = std::format("{}: field '{}' of '{}' {}", owner, field, type, error.message);
    if (error.pointer.size() > field.size() + 1)
    {
        message.append(std::format(" (at {})", error.pointer));
    }
    return message;
}

// "/speed" -> "test.Follow.speed".
std::string qualifiedName(std::string_view type, std::string_view pointer)
{
    std::string name(type);
    for (const char c : pointer)
    {
        name.push_back(c == '/' ? '.' : c);
    }
    return name;
}

// A default-constructed object of a reflected type, freed on destruction.
class ScratchObject : NonCopyable
{
public:
    explicit ScratchObject(const TypeReflection& type)
        : m_type(type)
        , m_data(::operator new(type.size, std::align_val_t{type.alignment}))
    {
        type.ops->construct(m_data);
    }

    ~ScratchObject()
    {
        m_type.ops->destroy(m_data);
        ::operator delete(m_data, std::align_val_t{m_type.alignment});
    }

    [[nodiscard]] void* data() const noexcept { return m_data; }

private:
    const TypeReflection& m_type;
    void* m_data;
};

class SceneWriter
{
public:
    SceneWriter(const World& world, const SceneSaveOptions& options, SceneSaveResult& result)
        : m_world(world)
        , m_options(options)
        , m_result(result)
        , m_resolver(m_ids)
    {
    }

    bool write()
    {
        std::vector<Entity> all;
        std::span<const Entity> entities;
        if (m_options.subset)
        {
            entities = *m_options.subset;
        }
        else
        {
            m_world.entities(all);
            entities = all;
        }
        std::vector<Entity> withMembers;
        if (m_options.prefabMode == PrefabSaveMode::Records && addInstanceMembers(entities, withMembers))
        {
            entities = withMembers;
        }
        if (!collectIds(entities))
        {
            return false;
        }
        findOrphans();
        if (!checkPrefabRoot())
        {
            return false;
        }

        JsonMutValue root = m_doc.makeObject();
        m_doc.setRoot(root);
        root.add("format", m_doc.makeString(m_options.prefabFile ? kPrefabFormatName : kFormatName))
            .add("version", m_doc.makeU64(kSceneFormatVersion));
        // Filled last; members added later stay in this position.
        JsonMutValue types = root.addObject("types");
        if (!writeSettings(root.addObject("settings")))
        {
            return false;
        }
        JsonMutValue array = root.addArray("entities");
        std::vector<Entry> entries;
        for (const Saved& saved : m_order)
        {
            bool written = true;
            switch (kindOf(saved.entity))
            {
            case Kind::Plain:
                written = writeEntity(array.pushObject(), saved, entries);
                break;
            case Kind::Record:
                written = writeRecord(array.pushObject(), saved.entity);
                break;
            case Kind::Unresolved:
                written = writeUnresolved(array.pushObject(), saved);
                break;
            case Kind::Member:
                break;
            }
            if (!written)
            {
                return false;
            }
        }
        for (const auto& [name, version] : m_typeVersions)
        {
            types.add(name, m_doc.makeU64(version));
        }

        if (auto written = m_doc.write(m_result.json); !written)
        {
            m_result.json.clear();
            return fail(std::format("the scene could not be written: {}", written.error().message));
        }
        m_result.entities = static_cast<u32>(m_order.size());
        return true;
    }

private:
    struct Saved
    {
        u64 guid;
        Entity entity;
    };

    static constexpr u32 kNoItem = ~0u;

    // One saved entity while orderByHierarchy() sorts.
    struct OrderItem
    {
        u32 order = 0;
        u32 parent = kNoItem;
        u32 firstChild = 0;
        u32 childCount = 0;
        bool visited = false;
    };

    // One value to write under a stable name: a reflected object or preserved JSON.
    struct Entry
    {
        std::string_view name;
        const TypeReflection* type = nullptr;
        const void* object = nullptr;
        const PreservedValue* preserved = nullptr;
    };

    // How a saved entity is written.
    enum class Kind : u8
    {
        Plain,      // its components (and, with PrefabSaveMode::Expanded, its prefab link)
        Record,     // the root of an instance: the instance's record
        Member,     // another entity of an instance: written with its record
        Unresolved, // an UnresolvedPrefab: its record again
    };

    bool fail(std::string message)
    {
        m_result.error = std::move(message);
        return false;
    }

    [[nodiscard]] Kind kindOf(Entity entity) const
    {
        if (m_options.prefabMode == PrefabSaveMode::Records)
        {
            const PrefabMember* member = m_world.tryGet<PrefabMember>(entity);
            if (member != nullptr && m_instances.contains(member->instance.value) &&
                !m_orphans.contains(entity))
            {
                const PrefabInstance* instance = m_world.tryGet<PrefabInstance>(entity);
                return instance != nullptr && instance->instance == member->instance ? Kind::Record
                                                                                     : Kind::Member;
            }
        }
        return m_world.has<UnresolvedPrefab>(entity) ? Kind::Unresolved : Kind::Plain;
    }

    // Finds the instances whose roots are among @p entities and fills @p out with @p entities and the
    // instances' other members. Returns false (leaving @p out empty) if no member is missing.
    bool addInstanceMembers(std::span<const Entity> entities, std::vector<Entity>& out)
    {
        for (const Entity entity : entities)
        {
            const PrefabInstance* instance = m_world.tryGet<PrefabInstance>(entity);
            const PrefabMember* member = m_world.tryGet<PrefabMember>(entity);
            if (instance != nullptr && member != nullptr && member->instance == instance->instance)
            {
                m_instances.insert(instance->instance.value);
            }
        }
        if (m_instances.empty())
        {
            return false;
        }
        const std::unordered_set<Entity> saved(entities.begin(), entities.end());
        std::vector<Entity> extra;
        std::vector<Entity> all;
        m_world.entities(all);
        for (const Entity entity : all)
        {
            const PrefabMember* member = m_world.tryGet<PrefabMember>(entity);
            if (member != nullptr && m_instances.contains(member->instance.value) && !saved.contains(entity))
            {
                extra.push_back(entity);
            }
        }
        if (extra.empty())
        {
            return false;
        }
        out.reserve(entities.size() + extra.size());
        out.assign(entities.begin(), entities.end());
        out.insert(out.end(), extra.begin(), extra.end());
        return true;
    }

    // Finds the members whose prefab no longer has them (it changed since they were created): no record can
    // hold them, so they are saved as plain entities, which keeps references to them. An instance whose root
    // is such a member is saved entirely as plain entities.
    void findOrphans()
    {
        if (m_instances.empty() || !m_options.prefabs)
        {
            return;
        }
        std::unordered_map<u64, std::string> paths;
        for (const Saved& saved : m_order)
        {
            const PrefabInstance* instance = m_world.tryGet<PrefabInstance>(saved.entity);
            const PrefabMember* member = m_world.tryGet<PrefabMember>(saved.entity);
            if (instance != nullptr && member != nullptr && member->instance == instance->instance)
            {
                paths.emplace(instance->instance.value, std::string(instance->prefab.path()));
            }
        }
        std::unordered_set<u64> lostRoots;
        for (const Saved& saved : m_order)
        {
            const PrefabMember* member = m_world.tryGet<PrefabMember>(saved.entity);
            const auto path = member != nullptr ? paths.find(member->instance.value) : paths.end();
            if (path == paths.end())
            {
                continue;
            }
            const BaseExpansion& expansion = baseExpansion(path->second);
            if (expansion.error.empty() && !expansion.locals.contains(member->local.value))
            {
                m_orphans.insert(saved.entity);
                const PrefabInstance* instance = m_world.tryGet<PrefabInstance>(saved.entity);
                if (instance != nullptr && instance->instance == member->instance)
                {
                    lostRoots.insert(member->instance.value);
                }
            }
        }
        for (const u64 instance : lostRoots)
        {
            m_instances.erase(instance);
        }
    }

    // A prefab file has exactly one root: one written entity or expanded record whose parent is not saved.
    bool checkPrefabRoot()
    {
        if (!m_options.prefabFile)
        {
            return true;
        }
        usize roots = 0;
        for (const Saved& saved : m_order)
        {
            // An unresolved nested record names its parent in its overrides, not in a Hierarchy.
            const Kind kind = kindOf(saved.entity);
            if (kind != Kind::Member && kind != Kind::Unresolved && savedParent(saved.entity).isNull())
            {
                ++roots;
            }
        }
        if (roots != 1)
        {
            return fail(std::format("a prefab must have exactly one root entity, not {}", roots));
        }
        return true;
    }

    bool collectIds(std::span<const Entity> entities)
    {
        m_order.reserve(entities.size());
        m_ids.reserve(entities.size());
        std::unordered_set<u64> used;
        used.reserve(entities.size());
        for (const Entity entity : entities)
        {
            ILL_ASSERT(m_world.isAlive(entity), "saveScene() with a dead entity");
            const EntityGuid* guid = m_world.tryGet<EntityGuid>(entity);
            if (guid == nullptr || guid->isNull())
            {
                return fail(std::format("the entity with handle index {} has no EntityGuid; call "
                                        "assignMissingGuids() before saving",
                                        entity.index));
            }
            if (!used.insert(guid->value).second)
            {
                return fail(std::format("id {} is used by two saved entities", toString(*guid)));
            }
            m_ids.emplace(entity, guid->value);
            m_order.push_back(Saved{guid->value, entity});
        }
        if (m_options.order == SceneOrder::ById)
        {
            std::sort(m_order.begin(), m_order.end(),
                      [](const Saved& a, const Saved& b) { return a.guid < b.guid; });
        }
        else if (m_options.order == SceneOrder::Hierarchy)
        {
            orderByHierarchy();
        }
        return true;
    }

    // The saved parent of @p entity, or the null entity if it has none (or its parent is not saved).
    [[nodiscard]] Entity savedParent(Entity entity) const
    {
        const Hierarchy* hierarchy = m_world.tryGet<Hierarchy>(entity);
        if (hierarchy == nullptr || hierarchy->parent.isNull() || !m_world.isAlive(hierarchy->parent) ||
            !m_ids.contains(hierarchy->parent))
        {
            return Entity{};
        }
        return hierarchy->parent;
    }

    // Sorts m_order into depth-first pre-order of the saved entities' hierarchy (see SceneOrder::Hierarchy).
    void orderByHierarchy()
    {
        const auto count = static_cast<u32>(m_order.size());
        std::unordered_map<Entity, u32> positions;
        positions.reserve(count);
        for (u32 i = 0; i < count; ++i)
        {
            positions.emplace(m_order[i].entity, i);
        }
        std::vector<OrderItem> items(count);
        for (u32 i = 0; i < count; ++i)
        {
            const Hierarchy* hierarchy = m_world.tryGet<Hierarchy>(m_order[i].entity);
            items[i].order = hierarchy != nullptr ? hierarchy->order : std::numeric_limits<u32>::max();
            const Entity parent = savedParent(m_order[i].entity);
            if (!parent.isNull())
            {
                items[i].parent = positions.at(parent);
                ++items[items[i].parent].childCount;
            }
        }
        u32 offset = 0;
        for (OrderItem& item : items)
        {
            item.firstChild = offset;
            offset += item.childCount;
            item.childCount = 0;
        }
        std::vector<u32> children(offset);
        std::vector<u32> roots;
        for (u32 i = 0; i < count; ++i)
        {
            if (items[i].parent == kNoItem)
            {
                roots.push_back(i);
            }
            else
            {
                OrderItem& parent = items[items[i].parent];
                children[parent.firstChild + parent.childCount++] = i;
            }
        }
        const auto less = [&](u32 a, u32 b) {
            return items[a].order != items[b].order ? items[a].order < items[b].order
                                                    : m_order[a].guid < m_order[b].guid;
        };
        for (const OrderItem& item : items)
        {
            std::sort(children.begin() + item.firstChild,
                      children.begin() + item.firstChild + item.childCount, less);
        }
        std::sort(roots.begin(), roots.end(), less);

        std::vector<Saved> sorted;
        sorted.reserve(count);
        std::vector<u32> stack;
        const auto visit = [&](u32 root) {
            items[root].visited = true;
            stack.assign(1, root);
            while (!stack.empty())
            {
                const u32 i = stack.back();
                stack.pop_back();
                sorted.push_back(m_order[i]);
                for (u32 c = items[i].childCount; c-- > 0;)
                {
                    const u32 child = children[items[i].firstChild + c];
                    if (!items[child].visited)
                    {
                        items[child].visited = true;
                        stack.push_back(child);
                    }
                }
            }
        };
        for (const u32 root : roots)
        {
            visit(root);
        }
        if (sorted.size() != count)
        {
            // Parents that form a cycle: start from the first unvisited entity of each.
            roots.clear();
            for (u32 i = 0; i < count; ++i)
            {
                if (!items[i].visited)
                {
                    roots.push_back(i);
                }
            }
            std::sort(roots.begin(), roots.end(), less);
            for (const u32 root : roots)
            {
                if (!items[root].visited)
                {
                    visit(root);
                }
            }
        }
        m_order = std::move(sorted);
    }

    // Records the version of a type for the "types" table.
    bool recordVersion(std::string_view name, u32 version)
    {
        const auto [it, inserted] = m_typeVersions.emplace(name, version);
        if (!inserted && it->second != version)
        {
            return fail(std::format("'{}' would be saved with versions {} and {}; load the scene again to "
                                    "update its preserved data",
                                    name, it->second, version));
        }
        return true;
    }

    // Writes @p entries, sorted by name, into @p object. Of entries with the same name only the first
    // after sorting is written: a reflected value comes before preserved data.
    bool writeEntries(JsonMutValue object, std::vector<Entry>& entries, std::string_view owner)
    {
        return writeEntries(m_doc, m_resolver, object, entries, owner);
    }

    // writeEntries() into @p doc, writing Entity fields with @p resolver.
    bool writeEntries(JsonMutDocument& doc, const EntityResolver& resolver, JsonMutValue object,
                      std::vector<Entry>& entries, std::string_view owner)
    {
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.name != b.name ? a.name < b.name : a.type != nullptr && b.type == nullptr;
        });
        const ValueWriteOptions options{.entities = &resolver};
        for (usize i = 0; i < entries.size(); ++i)
        {
            const Entry& entry = entries[i];
            if (i > 0 && entries[i - 1].name == entry.name)
            {
                continue;
            }
            if (entry.type != nullptr)
            {
                Expected<JsonMutValue, ValueError> value =
                    writeValue(doc, *entry.type, entry.object, options);
                if (!value)
                {
                    return fail(fieldMessage(owner, entry.name, value.error()));
                }
                object.add(entry.name, *value);
                if (!recordVersion(entry.name, entry.type->version))
                {
                    return false;
                }
                continue;
            }
            Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(entry.preserved->value.text);
            if (!parsed)
            {
                return fail(
                    std::format("{}: the preserved data of '{}' is not valid JSON", owner, entry.name));
            }
            object.add(entry.name, doc.copy(parsed->root()));
            if (entry.preserved->version && !recordVersion(entry.name, *entry.preserved->version))
            {
                return false;
            }
        }
        return true;
    }

    bool writeSettings(JsonMutValue object)
    {
        std::vector<Entry> entries;
        entries.reserve(m_options.settings.size() + m_options.preservedSettings.size());
        for (const SceneSettings& settings : m_options.settings)
        {
            ILL_ASSERT(settings.type != nullptr && settings.value != nullptr,
                       "SceneSettings without a type or value");
            entries.push_back(Entry{settings.type->name, settings.type, settings.value, nullptr});
        }
        for (const PreservedValue& preserved : m_options.preservedSettings)
        {
            entries.push_back(Entry{preserved.type, nullptr, nullptr, &preserved});
        }
        return writeEntries(object, entries, "settings");
    }

    bool writeEntity(JsonMutValue object, const Saved& saved, std::vector<Entry>& entries)
    {
        const std::array<char, kGuidTextLength> id = formatGuid(EntityGuid{saved.guid});
        object.add("id", m_doc.makeString(textOf(id)));
        const bool derived = m_world.has<LocalTransform>(saved.entity) && !savedParent(saved.entity).isNull();
        m_result.skippedComponents += collectEntries(m_world, saved.entity, derived, entries);
        if (!writeEntries(object.addObject("components"), entries, std::format("entity {}", textOf(id))))
        {
            return false;
        }
        const PrefabMember* member = m_world.tryGet<PrefabMember>(saved.entity);
        if (m_options.prefabMode == PrefabSaveMode::Expanded && member != nullptr)
        {
            JsonMutValue link = object.addObject("prefabLink");
            link.add("instance", m_doc.makeString(textOf(formatGuid(member->instance))))
                .add("local", m_doc.makeString(textOf(formatGuid(member->local))));
            const PrefabInstance* instance = m_world.tryGet<PrefabInstance>(saved.entity);
            if (instance != nullptr && instance->instance == member->instance)
            {
                link.add("prefab", m_doc.makeString(instance->prefab.path()));
            }
        }
        return true;
    }

    // Fills @p entries with the saved Components of @p entity in @p world (without its Transform if
    // @p skipTransform) and returns how many Components are not saved.
    static u32 collectEntries(const World& world, Entity entity, bool skipTransform,
                              std::vector<Entry>& entries)
    {
        entries.clear();
        u32 skipped = 0;
        const ComponentRegistry& registry = world.registry();
        for (const ComponentId component : world.componentsOf(entity))
        {
            if (component == componentId<EntityGuid>())
            {
                continue; // written as "id"
            }
            if (component == componentId<Transform>() && skipTransform)
            {
                continue; // derived from the LocalTransform (and the parent's Transform)
            }
            if (component == componentId<UnknownComponents>())
            {
                const auto& unknown =
                    *static_cast<const UnknownComponents*>(world.componentData(entity, component));
                for (const PreservedValue& preserved : unknown.components)
                {
                    entries.push_back(Entry{preserved.type, nullptr, nullptr, &preserved});
                }
                continue;
            }
            const TypeReflection* reflection = registry.reflection(component);
            if (reflection == nullptr || reflection->meta.transient)
            {
                ++skipped; // not reflected, or runtime state that is never saved
                continue;
            }
            entries.push_back(
                Entry{reflection->name, reflection, world.componentData(entity, component), nullptr});
        }
        return skipped;
    }

    // Writes the record of the instance whose root is @p root, with the overrides that turn a fresh
    // expansion of its prefab into the saved members.
    bool writeRecord(JsonMutValue object, Entity root)
    {
        const PrefabInstance& instance = m_world.get<PrefabInstance>(root);
        const std::string_view path = instance.prefab.path();
        const std::array<char, kGuidTextLength> id = formatGuid(instance.instance);
        object.add("prefab", m_doc.makeString(path)).add("id", m_doc.makeString(textOf(id)));
        if (!m_options.prefabs)
        {
            return fail(std::format(
                "the prefab instance {} cannot be saved without SceneSaveOptions::prefabs", textOf(id)));
        }
        const BaseExpansion& expansion = baseExpansion(path);
        if (!expansion.error.empty())
        {
            return fail(std::format("the prefab '{}' of instance {} cannot be loaded: {}", path, textOf(id),
                                    expansion.error));
        }
        const World& base = *expansion.world;
        const std::map<u64, Entity>& baseLocals = expansion.locals;
        // Ids of the base as this instance's: every id derives from the instance and the local id.
        std::unordered_map<Entity, u64> baseIds;
        baseIds.reserve(baseLocals.size());
        for (const auto& [local, entity] : baseLocals)
        {
            baseIds.emplace(entity, derivePrefabGuid(instance.instance, EntityGuid{local}).value);
        }
        if (m_members.empty())
        {
            // The saved members of every instance by local id, found once per save.
            for (const Saved& saved : m_order)
            {
                const PrefabMember* member = m_world.tryGet<PrefabMember>(saved.entity);
                if (member != nullptr && m_instances.contains(member->instance.value))
                {
                    m_members[member->instance.value].emplace(member->local.value, saved.entity);
                }
            }
        }
        const std::unordered_map<u64, Entity>& savedLocals = m_members[instance.instance.value];

        // Both sides' Components as JSON, read back to compare.
        JsonMutDocument sides;
        JsonMutValue sidesRoot = sides.makeObject();
        sides.setRoot(sidesRoot);
        const SceneGuidWriter baseResolver(baseIds);
        std::vector<Entry> entries;
        for (const auto& [local, baseEntity] : baseLocals)
        {
            const std::array<char, kGuidTextLength> key = formatGuid(EntityGuid{local});
            JsonMutValue pair = sidesRoot.addObject(textOf(key));
            (void)collectEntries(base, baseEntity, base.has<LocalTransform>(baseEntity), entries);
            if (!writeEntries(sides, baseResolver, pair.addObject("base"), entries, textOf(key)))
            {
                return false;
            }
            const auto found = savedLocals.find(local);
            if (found != savedLocals.end())
            {
                (void)collectEntries(m_world, found->second, m_world.has<LocalTransform>(found->second),
                                     entries);
                if (!writeEntries(sides, m_resolver, pair.addObject("saved"), entries, textOf(key)))
                {
                    return false;
                }
            }
        }
        std::string text;
        if (auto written = sides.write(text, JsonWriteOptions{.pretty = false}); !written)
        {
            return fail(std::format("instance {}: {}", textOf(id), written.error().message));
        }
        const Expected<JsonDocument, JsonError> compared = JsonDocument::parse(text);
        if (!compared)
        {
            return fail(std::format("instance {}: {}", textOf(id), compared.error().message));
        }

        JsonMutValue overrides = m_doc.makeObject();
        bool any = false;
        for (const JsonMember pair : compared->root().members())
        {
            const JsonValue saved = pair.value.find("saved");
            if (saved.isAbsent())
            {
                overrides.add(pair.key, m_doc.makeNull()); // removed from the instance
                any = true;
                continue;
            }
            const JsonValue baseComponents = pair.value.find("base");
            JsonMutValue patch = m_doc.makeObject();
            bool changed = false;
            for (const JsonMember component : baseComponents.members())
            {
                const JsonValue value = saved.find(component.key);
                if (value.isAbsent())
                {
                    patch.add(component.key, m_doc.makeNull());
                    changed = true;
                }
                else if (std::optional<JsonMutValue> diff = jsonPatch(m_doc, value, component.value))
                {
                    patch.add(component.key, *diff);
                    (void)recordOverrideVersion(component.key);
                    changed = true;
                }
            }
            for (const JsonMember component : saved.members())
            {
                if (baseComponents.find(component.key).isAbsent())
                {
                    patch.add(component.key, m_doc.copy(component.value));
                    (void)recordOverrideVersion(component.key);
                    changed = true;
                }
            }
            if (!m_result.error.empty())
            {
                return false;
            }
            if (changed)
            {
                overrides.add(pair.key, patch);
                any = true;
            }
        }
        if (any)
        {
            object.add("overrides", overrides);
        }
        return true;
    }

    // A prefab expanded once per save, to compare its instances with.
    struct BaseExpansion
    {
        std::unique_ptr<World> world;
        std::map<u64, Entity> locals; // by id in the prefab file
        std::string error;            // why it could not be expanded
    };

    const BaseExpansion& baseExpansion(std::string_view path)
    {
        const auto found = m_bases.find(path);
        if (found != m_bases.end())
        {
            return found->second;
        }
        BaseExpansion expansion;
        expansion.world = std::make_unique<World>();
        SceneLoadOptions options;
        options.prefabs = m_options.prefabs;
        const EntityGuid instance{1};
        const SceneLoadResult loaded =
            loadScene(*expansion.world, detail::instanceRecordScene(path, instance), options);
        if (!loaded.ok())
        {
            expansion.error = loaded.error;
        }
        else if (loaded.entities.empty() || expansion.world->has<UnresolvedPrefab>(loaded.entities.front()))
        {
            expansion.error = loaded.warnings.empty() ? std::string("it is empty") : loaded.warnings.back();
        }
        else
        {
            for (const Entity entity : loaded.entities)
            {
                expansion.locals.emplace(expansion.world->get<PrefabMember>(entity).local.value, entity);
            }
        }
        return m_bases.emplace(std::string(path), std::move(expansion)).first->second;
    }

    // Records the version of a registered Component type written in overrides.
    bool recordOverrideVersion(std::string_view name)
    {
        const ComponentId id = m_world.registry().findByStableName(name);
        return id == kInvalidComponentId || recordVersion(name, m_world.registry().reflection(id)->version);
    }

    // Writes the record kept by an UnresolvedPrefab again, with the entity's id.
    bool writeUnresolved(JsonMutValue object, const Saved& saved)
    {
        const UnresolvedPrefab& unresolved = m_world.get<UnresolvedPrefab>(saved.entity);
        const std::array<char, kGuidTextLength> id = formatGuid(EntityGuid{saved.guid});
        object.add("prefab", m_doc.makeString(unresolved.prefab)).add("id", m_doc.makeString(textOf(id)));
        if (unresolved.overrides.empty())
        {
            return true;
        }
        const Expected<JsonDocument, JsonError> overrides = JsonDocument::parse(unresolved.overrides);
        if (!overrides || !overrides->root().isObject())
        {
            return fail(std::format("the kept overrides of instance {} are not a JSON object", textOf(id)));
        }
        object.add("overrides", m_doc.copy(overrides->root()));
        return true;
    }

    const World& m_world;
    const SceneSaveOptions& m_options;
    SceneSaveResult& m_result;
    std::vector<Saved> m_order;
    std::unordered_set<u64> m_instances;  // ids of the instances written as records
    std::unordered_set<Entity> m_orphans; // members their prefab no longer has, written as plain entities
    std::map<std::string, BaseExpansion, std::less<>> m_bases; // by prefab path
    // The saved members of each instance (by instance id), by local id.
    std::unordered_map<u64, std::unordered_map<u64, Entity>> m_members;
    std::unordered_map<Entity, u64> m_ids;
    SceneGuidWriter m_resolver;
    std::map<std::string_view, u32> m_typeVersions; // sorted: the "types" table is written in name order
    JsonMutDocument m_doc;
};

// An EntityGuid that a load assigns, checked against the world's ids once every entity exists.
struct IdClaim
{
    usize index;     // The entity (or record) of the scene file that assigns it.
    EntityGuid file; // Its id in the scene file.
    bool instance;   // True if an entity of the record's instance gets it.
    u32 holders = 0; // Entities of the world with this id (while checking).
};

using IdClaims = std::unordered_map<u64, IdClaim>;

// Where the reader of a prefab file (expanding one instance record) puts its entities.
struct PrefabScope
{
    EntityGuid instance;                   // The instance's id (PrefabMember::instance).
    u32 depth = 0;                         // Nesting depth of this prefab (1 for an instance in the scene).
    const std::vector<std::string>* stack; // Paths of the prefabs being expanded, outermost first.
    IdClaims* claims;                      // Ids assigned by the whole load.
    usize index = 0;                       // The scene file's record that this expansion belongs to.
    bool checkExisting = false;            // Whether ids are checked against the world's.
    const PrefabScope* outer = nullptr;    // The scope of the prefab that contains this instance's record.
    EntityGuid record;                     // The record's id in that prefab (with `outer`).
};

// The EntityGuid of the entity with id @p local in the prefab of @p scope: derived from the outermost
// instance and the entity's id in the outermost prefab (ADR 0046).
EntityGuid scopeGuid(const PrefabScope& scope, EntityGuid local) noexcept
{
    return scope.outer == nullptr ? derivePrefabGuid(scope.instance, local)
                                  : scopeGuid(*scope.outer, derivePrefabGuid(scope.record, local));
}

class SceneReader
{
public:
    SceneReader(World& world, const SceneLoadOptions& options, SceneLoadResult& result,
                const PrefabScope* scope = nullptr)
        : m_world(world)
        , m_options(options)
        , m_result(result)
        , m_guids(options.guids != nullptr ? *options.guids : GuidGenerator::shared())
        , m_scope(scope)
    {
    }

    // Creates every entity first, then fills in components, so entity fields can refer forward. On
    // failure, sets the error; the caller destroys the created entities.
    bool read(JsonValue root)
    {
        if (!root.isObject())
        {
            return fail("the scene is not a JSON object");
        }
        const std::optional<std::string_view> format = root.find("format").asString();
        if (format != kFormatName && format != kPrefabFormatName)
        {
            return fail("not an illusion scene (missing \"format\": \"illusion.scene\")");
        }
        const std::optional<u64> version = root.find("version").asU64();
        if (version != 1u && version != kSceneFormatVersion)
        {
            return fail(
                std::format("unsupported scene version (this build reads 1 and {})", kSceneFormatVersion));
        }
        m_result.version = static_cast<u32>(*version);
        const JsonValue entities = root.find("entities");
        if (!entities.isArray())
        {
            return fail("\"entities\" must be an array");
        }
        if (!readTypes(root.find("types")))
        {
            return false;
        }
        m_result.entities.reserve(entities.size());
        if (m_result.version == 1)
        {
            return readVersion1(entities);
        }
        return createEntities(entities) && readComponents() && applyRecords() && checkClaims() &&
               readSettings(root.find("settings"));
    }

    // Reads a prefab file for an instance (a reader made with a PrefabScope). On failure, sets the error;
    // the caller destroys the created entities.
    bool readPrefab(JsonValue root)
    {
        if (!root.isObject() || root.find("format").asString() != kPrefabFormatName)
        {
            return fail("not an illusion prefab (missing \"format\": \"illusion.prefab\")");
        }
        if (root.find("version").asU64() != kSceneFormatVersion)
        {
            return fail(std::format("unsupported prefab version (this build reads {})", kSceneFormatVersion));
        }
        m_result.version = kSceneFormatVersion;
        const JsonValue entities = root.find("entities");
        if (!entities.isArray())
        {
            return fail("\"entities\" must be an array");
        }
        if (!root.find("settings").isAbsent() && root.find("settings").size() > 0)
        {
            warnOnce("the prefab's settings are ignored");
        }
        if (!readTypes(root.find("types")) || !createEntities(entities) || !readComponents() ||
            !applyRecords())
        {
            return false;
        }
        std::unordered_set<Entity> members(m_result.entities.begin(), m_result.entities.end());
        usize roots = 0;
        for (const Entity entity : m_result.entities)
        {
            if (m_world.has<UnresolvedPrefab>(entity))
            {
                continue; // a nested instance kept unresolved, whose parent is in its overrides
            }
            const Hierarchy* hierarchy = m_world.tryGet<Hierarchy>(entity);
            if (hierarchy == nullptr || !members.contains(hierarchy->parent))
            {
                m_root = entity;
                ++roots;
            }
        }
        if (roots != 1)
        {
            return fail(std::format("a prefab must have exactly one root entity, not {}", roots));
        }
        return true;
    }

    // The root of a prefab read by readPrefab().
    [[nodiscard]] Entity root() const noexcept { return m_root; }

    // Asset references read so far (merged into the reader of the enclosing file).
    [[nodiscard]] std::vector<AssetId>& assets() noexcept { return m_assets; }

    // True if an entity id was already used, which no fallback can fix.
    [[nodiscard]] bool collided() const noexcept { return m_collided; }

    // True if a record could not be expanded, so references to its entities read as null.
    [[nodiscard]] bool lenient() const noexcept { return m_lenient; }

    // Copies the settings that were read into their targets; call after read() succeeded.
    void commitSettings() const
    {
        for (const PendingSettings& pending : m_pending)
        {
            pending.target->type->ops->assign(pending.target->value, pending.value->data());
        }
    }

    // Fills SceneLoadResult::missingAssets from the asset references read; call after read() succeeded.
    void findMissingAssets()
    {
        std::sort(m_assets.begin(), m_assets.end());
        m_assets.erase(std::unique(m_assets.begin(), m_assets.end()), m_assets.end());
        const AssetPathTable& paths = AssetPathTable::instance();
        for (const AssetId id : m_assets)
        {
            const std::string_view path = paths.path(id);
            const std::optional<AssetPathParts> parts = splitAssetPath(path);
            if (!parts)
            {
                continue;
            }
            const bool exists = isOnBuiltinMount(parts->file)
                                    ? parts->fragment == AssetFragment::None && isBuiltinAsset(parts->file)
                                    : !m_options.assetExists || m_options.assetExists(parts->file);
            if (!exists)
            {
                m_result.missingAssets.emplace_back(path);
            }
        }
        std::sort(m_result.missingAssets.begin(), m_result.missingAssets.end());
    }

private:
    struct PendingSettings
    {
        const SceneSettingsTarget* target;
        std::unique_ptr<ScratchObject> value;
    };

    bool fail(std::string message)
    {
        m_result.error = std::move(message);
        return false;
    }

    void warnOnce(std::string message)
    {
        if (m_warned.insert(message).second)
        {
            m_result.warnings.push_back(std::move(message));
        }
    }

    bool readTypes(JsonValue types)
    {
        if (types.isAbsent())
        {
            return true;
        }
        if (!types.isObject())
        {
            return fail("\"types\" must be an object");
        }
        for (const JsonMember member : types.members())
        {
            const std::optional<u64> version = member.value.asU64();
            if (!version || *version > std::numeric_limits<u32>::max())
            {
                return fail(std::format("version of type '{}' must be an unsigned integer", member.key));
            }
            m_typeVersions.emplace(member.key, static_cast<u32>(*version));
            const ComponentId id = m_world.registry().findByStableName(member.key);
            if (id == kInvalidComponentId)
            {
                continue; // settings, or reported once per component below
            }
            const TypeReflection* reflection = m_world.registry().reflection(id);
            if (*version != reflection->version)
            {
                warnOnce(std::format("'{}' was saved with version {}, loading as version {}",
                                     reflection->name, *version, reflection->version));
            }
        }
        return true;
    }

    std::optional<u32> savedVersion(std::string_view name) const
    {
        const auto it = m_typeVersions.find(name);
        return it != m_typeVersions.end() ? std::optional<u32>(it->second) : std::nullopt;
    }

    bool readVersion1(JsonValue entities)
    {
        for (const JsonValue entity : entities.elements())
        {
            if (!entity.isObject())
            {
                return fail(std::format("entity {} is not an object", m_result.entities.size()));
            }
            const Entity created = m_world.create();
            m_result.entities.push_back(created);
            m_world.emplace<EntityGuid>(created, m_guids.next());
        }
        // The resolver views the entity list, which no longer grows.
        const SceneIndexReader resolver(m_result.entities);
        usize index = 0;
        for (const JsonValue entity : entities.elements())
        {
            if (!readEntity(m_result.entities[index], index, entity, resolver))
            {
                return false;
            }
            ++index;
        }
        return true;
    }

    // The EntityGuid of the entity with id @p id in this file.
    EntityGuid mapId(EntityGuid id)
    {
        if (m_scope != nullptr)
        {
            return scopeGuid(*m_scope, id);
        }
        return m_options.ids == IdPolicy::Keep ? id : m_guids.next();
    }

    bool createEntities(JsonValue entities)
    {
        m_fileEntities.reserve(entities.size());
        m_result.fileIds.reserve(entities.size());
        usize index = 0;
        for (const JsonValue entity : entities.elements())
        {
            if (!entity.isObject())
            {
                return fail(std::format("entity {} is not an object", index));
            }
            if (!entity.find("prefab").isAbsent())
            {
                if (!createInstance(index++, entity))
                {
                    return false;
                }
                continue;
            }
            for (const JsonMember member : entity.members())
            {
                if (member.key != "id" && member.key != "components" && member.key != "prefabLink")
                {
                    warnOnce(std::format("unknown entity member '{}' skipped", member.key));
                }
            }
            const std::optional<EntityGuid> guid = readId(index, entity);
            if (!guid)
            {
                return false;
            }
            const JsonValue components = entity.find("components");
            if (!components.isAbsent() && !components.isObject())
            {
                return fail(std::format("entity {}: \"components\" must be an object", index));
            }
            const EntityGuid assigned = mapId(*guid);
            if (!claim(index, *guid, assigned))
            {
                return false;
            }
            const Entity created = m_world.create();
            m_result.entities.push_back(created);
            m_result.fileIds.push_back(*guid);
            m_fileEntities.emplace(guid->value, created);
            m_world.emplace<EntityGuid>(created, assigned);
            m_plain.push_back(PlainEntity{created, index, components});
            if (!readLink(index, created, entity.find("prefabLink")))
            {
                return false;
            }
            ++index;
        }
        return true;
    }

    // The "id" of entity @p index, which no other entity or record of the file has.
    std::optional<EntityGuid> readId(usize index, JsonValue entity)
    {
        const std::optional<std::string_view> text = entity.find("id").asString();
        const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
        if (!guid)
        {
            (void)fail(std::format(
                "entity {}: \"id\" must be a string of 16 hexadecimal digits, not all zero", index));
            return std::nullopt;
        }
        if (m_fileEntities.contains(guid->value) || !m_recordIds.insert(guid->value).second)
        {
            (void)fail(std::format("entity {}: id {} appears twice", index, toString(*guid)));
            return std::nullopt;
        }
        return guid;
    }

    // Reserves @p assigned for the entity @p index; checkClaims() checks it against the world's ids.
    bool claim(usize index, EntityGuid fileId, EntityGuid assigned)
    {
        const bool check = m_scope != nullptr ? m_scope->checkExisting : m_options.ids == IdPolicy::Keep;
        if (!check)
        {
            return true;
        }
        IdClaims& claims = m_scope != nullptr ? *m_scope->claims : m_claims;
        const IdClaim claim{m_scope != nullptr ? m_scope->index : index, fileId, m_scope != nullptr};
        if (!claims.emplace(assigned.value, claim).second)
        {
            m_collided = true;
            return fail(collisionMessage(claim, assigned));
        }
        return true;
    }

    static std::string collisionMessage(const IdClaim& claim, EntityGuid assigned)
    {
        return claim.instance ? std::format("entity {}: id {} of the instance already exists in the world",
                                            claim.index, toString(assigned))
                              : std::format("entity {}: id {} already exists in the world", claim.index,
                                            toString(claim.file));
    }

    // Fails if an entity that was in the world before this load has an id the load assigned: one pass over
    // the world's ids, so loading next to a large World stays cheap.
    bool checkClaims()
    {
        if (m_claims.empty())
        {
            return true;
        }
        ComponentMask all;
        all.set(componentId<EntityGuid>());
        m_world.forEachMatching(all, {}, [this](const RawChunk& chunk) {
            const auto* guids = static_cast<const EntityGuid*>(chunk.column(componentId<EntityGuid>()));
            for (u32 i = 0; i < chunk.size(); ++i)
            {
                const auto it = m_claims.find(guids[i].value);
                if (it != m_claims.end())
                {
                    ++it->second.holders;
                }
            }
        });
        const IdClaims::value_type* first = nullptr;
        for (const IdClaims::value_type& entry : m_claims)
        {
            if (entry.second.holders > 1 && (first == nullptr || entry.second.index < first->second.index))
            {
                first = &entry;
            }
        }
        return first == nullptr || fail(collisionMessage(first->second, EntityGuid{first->first}));
    }

    // Restores the PrefabMember (and PrefabInstance) saved with PrefabSaveMode::Expanded. Links only apply
    // to entities that keep their ids; copies with new ids are plain entities.
    bool readLink(usize index, Entity entity, JsonValue link)
    {
        if (link.isAbsent() || m_scope != nullptr || m_options.ids != IdPolicy::Keep)
        {
            return true;
        }
        const std::optional<std::string_view> instance = link.find("instance").asString();
        const std::optional<std::string_view> local = link.find("local").asString();
        const std::optional<EntityGuid> instanceId = instance ? parseGuid(*instance) : std::nullopt;
        const std::optional<EntityGuid> localId = local ? parseGuid(*local) : std::nullopt;
        const JsonValue prefab = link.find("prefab");
        if (!link.isObject() || !instanceId || !localId || (!prefab.isAbsent() && !prefab.isString()))
        {
            return fail(std::format("entity {}: \"prefabLink\" must be an object with the ids \"instance\" "
                                    "and \"local\" (and, on a root, the \"prefab\" path)",
                                    index));
        }
        m_world.emplace<PrefabMember>(entity, PrefabMember{*instanceId, *localId});
        if (prefab.isString())
        {
            m_world.emplace<PrefabInstance>(entity,
                                            PrefabInstance{AssetRef(*prefab.asString()), *instanceId});
        }
        return true;
    }

    // Expands the instance record @p record (entity @p index of the file), or keeps it unresolved.
    bool createInstance(usize index, JsonValue record)
    {
        for (const JsonMember member : record.members())
        {
            if (member.key != "prefab" && member.key != "id" && member.key != "overrides")
            {
                warnOnce(std::format("unknown instance member '{}' skipped", member.key));
            }
        }
        const std::optional<std::string_view> path = record.find("prefab").asString();
        const std::optional<AssetPathParts> parts = path ? splitAssetPath(*path) : std::nullopt;
        if (!parts || parts->fragment != AssetFragment::None)
        {
            return fail(std::format("entity {}: \"prefab\" must be the path of a prefab asset", index));
        }
        const std::optional<EntityGuid> id = readId(index, record);
        if (!id)
        {
            return false;
        }
        const JsonValue overrides = record.find("overrides");
        if (!overrides.isAbsent() && !overrides.isObject())
        {
            return fail(std::format("entity {}: \"overrides\" must be an object", index));
        }
        m_assets.push_back(AssetPathTable::instance().intern(*path));
        const EntityGuid instance = mapId(*id);
        std::string why;
        if (expand(index, *path, *id, instance, overrides, why))
        {
            return true;
        }
        if (!m_result.error.empty())
        {
            return false;
        }
        return keepUnresolved(index, *path, *id, instance, overrides, why);
    }

    // Creates the entities of the prefab at @p path for the instance @p instance. Returns false with the
    // error set if the load must fail, or with @p why set if the record should be kept unresolved.
    bool expand(usize index, std::string_view path, EntityGuid id, EntityGuid instance, JsonValue overrides,
                std::string& why)
    {
        const u32 depth = m_scope != nullptr ? m_scope->depth + 1 : 1;
        if (depth > kMaxPrefabDepth)
        {
            why = std::format("prefabs nest deeper than {}", kMaxPrefabDepth);
            return false;
        }
        std::vector<std::string> stack;
        if (m_scope != nullptr)
        {
            stack = *m_scope->stack;
        }
        if (std::find(stack.begin(), stack.end(), path) != stack.end())
        {
            why = "the prefab contains itself";
            return false;
        }
        stack.emplace_back(path);
        if (!m_options.prefabs)
        {
            why = "prefab files cannot be read here (no SceneLoadOptions::prefabs)";
            return false;
        }
        const std::optional<std::string> text = m_options.prefabs(path);
        if (!text)
        {
            why = "the file cannot be read";
            return false;
        }
        JsonReadOptions readOptions;
        readOptions.maxBytes = std::numeric_limits<usize>::max();
        readOptions.allowDuplicateKeys = true;
        const Expected<JsonDocument, JsonError> doc = JsonDocument::parse(*text, readOptions);
        if (!doc)
        {
            why = std::format("invalid JSON at line {}, column {}: {}", doc.error().line, doc.error().column,
                              doc.error().message);
            return false;
        }
        const bool checkExisting =
            m_scope != nullptr ? m_scope->checkExisting : m_options.ids == IdPolicy::Keep;
        const PrefabScope scope{instance,
                                depth,
                                &stack,
                                m_scope != nullptr ? m_scope->claims : &m_claims,
                                m_scope != nullptr ? m_scope->index : index,
                                checkExisting,
                                m_scope,
                                id};
        SceneLoadResult loaded;
        SceneReader reader(m_world, m_options, loaded, &scope);
        if (!reader.readPrefab(doc->root()))
        {
            for (const Entity entity : loaded.entities)
            {
                m_world.destroy(entity);
            }
            if (reader.collided())
            {
                m_collided = true;
                return fail(std::format("entity {}: {}", index, loaded.error));
            }
            why = std::move(loaded.error);
            return false;
        }
        for (std::string& warning : loaded.warnings)
        {
            warnOnce(std::format("prefab '{}': {}", path, warning));
        }
        m_assets.insert(m_assets.end(), reader.assets().begin(), reader.assets().end());
        m_result.clearedReferences += loaded.clearedReferences;
        m_lenient = m_lenient || reader.lenient();
        PendingRecord pending{overrides, instance, std::string(path), {}};
        pending.locals.reserve(loaded.entities.size());
        for (usize i = 0; i < loaded.entities.size(); ++i)
        {
            const Entity entity = loaded.entities[i];
            const EntityGuid local = loaded.fileIds[i];
            const EntityGuid key = derivePrefabGuid(id, local);
            // Push first: a failed load destroys every entity in the result.
            m_result.entities.push_back(entity);
            m_result.fileIds.push_back(key);
            if (!m_fileEntities.emplace(key.value, entity).second || m_recordIds.contains(key.value))
            {
                return fail(
                    std::format("entity {}: an entity of the instance has the id {} of another entity "
                                "in the file",
                                index, toString(key)));
            }
            m_world.set(entity, PrefabMember{instance, local});
            pending.locals.emplace(local.value, entity);
        }
        m_world.set(reader.root(), PrefabInstance{AssetRef(path), instance});
        m_records.push_back(std::move(pending));
        return true;
    }

    // Keeps the record of an instance whose prefab could not be loaded as one UnresolvedPrefab entity.
    bool keepUnresolved(usize index, std::string_view path, EntityGuid id, EntityGuid instance,
                        JsonValue overrides, std::string_view why)
    {
        if (!claim(index, id, instance))
        {
            return false;
        }
        UnresolvedPrefab unresolved{std::string(path), {}};
        if (!overrides.isAbsent())
        {
            if (auto written = overrides.write(unresolved.overrides, JsonWriteOptions{.pretty = false});
                !written)
            {
                return fail(std::format("entity {}: the overrides could not be kept: {}", index,
                                        written.error().message));
            }
        }
        const Entity created = m_world.create();
        m_result.entities.push_back(created);
        m_result.fileIds.push_back(id);
        m_world.emplace<EntityGuid>(created, instance);
        m_world.emplace<UnresolvedPrefab>(created, std::move(unresolved));
        m_lenient = true;
        warnOnce(
            std::format("prefab '{}' of instance {} could not be loaded ({}); the instance is kept unchanged",
                        path, toString(id), why));
        return true;
    }

    bool readComponents()
    {
        const SceneGuidReader resolver(m_fileEntities, m_lenient);
        for (const PlainEntity& plain : m_plain)
        {
            if (!plain.components.isAbsent() &&
                !readEntity(plain.entity, plain.index, plain.components, resolver))
            {
                return false;
            }
        }
        warnCleared(resolver);
        return true;
    }

    void warnCleared(const SceneGuidReader& resolver)
    {
        m_result.clearedReferences += static_cast<u32>(resolver.cleared());
        if (resolver.cleared() > 0)
        {
            warnOnce("references to entities of unresolved prefab instances were cleared");
        }
    }

    // Applies the overrides of the expanded records, then removes the entities they remove.
    bool applyRecords()
    {
        const SceneGuidReader resolver(m_fileEntities, m_lenient);
        std::vector<Entity> removed;
        for (const PendingRecord& record : m_records)
        {
            if (record.overrides.isAbsent())
            {
                continue;
            }
            const std::string owner =
                std::format("instance {} of '{}'", toString(record.instance), record.path);
            for (const JsonMember member : record.overrides.members())
            {
                const std::optional<EntityGuid> local = parseGuid(member.key);
                if (!local)
                {
                    return fail(std::format("{}: override key '{}' is not an entity id", owner, member.key));
                }
                const auto it = record.locals.find(local->value);
                if (it == record.locals.end())
                {
                    warnOnce(
                        std::format("{}: the overrides of entity {} match no entity of the prefab and are "
                                    "dropped",
                                    owner, member.key));
                    continue;
                }
                if (member.value.isNull())
                {
                    removed.push_back(it->second);
                    continue;
                }
                if (!member.value.isObject())
                {
                    return fail(std::format("{}: the overrides of entity {} must be an object or null", owner,
                                            member.key));
                }
                if (!applyOverrides(it->second, std::format("{}, entity {}", owner, member.key), member.value,
                                    resolver))
                {
                    return false;
                }
            }
        }
        warnCleared(resolver);
        for (const Entity entity : removed)
        {
            const auto position = std::find(m_result.entities.begin(), m_result.entities.end(), entity);
            const auto offset = position - m_result.entities.begin();
            m_fileEntities.erase(m_result.fileIds[static_cast<usize>(offset)].value);
            m_result.entities.erase(position);
            m_result.fileIds.erase(m_result.fileIds.begin() + offset);
            m_world.destroy(entity);
        }
        return true;
    }

    // Applies the Component overrides @p object to @p entity: null removes a Component, an object merges
    // into it field by field (into its default value if the entity does not have it).
    bool applyOverrides(Entity entity, const std::string& owner, JsonValue object,
                        const EntityResolver& resolver)
    {
        for (const JsonMember member : object.members())
        {
            const ComponentId id = m_world.registry().findByStableName(member.key);
            if (id == kInvalidComponentId)
            {
                warnOnce(std::format("override of unknown component type '{}' dropped", member.key));
                continue;
            }
            const TypeReflection& reflection = *m_world.registry().reflection(id);
            if (id == componentId<EntityGuid>() || reflection.meta.transient)
            {
                return fail(std::format("{}: '{}' cannot be overridden", owner, member.key));
            }
            if (member.value.isNull())
            {
                (void)m_world.remove(entity, id);
                continue;
            }
            if (!member.value.isObject())
            {
                return fail(
                    std::format("{}: the override of '{}' must be an object or null", owner, member.key));
            }
            void* data = hasComponent(entity, id) ? m_world.componentData(entity, id)
                                                  : m_world.emplaceDefault(entity, id);
            ValueReadOptions options;
            options.mode = JsonReadMode::Merge; // into the prefab's value
            options.entities = &resolver;
            options.savedVersion = savedVersion(member.key);
            options.atomic = false; // a failed load removes the entity anyway
            options.referencedAssets = &m_assets;
            const ValueReadResult read = readValue(reflection, data, member.value, options);
            if (!read.ok())
            {
                return fail(fieldMessage(owner, reflection.name, read.errors.front()));
            }
            for (const std::string& key : read.unknownKeys)
            {
                warnOnce(std::format("unknown field '{}' skipped", qualifiedName(reflection.name, key)));
            }
        }
        return true;
    }

    bool readEntity(Entity entity, usize index, JsonValue object, const EntityResolver& resolver)
    {
        UnknownComponents unknown;
        for (const JsonMember member : object.members())
        {
            const ComponentId id = m_world.registry().findByStableName(member.key);
            if (id == kInvalidComponentId)
            {
                if (!preserve(unknown.components, member, std::format("entity {}", index)))
                {
                    return false;
                }
                warnOnce(std::format("unknown component type '{}' preserved", member.key));
                continue;
            }
            if (id == componentId<EntityGuid>())
            {
                return fail(
                    std::format("entity {}: '{}' must be written as the entity's \"id\"", index, member.key));
            }
            const TypeReflection& reflection = *m_world.registry().reflection(id);
            if (reflection.meta.transient)
            {
                warnOnce(std::format("runtime component type '{}' skipped", member.key));
                continue;
            }
            if (!member.value.isObject())
            {
                return fail(std::format("entity {}: component '{}' must be an object", index, member.key));
            }
            if (hasComponent(entity, id))
            {
                return fail(std::format("entity {}: component '{}' appears twice", index, member.key));
            }
            void* data = m_world.emplaceDefault(entity, id);
            ValueReadOptions options;
            options.mode = JsonReadMode::Merge; // into the new default value
            options.entities = &resolver;
            options.savedVersion = savedVersion(member.key);
            options.atomic = false; // a failed load removes the entity anyway
            options.referencedAssets = &m_assets;
            const ValueReadResult read = readValue(reflection, data, member.value, options);
            if (!read.ok())
            {
                return fail(
                    fieldMessage(std::format("entity {}", index), reflection.name, read.errors.front()));
            }
            for (const std::string& key : read.unknownKeys)
            {
                warnOnce(std::format("unknown field '{}' skipped", qualifiedName(reflection.name, key)));
            }
        }
        if (!unknown.components.empty())
        {
            m_world.emplace<UnknownComponents>(entity, std::move(unknown));
        }
        return true;
    }

    // Keeps @p member as preserved JSON in @p values.
    bool preserve(std::vector<PreservedValue>& values, const JsonMember& member, std::string_view owner)
    {
        for (const PreservedValue& value : values)
        {
            if (value.type == member.key)
            {
                return fail(std::format("{}: '{}' appears twice", owner, member.key));
            }
        }
        PreservedValue value;
        value.type = std::string(member.key);
        value.version = savedVersion(member.key);
        if (auto written = member.value.write(value.value.text, JsonWriteOptions{.pretty = false}); !written)
        {
            return fail(
                std::format("{}: '{}' could not be kept: {}", owner, member.key, written.error().message));
        }
        values.push_back(std::move(value));
        return true;
    }

    bool readSettings(JsonValue settings)
    {
        if (settings.isAbsent())
        {
            return true;
        }
        if (!settings.isObject())
        {
            return fail("\"settings\" must be an object");
        }
        const StructRegistry& structs = m_world.registry().structs();
        const SceneGuidReader resolver(m_fileEntities);
        std::unordered_set<std::string_view> seen;
        for (const JsonMember member : settings.members())
        {
            if (!seen.insert(member.key).second)
            {
                return fail(std::format("settings: '{}' appears twice", member.key));
            }
            const StructReflection* type = structs.reflection(structs.findByStableName(member.key));
            const SceneSettingsTarget* target = nullptr;
            for (const SceneSettingsTarget& candidate : m_options.settings)
            {
                ILL_ASSERT(candidate.type != nullptr && candidate.value != nullptr,
                           "SceneSettingsTarget without a type or value");
                if (candidate.type->name == member.key)
                {
                    target = &candidate;
                }
            }
            if (target == nullptr)
            {
                if (type == nullptr)
                {
                    warnOnce(std::format("unknown settings type '{}' preserved", member.key));
                }
                if (!preserve(m_result.preservedSettings, member, "settings"))
                {
                    return false;
                }
                continue;
            }
            const std::optional<u32> version = savedVersion(member.key);
            if (version && *version != target->type->version)
            {
                warnOnce(std::format("'{}' was saved with version {}, loading as version {}", member.key,
                                     *version, target->type->version));
            }
            auto value = std::make_unique<ScratchObject>(*target->type);
            ValueReadOptions options;
            options.entities = &resolver;
            options.savedVersion = version;
            options.atomic = false; // the scratch value is discarded on failure
            options.referencedAssets = &m_assets;
            const ValueReadResult read = readValue(*target->type, value->data(), member.value, options);
            if (!read.ok())
            {
                return fail(fieldMessage("settings", member.key, read.errors.front()));
            }
            for (const std::string& key : read.unknownKeys)
            {
                warnOnce(std::format("unknown field '{}' skipped", qualifiedName(member.key, key)));
            }
            m_pending.push_back(PendingSettings{target, std::move(value)});
        }
        return true;
    }

    bool hasComponent(Entity entity, ComponentId id) const
    {
        for (const ComponentId existing : m_world.componentsOf(entity))
        {
            if (existing == id)
            {
                return true;
            }
        }
        return false;
    }

    // A plain entity of the file and its components, read after every entity exists.
    struct PlainEntity
    {
        Entity entity;
        usize index;
        JsonValue components;
    };

    // An expanded instance record whose overrides are applied after every entity exists.
    struct PendingRecord
    {
        JsonValue overrides;
        EntityGuid instance;
        std::string path;
        std::unordered_map<u64, Entity> locals; // by id in the prefab file
    };

    World& m_world;
    const SceneLoadOptions& m_options;
    SceneLoadResult& m_result;
    GuidGenerator& m_guids;
    const PrefabScope* m_scope;
    std::unordered_map<u64, Entity> m_fileEntities;
    std::unordered_set<u64> m_recordIds; // ids of every entry of the file, to find duplicates
    IdClaims m_claims;                   // ids assigned by this load (top-level reader with IdPolicy::Keep)
    std::vector<PlainEntity> m_plain;
    std::vector<PendingRecord> m_records;
    Entity m_root;
    bool m_collided = false;
    bool m_lenient = false;
    std::unordered_map<std::string_view, u32> m_typeVersions; // keys view the parsed document
    std::vector<PendingSettings> m_pending;
    std::unordered_set<std::string> m_warned;
    std::vector<AssetId> m_assets;
};

} // namespace

SceneSaveResult saveScene(const World& world, const SceneSaveOptions& options)
{
    ILL_PROFILE_SCOPE();
    SceneSaveResult result;
    SceneWriter(world, options, result).write();
    return result;
}

SceneLoadResult loadScene(World& world, std::string_view json, const SceneLoadOptions& options)
{
    ILL_PROFILE_SCOPE();
    SceneLoadResult result;
    JsonReadOptions readOptions;
    readOptions.maxBytes = std::numeric_limits<usize>::max();
    readOptions.allowDuplicateKeys = true; // reported as a component that appears twice
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json, readOptions);
    if (!doc)
    {
        result.error = std::format("invalid JSON at line {}, column {}: {}", doc.error().line,
                                   doc.error().column, doc.error().message);
        return result;
    }
    SceneReader reader(world, options, result);
    if (reader.read(doc->root()))
    {
        reader.commitSettings();
        reader.findMissingAssets();
        return result;
    }
    for (const Entity entity : result.entities)
    {
        world.destroy(entity);
    }
    result.entities.clear();
    result.fileIds.clear();
    result.preservedSettings.clear();
    result.warnings.clear();
    return result;
}

} // namespace illusion

#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/EntityGuid.hpp>
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

// Version 2: Entity fields are ids of entities in the file.
class SceneGuidReader final : public EntityResolver
{
public:
    explicit SceneGuidReader(const std::unordered_map<u64, Entity>& entities) noexcept
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
        const std::optional<std::string_view> text = value.asString();
        const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
        const auto it = guid ? m_entities.find(guid->value) : m_entities.end();
        if (it == m_entities.end())
        {
            return unexpected(std::string("must be null or the id of an entity in this scene"));
        }
        return it->second;
    }

private:
    const std::unordered_map<u64, Entity>& m_entities;
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
        if (!collectIds(entities))
        {
            return false;
        }

        JsonMutValue root = m_doc.makeObject();
        m_doc.setRoot(root);
        root.add("format", m_doc.makeString(kFormatName)).add("version", m_doc.makeU64(kSceneFormatVersion));
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
            if (!writeEntity(array.pushObject(), saved, entries))
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

    // One value to write under a stable name: a reflected object or preserved JSON.
    struct Entry
    {
        std::string_view name;
        const TypeReflection* type = nullptr;
        const void* object = nullptr;
        const PreservedValue* preserved = nullptr;
    };

    bool fail(std::string message)
    {
        m_result.error = std::move(message);
        return false;
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
        return true;
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
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.name != b.name ? a.name < b.name : a.type != nullptr && b.type == nullptr;
        });
        const ValueWriteOptions options{.entities = &m_resolver};
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
                    writeValue(m_doc, *entry.type, entry.object, options);
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
            object.add(entry.name, m_doc.copy(parsed->root()));
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
        entries.clear();
        const ComponentRegistry& registry = m_world.registry();
        for (const ComponentId component : m_world.componentsOf(saved.entity))
        {
            if (component == m_guidId)
            {
                continue; // written as "id"
            }
            if (component == m_unknownId)
            {
                const auto& unknown =
                    *static_cast<const UnknownComponents*>(m_world.componentData(saved.entity, component));
                for (const PreservedValue& preserved : unknown.components)
                {
                    entries.push_back(Entry{preserved.type, nullptr, nullptr, &preserved});
                }
                continue;
            }
            const TypeReflection* reflection = registry.reflection(component);
            if (reflection == nullptr || reflection->meta.transient)
            {
                ++m_result.skippedComponents; // not reflected, or runtime state that is never saved
                continue;
            }
            entries.push_back(
                Entry{reflection->name, reflection, m_world.componentData(saved.entity, component), nullptr});
        }
        return writeEntries(object.addObject("components"), entries, std::format("entity {}", textOf(id)));
    }

    const World& m_world;
    const SceneSaveOptions& m_options;
    SceneSaveResult& m_result;
    const ComponentId m_guidId = componentId<EntityGuid>();
    const ComponentId m_unknownId = componentId<UnknownComponents>();
    std::vector<Saved> m_order;
    std::unordered_map<Entity, u64> m_ids;
    SceneGuidWriter m_resolver;
    std::map<std::string_view, u32> m_typeVersions; // sorted: the "types" table is written in name order
    JsonMutDocument m_doc;
};

class SceneReader
{
public:
    SceneReader(World& world, const SceneLoadOptions& options, SceneLoadResult& result)
        : m_world(world)
        , m_options(options)
        , m_result(result)
        , m_guids(options.guids != nullptr ? *options.guids : GuidGenerator::shared())
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
        if (root.find("format").asString() != kFormatName)
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
        return createEntities(entities) && readComponents(entities) && readSettings(root.find("settings"));
    }

    // Copies the settings that were read into their targets; call after read() succeeded.
    void commitSettings() const
    {
        for (const PendingSettings& pending : m_pending)
        {
            pending.target->type->ops->assign(pending.target->value, pending.value->data());
        }
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
            if (!readEntity(index++, entity, resolver))
            {
                return false;
            }
        }
        return true;
    }

    bool createEntities(JsonValue entities)
    {
        std::unordered_set<u64> existing;
        if (m_options.ids == IdPolicy::Keep)
        {
            ComponentMask all;
            all.set(componentId<EntityGuid>());
            m_world.forEachMatching(all, {}, [&existing](const RawChunk& chunk) {
                const auto* guids = static_cast<const EntityGuid*>(chunk.column(componentId<EntityGuid>()));
                for (u32 i = 0; i < chunk.size(); ++i)
                {
                    existing.insert(guids[i].value);
                }
            });
        }
        m_fileEntities.reserve(entities.size());
        m_result.fileIds.reserve(entities.size());
        usize index = 0;
        for (const JsonValue entity : entities.elements())
        {
            if (!entity.isObject())
            {
                return fail(std::format("entity {} is not an object", index));
            }
            for (const JsonMember member : entity.members())
            {
                if (member.key != "id" && member.key != "components")
                {
                    warnOnce(std::format("unknown entity member '{}' skipped", member.key));
                }
            }
            const std::optional<std::string_view> text = entity.find("id").asString();
            const std::optional<EntityGuid> guid = text ? parseGuid(*text) : std::nullopt;
            if (!guid)
            {
                return fail(std::format(
                    "entity {}: \"id\" must be a string of 16 hexadecimal digits, not all zero", index));
            }
            if (m_fileEntities.contains(guid->value))
            {
                return fail(std::format("entity {}: id {} appears twice", index, toString(*guid)));
            }
            if (existing.contains(guid->value))
            {
                return fail(
                    std::format("entity {}: id {} already exists in the world", index, toString(*guid)));
            }
            const JsonValue components = entity.find("components");
            if (!components.isAbsent() && !components.isObject())
            {
                return fail(std::format("entity {}: \"components\" must be an object", index));
            }
            const Entity created = m_world.create();
            m_result.entities.push_back(created);
            m_result.fileIds.push_back(*guid);
            m_fileEntities.emplace(guid->value, created);
            m_world.emplace<EntityGuid>(created, m_options.ids == IdPolicy::Keep ? *guid : m_guids.next());
            ++index;
        }
        return true;
    }

    bool readComponents(JsonValue entities)
    {
        const SceneGuidReader resolver(m_fileEntities);
        usize index = 0;
        for (const JsonValue entity : entities.elements())
        {
            const JsonValue components = entity.find("components");
            if (!components.isAbsent() && !readEntity(index, components, resolver))
            {
                return false;
            }
            ++index;
        }
        return true;
    }

    bool readEntity(usize index, JsonValue object, const EntityResolver& resolver)
    {
        const Entity entity = m_result.entities[index];
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

    World& m_world;
    const SceneLoadOptions& m_options;
    SceneLoadResult& m_result;
    GuidGenerator& m_guids;
    std::unordered_map<u64, Entity> m_fileEntities;
    std::unordered_map<std::string_view, u32> m_typeVersions; // keys view the parsed document
    std::vector<PendingSettings> m_pending;
    std::unordered_set<std::string> m_warned;
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

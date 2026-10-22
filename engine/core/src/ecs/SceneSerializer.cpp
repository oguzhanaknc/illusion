#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>

#include <format>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.scene";

// Entity fields are indices into the scene's entity array.
class SceneEntityWriter final : public EntityResolver
{
public:
    explicit SceneEntityWriter(std::span<const Entity> entities)
    {
        m_indices.reserve(entities.size());
        for (usize i = 0; i < entities.size(); ++i)
        {
            m_indices.emplace(entities[i], static_cast<u64>(i));
        }
    }

    JsonMutValue writeEntity(JsonMutDocument& doc, Entity entity) const override
    {
        const auto it = m_indices.find(entity);
        return it != m_indices.end() ? doc.makeU64(it->second) : doc.makeNull();
    }

    Expected<Entity, std::string> readEntity(JsonValue) const override
    {
        return unexpected(std::string("cannot be read by a scene writer"));
    }

private:
    std::unordered_map<Entity, u64> m_indices;
};

class SceneEntityReader final : public EntityResolver
{
public:
    explicit SceneEntityReader(std::span<const Entity> entities) noexcept
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

// "entity 0: field 'vec3' of 'test.Everything' is not finite", with the full pointer for nested values.
std::string fieldMessage(usize index, std::string_view type, const ValueError& error)
{
    const std::string_view field = firstToken(error.pointer);
    std::string message = std::format("entity {}: field '{}' of '{}' {}", index, field, type, error.message);
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

SceneSaveResult writeScene(const World& world, std::span<const Entity> entities)
{
    SceneSaveResult result;
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    doc.setRoot(root);
    root.add("format", doc.makeString(kFormatName)).add("version", doc.makeU64(kSceneFormatVersion));
    JsonMutValue types = root.addObject("types");
    JsonMutValue array = root.addArray("entities");

    const SceneEntityWriter resolver(entities);
    const ValueWriteOptions options{.entities = &resolver, .u64AsString = false};
    const ComponentRegistry& registry = world.registry();
    std::unordered_set<ComponentId> usedTypes;
    for (usize index = 0; index < entities.size(); ++index)
    {
        const Entity entity = entities[index];
        ILL_ASSERT(world.isAlive(entity), "saveScene() with a dead entity");
        JsonMutValue object = array.pushObject();
        for (const ComponentId id : world.componentsOf(entity))
        {
            const TypeReflection* reflection = registry.reflection(id);
            if (reflection == nullptr)
            {
                ++result.skippedComponents;
                continue;
            }
            if (reflection->meta.transient)
            {
                ++result.skippedComponents; // runtime state is never saved
                continue;
            }
            Expected<JsonMutValue, ValueError> component =
                writeValue(doc, *reflection, world.componentData(entity, id), options);
            if (!component)
            {
                result.error = fieldMessage(index, reflection->name, component.error());
                return result;
            }
            object.add(reflection->name, *component);
            if (usedTypes.insert(id).second)
            {
                types.add(reflection->name, doc.makeU64(reflection->version));
            }
        }
    }

    if (auto written = doc.write(result.json); !written)
    {
        result.error = std::format("the scene could not be written: {}", written.error().message);
        result.json.clear();
        return result;
    }
    result.entities = static_cast<u32>(entities.size());
    return result;
}

class SceneReader
{
public:
    SceneReader(World& world, SceneLoadResult& result)
        : m_world(world)
        , m_result(result)
        , m_resolver(result.entities)
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
        if (root.find("version").asU64() != kSceneFormatVersion)
        {
            return fail(std::format("unsupported scene version (expected {})", kSceneFormatVersion));
        }
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
        for (const JsonValue entity : entities.elements())
        {
            if (!entity.isObject())
            {
                return fail(std::format("entity {} is not an object", m_result.entities.size()));
            }
            m_result.entities.push_back(m_world.create());
        }
        // The resolver views the entity list, which no longer grows.
        m_resolver = SceneEntityReader(m_result.entities);
        usize index = 0;
        for (const JsonValue entity : entities.elements())
        {
            if (!readEntity(index++, entity))
            {
                return false;
            }
        }
        return true;
    }

private:
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
            const ComponentId id = m_world.registry().findByStableName(member.key);
            if (id == kInvalidComponentId)
            {
                continue; // reported once per entity component below
            }
            const TypeReflection* reflection = m_world.registry().reflection(id);
            m_savedVersions.emplace(id, static_cast<u32>(*version));
            if (*version != reflection->version)
            {
                warnOnce(std::format("'{}' was saved with version {}, loading as version {}",
                                     reflection->name, *version, reflection->version));
            }
        }
        return true;
    }

    bool readEntity(usize index, JsonValue object)
    {
        const Entity entity = m_result.entities[index];
        for (const JsonMember member : object.members())
        {
            const ComponentId id = m_world.registry().findByStableName(member.key);
            if (id == kInvalidComponentId)
            {
                warnOnce(std::format("unknown component type '{}' skipped", member.key));
                continue;
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
            options.entities = &m_resolver;
            options.atomic = false; // a failed load removes the entity anyway
            if (const auto saved = m_savedVersions.find(id); saved != m_savedVersions.end())
            {
                options.savedVersion = saved->second;
            }
            const ValueReadResult read = readValue(reflection, data, member.value, options);
            if (!read.ok())
            {
                return fail(fieldMessage(index, reflection.name, read.errors.front()));
            }
            for (const std::string& unknown : read.unknownKeys)
            {
                warnOnce(std::format("unknown field '{}' skipped", qualifiedName(reflection.name, unknown)));
            }
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
    SceneLoadResult& m_result;
    SceneEntityReader m_resolver;
    std::unordered_map<ComponentId, u32> m_savedVersions;
    std::unordered_set<std::string> m_warned;
};

} // namespace

SceneSaveResult saveScene(const World& world)
{
    std::vector<Entity> entities;
    world.entities(entities);
    return saveScene(world, entities);
}

SceneSaveResult saveScene(const World& world, std::span<const Entity> entities)
{
    ILL_PROFILE_SCOPE();
    return writeScene(world, entities);
}

SceneLoadResult loadScene(World& world, std::string_view json)
{
    ILL_PROFILE_SCOPE();
    SceneLoadResult result;
    JsonReadOptions options;
    options.maxBytes = std::numeric_limits<usize>::max();
    options.allowDuplicateKeys = true; // reported as a component that appears twice
    Expected<JsonDocument, JsonError> doc = JsonDocument::parse(json, options);
    if (!doc)
    {
        result.error = std::format("invalid JSON at line {}, column {}: {}", doc.error().line,
                                   doc.error().column, doc.error().message);
        return result;
    }
    if (!SceneReader(world, result).read(doc->root()))
    {
        for (const Entity entity : result.entities)
        {
            world.destroy(entity);
        }
        result.entities.clear();
        result.warnings.clear();
    }
    return result;
}

} // namespace illusion

#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/World.hpp>

#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <yyjson.h>

namespace illusion
{
namespace
{

constexpr std::string_view kFormatName = "illusion.scene";

struct MutableDocumentDeleter
{
    void operator()(yyjson_mut_doc* doc) const noexcept { yyjson_mut_doc_free(doc); }
};

struct DocumentDeleter
{
    void operator()(yyjson_doc* doc) const noexcept { yyjson_doc_free(doc); }
};

struct FreeDeleter
{
    void operator()(char* text) const noexcept { std::free(text); }
};

yyjson_mut_val* key(yyjson_mut_doc* doc, std::string_view name)
{
    // Registry-owned names outlive the document, so they are not copied.
    return yyjson_mut_strn(doc, name.data(), name.size());
}

bool isFinite(std::span<const f32> values) noexcept
{
    for (const f32 value : values)
    {
        if (!std::isfinite(value))
        {
            return false;
        }
    }
    return true;
}

yyjson_mut_val* floatArray(yyjson_mut_doc* doc, std::span<const f32> values)
{
    yyjson_mut_val* array = yyjson_mut_arr(doc);
    for (const f32 value : values)
    {
        yyjson_mut_arr_add_val(array, yyjson_mut_float(doc, value));
    }
    return array;
}

bool isScalarArray(yyjson_mut_val* value)
{
    if (!yyjson_mut_is_arr(value))
    {
        return false;
    }
    yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(value);
    while (yyjson_mut_val* element = yyjson_mut_arr_iter_next(&iter))
    {
        if (yyjson_mut_is_arr(element) || yyjson_mut_is_obj(element))
        {
            return false;
        }
    }
    return true;
}

// Pretty-prints with two-space indentation, but keeps arrays of scalars (vectors, quaternions) on one
// line so that scene files stay short and diff well. Scalars are formatted by yyjson (shortest
// round-trip numbers, escaped strings) through a reusable pool, so printing does not allocate per value.
class JsonPrinter
{
public:
    explicit JsonPrinter(std::string& out)
        : m_out(out)
    {
        yyjson_alc_pool_init(&m_pool, m_buffer, sizeof(m_buffer));
    }

    void print(yyjson_mut_val* value, usize depth)
    {
        if (yyjson_mut_is_obj(value) && yyjson_mut_obj_size(value) > 0)
        {
            m_out.push_back('{');
            yyjson_mut_obj_iter iter = yyjson_mut_obj_iter_with(value);
            bool first = true;
            while (yyjson_mut_val* name = yyjson_mut_obj_iter_next(&iter))
            {
                m_out.append(first ? "" : ",");
                first = false;
                newline(depth + 1);
                scalar(name);
                m_out.append(": ");
                print(yyjson_mut_obj_iter_get_val(name), depth + 1);
            }
            newline(depth);
            m_out.push_back('}');
        }
        else if (yyjson_mut_is_arr(value) && yyjson_mut_arr_size(value) > 0)
        {
            const bool oneLine = isScalarArray(value);
            m_out.push_back('[');
            yyjson_mut_arr_iter iter = yyjson_mut_arr_iter_with(value);
            bool first = true;
            while (yyjson_mut_val* element = yyjson_mut_arr_iter_next(&iter))
            {
                m_out.append(first ? "" : oneLine ? ", " : ",");
                first = false;
                if (!oneLine)
                {
                    newline(depth + 1);
                }
                print(element, depth + 1);
            }
            if (!oneLine)
            {
                newline(depth);
            }
            m_out.push_back(']');
        }
        else
        {
            scalar(value); // scalars and empty containers
        }
    }

private:
    void newline(usize indent)
    {
        m_out.push_back('\n');
        m_out.append(indent * 2, ' ');
    }

    void scalar(yyjson_mut_val* value)
    {
        usize length = 0;
        if (char* text = yyjson_mut_val_write_opts(value, 0, &m_pool, &length, nullptr))
        {
            m_out.append(text, length);
            m_pool.free(m_pool.ctx, text);
            return;
        }
        // Too large for the pool (a long string): use the heap.
        const std::unique_ptr<char, FreeDeleter> text(yyjson_mut_val_write(value, 0, &length));
        ILL_CHECK(text != nullptr, "out of memory");
        m_out.append(text.get(), length);
    }

    std::string& m_out;
    yyjson_alc m_pool{};
    alignas(16) char m_buffer[4096];
};

class SceneWriter
{
public:
    SceneWriter(const World& world, std::span<const Entity> entities)
        : m_world(world)
        , m_entities(entities)
        , m_doc(yyjson_mut_doc_new(nullptr))
    {
        m_indices.reserve(entities.size());
        for (usize i = 0; i < entities.size(); ++i)
        {
            m_indices.emplace(entities[i], static_cast<u64>(i));
        }
    }

    SceneSaveResult write()
    {
        SceneSaveResult result;
        yyjson_mut_doc* doc = m_doc.get();
        ILL_CHECK(doc != nullptr, "out of memory");
        yyjson_mut_val* root = yyjson_mut_obj(doc);
        yyjson_mut_doc_set_root(doc, root);
        yyjson_mut_obj_add(root, key(doc, "format"), key(doc, kFormatName));
        yyjson_mut_obj_add(root, key(doc, "version"), yyjson_mut_uint(doc, kSceneFormatVersion));
        yyjson_mut_val* types = yyjson_mut_obj(doc);
        yyjson_mut_obj_add(root, key(doc, "types"), types);
        yyjson_mut_val* entities = yyjson_mut_arr(doc);
        yyjson_mut_obj_add(root, key(doc, "entities"), entities);

        const ComponentRegistry& registry = m_world.registry();
        std::unordered_set<ComponentId> usedTypes;
        for (usize index = 0; index < m_entities.size(); ++index)
        {
            const Entity entity = m_entities[index];
            ILL_ASSERT(m_world.isAlive(entity), "saveScene() with a dead entity");
            yyjson_mut_val* object = yyjson_mut_obj(doc);
            yyjson_mut_arr_add_val(entities, object);
            for (const ComponentId id : m_world.componentsOf(entity))
            {
                const ComponentReflection* reflection = registry.reflection(id);
                if (reflection == nullptr)
                {
                    ++result.skippedComponents;
                    continue;
                }
                yyjson_mut_val* component = yyjson_mut_obj(doc);
                yyjson_mut_obj_add(object, key(doc, reflection->name), component);
                const void* data = m_world.componentData(entity, id);
                for (const FieldInfo& field : reflection->fields)
                {
                    yyjson_mut_val* value = writeField(data, field);
                    if (value == nullptr)
                    {
                        result.error = std::format("entity {}: field '{}' of '{}' is not finite", index,
                                                   field.name, reflection->name);
                        return result;
                    }
                    yyjson_mut_obj_add(component, key(doc, field.name), value);
                }
                if (usedTypes.insert(id).second)
                {
                    yyjson_mut_obj_add(types, key(doc, reflection->name),
                                       yyjson_mut_uint(doc, reflection->version));
                }
            }
        }

        JsonPrinter(result.json).print(root, 0);
        result.json.push_back('\n');
        result.entities = static_cast<u32>(m_entities.size());
        return result;
    }

private:
    // Returns nullptr for values that JSON cannot store (non-finite floats).
    yyjson_mut_val* writeField(const void* component, const FieldInfo& field)
    {
        yyjson_mut_doc* doc = m_doc.get();
        const void* data = static_cast<const std::byte*>(component) + field.offset;
        switch (field.type)
        {
        case FieldType::Bool:
            return yyjson_mut_bool(doc, *static_cast<const bool*>(data));
        case FieldType::I32:
            return yyjson_mut_sint(doc, *static_cast<const i32*>(data));
        case FieldType::U32:
            return yyjson_mut_uint(doc, *static_cast<const u32*>(data));
        case FieldType::I64:
            return yyjson_mut_sint(doc, *static_cast<const i64*>(data));
        case FieldType::U64:
            return yyjson_mut_uint(doc, *static_cast<const u64*>(data));
        case FieldType::F32:
        {
            const f32 value = *static_cast<const f32*>(data);
            return std::isfinite(value) ? yyjson_mut_float(doc, value) : nullptr;
        }
        case FieldType::F64:
        {
            const f64 value = *static_cast<const f64*>(data);
            return std::isfinite(value) ? yyjson_mut_double(doc, value) : nullptr;
        }
        case FieldType::Vec2:
        case FieldType::Vec3:
        case FieldType::Vec4:
        {
            const usize count = field.type == FieldType::Vec2 ? 2 : field.type == FieldType::Vec3 ? 3 : 4;
            const std::span<const f32> values(static_cast<const f32*>(data), count);
            return isFinite(values) ? floatArray(doc, values) : nullptr;
        }
        case FieldType::Quat:
        {
            // Written as [x, y, z, w], the order used by glTF.
            const Quat& q = *static_cast<const Quat*>(data);
            const f32 values[4] = {q.x, q.y, q.z, q.w};
            return isFinite(values) ? floatArray(doc, values) : nullptr;
        }
        case FieldType::String:
        {
            const std::string& text = *static_cast<const std::string*>(data);
            return yyjson_mut_strncpy(doc, text.data(), text.size());
        }
        case FieldType::Entity:
        {
            const auto it = m_indices.find(*static_cast<const Entity*>(data));
            return it != m_indices.end() ? yyjson_mut_uint(doc, it->second) : yyjson_mut_null(doc);
        }
        case FieldType::Enum:
        {
            const i64 value = readEnumField(component, field);
            for (const EnumValue& named : field.enumValues)
            {
                if (named.value == value)
                {
                    return key(doc, named.name);
                }
            }
            return yyjson_mut_sint(doc, value); // a value without a name is kept as a number
        }
        }
        return yyjson_mut_null(doc);
    }

    const World& m_world;
    std::span<const Entity> m_entities;
    std::unique_ptr<yyjson_mut_doc, MutableDocumentDeleter> m_doc;
    std::unordered_map<Entity, u64> m_indices;
};

// 1-based line and column of byte @p position, for error messages.
std::string describePosition(std::string_view text, usize position)
{
    usize line = 1;
    usize column = 1;
    for (usize i = 0; i < position && i < text.size(); ++i)
    {
        if (text[i] == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
    }
    return std::format("line {}, column {}", line, column);
}

std::string_view stringOf(yyjson_val* value) noexcept
{
    return {yyjson_get_str(value), yyjson_get_len(value)};
}

class SceneReader
{
public:
    SceneReader(World& world, SceneLoadResult& result)
        : m_world(world)
        , m_result(result)
    {
    }

    // Creates every entity first, then fills in components, so entity fields can refer forward. On
    // failure, sets the error; the caller destroys the created entities.
    bool read(yyjson_val* root)
    {
        if (!yyjson_is_obj(root))
        {
            return fail("the scene is not a JSON object");
        }
        yyjson_val* format = yyjson_obj_get(root, "format");
        if (!yyjson_is_str(format) || stringOf(format) != kFormatName)
        {
            return fail("not an illusion scene (missing \"format\": \"illusion.scene\")");
        }
        yyjson_val* version = yyjson_obj_get(root, "version");
        if (!yyjson_is_uint(version) || yyjson_get_uint(version) != kSceneFormatVersion)
        {
            return fail(std::format("unsupported scene version (expected {})", kSceneFormatVersion));
        }
        yyjson_val* entities = yyjson_obj_get(root, "entities");
        if (!yyjson_is_arr(entities))
        {
            return fail("\"entities\" must be an array");
        }
        if (!readTypes(yyjson_obj_get(root, "types")))
        {
            return false;
        }

        // Iterate: yyjson_arr_get() walks the array, which would make loading quadratic.
        m_result.entities.reserve(yyjson_arr_size(entities));
        yyjson_arr_iter iter = yyjson_arr_iter_with(entities);
        while (yyjson_val* entity = yyjson_arr_iter_next(&iter))
        {
            if (!yyjson_is_obj(entity))
            {
                return fail(std::format("entity {} is not an object", m_result.entities.size()));
            }
            m_result.entities.push_back(m_world.create());
        }
        iter = yyjson_arr_iter_with(entities);
        for (usize index = 0; yyjson_val* entity = yyjson_arr_iter_next(&iter); ++index)
        {
            if (!readEntity(index, entity))
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

    bool readTypes(yyjson_val* types)
    {
        if (types == nullptr)
        {
            return true;
        }
        if (!yyjson_is_obj(types))
        {
            return fail("\"types\" must be an object");
        }
        yyjson_obj_iter iter = yyjson_obj_iter_with(types);
        while (yyjson_val* name = yyjson_obj_iter_next(&iter))
        {
            yyjson_val* version = yyjson_obj_iter_get_val(name);
            if (!yyjson_is_uint(version))
            {
                return fail(std::format("version of type '{}' must be an unsigned integer", stringOf(name)));
            }
            const ComponentId id = m_world.registry().findByStableName(stringOf(name));
            if (id == kInvalidComponentId)
            {
                continue; // reported once per entity component below
            }
            const ComponentReflection* reflection = m_world.registry().reflection(id);
            if (yyjson_get_uint(version) != reflection->version)
            {
                m_result.warnings.push_back(
                    std::format("'{}' was saved with version {}, loading as version {}", reflection->name,
                                yyjson_get_uint(version), reflection->version));
            }
        }
        return true;
    }

    bool readEntity(usize index, yyjson_val* object)
    {
        const Entity entity = m_result.entities[index];
        yyjson_obj_iter iter = yyjson_obj_iter_with(object);
        while (yyjson_val* name = yyjson_obj_iter_next(&iter))
        {
            yyjson_val* fields = yyjson_obj_iter_get_val(name);
            const std::string_view typeName = stringOf(name);
            const ComponentId id = m_world.registry().findByStableName(typeName);
            if (id == kInvalidComponentId)
            {
                if (m_unknownTypes.insert(std::string(typeName)).second)
                {
                    m_result.warnings.push_back(std::format("unknown component type '{}' skipped", typeName));
                }
                continue;
            }
            if (!yyjson_is_obj(fields))
            {
                return fail(std::format("entity {}: component '{}' must be an object", index, typeName));
            }
            if (hasComponent(entity, id))
            {
                return fail(std::format("entity {}: component '{}' appears twice", index, typeName));
            }
            void* data = m_world.emplaceDefault(entity, id);
            if (!readFields(index, *m_world.registry().reflection(id), data, fields))
            {
                return false;
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

    bool readFields(usize index, const ComponentReflection& reflection, void* component, yyjson_val* fields)
    {
        yyjson_obj_iter iter = yyjson_obj_iter_with(fields);
        while (yyjson_val* name = yyjson_obj_iter_next(&iter))
        {
            const std::string_view fieldName = stringOf(name);
            const FieldInfo* field = reflection.field(fieldName);
            if (field == nullptr)
            {
                const std::string qualified = std::format("{}.{}", reflection.name, fieldName);
                if (m_unknownFields.insert(qualified).second)
                {
                    m_result.warnings.push_back(std::format("unknown field '{}' skipped", qualified));
                }
                continue;
            }
            if (!readField(component, *field, yyjson_obj_iter_get_val(name)))
            {
                return fail(std::format("entity {}: field '{}' of '{}' {}", index, fieldName, reflection.name,
                                        m_fieldError));
            }
        }
        return true;
    }

    bool fieldError(std::string_view message)
    {
        m_fieldError = message;
        return false;
    }

    template <class T>
    bool readInteger(yyjson_val* value, void* out)
    {
        if (yyjson_is_uint(value))
        {
            const u64 number = yyjson_get_uint(value);
            if (number > static_cast<u64>(std::numeric_limits<T>::max()))
            {
                return fieldError("is out of range");
            }
            *static_cast<T*>(out) = static_cast<T>(number);
            return true;
        }
        if (yyjson_is_sint(value))
        {
            const i64 number = yyjson_get_sint(value);
            if constexpr (std::is_unsigned_v<T>)
            {
                return fieldError("must not be negative");
            }
            else if (number < static_cast<i64>(std::numeric_limits<T>::min()))
            {
                return fieldError("is out of range");
            }
            else
            {
                *static_cast<T*>(out) = static_cast<T>(number);
                return true;
            }
        }
        return fieldError("must be an integer");
    }

    // Converting a double outside the f32 range would be undefined behaviour.
    bool readFloat(yyjson_val* value, f32& out)
    {
        const f64 number = yyjson_get_num(value);
        if (std::abs(number) > static_cast<f64>(std::numeric_limits<f32>::max()))
        {
            return fieldError("is out of range");
        }
        out = static_cast<f32>(number);
        return true;
    }

    bool readFloats(yyjson_val* value, f32* out, usize count)
    {
        if (!yyjson_is_arr(value) || yyjson_arr_size(value) != count)
        {
            return fieldError(std::format("must be an array of {} numbers", count));
        }
        for (usize i = 0; i < count; ++i)
        {
            yyjson_val* element = yyjson_arr_get(value, i);
            if (!yyjson_is_num(element))
            {
                return fieldError(std::format("must be an array of {} numbers", count));
            }
            if (!readFloat(element, out[i]))
            {
                return false;
            }
        }
        return true;
    }

    bool readField(void* component, const FieldInfo& field, yyjson_val* value)
    {
        void* data = static_cast<std::byte*>(component) + field.offset;
        switch (field.type)
        {
        case FieldType::Bool:
            if (!yyjson_is_bool(value))
            {
                return fieldError("must be true or false");
            }
            *static_cast<bool*>(data) = yyjson_get_bool(value);
            return true;
        case FieldType::I32:
            return readInteger<i32>(value, data);
        case FieldType::U32:
            return readInteger<u32>(value, data);
        case FieldType::I64:
            return readInteger<i64>(value, data);
        case FieldType::U64:
            return readInteger<u64>(value, data);
        case FieldType::F32:
            if (!yyjson_is_num(value))
            {
                return fieldError("must be a number");
            }
            return readFloat(value, *static_cast<f32*>(data));
        case FieldType::F64:
            if (!yyjson_is_num(value))
            {
                return fieldError("must be a number");
            }
            *static_cast<f64*>(data) = yyjson_get_num(value);
            return true;
        case FieldType::Vec2:
            return readFloats(value, &static_cast<Vec2*>(data)->x, 2);
        case FieldType::Vec3:
            return readFloats(value, &static_cast<Vec3*>(data)->x, 3);
        case FieldType::Vec4:
            return readFloats(value, &static_cast<Vec4*>(data)->x, 4);
        case FieldType::Quat:
        {
            f32 values[4] = {};
            if (!readFloats(value, values, 4))
            {
                return false;
            }
            *static_cast<Quat*>(data) = Quat(values[3], values[0], values[1], values[2]);
            return true;
        }
        case FieldType::String:
            if (!yyjson_is_str(value))
            {
                return fieldError("must be a string");
            }
            static_cast<std::string*>(data)->assign(stringOf(value));
            return true;
        case FieldType::Entity:
            if (yyjson_is_null(value))
            {
                *static_cast<Entity*>(data) = kNullEntity;
                return true;
            }
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) >= m_result.entities.size())
            {
                return fieldError("must be null or the index of an entity in this scene");
            }
            *static_cast<Entity*>(data) = m_result.entities[static_cast<usize>(yyjson_get_uint(value))];
            return true;
        case FieldType::Enum:
            if (yyjson_is_str(value))
            {
                for (const EnumValue& named : field.enumValues)
                {
                    if (named.name == stringOf(value))
                    {
                        writeEnumField(component, field, named.value);
                        return true;
                    }
                }
                return fieldError(std::format("has unknown value '{}'", stringOf(value)));
            }
            if (yyjson_is_int(value))
            {
                writeEnumField(component, field,
                               yyjson_is_sint(value) ? yyjson_get_sint(value)
                                                     : static_cast<i64>(yyjson_get_uint(value)));
                return true;
            }
            return fieldError("must be a value name");
        }
        return fieldError("has an unsupported type");
    }

    World& m_world;
    SceneLoadResult& m_result;
    std::string m_fieldError;
    std::unordered_set<std::string> m_unknownTypes;
    std::unordered_set<std::string> m_unknownFields;
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
    return SceneWriter(world, entities).write();
}

SceneLoadResult loadScene(World& world, std::string_view json)
{
    ILL_PROFILE_SCOPE();
    SceneLoadResult result;
    yyjson_read_err error{};
    // Without YYJSON_READ_INSITU the input is only read, never modified.
    const std::unique_ptr<yyjson_doc, DocumentDeleter> doc(
        yyjson_read_opts(const_cast<char*>(json.data()), json.size(), 0, nullptr, &error));
    if (doc == nullptr)
    {
        result.error = std::format("invalid JSON at {}: {}", describePosition(json, error.pos),
                                   error.msg != nullptr ? error.msg : "unknown error");
        return result;
    }
    if (!SceneReader(world, result).read(yyjson_doc_get_root(doc.get())))
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

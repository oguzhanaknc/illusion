// Checks the reflection metadata of every engine type, so that the editor shows them consistently.

#include <illusion/assets/AssetReflection.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace illusion;

namespace
{

void reflectEngine()
{
    reflectCoreComponents();
    reflectRenderComponents();
    reflectPhysicsComponents();
    reflectAudioComponents();
    reflectAssetStructs();
}

std::vector<const TypeReflection*> engineComponents()
{
    const ComponentRegistry& registry = ComponentRegistry::instance();
    std::vector<const TypeReflection*> types;
    for (u32 id = 0; id < registry.count(); ++id)
    {
        const TypeReflection* type = registry.reflection(static_cast<ComponentId>(id));
        if (type != nullptr && type->name.starts_with("illusion."))
        {
            types.push_back(type);
        }
    }
    return types;
}

std::vector<const TypeReflection*> engineStructs()
{
    const StructRegistry& structs = ComponentRegistry::instance().structs();
    std::vector<const TypeReflection*> types;
    for (u32 id = 0; id < structs.count(); ++id)
    {
        const TypeReflection* type = structs.reflection(static_cast<StructId>(id));
        if (type->name.starts_with("illusion."))
        {
            types.push_back(type);
        }
    }
    return types;
}

std::string lower(std::string_view text)
{
    std::string result(text);
    for (char& c : result)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

const ValueType& leafOf(const ValueType& type)
{
    const ValueType* leaf = &type;
    while (leaf->element != nullptr)
    {
        leaf = leaf->element;
    }
    return *leaf;
}

void checkFields(const TypeReflection& type)
{
    for (const FieldInfo& field : type.fields)
    {
        CAPTURE(type.name);
        CAPTURE(field.name);
        const std::string name = lower(field.name);
        const ValueType& leaf = leafOf(field);
        const bool vector = leaf.type == FieldType::Vector3 || leaf.type == FieldType::Vector4;
        const bool angleName =
            name.find("angle") != std::string::npos || name.starts_with("fov") || name.ends_with("slope") ||
            (name.find("angular") != std::string::npos && name.find("damping") == std::string::npos);
        if (angleName && (leaf.type == FieldType::F32 || leaf.type == FieldType::F64))
        {
            CHECK(field.meta.hint == FieldHint::Angle);
        }
        if (field.meta.hint == FieldHint::Angle || field.meta.unit == Unit::Radians)
        {
            CHECK(field.meta.unit == Unit::Radians);
            CHECK(field.meta.displayUnit == Unit::Degrees);
            CHECK(field.meta.hint == FieldHint::Angle);
        }
        const bool colorName = name.find("color") != std::string::npos ||
                               name.find("radiance") != std::string::npos ||
                               name.find("emissive") != std::string::npos || name == "zenith" ||
                               name == "horizon" || name == "ground";
        if (colorName && vector)
        {
            CHECK((field.meta.hint == FieldHint::Color || field.meta.hint == FieldHint::HdrColor));
        }
        if (name == "layer")
        {
            CHECK(field.meta.hint == FieldHint::Layer);
            CHECK(field.meta.max == static_cast<f64>(kMaxPhysicsLayers - 1));
        }
        if (field.type == FieldType::Struct)
        {
            checkFields(*field.structType);
        }
    }
}

// Converts a heading into the anchor MkDocs gives it.
std::string slugOf(std::string_view heading)
{
    std::string slug;
    for (const char c : heading)
    {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0)
        {
            slug.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        else if ((c == ' ' || c == '-') && !slug.empty() && slug.back() != '-')
        {
            slug.push_back('-');
        }
    }
    return slug;
}

bool docsAnchorExists(std::string_view anchor)
{
    const std::filesystem::path docs = utf8Path(ILLUSION_TEST_DATA_DIR) / ".." / ".." / "docs" / "content";
    const usize hash = anchor.find('#');
    const std::string_view page = anchor.substr(0, hash);
    for (const char* language : {"en", "tr"})
    {
        if (!std::filesystem::exists(docs / language / page))
        {
            return false;
        }
    }
    if (hash == std::string_view::npos)
    {
        return true;
    }
    std::ifstream file(docs / "en" / page);
    std::string line;
    while (std::getline(file, line))
    {
        if (line.starts_with('#'))
        {
            const usize text = line.find_first_not_of("# ");
            if (text != std::string::npos &&
                slugOf(std::string_view(line).substr(text)) == anchor.substr(hash + 1))
            {
                return true;
            }
        }
    }
    return false;
}

// Writes a default-constructed value of @p type as JSON and reads it back.
ValueReadResult roundTripDefault(const TypeReflection& type)
{
    void* object = ::operator new(type.size, std::align_val_t{type.alignment});
    type.ops->construct(object);
    JsonMutDocument doc;
    auto written = writeValue(doc, type, object);
    ValueReadResult result;
    if (!written)
    {
        result.errors.push_back(written.error());
    }
    else
    {
        doc.setRoot(*written);
        std::string text;
        REQUIRE(doc.write(text).hasValue());
        auto parsed = JsonDocument::parse(text);
        REQUIRE(parsed.hasValue());
        result = readValue(type, object, parsed->root());
    }
    type.ops->destroy(object);
    ::operator delete(object, std::align_val_t{type.alignment});
    return result;
}

} // namespace

TEST_CASE("every engine Component has complete metadata")
{
    reflectEngine();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const std::vector<const TypeReflection*> components = engineComponents();
    CHECK(components.size() >= 15);
    for (const TypeReflection* type : components)
    {
        CAPTURE(type->name);
        CHECK_FALSE(type->meta.displayName.empty());
        if (type->meta.transient)
        {
            continue;
        }
        CHECK_FALSE(type->meta.category.empty());
        CHECK_FALSE(type->meta.icon.empty());
        CHECK_FALSE(type->meta.description.empty());
        CHECK(docsAnchorExists(type->meta.docsAnchor));
        for (const std::string_view required : type->meta.requiredComponents)
        {
            CAPTURE(required);
            CHECK(registry.findByStableName(required) != kInvalidComponentId);
        }
        checkFields(*type);
    }
    const TypeReflection& rigidBody = *registry.reflection(registry.findByStableName("illusion.RigidBody"));
    CHECK(std::ranges::find(rigidBody.meta.requiredComponents, "illusion.Collider") !=
          rigidBody.meta.requiredComponents.end());
    CHECK(registry.reflection(registry.findByStableName("illusion.AudioListener"))->meta.singleton);
}

TEST_CASE("every engine struct has consistent metadata")
{
    reflectEngine();
    const std::vector<const TypeReflection*> structs = engineStructs();
    CHECK(structs.size() >= 12);
    for (const TypeReflection* type : structs)
    {
        CAPTURE(type->name);
        CHECK_FALSE(type->meta.displayName.empty());
        if (!type->meta.docsAnchor.empty())
        {
            CHECK(docsAnchorExists(type->meta.docsAnchor));
        }
        checkFields(*type);
    }
}

TEST_CASE("default values of every engine type are valid JSON for that type")
{
    reflectEngine();
    std::vector<const TypeReflection*> types = engineComponents();
    const std::vector<const TypeReflection*> structs = engineStructs();
    types.insert(types.end(), structs.begin(), structs.end());
    for (const TypeReflection* type : types)
    {
        CAPTURE(type->name);
        if (type->size == 0 || type->meta.transient)
        {
            continue;
        }
        const ValueReadResult result = roundTripDefault(*type);
        INFO((result.ok() ? std::string()
                          : result.errors.front().pointer + ": " + result.errors.front().message));
        CHECK(result.ok());
        CHECK(result.unknownKeys.empty());
    }
}

TEST_CASE("runtime state Components are never saved")
{
    reflectEngine();
    World world;
    world.create(Transform{}, PhysicsBody{}, PhysicsJoint{}, CharacterBody{}, AudioPlayback{});
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());
    CHECK(saved.skippedComponents == 4);
    for (const char* name : {"illusion.PhysicsBody", "illusion.PhysicsJoint", "illusion.CharacterBody",
                             "illusion.AudioPlayback"})
    {
        CAPTURE(name);
        CHECK(saved.json.find(name) == std::string::npos);
    }

    World loaded;
    const SceneLoadResult result = loadScene(loaded, R"({"format": "illusion.scene", "version": 1,
        "entities": [{"illusion.PhysicsBody": {}, "illusion.Transform": {}}, {"illusion.PhysicsBody": {}}]})");
    REQUIRE(result.ok());
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings[0] == "runtime component type 'illusion.PhysicsBody' skipped");
    CHECK_FALSE(loaded.has<PhysicsBody>(result.entities[0]));
    CHECK(loaded.has<Transform>(result.entities[0]));
}

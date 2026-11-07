// The showcase project (samples/showcase): its everything scene sets every field of every saved engine
// Component, and the project opens without missing assets or unresolved prefabs.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/ecs/ComponentJson.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/Components.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;
using test::ok;

namespace
{

// A copy of the showcase project opened in an editor (the copy keeps the editor's state files out of the
// repository).
struct ShowcaseFixture
{
    ShowcaseFixture()
    {
        test::reflectEditorTestComponents();
        reflectRenderComponents();
        reflectPhysicsComponents();
        reflectAudioComponents();
        reflectParticleComponents();
        const std::filesystem::path project = directory.path / "showcase";
        std::filesystem::copy(utf8Path(ILLUSION_SAMPLES_DIR) / "showcase", project,
                              std::filesystem::copy_options::recursive);
        EditorContextOptions options;
        options.workspace = directory.path;
        editor = std::make_unique<EditorContext>(options);
        const Expected<void, CommandError> opened = editor->openProject(project);
        REQUIRE_MESSAGE(opened.hasValue(), (opened ? std::string() : opened.error().message));
    }

    test::TempDirectory directory;
    std::unique_ptr<EditorContext> editor;
};

// @p object of @p type as JSON text.
std::string jsonOf(const TypeReflection& type, const void* object)
{
    JsonMutDocument doc;
    Expected<JsonMutValue, ValueError> written = writeValue(doc, type, object);
    REQUIRE(written.hasValue());
    doc.setRoot(*written);
    std::string text;
    REQUIRE(doc.write(text).hasValue());
    return text;
}

// The text of every member of the JSON object @p text.
std::map<std::string, std::string> membersOf(const std::string& text)
{
    const Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(text);
    REQUIRE(parsed.hasValue());
    std::map<std::string, std::string> out;
    for (const auto& member : parsed->root().members())
    {
        std::string value;
        REQUIRE(member.value.write(value).hasValue());
        out.emplace(std::string(member.key), std::move(value));
    }
    return out;
}

} // namespace

TEST_CASE(
    "the showcase scene gives every field of every saved engine Component a value other than its default")
{
    ShowcaseFixture fixture;
    const World& world = fixture.editor->world();
    const ComponentRegistry& registry = world.registry();
    std::vector<Entity> entities;
    world.entities(entities);
    std::vector<std::string> missing;
    std::string defaults;
    for (u32 index = 0; index < registry.count(); ++index)
    {
        const auto id = static_cast<ComponentId>(index);
        const TypeReflection* type = registry.reflection(id);
        if (type == nullptr || !type->name.starts_with("illusion.") || type->meta.transient)
        {
            continue;
        }
        // The default value, and which top-level fields some entity sets to something else.
        std::map<std::string, std::string> fields;
        if (type->size != 0)
        {
            void* object = ::operator new(type->size, std::align_val_t{type->alignment});
            type->ops->construct(object);
            const std::string text = jsonOf(*type, object);
            type->ops->destroy(object);
            ::operator delete(object, std::align_val_t{type->alignment});
            fields = membersOf(text);
            defaults += std::string(type->name) + " " + text + "\n";
        }
        std::set<std::string> changed;
        bool present = false;
        for (const Entity entity : entities)
        {
            const std::span<const ComponentId> components = world.componentsOf(entity);
            if (std::find(components.begin(), components.end(), id) == components.end())
            {
                continue;
            }
            present = true;
            const void* data = world.componentData(entity, id);
            if (type->size == 0 || data == nullptr)
            {
                continue;
            }
            for (const auto& [field, value] : membersOf(jsonOf(*type, data)))
            {
                if (fields.contains(field) && fields.at(field) != value)
                {
                    changed.insert(field);
                }
            }
        }
        if (!present)
        {
            missing.push_back(std::string(type->name));
            continue;
        }
        for (const auto& [field, value] : fields)
        {
            if (!changed.contains(field))
            {
                missing.push_back(std::string(type->name) + "/" + field);
            }
        }
    }
    std::string list;
    for (const std::string& entry : missing)
    {
        list += entry + "\n";
    }
    INFO("left at their defaults:\n" << list << "defaults:\n" << defaults);
    CHECK(missing.empty());
}

TEST_CASE("the showcase project opens without missing assets or unresolved prefabs")
{
    ShowcaseFixture fixture;
    const JsonDocument diagnostics = ok(*fixture.editor, "diagnostics.get");
    for (const JsonValue issue : diagnostics.root().find("issues").elements())
    {
        const std::string_view code = issue.find("code").asString().value_or("");
        INFO(code << ": " << issue.find("message").asString().value_or(""));
        CHECK(code != "asset.missing");
        CHECK(code != "prefab.unresolved");
    }
}

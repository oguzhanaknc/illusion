#pragma once

// Shared setup of the editor core tests: test Components with entity and asset references, test settings
// types, a project made from the empty template, and a fixture scene built through commands.

#include "TestSupport.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/UiHost.hpp>
#include <illusion/editor/core/ViewportHost.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editortest
{

/// A Component with an entity reference (like Joint::connected).
struct Link
{
    illusion::Entity target;     ///< The linked entity.
    illusion::f32 weight = 1.0f; ///< A number to edit.
};

/// A Component with asset references (like MeshRenderer).
struct Look
{
    illusion::AssetRef material;            ///< A material.
    std::vector<illusion::AssetRef> extras; ///< More assets.
};

/// Scene settings with an asset and an entity reference.
struct Difficulty
{
    illusion::u32 enemies = 3;  ///< A number.
    illusion::f32 speed = 1.0f; ///< Another number.
    illusion::AssetRef music;   ///< An asset.
    illusion::Entity boss;      ///< An entity of the scene.
};

/// Project settings.
struct Controls
{
    bool invertY = false;             ///< A flag.
    illusion::f32 sensitivity = 1.0f; ///< A number.
    illusion::AssetRef cursor;        ///< An asset.
};

} // namespace editortest

namespace illusion::test
{

/// Reflects the test types (once): editortest::Link as "test.Link", editortest::Look as "test.Look" and the
/// settings structs "test.Difficulty" and "test.Controls".
inline void reflectEditorTestComponents()
{
    static const bool reflected = [] {
        reflectCoreComponents();
        reflect<editortest::Link>("test.Link")
            .meta({.displayName = "Link", .category = "Test"})
            .field("target", &editortest::Link::target)
            .field("weight", &editortest::Link::weight, {.min = 0.0, .max = 100.0});
        reflect<editortest::Look>("test.Look")
            .meta({.displayName = "Look", .category = "Test"})
            .field("material", &editortest::Look::material, {.assetKind = AssetKind::Material})
            .field("extras", &editortest::Look::extras);
        reflectStruct<editortest::Difficulty>("test.Difficulty")
            .field("enemies", &editortest::Difficulty::enemies, {.max = 1000.0})
            .field("speed", &editortest::Difficulty::speed)
            .field("music", &editortest::Difficulty::music, {.assetKind = AssetKind::AudioClip})
            .field("boss", &editortest::Difficulty::boss);
        reflectStruct<editortest::Controls>("test.Controls")
            .field("invertY", &editortest::Controls::invertY)
            .field("sensitivity", &editortest::Controls::sensitivity, {.min = 0.0})
            .field("cursor", &editortest::Controls::cursor, {.assetKind = AssetKind::Texture});
        return true;
    }();
    (void)reflected;
}

/// Registers the test settings types in @p editor: "test.Difficulty" for scenes and "test.Controls" in
/// "settings:/controls.json".
inline void addTestSettings(editor::EditorContext& editor)
{
    reflectEditorTestComponents();
    editor.settings().add("test.Difficulty", editor::SettingsScope::Scene);
    editor.settings().add("test.Controls", editor::SettingsScope::Project, "settings:/controls.json");
}

/// editor-data next to the test executable.
inline std::filesystem::path editorDataDirectory()
{
    return executableDirectory() / "editor-data";
}

/// Parses JSON text in a test (fails the test when it is invalid).
inline JsonDocument parseJson(std::string_view text)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(text);
    REQUIRE_MESSAGE(parsed.hasValue(), std::string(text));
    return std::move(parsed).value();
}

/// Runs a command with JSON params given as text; an async command is advanced until it finishes.
inline Expected<std::string, editor::CommandError> run(editor::EditorContext& editor, std::string_view name,
                                                       std::string_view params = "{}",
                                                       const editor::CommandOptions& options = {})
{
    const JsonDocument doc = parseJson(params);
    const editor::CommandDescriptor* command = editor.commands().find(name);
    if (command == nullptr || !command->info.flags.async)
    {
        return editor.executeJson(name, doc.root(), options);
    }
    Expected<std::unique_ptr<editor::RunningCommand>, editor::CommandError> started =
        editor.startJson(name, doc.root(), options);
    if (!started)
    {
        return unexpected(std::move(started).error());
    }
    for (;;)
    {
        Expected<std::optional<std::string>, editor::CommandError> updated = started.value()->update();
        if (!updated)
        {
            return unexpected(std::move(updated).error());
        }
        if (updated->has_value())
        {
            return std::move(**updated);
        }
    }
}

/// Runs a command that must succeed and returns its result.
inline JsonDocument ok(editor::EditorContext& editor, std::string_view name, std::string_view params = "{}",
                       const editor::CommandOptions& options = {})
{
    Expected<std::string, editor::CommandError> result = run(editor, name, params, options);
    REQUIRE_MESSAGE(result.hasValue(), (std::string(name) + " " + std::string(params) + ": " +
                                        (result ? std::string() : result.error().message)));
    return parseJson(*result);
}

/// Runs a command that must fail and returns the error.
inline editor::CommandError fails(editor::EditorContext& editor, std::string_view name,
                                  std::string_view params)
{
    Expected<std::string, editor::CommandError> result = run(editor, name, params);
    REQUIRE_MESSAGE(!result.hasValue(), (std::string(name) + " " + std::string(params) + " succeeded"));
    return std::move(result).error();
}

/// A UiHost without a window for the core tests: two panels, the built-in "default" layout and a saved one,
/// and screenshots that finish one frame after they were requested.
class FakeUi final : public editor::UiHost
{
public:
    std::vector<editor::UiPanelInfo> panels() const override
    {
        return {editor::UiPanelInfo{"panel.console", "Console", consoleOpen, true},
                editor::UiPanelInfo{"window.preferences", "Preferences", false, false}};
    }

    Expected<void, editor::CommandError> showPanel(std::string_view id, bool open) override
    {
        if (id == "panel.console")
        {
            consoleOpen = open;
        }
        return {};
    }

    std::vector<editor::UiLayoutInfo> layouts() const override
    {
        std::vector<editor::UiLayoutInfo> out{editor::UiLayoutInfo{"default", true, active == "default"}};
        for (const std::string& name : saved)
        {
            out.push_back(editor::UiLayoutInfo{name, false, active == name});
        }
        return out;
    }

    Expected<void, editor::CommandError> applyLayout(std::string_view name) override
    {
        active = std::string(name);
        return {};
    }

    Expected<void, editor::CommandError> saveLayout(std::string_view name) override
    {
        if (std::find(saved.begin(), saved.end(), name) == saved.end())
        {
            saved.emplace_back(name);
        }
        active = std::string(name);
        return {};
    }

    Expected<void, editor::CommandError> deleteLayout(std::string_view name) override
    {
        std::erase(saved, std::string(name));
        return {};
    }

    void resetLayout() override { active = "default"; }

    Expected<void, editor::CommandError> openPalette(std::string_view mode, std::string_view query) override
    {
        palette = std::string(mode) + ":" + std::string(query);
        return {};
    }

    void notify(editor::UiNotice, std::string_view message) override { notices.emplace_back(message); }

    void requestQuit() override { quit = true; }

    Expected<u64, editor::CommandError> requestScreenshot(UVec2 size) override
    {
        requested = size;
        return ++ticket;
    }

    std::optional<Expected<editor::UiScreenshot, editor::CommandError>> screenshot(u64) override
    {
        if (!pending)
        {
            pending = true;
            return std::nullopt;
        }
        pending = false;
        return Expected<editor::UiScreenshot, editor::CommandError>(
            editor::UiScreenshot{requested, std::string("\x89PNG\r\n\x1a\n", 8)});
    }

    bool consoleOpen = false;
    std::string active = "default";
    std::vector<std::string> saved{"saved"};
    std::string palette;
    std::vector<std::string> notices;
    bool quit = false;
    UVec2 requested{0};
    u64 ticket = 0;
    bool pending = false;
};

/// Viewports without a GPU: a "scene" viewport of 1280x720, a "game" viewport of 640x360 looking through
/// `gameCamera`, and the secondary viewports viewport.create opened. Picks and readbacks answer at once with
/// `hit`, `color` and `entities`.
class FakeViewports final : public editor::ViewportHost
{
public:
    struct Secondary
    {
        std::string id;
        editor::ViewportCamera camera;
        editor::ViewportSettings settings;
    };

    std::vector<editor::ViewportInfo> viewports() const override
    {
        const f32 aspect = static_cast<f32>(size.x) / static_cast<f32>(size.y);
        std::vector<editor::ViewportInfo> out;
        out.push_back(editor::ViewportInfo{
            "scene", editor::ViewportKind::Scene, size, true, camera, editor::viewportEye(camera), settings,
            Entity{}, editor::viewportViewMatrix(camera), editor::viewportProjectionMatrix(camera, aspect)});
        out.push_back(editor::ViewportInfo{"game", editor::ViewportKind::Game, UVec2(640, 360), false,
                                           editor::ViewportCamera{}, Vec3(0.0f), gameSettings, gameCamera,
                                           Mat4(1.0f), Mat4(1.0f)});
        for (const Secondary& viewport : secondary)
        {
            out.push_back(editor::ViewportInfo{viewport.id, editor::ViewportKind::Scene, size, true,
                                               viewport.camera, editor::viewportEye(viewport.camera),
                                               viewport.settings, Entity{},
                                               editor::viewportViewMatrix(viewport.camera),
                                               editor::viewportProjectionMatrix(viewport.camera, aspect)});
        }
        return out;
    }

    Expected<void, editor::CommandError> setCamera(std::string_view id, const editor::ViewportCamera& value,
                                                   bool animate) override
    {
        (id == "scene" ? camera : find(id).camera) = value;
        animated = animate;
        return {};
    }

    Expected<void, editor::CommandError> setSettings(std::string_view id,
                                                     const editor::ViewportSettings& value) override
    {
        (id == "scene" ? settings : id == "game" ? gameSettings : find(id).settings) = value;
        return {};
    }

    Expected<editor::ViewportCamera, editor::CommandError> framing(std::string_view,
                                                                   std::span<const Entity> targets) override
    {
        framed.assign(targets.begin(), targets.end());
        editor::ViewportCamera out = camera;
        out.distance = 3.0f;
        return out;
    }

    Expected<u64, editor::CommandError> requestPick(std::string_view, Vec2 pixel, bool cycle) override
    {
        pickedAt = pixel;
        cycled = cycle;
        return ++ticket;
    }

    std::optional<Expected<std::optional<editor::ViewportHit>, editor::CommandError>> pick(u64) override
    {
        return Expected<std::optional<editor::ViewportHit>, editor::CommandError>(hit);
    }

    editor::ViewportFrame frame(std::string_view, UVec2, const editor::ViewportInput&,
                                const editor::ViewportStyle&) override
    {
        return {};
    }

    Expected<std::string, editor::CommandError> createViewport() override
    {
        if (secondary.size() >= editor::kMaxSecondaryViewports)
        {
            return unexpected(editor::CommandError::make(editor::ErrorKind::LimitExceeded, "full"));
        }
        std::string id = "scene-" + std::to_string(secondary.size() + 2);
        secondary.push_back(Secondary{id, camera, settings});
        ++version;
        return id;
    }

    u64 viewportsVersion() const noexcept override { return version; }

    Expected<void, editor::CommandError> closeViewport(std::string_view id) override
    {
        std::erase_if(secondary, [&](const Secondary& viewport) { return viewport.id == id; });
        ++version;
        return {};
    }

    Expected<u64, editor::CommandError> requestReadback(std::string_view, editor::ViewportChannel channel,
                                                        UVec2 origin, UVec2 extent) override
    {
        readChannel = channel;
        readOrigin = origin;
        readExtent = extent;
        return ++ticket;
    }

    std::optional<Expected<editor::ViewportReadback, editor::CommandError>> readback(u64) override
    {
        editor::ViewportReadback out;
        out.size = size;
        out.origin = readOrigin;
        out.extent = readExtent;
        if (readChannel == editor::ViewportChannel::Color)
        {
            out.color.resize(static_cast<usize>(readExtent.x) * readExtent.y * 4);
            for (usize i = 0; i < out.color.size(); ++i)
            {
                out.color[i] = color[i % 4];
            }
        }
        else
        {
            out.entities = entities;
        }
        return Expected<editor::ViewportReadback, editor::CommandError>(std::move(out));
    }

    Expected<editor::ViewportRenderStats, editor::CommandError> renderStats(std::string_view) const override
    {
        return stats;
    }

    editor::ViewportGpuInfo gpuInfo() const override
    {
        editor::ViewportGpuInfo info;
        info.name = "Fake GPU";
        info.validation = true;
        return info;
    }

    Secondary& find(std::string_view id)
    {
        const auto found = std::find_if(secondary.begin(), secondary.end(),
                                        [&](const Secondary& viewport) { return viewport.id == id; });
        REQUIRE(found != secondary.end());
        return *found;
    }

    UVec2 size{1280, 720};
    editor::ViewportCamera camera;
    editor::ViewportSettings settings;
    editor::ViewportSettings gameSettings;
    Entity gameCamera{};
    std::vector<Secondary> secondary;
    bool animated = false;
    std::vector<Entity> framed;
    Vec2 pickedAt{0.0f};
    bool cycled = false;
    std::optional<editor::ViewportHit> hit;
    std::array<u8, 4> color{255, 0, 0, 255};
    std::vector<Entity> entities;
    editor::ViewportRenderStats stats;
    editor::ViewportChannel readChannel = editor::ViewportChannel::Color;
    UVec2 readOrigin{0};
    UVec2 readExtent{0};
    u64 ticket = 0;
    u64 version = 0;
};

/// A temporary project made from the empty template, open in an EditorContext.
struct EditorFixture
{
    TempDirectory directory;
    FakeUi ui;
    FakeViewports viewports;
    std::unique_ptr<editor::EditorContext> editor;

    explicit EditorFixture(u64 seed = 1, editor::EditorContextOptions options = {})
    {
        reflectEditorTestComponents();
        Expected<editor::Project, std::string> project = editor::Project::create(
            directory.path / "project", "Test", editorDataDirectory() / "templates" / "empty");
        REQUIRE_MESSAGE(project.hasValue(), (project ? std::string() : project.error()));
        open(seed, options);
    }

    /// (Re)creates the editor on the project.
    void open(u64 seed, editor::EditorContextOptions options = {})
    {
        editor.reset();
        options.seed = seed;
        if (options.workspace.empty())
        {
            options.workspace = directory.path;
        }
        editor = std::make_unique<editor::EditorContext>(options);
        editor->setUi(&ui);
        editor->setViewports(&viewports);
        const Expected<void, editor::CommandError> opened = editor->openProject(directory.path / "project");
        REQUIRE_MESSAGE(opened.hasValue(), (opened ? std::string() : opened.error().message));
        addTestSettings(*editor);
    }

    /// Builds the fixture scene and saves it to assets:/scenes/main.scene (history cleared, not dirty):
    ///
    ///     /Game                  Transform
    ///       /Game/Board          LocalTransform
    ///         /Game/Board/Cell 1
    ///         /Game/Board/Cell 2
    ///       /Game/Player         test.Link -> /Game/Board
    ///     /Light                 (no Hierarchy)
    void buildScene()
    {
        editor::EditorContext& e = *editor;
        ok(e, "entity.create",
           R"({"name":"Game","index":0,"components":{"illusion.Transform":{"position":[1,0,0]}}})");
        ok(e, "entity.create",
           R"({"name":"Board","parent":"/Game","components":{"illusion.LocalTransform":{"position":[0,2,0]}}})");
        ok(e, "entity.create", R"({"name":"Cell 1","parent":"/Game/Board"})");
        ok(e, "entity.create", R"({"name":"Cell 2","parent":"/Game/Board"})");
        const JsonDocument board = ok(e, "entity.get", R"({"entity":"/Game/Board"})");
        const std::string boardId(board.root().pointer("/entity/id").asString().value_or(""));
        ok(e, "entity.create",
           R"({"name":"Player","parent":"/Game","components":{"test.Link":{"target":")" + boardId +
               R"("}}})");
        ok(e, "entity.create", R"({"name":"Light","components":{"illusion.Transform":{}}})");
        ok(e, "scene.save");
        ok(e, "history.clear");
    }

    /// Adds assets to the fixture scene (after buildScene()) and saves it again (history cleared, not dirty):
    ///
    ///     assets:/materials/red.material   textures.baseColor -> assets:/textures/red.png
    ///     assets:/textures/red.png
    ///     assets:/scenes/other.scene       an entity "Prop" with test.Look -> red.material
    ///     /Light gets test.Look            material -> red.material, extras ->
    ///     [assets:/models/ship.glb#mesh/0]
    void addAssets()
    {
        editor::EditorContext& e = *editor;
        ok(e, "project.file_write", R"({"path":"assets:/textures/red.png","text":"not really a png"})");
        ok(e, "material.create",
           R"({"path":"assets:/materials/red.material","value":{"material":{"baseColorFactor":[0.8,0.1,0.1,1]},)"
           R"("textures":{"baseColor":"assets:/textures/red.png"}}})");
        ok(e, "project.file_write",
           R"({"path":"assets:/scenes/other.scene","text":"{\"format\":\"illusion.scene\",\"version\":2,)"
           R"(\"entities\":[{\"id\":\"00000000000000a1\",\"components\":{\"illusion.Name\":)"
           R"({\"value\":\"Prop\"},\"test.Look\":{\"material\":\"assets:/materials/red.material\"}}}]}"})");
        ok(e, "component.add",
           R"({"entity":"/Light","type":"test.Look","value":{"material":"assets:/materials/red.material",)"
           R"("extras":["assets:/models/ship.glb#mesh/0"]}})");
        ok(e, "scene.save");
        ok(e, "history.clear");
    }

    /// The scene as JSON text.
    [[nodiscard]] std::string scene() const
    {
        Expected<std::string, editor::CommandError> json = editor->sceneJson();
        REQUIRE(json.hasValue());
        return *json;
    }
};

} // namespace illusion::test

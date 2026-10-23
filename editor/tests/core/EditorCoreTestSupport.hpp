#pragma once

// Shared setup of the editor core tests: a test Component with an entity reference, a project made from the
// empty template, and a fixture scene built through commands.

#include "TestSupport.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/editor/core/CoreCommands.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace editortest
{

/// A Component with an entity reference (like Joint::connected).
struct Link
{
    illusion::Entity target;     ///< The linked entity.
    illusion::f32 weight = 1.0f; ///< A number to edit.
};

} // namespace editortest

namespace illusion::test
{

/// Reflects editortest::Link as "test.Link" (once).
inline void reflectEditorTestComponents()
{
    static const bool reflected = [] {
        reflectCoreComponents();
        reflect<editortest::Link>("test.Link")
            .meta({.displayName = "Link", .category = "Test"})
            .field("target", &editortest::Link::target)
            .field("weight", &editortest::Link::weight, {.min = 0.0, .max = 100.0});
        return true;
    }();
    (void)reflected;
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

/// Runs a command with JSON params given as text.
inline Expected<std::string, editor::CommandError> run(editor::EditorContext& editor, std::string_view name,
                                                       std::string_view params = "{}",
                                                       const editor::CommandOptions& options = {})
{
    const JsonDocument doc = parseJson(params);
    return editor.executeJson(name, doc.root(), options);
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

/// A temporary project made from the empty template, open in an EditorContext.
struct EditorFixture
{
    TempDirectory directory;
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
        editor = std::make_unique<editor::EditorContext>(options);
        const Expected<void, editor::CommandError> opened = editor->openProject(directory.path / "project");
        REQUIRE_MESSAGE(opened.hasValue(), (opened ? std::string() : opened.error().message));
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

    /// The scene as JSON text.
    [[nodiscard]] std::string scene() const
    {
        Expected<std::string, editor::CommandError> json = editor->sceneJson();
        REQUIRE(json.hasValue());
        return *json;
    }
};

} // namespace illusion::test

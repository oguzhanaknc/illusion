#include "EditorCoreTestSupport.hpp"

#include <illusion/editor/core/ChangeJournal.hpp>
#include <illusion/editor/core/JsonSchema.hpp>
#include <illusion/editor/core/Project.hpp>
#include <illusion/editor/core/UndoStack.hpp>

#include <fstream>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

UndoStep componentStep(std::string mergeKey, u64 entity, std::string before, std::string after,
                       std::string origin = "ui")
{
    UndoStep step;
    step.label = "Set";
    step.origin = std::move(origin);
    step.mergeKey = std::move(mergeKey);
    step.operations.push_back(
        ComponentChange{EntityGuid{entity}, "illusion.Transform", std::move(before), std::move(after)});
    return step;
}

} // namespace

TEST_CASE("project manifests round-trip and report what is wrong")
{
    ProjectManifest manifest;
    manifest.name = "Tetris";
    manifest.version = "1.2.0";
    manifest.engineVersion = "0.7.0";
    manifest.startupScene = "assets:/scenes/main.scene";
    manifest.modules = {"tetris"};
    Expected<ProjectManifest, std::string> parsed = parseProjectManifest(writeProjectManifest(manifest));
    REQUIRE(parsed.hasValue());
    CHECK(parsed->name == "Tetris");
    CHECK(parsed->version == "1.2.0");
    CHECK(parsed->startupScene == "assets:/scenes/main.scene");
    CHECK(parsed->modules == std::vector<std::string>{"tetris"});

    CHECK_FALSE(parseProjectManifest("{").hasValue());
    CHECK_FALSE(parseProjectManifest(R"({"format":"other","name":"x"})").hasValue());
    CHECK_FALSE(parseProjectManifest(R"({"format":"illusion.project","name":""})").hasValue());
    CHECK_FALSE(
        parseProjectManifest(R"({"format":"illusion.project","name":"x","version":"one"})").hasValue());
    CHECK_FALSE(parseProjectManifest(R"({"format":"illusion.project","name":"x","modules":[1]})").hasValue());
    CHECK(parseProjectManifest(R"({"format":"illusion.project","name":"x"})")->version == "0.1.0");
}

TEST_CASE("project.create and project.open work only inside the workspace")
{
    test::reflectEditorTestComponents();
    test::TempDirectory directory;
    EditorContext closed(EditorContextOptions{.seed = 3});
    CHECK(test::fails(closed, "project.create", R"({"directory":"game","name":"Game"})").kind ==
          ErrorKind::Unsupported);

    EditorContext editor(EditorContextOptions{.seed = 3, .workspace = directory.path});
    CHECK(test::fails(editor, "project.create", R"({"directory":"../escape","name":"Game"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(test::fails(editor, "project.create", R"({"directory":"/abs","name":"Game"})").kind ==
          ErrorKind::ValidationFailed);
    const CommandError unknown =
        test::fails(editor, "project.create", R"({"directory":"game","name":"Game","template":"3d-startr"})");
    CHECK(unknown.kind == ErrorKind::ValidationFailed);
    REQUIRE_FALSE(unknown.suggestions.empty());
    CHECK(unknown.suggestions[0] == "3d-starter");
    const JsonDocument dry =
        ok(editor, "project.create", R"({"directory":"game","name":"Game","dryRun":true})");
    CHECK_FALSE(std::filesystem::exists(directory.path / "game"));
    CHECK(dry.root().find("name").asString() == "Game");

    const JsonDocument created = ok(
        editor, "project.create", R"({"directory":"games/tetris","name":"Tetris","template":"3d-starter"})");
    CHECK(created.root().find("name").asString() == "Tetris");
    REQUIRE(editor.project() != nullptr);
    CHECK(std::filesystem::equivalent(editor.project()->root(), directory.path / "games" / "tetris"));
    CHECK(editor.document().path() == "assets:/scenes/main.scene");
    CHECK(test::fails(editor, "project.create", R"({"directory":"games/tetris","name":"Again"})").kind ==
          ErrorKind::AlreadyExists);

    ok(editor, "project.create", R"({"directory":"second","name":"Second"})");
    CHECK(editor.project()->manifest().name == "Second");
    ok(editor, "entity.create", R"({"name":"Unsaved"})");
    CHECK(test::fails(editor, "project.open", R"({"directory":"games/tetris"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(test::fails(editor, "project.open", R"({"directory":"nothing","discardChanges":true})").kind ==
          ErrorKind::NotFound);
    ok(editor, "project.open", R"({"directory":"games/tetris","discardChanges":true})");
    CHECK(editor.project()->manifest().name == "Tetris");
}

TEST_CASE("projects are created from templates and open with their mounts")
{
    test::reflectEditorTestComponents();
    test::TempDirectory directory;
    const std::filesystem::path templates = test::editorDataDirectory() / "templates";
    Expected<Project, std::string> project =
        Project::create(directory.path / "game", "My Game", templates / "3d-starter");
    REQUIRE_MESSAGE(project.hasValue(), (project ? std::string() : project.error()));
    CHECK(project->manifest().name == "My Game");
    CHECK_FALSE(project->manifest().engineVersion.empty());
    CHECK(std::filesystem::is_directory(directory.path / "game" / "settings"));
    CHECK(std::filesystem::is_directory(directory.path / "game" / ".illusion" / "cache"));

    EditorContext editor(EditorContextOptions{.seed = 3});
    REQUIRE(editor.openProject(directory.path / "game").hasValue());
    CHECK(editor.document().path() == "assets:/scenes/main.scene");
    CHECK(editor.world().aliveCount() == 5);
    CHECK_FALSE(editor.dirty());
    const JsonDocument info = ok(editor, "project.info");
    CHECK(info.root().find("name").asString() == "My Game");
    CHECK(info.root().find("mounts").size() == 4);

    CHECK_FALSE(Project::create(directory.path / "game", "Again", templates / "empty").hasValue());
    CHECK_FALSE(Project::create(directory.path / "other", "", templates / "empty").hasValue());
    CHECK_FALSE(Project::create(directory.path / "other", "X", templates / "missing").hasValue());
    CHECK_FALSE(Project::open(directory.path / "nothing").hasValue());
    CHECK(editor.openProject(directory.path / "nothing").error().kind == ErrorKind::NotFound);
}

TEST_CASE("entity references resolve ids, paths, unique names and the selection")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    EntityIndex& index = editor.entities();
    Selection& selection = editor.selection();

    const Expected<Entity, CommandError> board = index.resolve("/Game/Board", selection);
    REQUIRE(board.hasValue());
    const EntitySummary summary = index.summary(*board);
    CHECK(summary.name == "Board");
    CHECK(summary.path == "/Game/Board");
    CHECK(index.resolve(summary.id, selection).value() == *board);
    CHECK(index.resolve("name:Board", selection).value() == *board);
    CHECK(index.resolve("/Game/Board/Cell 2", selection).hasValue());

    CHECK(index.resolve("$selection", selection).error().kind == ErrorKind::PreconditionFailed);
    selection.set(std::vector<EntityGuid>{index.guidOf(*board)});
    CHECK(index.resolve("$selection", selection).value() == *board);

    const CommandError missing = index.resolve("/Game/Bord", selection, "/entity").error();
    CHECK(missing.kind == ErrorKind::NotFound);
    REQUIRE(missing.errors.size() == 1);
    CHECK(missing.errors[0].pointer == "/entity");
    REQUIRE_FALSE(missing.suggestions.empty());
    CHECK(missing.suggestions[0] == "/Game/Board");
    CHECK(index.resolve("name:Bard", selection).error().suggestions.at(0) == "name:Board");
    CHECK(index.resolve("0000000000000001", selection).error().kind == ErrorKind::NotFound);
    CHECK(index.resolve("", selection).error().kind == ErrorKind::ValidationFailed);
    CHECK(index.resolve("Game", selection).error().kind == ErrorKind::ValidationFailed);
    CHECK(index.resolve("/Game//Board", selection).error().kind == ErrorKind::ValidationFailed);

    // Duplicate names are ambiguous by name and by path, but ids and '#id' segments still work.
    ok(editor, "entity.create", R"({"name":"Cell 1","parent":"/Game/Board"})");
    CHECK(index.resolve("/Game/Board/Cell 1", selection).error().kind == ErrorKind::Ambiguous);
    CHECK(index.resolve("name:Cell 1", selection).error().kind == ErrorKind::Ambiguous);
    const JsonDocument unnamed = ok(editor, "entity.create", R"({"parent":"/Game"})");
    const std::string_view path = unnamed.root().pointer("/entity/path").asString().value_or("");
    CHECK(path.starts_with("/Game/#"));
    CHECK(index.resolve(path, selection).hasValue());

    std::vector<Entity> all;
    selection.add(index.guidOf(index.resolve("/Light", selection).value()));
    REQUIRE(
        index.resolveAll(std::vector<std::string>{"$selection", "/Game/Board"}, selection, "/entities", all)
            .hasValue());
    CHECK(all.size() == 2);
    CHECK(index.resolve("$selection", selection).error().kind == ErrorKind::Ambiguous);
}

TEST_CASE("the undo stack merges continuous edits, drops redo steps and trims to its budget")
{
    UndoStack stack;
    CHECK(stack.stateId() == 0);
    stack.push(componentStep("drag", 1, "a", "b"));
    const u64 first = stack.stateId();
    CHECK(stack.push(componentStep("drag", 1, "b", "c")).merged);
    CHECK(stack.steps().size() == 1);
    const auto& change = std::get<ComponentChange>(stack.steps()[0].operations[0]);
    CHECK(change.before == "a");
    CHECK(change.after == "c");
    CHECK(stack.stateId() == first);

    CHECK_FALSE(stack.push(componentStep("drag", 1, "c", "d", "rpc:agent")).merged); // another origin
    CHECK_FALSE(stack.push(componentStep("", 1, "d", "e")).merged);
    CHECK(stack.steps().size() == 3);
    stack.stepBack();
    CHECK(stack.position() == 2);
    REQUIRE(stack.redoStep() != nullptr);
    stack.push(componentStep("drag", 2, "x", "y"));
    CHECK(stack.steps().size() == 3); // the undone step is gone
    CHECK(stack.redoStep() == nullptr);
    stack.seal();
    CHECK_FALSE(stack.push(componentStep("drag", 2, "y", "z")).merged);

    UndoStack small(1500);
    usize trimmed = 0;
    for (int i = 0; i < 20; ++i)
    {
        trimmed += small.push(componentStep("", 1, std::string(100, 'a'), std::string(100, 'b'))).trimmed;
    }
    CHECK(trimmed > 0);
    CHECK(small.bytes() <= 1500);
    CHECK(small.steps().size() == 20 - trimmed);
    CHECK(small.position() == small.steps().size());
    const u64 before = small.stateId();
    CHECK(small.clear() == 20 - trimmed);
    CHECK(small.stateId() != before);
}

TEST_CASE("the change journal keeps changes by revision and drops whole revisions")
{
    ChangeJournal journal(4);
    journal.record(EntityGuid{1}, ChangeKind::Created);
    journal.record(EntityGuid{1}, ChangeKind::Created);
    CHECK(journal.endRevision() == 1);
    CHECK(journal.endRevision() == 1); // nothing pending
    journal.record(EntityGuid{1}, ChangeKind::Changed, "illusion.Name");
    journal.record(EntityGuid{2}, ChangeKind::Changed, "illusion.Name");
    journal.endRevision();
    std::vector<EntityChange> changes;
    REQUIRE(journal.changesSince(0, changes));
    REQUIRE(changes.size() == 3);
    CHECK(changes[0].kind == ChangeKind::Created);
    CHECK(changes[2].revision == 2);
    changes.clear();
    REQUIRE(journal.changesSince(1, changes));
    CHECK(changes.size() == 2);

    for (u64 i = 0; i < 3; ++i)
    {
        journal.record(EntityGuid{10 + i}, ChangeKind::Destroyed);
        journal.endRevision();
    }
    changes.clear();
    CHECK_FALSE(journal.changesSince(0, changes)); // revision 1 was dropped
    CHECK(journal.changesSince(journal.revision() - 1, changes));
    CHECK_FALSE(journal.changesSince(journal.revision() + 1, changes));
    journal.reset();
    CHECK_FALSE(journal.changesSince(journal.revision() - 1, changes));
    CHECK(changeKindName(ChangeKind::Destroyed) == "destroyed");
}

TEST_CASE("the event hub delivers each topic once per flush")
{
    EventHub hub;
    std::vector<std::string> seen;
    const u32 all =
        hub.subscribe("*", [&](const EditorEvent& event) { seen.push_back(event.topic + event.payload); });
    hub.subscribe("scene.changed",
                  [&](const EditorEvent& event) { seen.push_back("only " + event.payload); });
    hub.post("scene.changed", "1");
    hub.post("selection.changed", "x");
    hub.post("scene.changed", "2");
    CHECK(hub.pending() == 2);
    hub.flush();
    CHECK(seen == std::vector<std::string>{"scene.changed2", "only 2", "selection.changedx"});
    CHECK(hub.unsubscribe(all));
    CHECK_FALSE(hub.unsubscribe(all));
    seen.clear();
    hub.post("selection.changed", "y");
    hub.flush();
    CHECK(seen.empty());
}

TEST_CASE("generated schemas describe the JSON encoding and validate values")
{
    test::reflectEditorTestComponents();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const TypeReflection& transform = *registry.reflection(registry.findByStableName("illusion.Transform"));
    JsonMutDocument doc;
    doc.setRoot(writeTypeSchema(doc, transform));
    std::string text;
    REQUIRE(doc.write(text, JsonWriteOptions{.pretty = false}));
    const JsonDocument schema = test::parseJson(text);
    CHECK(schema.root().find("additionalProperties").asBool() == false);
    CHECK(schema.root().find("required").size() == 3);

    CHECK(validateJson(schema.root(),
                       test::parseJson(R"({"position":[0,1,2],"rotation":[0,0,0,1],"scale":[1,1,1]})").root())
              .empty());
    // Units accept tagged numbers.
    const std::vector<ValueError> wrong =
        validateJson(schema.root(), test::parseJson(R"({"position":[0,1],"rotation":"x","extra":1})").root());
    REQUIRE(wrong.size() == 4);
    CHECK(wrong[0].pointer == "/position");
    CHECK(wrong[1].pointer == "/rotation");
    CHECK(wrong[2].pointer == "/extra");
    CHECK(wrong[3].pointer == "/scale");

    const TypeReflection& link = *registry.reflection(registry.findByStableName("test.Link"));
    JsonMutDocument linkDoc;
    linkDoc.setRoot(writeTypeSchema(linkDoc, link));
    std::string linkText;
    REQUIRE(linkDoc.write(linkText));
    const JsonDocument linkSchema = test::parseJson(linkText);
    CHECK(validateJson(linkSchema.root(), test::parseJson(R"({"target":null,"weight":5})").root()).empty());
    CHECK(validateJson(linkSchema.root(),
                       test::parseJson(R"({"target":"9f2c41aa0b7d3e15","weight":101})").root())
              .size() == 1);
    CHECK(validateJson(linkSchema.root(), test::parseJson(R"({"target":3,"weight":-1})").root()).size() == 2);
}

TEST_CASE("command errors carry codes, pointers and suggestions")
{
    CHECK(static_cast<int>(ErrorKind::NotFound) == 1200);
    CHECK(errorKindName(ErrorKind::PreconditionFailed) == "precondition_failed");
    CHECK(CommandError::make(ErrorKind::Conflict, "x").retryable);
    CommandError error = withPointerPrefix(CommandError::invalid("/x", "must be a number"), "/value");
    CHECK(error.errors[0].pointer == "/value/x");
    CHECK(error.message == "'/value/x' must be a number");
    JsonMutDocument doc;
    doc.setRoot(writeCommandError(doc, error));
    std::string text;
    REQUIRE(doc.write(text, JsonWriteOptions{.pretty = false}));
    CHECK(
        text ==
        R"({"code":1202,"message":"'/value/x' must be a number","data":{"kind":"validation_failed",)"
        R"("errors":[{"path":"/value/x","message":"must be a number"}],"suggestions":[],"retryable":false}})");
    const std::vector<std::string> matches =
        closestMatches("entity.craete", {"entity.create", "entity.destroy", "scene.create_thing", "zzz"});
    REQUIRE_FALSE(matches.empty());
    CHECK(matches[0] == "entity.create");
}

TEST_CASE("operations round-trip through the journal encoding and invert")
{
    const Operation change =
        ComponentChange{EntityGuid{0x1234}, "illusion.Name", std::nullopt, R"({"value":"A"})"};
    const Operation spawn =
        EntityLifetime{{EntityRecord{EntityGuid{5}, R"({"illusion.Name":{"value":"B"}})"}}, true};
    const Operation file = FileChange{FileChange::Kind::Move, "assets:/a", "assets:/b", {}, {}};
    const Operation settings =
        SettingsChange{"test.Difficulty", R"({"enemies":2})", std::nullopt, 1u, std::nullopt};
    for (const Operation& operation : {change, spawn, file, settings})
    {
        JsonMutDocument doc;
        doc.setRoot(writeOperation(doc, operation));
        std::string text;
        REQUIRE(doc.write(text));
        const JsonDocument parsed = test::parseJson(text);
        Expected<Operation, std::string> read = readOperation(parsed.root());
        REQUIRE(read.hasValue());
        CHECK(read->index() == operation.index());
        CHECK(operationBytes(*read) == operationBytes(operation));
    }
    CHECK(std::get<ComponentChange>(inverted(change)).before == R"({"value":"A"})");
    CHECK_FALSE(std::get<EntityLifetime>(inverted(spawn)).spawn);
    CHECK(std::get<FileChange>(inverted(file)).path == "assets:/b");
    const SettingsChange undone = std::get<SettingsChange>(inverted(settings));
    CHECK(undone.after == R"({"enemies":2})");
    CHECK(undone.afterVersion == 1u);
    CHECK_FALSE(undone.before.has_value());
    CHECK_FALSE(undone.beforeVersion.has_value());
    JsonMutDocument settingsDoc;
    settingsDoc.setRoot(writeOperation(settingsDoc, settings));
    std::string settingsText;
    REQUIRE(settingsDoc.write(settingsText));
    const Expected<Operation, std::string> settingsRead = readOperation(test::parseJson(settingsText).root());
    REQUIRE(settingsRead.hasValue());
    CHECK(std::get<SettingsChange>(*settingsRead).beforeVersion == 1u);
    CHECK_FALSE(
        readOperation(
            test::parseJson(R"({"op":"settings","type":"t","before":null,"after":null,"afterVersion":-1})")
                .root())
            .hasValue());
    CHECK_FALSE(readOperation(test::parseJson(R"({"op":"nope"})").root()).hasValue());
    CHECK_FALSE(readOperation(test::parseJson(R"({"op":"component","entity":"x"})").root()).hasValue());
}

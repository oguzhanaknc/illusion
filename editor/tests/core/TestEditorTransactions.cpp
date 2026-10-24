#include "EditorCoreTestSupport.hpp"

#include <illusion/core/Log.hpp>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// Options of a second client (an RPC connection) next to the default caller 0.
CommandOptions client(u64 caller, std::string_view origin)
{
    CommandOptions options;
    options.caller = caller;
    options.origin = origin;
    return options;
}

// Refuses commands of one permission class, like the RPC server's permission modes.
class DenyFiles final : public CommandGate
{
public:
    Expected<void, CommandError> check(const CommandDescriptor& command) const override
    {
        if (command.info.permission == PermissionClass::Files)
        {
            return unexpected(CommandError::make(ErrorKind::PermissionDenied, "files are not allowed"));
        }
        return {};
    }
};

Expected<NoResult, CommandError> ping(CommandContext&, const NoParams&)
{
    return NoResult{};
}

} // namespace

TEST_CASE("a transaction makes one undo step of its commands, which undo and redo together")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    ok(editor, "transaction.begin", R"({"label":"Build"})");
    REQUIRE(editor.transaction() != nullptr);
    const u64 revision = editor.revision();
    const JsonDocument created = ok(editor, "entity.create", R"({"name":"A","parent":"/Game"})");
    CHECK(created.root().pointer("/_editor/transaction").asU64() == 0u);
    CHECK(editor.revision() == revision + 1);
    ok(editor, "entity.rename", R"({"entity":"/Game/A","name":"B"})");
    ok(editor, "entity.destroy", R"({"entities":["/Light"]})");
    CHECK(editor.history().steps().empty());
    CHECK(editor.dirty());
    CHECK(editor.transaction()->commands == 3);

    const JsonDocument committed = ok(editor, "transaction.commit");
    CHECK(committed.root().pointer("/commands").asU64() == 3u);
    CHECK(committed.root().pointer("/position").asU64() == 1u);
    CHECK(committed.root().pointer("/_editor/transaction").asU64() == editor.history().steps()[0].id);
    CHECK(editor.transaction() == nullptr);
    REQUIRE(editor.history().steps().size() == 1);
    CHECK(editor.history().steps()[0].label == "Build");
    CHECK(editor.history().steps()[0].origin == "ui");
    const std::string after = fixture.scene();

    REQUIRE(editor.undo().value() == 1);
    CHECK(fixture.scene() == before);
    REQUIRE(editor.redo().value() == 1);
    CHECK(fixture.scene() == after);
}

TEST_CASE("cancelling a transaction reverts its changes and the selection and leaves the history alone")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "selection.set", R"({"entities":["/Light"]})");
    const std::string before = fixture.scene();
    ok(editor, "transaction.begin");
    ok(editor, "entity.create", R"({"name":"A"})");
    ok(editor, "selection.set", R"({"entities":["/A"]})");
    ok(editor, "component.set",
       R"({"entity":"/Game/Board","type":"illusion.LocalTransform","value":{"position":[5,5,5]}})");
    const JsonDocument cancelled = ok(editor, "transaction.cancel");
    CHECK(cancelled.root().pointer("/commands").asU64() == 2u);
    CHECK(editor.transaction() == nullptr);
    CHECK(fixture.scene() == before);
    CHECK(editor.history().steps().empty());
    CHECK_FALSE(editor.dirty());
    CHECK(ok(editor, "selection.get").root().pointer("/entities/0/path").asString() == "/Light");
}

TEST_CASE("an empty transaction commits no undo step")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "transaction.begin");
    ok(editor, "scene.info");
    const JsonDocument committed = ok(editor, "transaction.commit");
    CHECK(committed.root().pointer("/_editor/transaction").asU64() == 0u);
    CHECK(editor.history().steps().empty());
}

TEST_CASE("while a transaction is open other callers cannot change the scene")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const CommandOptions agent = client(7, "rpc:agent");
    const CommandOptions other = client(8, "rpc:other");
    ok(editor, "transaction.begin", R"({"label":"Mine"})", agent);

    const CommandError busy = test::run(editor, "entity.create", R"({"name":"X"})", other).error();
    CHECK(busy.kind == ErrorKind::Busy);
    CHECK(busy.retryable);
    CHECK(test::run(editor, "transaction.begin", "{}", other).error().kind == ErrorKind::Busy);
    CHECK(test::run(editor, "transaction.commit", "{}", other).error().kind == ErrorKind::PreconditionFailed);
    CHECK(test::run(editor, "editor.batch", R"({"steps":[{"method":"scene.info"}]})", other).error().kind ==
          ErrorKind::Busy);
    // Reading and selecting are fine.
    ok(editor, "scene.tree", "{}", other);
    ok(editor, "selection.set", R"({"entities":["/Light"]})", other);
    // Commands that replace or save the scene or move through history wait for everyone.
    const CommandError save = test::run(editor, "scene.save", "{}", agent).error();
    CHECK(save.kind == ErrorKind::PreconditionFailed);
    CHECK_FALSE(save.retryable);
    CHECK(test::run(editor, "history.undo", "{}", other).error().kind == ErrorKind::Busy);
    CHECK(test::run(editor, "transaction.begin", "{}", agent).error().kind == ErrorKind::PreconditionFailed);
    CHECK(editor.undo().error().kind == ErrorKind::PreconditionFailed);
    CHECK(editor.saveScene().error().kind == ErrorKind::PreconditionFailed);

    ok(editor, "entity.create", R"({"name":"Mine"})", agent);
    ok(editor, "transaction.commit", "{}", agent);
    ok(editor, "entity.create", R"({"name":"X"})", other);
    CHECK(editor.history().steps().size() == 2);
}

TEST_CASE("a transaction left by its caller is cancelled only for that caller")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    ok(editor, "transaction.begin", "{}", client(3, "rpc:a"));
    ok(editor, "entity.create", R"({"name":"A"})", client(3, "rpc:a"));
    CHECK_FALSE(editor.abandonTransaction(4));
    CHECK(editor.transaction() != nullptr);
    CHECK(editor.abandonTransaction(3));
    CHECK(editor.transaction() == nullptr);
    CHECK(fixture.scene() == before);
}

TEST_CASE("opening another scene ends an open transaction")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    REQUIRE(editor.beginTransaction("Open", {}).hasValue());
    ok(editor, "entity.create", R"({"name":"A"})");
    editor.newScene();
    CHECK(editor.transaction() == nullptr);
    CHECK_FALSE(editor.dirty());
}

TEST_CASE("the crash journal of a cancelled transaction recovers the scene before it")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    ok(editor, "transaction.begin");
    ok(editor, "entity.create", R"({"name":"A","parent":"/Game"})");
    ok(editor, "entity.destroy", R"({"entities":["/Light"]})");
    ok(editor, "transaction.cancel");
    ok(editor, "entity.rename", R"({"entity":"/Game","name":"Root"})");
    editor.endFrame(0.0);
    const std::string expected = fixture.scene();

    // A second editor on the same project finds this session's journal as a crashed one.
    EditorContext second(EditorContextOptions{.seed = 2});
    REQUIRE(second.openProject(fixture.directory.path / "project").hasValue());
    REQUIRE(second.recover().hasValue());
    const Expected<std::string, CommandError> recovered = second.sceneJson();
    REQUIRE(recovered.hasValue());
    CHECK(*recovered == expected);
    CHECK(before != expected);
}

TEST_CASE("editor.batch runs its steps as one undo step and passes results on with $ references")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    const JsonDocument result = ok(editor, "editor.batch", R"({"label":"Board","steps":[
        {"as":"board","method":"entity.create","params":{"name":"Field","parent":"/Game"}},
        {"as":"cells","method":"entity.create","params":{"name":"Cell","parent":"$board.entity.id"}},
        {"method":"entity.rename","params":{"entity":"$cells.entity.path","name":"$$money"}},
        {"method":"selection.set","params":{"entities":["$board.entity.id"]}},
        {"method":"scene.info"}]})");
    CHECK(result.root().pointer("/steps").size() == 5);
    CHECK(result.root().pointer("/failed").asU64() == 0u);
    CHECK(result.root().pointer("/_editor/operations").asU64() > 0u);
    CHECK(result.root().pointer("/_editor/transaction").asU64() == editor.history().steps()[0].id);
    CHECK(ok(editor, "entity.get", R"({"entity":"/Game/Field/$money"})")
              .root()
              .pointer("/entity/name")
              .asString() == "$money");
    REQUIRE(editor.history().steps().size() == 1);
    CHECK(editor.history().steps()[0].label == "Board");
    CHECK(editor.selection().size() == 1);
    REQUIRE(editor.undo().value() == 1);
    CHECK(fixture.scene() == before);
}

TEST_CASE("a failing step reverts an atomic batch and is reported by a non-atomic one")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    const u64 revision = editor.revision();
    const CommandError error = fails(editor, "editor.batch", R"({"steps":[
        {"method":"entity.create","params":{"name":"A"}},
        {"method":"entity.rename","params":{"entity":"/Nope","name":"B"}}]})");
    CHECK(error.kind == ErrorKind::NotFound);
    CHECK(error.message.starts_with("step 1 (entity.rename) failed: "));
    CHECK(fixture.scene() == before);
    CHECK(editor.history().steps().empty());
    CHECK(editor.transaction() == nullptr);
    CHECK(editor.revision() > revision);

    const JsonDocument partial = ok(editor, "editor.batch", R"({"atomic":false,"steps":[
        {"method":"entity.create","params":{"name":"A"}},
        {"method":"entity.rename","params":{"entity":"/Nope","name":"B"}},
        {"method":"entity.create","params":{"name":"C"}}]})");
    CHECK(partial.root().pointer("/failed").asU64() == 1u);
    CHECK(partial.root().pointer("/steps/1/error/code").asU64() == 1200u);
    CHECK(partial.root().pointer("/steps/1/result").isNull());
    CHECK_FALSE(partial.root().pointer("/steps/2/result").isAbsent());
    ok(editor, "entity.get", R"({"entity":"/A"})");
    ok(editor, "entity.get", R"({"entity":"/C"})");
    CHECK(editor.history().steps().size() == 1);
}

TEST_CASE("a dry batch changes nothing, not even the revision")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "selection.set", R"({"entities":["/Light"]})");
    const std::string before = fixture.scene();
    const u64 revision = editor.revision();
    const JsonDocument result = ok(editor, "editor.batch", R"({"dryRun":true,"steps":[
        {"as":"a","method":"entity.create","params":{"name":"A"}},
        {"method":"component.add","params":{"entity":"$a.entity.id","type":"test.Link"}},
        {"method":"selection.set","params":{"entities":["/Game"]}}]})");
    CHECK(result.root().pointer("/_editor/dryRun").asBool() == true);
    CHECK(fixture.scene() == before);
    CHECK(editor.revision() == revision);
    CHECK(editor.history().steps().empty());
    CHECK(editor.transaction() == nullptr);
    CHECK(ok(editor, "selection.get").root().pointer("/entities/0/path").asString() == "/Light");
}

TEST_CASE("a batch inside an open transaction joins it, and a failing atomic batch reverts only itself")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "transaction.begin", R"({"label":"Outer"})");
    ok(editor, "entity.create", R"({"name":"Kept"})");
    const std::string kept = fixture.scene();
    fails(editor, "editor.batch", R"({"steps":[
        {"method":"entity.create","params":{"name":"Gone"}},
        {"method":"entity.get","params":{"entity":"/Nope"}}]})");
    CHECK(fixture.scene() == kept);
    REQUIRE(editor.transaction() != nullptr);
    ok(editor, "editor.batch", R"({"steps":[{"method":"entity.create","params":{"name":"Also"}}]})");
    CHECK(editor.history().steps().empty());
    ok(editor, "transaction.commit");
    REQUIRE(editor.history().steps().size() == 1);
    CHECK(editor.history().steps()[0].label == "Outer");
}

TEST_CASE("editor.batch validates every step before running any")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    const auto invalid = [&](std::string_view params, std::string_view pointer) {
        CAPTURE(params);
        const CommandError error = fails(editor, "editor.batch", params);
        REQUIRE_FALSE(error.errors.empty());
        CHECK(error.errors[0].pointer == pointer);
        return error;
    };
    invalid(R"({"steps":[]})", "/steps");
    invalid(R"({"steps":[{"method":"entity.create","params":{"name":"A"}},{"method":"entity.craete"}]})",
            "/steps/1/method");
    invalid(R"({"steps":[{"method":"editor.wait","params":{"frames":1}}]})", "/steps/0/method");
    invalid(R"({"steps":[{"method":"editor.batch","params":{"steps":[]}}]})", "/steps/0/method");
    invalid(R"({"steps":[{"method":"transaction.commit"}]})", "/steps/0/method");
    invalid(R"({"steps":[{"as":"a b","method":"scene.info"}]})", "/steps/0/as");
    invalid(R"({"steps":[{"as":"selection","method":"scene.info"}]})", "/steps/0/as");
    invalid(R"({"steps":[{"as":"x","method":"scene.info"},{"as":"x","method":"scene.info"}]})",
            "/steps/1/as");
    invalid(R"({"steps":[{"method":"scene.info","params":[1]}]})", "/steps/0/params");
    invalid(R"({"label":"","steps":[{"method":"scene.info"}]})", "/label");
    const CommandError unknown = invalid(
        R"({"steps":[{"as":"board","method":"entity.create","params":{"name":"A"}},
            {"method":"entity.get","params":{"entity":"$baord.entity.id"}}]})",
        "/steps/1/params/entity");
    CHECK(unknown.suggestions == std::vector<std::string>{"board"});
    invalid(R"({"steps":[{"as":"board","method":"entity.create","params":{"name":"A"}},
            {"method":"entity.get","params":{"entity":"$board.entity.nope"}}]})",
            "/steps/1/params/entity");
    CHECK(fixture.scene() == before);
    CHECK(editor.history().steps().empty());
}

TEST_CASE("the gate of a call also decides about each step of a batch")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const DenyFiles gate;
    CommandOptions options;
    options.gate = &gate;
    const CommandError error = test::run(editor, "editor.batch", R"({"steps":[
        {"method":"entity.create","params":{"name":"A"}},
        {"method":"project.file_write","params":{"path":"assets:/x.txt","text":"x"}}]})",
                                         options)
                                   .error();
    CHECK(error.kind == ErrorKind::PermissionDenied);
    CHECK(editor.history().steps().empty());
    CHECK(test::run(editor, "project.file_write", R"({"path":"assets:/x.txt","text":"x"})", options)
              .error()
              .kind == ErrorKind::PermissionDenied);
}

TEST_CASE("undo of my own steps skips the independent later steps of others")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const CommandOptions me = client(1, "rpc:me");
    const CommandOptions other = client(2, "rpc:other");
    ok(editor, "component.set",
       R"({"entity":"/Game/Board","type":"illusion.LocalTransform","value":{"position":[1,1,1]}})", me);
    ok(editor, "entity.rename", R"({"entity":"/Light","name":"Lamp"})", other);
    ok(editor, "entity.create", R"({"name":"Theirs"})", other);

    const JsonDocument dry = ok(editor, "history.undo", R"({"scope":"mine","dryRun":true})", me);
    CHECK(dry.root().pointer("/steps").asU64() == 1u);
    CHECK(editor.history().position() == 3);

    const JsonDocument undone = ok(editor, "history.undo", R"({"scope":"mine"})", me);
    CHECK(undone.root().pointer("/steps").asU64() == 1u);
    const JsonDocument board =
        ok(editor, "component.get", R"({"entity":"/Game/Board","type":"illusion.LocalTransform"})");
    CHECK(board.root().pointer("/value/position/1").asF64() == doctest::Approx(2.0));
    ok(editor, "entity.get", R"({"entity":"/Lamp"})");
    ok(editor, "entity.get", R"({"entity":"/Theirs"})");
    CHECK(editor.history().position() == 2);
    // Nothing of mine is left to undo; redo applies my step again.
    CHECK(ok(editor, "history.undo", R"({"scope":"mine"})", me).root().pointer("/steps").asU64() == 0u);
    REQUIRE(editor.redo().value() == 1);
    const JsonDocument redone =
        ok(editor, "component.get", R"({"entity":"/Game/Board","type":"illusion.LocalTransform"})");
    CHECK(redone.root().pointer("/value/position/1").asF64() == doctest::Approx(1.0));
}

TEST_CASE("undo of my own steps refuses when a later step of another origin touched the same things")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const CommandOptions me = client(1, "rpc:me");
    const CommandOptions other = client(2, "rpc:other");
    ok(editor, "entity.create", R"({"name":"Mine"})", me);
    ok(editor, "component.add", R"({"entity":"/Mine","type":"test.Link"})", other);
    const std::string before = fixture.scene();
    const CommandError conflict = test::run(editor, "history.undo", R"({"scope":"mine"})", me).error();
    CHECK(conflict.kind == ErrorKind::Conflict);
    CHECK(conflict.message.find("entity") != std::string::npos);
    CHECK(test::run(editor, "history.undo", R"({"scope":"mine","dryRun":true})", me).error().kind ==
          ErrorKind::Conflict);
    CHECK(fixture.scene() == before);
    CHECK(editor.history().position() == 2);
    CHECK(fails(editor, "history.undo", R"({"scope":"yours"})").errors[0].pointer == "/scope");
}

TEST_CASE("operations overlap on shared Components, entities, references, files and settings")
{
    const EntityGuid a{0xa1};
    const EntityGuid b{0xb2};
    const std::string aText = '"' + std::string(formatGuid(a).data(), kGuidTextLength) + '"';
    const auto set = [](EntityGuid entity, std::string type, std::string value = "{}") {
        return Operation{ComponentChange{entity, std::move(type), std::nullopt, std::move(value)}};
    };
    const auto spawn = [](EntityGuid entity) {
        return Operation{EntityLifetime{{EntityRecord{entity, "{}"}}, true}};
    };
    const auto file = [](std::string path) {
        return Operation{FileChange{FileChange::Kind::Replace, std::move(path), "", std::nullopt, "blob"}};
    };
    const std::vector<Operation> linkToA{set(b, "test.Link", R"({"target":)" + aText + "}")};

    CHECK(findOverlap(std::vector{set(a, "x.A")}, std::vector{set(a, "x.A")}).has_value());
    CHECK_FALSE(findOverlap(std::vector{set(a, "x.A")}, std::vector{set(a, "x.B")}).has_value());
    CHECK_FALSE(findOverlap(std::vector{set(a, "x.A")}, std::vector{set(b, "x.A")}).has_value());
    CHECK(findOverlap(std::vector{spawn(a)}, std::vector{set(a, "x.B")}).has_value());
    CHECK(findOverlap(std::vector{set(a, "x.B")}, std::vector{spawn(a)}).has_value());
    CHECK(findOverlap(std::vector{spawn(a)}, linkToA).value().find("reference") != std::string::npos);
    CHECK_FALSE(findOverlap(std::vector{spawn(b)}, std::vector{spawn(a)}).has_value());
    CHECK(findOverlap(std::vector{file("assets:/x")}, std::vector{file("assets:/x/y.txt")}).has_value());
    CHECK_FALSE(findOverlap(std::vector{file("assets:/x")}, std::vector{file("assets:/xy.txt")}).has_value());
    SettingsChange difficulty;
    difficulty.type = "game.Difficulty";
    difficulty.after = "{}";
    const Operation settings{difficulty};
    CHECK(findOverlap(std::vector{settings}, std::vector{settings}).value() == "setting game.Difficulty");
    CHECK_FALSE(findOverlap(std::vector<Operation>{}, std::vector{settings}).has_value());
}

TEST_CASE("async commands start, advance once per update with progress, and finish with their result")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const JsonDocument params = test::parseJson(R"({"frames":3})");
    std::unique_ptr<RunningCommand> running = editor.startJson("editor.wait", params.root()).value();
    CHECK(running->command().info.flags.async);
    CHECK(running->frames() == 0);
    CHECK_FALSE(running->update().value().has_value());
    CHECK(running->progress().done == doctest::Approx(1.0));
    CHECK(running->progress().total == doctest::Approx(3.0));
    CHECK_FALSE(running->update().value().has_value());
    const std::optional<std::string> finished = running->update().value();
    REQUIRE(finished.has_value());
    const JsonDocument result = test::parseJson(*finished);
    CHECK(result.root().pointer("/frames").asU64() == 3u);
    CHECK(result.root().pointer("/_editor/dryRun").asBool() == false);
    CHECK(running->frames() == 3);

    // A dry run validates and finishes at once.
    const JsonDocument dry = test::parseJson(R"({"frames":5,"dryRun":true})");
    std::unique_ptr<RunningCommand> trial = editor.startJson("editor.wait", dry.root()).value();
    REQUIRE(trial->update().value().has_value());
    CHECK(trial->frames() == 0);
}

TEST_CASE("async and synchronous commands refuse the other entry point")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const JsonDocument params = test::parseJson(R"({"frames":1})");
    CHECK(editor.executeJson("editor.wait", params.root()).error().kind == ErrorKind::Unsupported);
    const JsonDocument none = test::parseJson("{}");
    CHECK(editor.startJson("scene.info", none.root()).error().kind == ErrorKind::Unsupported);
    CHECK(editor.startJson("scene.nope", none.root()).error().kind == ErrorKind::NotFound);
    const JsonDocument zero = test::parseJson(R"({"frames":0})");
    CHECK(editor.startJson("editor.wait", zero.root()).error().errors[0].pointer == "/frames");
    // Destroying a running command cancels it.
    std::unique_ptr<RunningCommand> running = editor.startJson("editor.wait", params.root()).value();
    running.reset();
    ok(editor, "scene.info");
}

TEST_CASE("registering a command posts commands.changed at the end of the frame")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    std::vector<std::string> payloads;
    const u32 subscription = editor.events().subscribe(
        "commands.changed", [&](const EditorEvent& event) { payloads.push_back(event.payload); });
    editor.endFrame(0.0);
    CHECK(payloads.empty());
    const usize count = editor.commands().commands().size();
    CommandRegistry& registry = editor.commands();
    const u64 revision = registry.revision();
    registry.add<&ping>(
        CommandInfo{.name = "test.ping", .agentSummary = "Answers. Example: {}.", .category = "Test"});
    CHECK(registry.revision() == revision + 1);
    editor.endFrame(0.0);
    REQUIRE(payloads.size() == 1);
    CHECK(payloads[0] == "{\"commands\":" + std::to_string(count + 1) + "}");
    editor.endFrame(0.0);
    CHECK(payloads.size() == 1);
    ok(editor, "test.ping");
    editor.events().unsubscribe(subscription);
}

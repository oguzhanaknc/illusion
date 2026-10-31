#include "EditorCommandCases.hpp"
#include "EditorCoreTestSupport.hpp"

#include <illusion/editor/core/JsonSchema.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// The fixture scene with its assets, a full clipboard, one undo step and a journal of a crashed session, so
// that every command has something to work on.
void prepare(EditorFixture& fixture)
{
    fixture.buildScene();
    fixture.addAssets();
    ok(*fixture.editor, "clipboard.copy", R"({"entities":["/Game/Board"]})");
    ok(*fixture.editor, "component.copy", R"({"entity":"/Game/Player","type":"test.Link"})");
    ok(*fixture.editor, "entity.rename", R"({"entity":"/Game/Board/Cell 2","name":"Cell 3"})");
    const std::filesystem::path journal =
        fixture.directory.path / "project" / ".illusion" / "journal" / "crashed.jsonl";
    std::ofstream out(journal, std::ios::binary);
    out << R"({"type":"session","session":"00000000000000ab","format":1})" << '\n'
        << R"({"type":"base","scene":"assets:/scenes/main.scene"})" << '\n';
}

struct State
{
    std::string scene;
    std::vector<EntityGuid> selection;
    usize position = 0;
    usize steps = 0;
    u64 revision = 0;
    bool dirty = false;
    std::map<std::string, std::string> files;

    friend bool operator==(const State&, const State&) = default;
};

State capture(EditorFixture& fixture)
{
    EditorContext& editor = *fixture.editor;
    State state;
    state.scene = fixture.scene();
    state.selection.assign(editor.selection().items().begin(), editor.selection().items().end());
    state.position = editor.history().position();
    state.steps = editor.history().steps().size();
    state.revision = editor.revision();
    state.dirty = editor.dirty();
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(fixture.directory.path / "project"))
    {
        const std::string name =
            entry.path().lexically_relative(fixture.directory.path / "project").generic_string();
        if (name.starts_with(".illusion/journal") || name.starts_with(".illusion/trash"))
        {
            continue; // the editor's own bookkeeping
        }
        std::string contents;
        if (entry.is_regular_file())
        {
            std::ifstream in(entry.path(), std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            contents = text.str();
        }
        state.files[name] = contents;
    }
    return state;
}

std::string withDryRun(std::string_view params)
{
    return params == "{}" ? std::string(R"({"dryRun":true})")
                          : R"({"dryRun":true,)" + std::string(params.substr(1));
}

JsonDocument schemaOf(const CommandDescriptor& command, bool result)
{
    JsonMutDocument doc;
    doc.setRoot(result ? writeCommandResultSchema(doc, command) : writeCommandParamsSchema(doc, command));
    std::string text;
    REQUIRE(doc.write(text, JsonWriteOptions{.pretty = false}));
    return test::parseJson(text);
}

std::string describeErrors(const std::vector<ValueError>& errors)
{
    std::string out;
    for (const ValueError& error : errors)
    {
        out += error.pointer + " " + error.message + "; ";
    }
    return out;
}

std::filesystem::path goldenPath()
{
    return utf8Path(ILLUSION_EDITOR_TEST_DATA) / "commands" / "catalog.json";
}

} // namespace

TEST_CASE("every core command has a test case")
{
    EditorContext editor(EditorContextOptions{.seed = 1});
    std::set<std::string> registered;
    for (const CommandDescriptor* command : editor.commands().commands())
    {
        registered.emplace(command->info.name);
    }
    std::set<std::string> cases;
    for (const test::CommandCase& entry : test::kCommandCases)
    {
        cases.emplace(entry.name);
    }
    CHECK(registered == cases);
    CHECK(registered.size() == 153);
}

TEST_CASE("every command succeeds on the fixture and its params and result match their schemas")
{
    for (const test::CommandCase& entry : test::kCommandCases)
    {
        CAPTURE(entry.name);
        EditorFixture fixture;
        prepare(fixture);
        if (const std::string_view setup = test::setupOf(entry.name); !setup.empty())
        {
            ok(*fixture.editor, setup);
        }
        const CommandDescriptor& command = *fixture.editor->commands().find(entry.name);
        const JsonDocument params = test::parseJson(entry.params);
        const std::vector<ValueError> paramErrors =
            validateJson(schemaOf(command, false).root(), params.root());
        CHECK_MESSAGE(paramErrors.empty(), describeErrors(paramErrors));
        const JsonDocument result = ok(*fixture.editor, entry.name, entry.params);
        const std::vector<ValueError> resultErrors =
            validateJson(schemaOf(command, true).root(), result.root());
        CHECK_MESSAGE(resultErrors.empty(), describeErrors(resultErrors));
    }
}

TEST_CASE("dry runs validate and change nothing")
{
    for (const test::CommandCase& entry : test::kCommandCases)
    {
        CAPTURE(entry.name);
        EditorFixture fixture;
        prepare(fixture);
        if (const std::string_view setup = test::setupOf(entry.name); !setup.empty())
        {
            ok(*fixture.editor, setup);
        }
        const State before = capture(fixture);
        const JsonDocument result = ok(*fixture.editor, entry.name, withDryRun(entry.params));
        CHECK(result.root().pointer("/_editor/dryRun").asBool() == true);
        const State after = capture(fixture);
        CHECK(after.scene == before.scene);
        CHECK(after.files == before.files);
        CHECK(after == before);
    }
}

TEST_CASE("the command catalogue matches its golden file")
{
    EditorContext editor(EditorContextOptions{.seed = 1});
    JsonMutDocument doc;
    JsonMutValue root = doc.makeObject();
    for (const CommandDescriptor* command : editor.commands().commands())
    {
        JsonMutValue entry = root.addObject(command->info.name);
        entry.add("version", doc.makeU64(command->info.version));
        entry.add("permission", doc.makeString(permissionClassName(command->info.permission)));
        entry.add("mode", doc.makeString(commandModeName(command->info.mode)));
        entry.add("undoable", doc.makeBool(command->info.flags.undoable));
        entry.add("readOnly", doc.makeBool(command->info.flags.readOnly));
        entry.add("params", writeCommandParamsSchema(doc, *command));
        entry.add("result", writeCommandResultSchema(doc, *command));
    }
    doc.setRoot(root);
    std::string text;
    REQUIRE(doc.write(text));
    text += '\n';
    const char* update = std::getenv("ILLUSION_UPDATE_GOLDENS");
    if (update != nullptr && std::string_view(update) == "1")
    {
        std::filesystem::create_directories(goldenPath().parent_path());
        std::ofstream(goldenPath(), std::ios::binary) << text;
    }
    std::ifstream in(goldenPath(), std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing golden file; run with ILLUSION_UPDATE_GOLDENS=1");
    std::ostringstream golden;
    golden << in.rdbuf();
    // A change here changes the protocol: bump the command's version when it is incompatible.
    CHECK_MESSAGE(golden.str() == text,
                  "the catalogue changed; run with ILLUSION_UPDATE_GOLDENS=1 and review");
}

TEST_CASE("params are validated with JSON Pointers, suggestions and reserved options")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;

    const CommandError unknown = fails(editor, "entity.craete", "{}");
    CHECK(unknown.kind == ErrorKind::NotFound);
    REQUIRE_FALSE(unknown.suggestions.empty());
    CHECK(unknown.suggestions[0] == "entity.create");

    const CommandError missing = fails(editor, "entity.rename", R"({"entity":"/Game"})");
    CHECK(missing.kind == ErrorKind::ValidationFailed);
    REQUIRE(missing.errors.size() == 1);
    CHECK(missing.errors[0].pointer == "/name");

    const CommandError wrongType = fails(editor, "entity.rename", R"({"entity":5,"name":"x"})");
    CHECK(wrongType.errors.at(0).pointer == "/entity");

    const CommandError extra = fails(editor, "entity.rename", R"({"entity":"/Game","name":"x","nmae":1})");
    CHECK(extra.errors.at(0).pointer == "/nmae");
    CHECK(extra.suggestions.at(0) == "name");

    CHECK(fails(editor, "scene.info", R"({"x":1})").errors.at(0).pointer == "/x");
    CHECK(fails(editor, "scene.info", "[]").kind == ErrorKind::ValidationFailed);
    CHECK(
        fails(editor, "entity.rename", R"({"entity":"/Game","name":"x","dryRun":1})").errors.at(0).pointer ==
        "/dryRun");
    CHECK(fails(editor, "entity.rename", R"({"entity":"/Game","name":"x","expectRevision":"x"})").kind ==
          ErrorKind::ValidationFailed);

    const std::string revision = std::to_string(editor.revision());
    ok(editor, "entity.rename", R"({"entity":"/Game","name":"G","expectRevision":)" + revision + "}");
    const CommandError conflict = fails(
        editor, "entity.rename", R"({"entity":"/G","name":"H","expectRevision":")" + revision + R"("})");
    CHECK(conflict.kind == ErrorKind::Conflict);
    CHECK(conflict.retryable);
}

TEST_CASE("results report revisions, transactions and warnings")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const u64 revision = editor.revision();
    const JsonDocument created = ok(editor, "entity.create", R"({"name":"A"})");
    CHECK(created.root().pointer("/_editor/revision").asU64() == revision + 1);
    CHECK(created.root().pointer("/_editor/transaction").asU64() > 0);
    CHECK(created.root().pointer("/_editor/operations").asU64() == 1);
    const JsonDocument read = ok(editor, "scene.info");
    CHECK(read.root().pointer("/_editor/transaction").asU64() == 0);
    CHECK(read.root().find("dirty").asBool() == true);

    // Typed calls run the same commands.
    EntityRenameParams params;
    params.entity = "name:A";
    params.name = "B";
    Expected<EntityResult, CommandError> renamed = editor.execute<EntityResult>("entity.rename", params);
    REQUIRE(renamed.hasValue());
    CHECK(renamed->entity.path == "/B");
    CHECK(editor.execute<EntityResult>("entity.nothing", params).error().kind == ErrorKind::NotFound);
}

TEST_CASE("project file commands are undoable and stay inside writable project mounts")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const std::filesystem::path assets = fixture.directory.path / "project" / "assets";

    CHECK(ok(editor, "project.file_write", R"({"path":"assets:/a.txt","text":"one"})")
              .root()
              .find("created")
              .asBool() == true);
    CHECK(ok(editor, "project.file_write", R"({"path":"assets:/a.txt","text":"two"})")
              .root()
              .find("created")
              .asBool() == false);
    CHECK(ok(editor, "project.file_read", R"({"path":"assets:/a.txt"})").root().find("text").asString() ==
          "two");
    ok(editor, "history.undo");
    CHECK(ok(editor, "project.file_read", R"({"path":"assets:/a.txt"})").root().find("text").asString() ==
          "one");
    ok(editor, "history.undo");
    CHECK_FALSE(std::filesystem::exists(assets / "a.txt"));
    ok(editor, "history.redo", R"({"steps":2})");
    CHECK(ok(editor, "project.file_read", R"({"path":"assets:/a.txt"})").root().find("text").asString() ==
          "two");

    ok(editor, "project.file_mkdir", R"({"path":"assets:/dir/sub"})");
    ok(editor, "project.file_write", R"({"path":"assets:/dir/sub/b.txt","text":"b"})");
    ok(editor, "project.file_move", R"({"from":"assets:/dir","to":"assets:/moved"})");
    CHECK(std::filesystem::exists(assets / "moved" / "sub" / "b.txt"));
    ok(editor, "project.file_delete", R"({"path":"assets:/moved"})");
    CHECK_FALSE(std::filesystem::exists(assets / "moved"));
    ok(editor, "history.undo");
    CHECK(std::filesystem::exists(assets / "moved" / "sub" / "b.txt"));
    ok(editor, "history.undo");
    CHECK(std::filesystem::exists(assets / "dir" / "sub" / "b.txt"));

    const JsonDocument listing =
        ok(editor, "project.file_list", R"({"path":"assets:/dir","recursive":true})");
    CHECK(listing.root().find("entries").size() == 2);
    CHECK(listing.root().pointer("/entries/1/path").asString() == "assets:/dir/sub/b.txt");
    CHECK(ok(editor, "project.file_stat", R"({"path":"assets:/nothing"})").root().find("exists").asBool() ==
          false);

    CHECK(fails(editor, "project.file_read", R"({"path":"assets:/nothing"})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "project.file_write", R"({"path":"assets:/../escape.txt","text":""})")
              .errors.at(0)
              .pointer == "/path");
    CHECK(fails(editor, "project.file_write", R"({"path":"other:/x.txt","text":""})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "project.file_write", R"({"path":"assets:/dir","text":""})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "project.file_move", R"({"from":"assets:/a.txt","to":"assets:/dir"})").kind ==
          ErrorKind::AlreadyExists);
    CHECK(fails(editor, "project.file_mkdir", R"({"path":"assets:/dir"})").kind == ErrorKind::AlreadyExists);
    CHECK(fails(editor, "project.file_delete", R"({"path":"assets:/"})").kind == ErrorKind::ValidationFailed);

    EditorContext loose(EditorContextOptions{.seed = 9});
    CHECK(fails(loose, "project.info", "{}").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(loose, "project.file_write", R"({"path":"assets:/x.txt","text":""})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(loose, "project.recover", "{}").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("scene commands open, save and protect unsaved changes")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string saved = fixture.scene();

    ok(editor, "entity.rename", R"({"entity":"/Light","name":"Lamp"})");
    CHECK(ok(editor, "scene.info").root().find("dirty").asBool() == true);
    CHECK(fails(editor, "scene.new", "{}").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "scene.open", R"({"path":"assets:/scenes/main.scene"})").kind ==
          ErrorKind::PreconditionFailed);
    ok(editor, "history.undo");
    CHECK(ok(editor, "scene.info").root().find("dirty").asBool() == false); // back at the saved state

    ok(editor, "scene.new");
    CHECK(editor.world().aliveCount() == 0);
    CHECK(fails(editor, "scene.save", "{}").kind == ErrorKind::PreconditionFailed);
    ok(editor, "scene.open", R"({"path":"assets:/scenes/main.scene"})");
    CHECK(fixture.scene() == saved);
    CHECK(editor.history().steps().empty());

    CHECK(fails(editor, "scene.save_as", R"({"path":"assets:/scenes/main.scene"})").kind ==
          ErrorKind::AlreadyExists);
    ok(editor, "scene.save_as", R"({"path":"assets:/scenes/main.scene","overwrite":true})");
    ok(editor, "scene.save_as", R"({"path":"assets:/scenes/two.scene"})");
    CHECK(editor.document().path() == "assets:/scenes/two.scene");
    CHECK(fails(editor, "scene.save_as", R"({"path":"nope"})").errors.at(0).pointer == "/path");
    CHECK(fails(editor, "scene.open", R"({"path":"assets:/scenes/none.scene"})").kind == ErrorKind::NotFound);
    ok(editor, "project.file_write", R"({"path":"assets:/scenes/bad.scene","text":"{"})");
    ok(editor, "scene.save");
    const CommandError bad =
        fails(editor, "scene.open", R"({"path":"assets:/scenes/bad.scene","discardChanges":true})");
    CHECK(bad.kind == ErrorKind::ValidationFailed);
    CHECK(fixture.scene() == saved); // a broken file leaves the open scene alone

    const JsonDocument tree = ok(editor, "scene.tree");
    REQUIRE(tree.root().find("nodes").size() == 6);
    CHECK(tree.root().pointer("/nodes/0/path").asString() == "/Game");
    CHECK(tree.root().pointer("/nodes/1/path").asString() == "/Game/Board");
    CHECK(tree.root().pointer("/nodes/2/depth").asU64() == 2);
    CHECK(tree.root().pointer("/nodes/5/path").asString() == "/Light");
    CHECK(ok(editor, "scene.tree", R"({"root":"/Game","depth":1})").root().find("nodes").size() == 3);
}

TEST_CASE("scene.query filters by Components, names and parents and pages with a cursor")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const JsonDocument all = ok(editor, "scene.query", R"({"has":["illusion.Hierarchy"]})");
    CHECK(all.root().find("total").asU64() == 5);
    CHECK(all.root().find("next").isNull());

    const JsonDocument first = ok(editor, "scene.query", R"({"has":["illusion.Hierarchy"],"limit":2})");
    CHECK(first.root().find("entities").size() == 2);
    const std::string next(first.root().find("next").asString().value_or(""));
    CHECK(next == "2");
    const JsonDocument second =
        ok(editor, "scene.query", R"({"has":["illusion.Hierarchy"],"limit":2,"cursor":")" + next + R"("})");
    CHECK(second.root().pointer("/entities/0/path").asString() == "/Game/Board/Cell 1");

    CHECK(ok(editor, "scene.query", R"({"without":["illusion.Hierarchy"]})").root().find("total").asU64() ==
          1);
    CHECK(ok(editor, "scene.query", R"({"name":"Cell ?"})").root().find("total").asU64() == 2);
    CHECK(ok(editor, "scene.query", R"({"under":"/Game"})").root().find("total").asU64() == 4);
    CHECK(ok(editor, "entity.find", R"({"name":"*a*"})").root().find("total").asU64() == 3);

    const CommandError type = fails(editor, "scene.query", R"({"has":["illusion.Transfrom"]})");
    CHECK(type.kind == ErrorKind::NotFound);
    CHECK(type.errors.at(0).pointer == "/has/0");
    CHECK(type.suggestions.at(0) == "illusion.Transform");
    CHECK(fails(editor, "scene.query", R"({"limit":0})").errors.at(0).pointer == "/limit");
    CHECK(fails(editor, "scene.query", R"({"cursor":"x"})").errors.at(0).pointer == "/cursor");
    CHECK(fails(editor, "scene.query", R"({"under":"/Nope"})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "entity.find", R"({"name":"*","limit":10001})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("entity commands create, rename, enable, duplicate and destroy with undo")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;

    const JsonDocument created =
        ok(editor, "entity.create",
           R"({"name":"Piece","parent":"/Game/Board","index":0,)"
           R"("components":{"illusion.LocalTransform":{"position":[1,0,0]},"test.Link":{"target":null}}})");
    CHECK(created.root().pointer("/entity/path").asString() == "/Game/Board/Piece");
    CHECK(ok(editor, "hierarchy.children", R"({"entity":"/Game/Board"})")
              .root()
              .pointer("/entities/0/name")
              .asString() == "Piece");

    CHECK(fails(editor, "entity.create", R"({"name":"a/b"})").errors.at(0).pointer == "/name");
    const CommandError type = fails(editor, "entity.create", R"({"components":{"test.Lnk":{}}})");
    CHECK(type.kind == ErrorKind::NotFound);
    CHECK(type.errors.at(0).pointer == "/components/test.Lnk");
    const CommandError value =
        fails(editor, "entity.create", R"({"components":{"test.Link":{"weight":"heavy"}}})");
    CHECK(value.kind == ErrorKind::ValidationFailed);
    CHECK(value.errors.at(0).pointer == "/components/test.Link/weight");
    CHECK(fails(editor, "entity.create", R"({"components":{"illusion.LocalTransform":{}}})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "entity.create", R"({"components":{"illusion.Hierarchy":{}}})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "entity.create", R"({"parent":"/Nope"})").errors.at(0).pointer == "/parent");

    ok(editor, "entity.set_enabled", R"({"entity":"/Game/Board","enabled":false})");
    CHECK(ok(editor, "entity.get", R"({"entity":"/Game/Board"})").root().find("enabled").asBool() == false);
    ok(editor, "history.undo");
    CHECK(ok(editor, "entity.get", R"({"entity":"/Game/Board"})").root().find("enabled").asBool() == true);

    const JsonDocument boardInfo = ok(editor, "entity.get", R"({"entity":"/Game/Board"})");
    const JsonDocument copies = ok(editor, "entity.duplicate", R"({"entities":["/Game/Board"]})");
    const std::string copyId(copies.root().pointer("/entities/0/id").asString().value_or(""));
    CHECK(editor.selection().size() == 1);
    const JsonDocument copy = ok(editor, "entity.get", R"({"entity":")" + copyId + R"("})");
    CHECK(copy.root().find("children").asU64() == 3);
    CHECK(boardInfo.root().pointer("/entity/id").asString() != copyId);
    // The copy sits right after the original.
    CHECK(ok(editor, "hierarchy.children", R"({"entity":"/Game"})")
              .root()
              .pointer("/entities/1/id")
              .asString() == copyId);
    ok(editor, "history.undo");
    CHECK(fails(editor, "entity.get", R"({"entity":")" + copyId + R"("})").kind == ErrorKind::NotFound);

    const JsonDocument get =
        ok(editor, "entity.get", R"({"entity":"/Game/Player","components":["test.Link"]})");
    CHECK(get.root().find("components").size() == 1);
    CHECK(get.root().pointer("/components/test.Link/target").asString() ==
          boardInfo.root().pointer("/entity/id").asString());

    CHECK(fails(editor, "entity.rename", R"({"entity":"/Light","name":"a/b"})").errors.at(0).pointer ==
          "/name");
    ok(editor, "entity.rename", R"({"entity":"/Light","name":""})");
    CHECK(ok(editor, "entity.get", R"({"entity":"name:Cell 1"})").root().pointer("/entity/name").asString() ==
          "Cell 1");
    CHECK(fails(editor, "entity.destroy", R"({"entities":[]})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("destroying an entity clears references to it and undo restores ids and references")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    const std::string boardId(ok(editor, "entity.get", R"({"entity":"/Game/Board"})")
                                  .root()
                                  .pointer("/entity/id")
                                  .asString()
                                  .value_or(""));
    ok(editor, "selection.set", R"({"entities":["/Game/Board/Cell 1","/Light"]})");

    const JsonDocument destroyed =
        ok(editor, "entity.destroy", R"({"entities":["/Game/Board","/Game/Board/Cell 1"]})");
    CHECK(destroyed.root().find("destroyed").asU64() == 3);
    CHECK(editor.selection().size() == 1);
    CHECK(ok(editor, "component.get", R"({"entity":"/Game/Player","type":"test.Link","path":"target"})")
              .root()
              .find("value")
              .isNull());

    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);
    CHECK(editor.selection().size() == 2);
    const JsonDocument link =
        ok(editor, "component.get", R"({"entity":"/Game/Player","type":"test.Link","path":"target"})");
    CHECK(link.root().find("value").asString() == boardId);
    CHECK(ok(editor, "entity.get", R"({"entity":"/Game/Board"})").root().pointer("/entity/id").asString() ==
          boardId);
    ok(editor, "history.redo");
    CHECK(fails(editor, "entity.get", R"({"entity":")" + boardId + R"("})").kind == ErrorKind::NotFound);
}

TEST_CASE("component commands add, set, reset and remove values with undo")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;

    const JsonDocument added =
        ok(editor, "component.add", R"({"entity":"/Light","type":"test.Link","value":{"weight":4}})");
    CHECK(added.root().find("added").size() == 1);
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Link","path":"weight"})")
              .root()
              .find("value")
              .asF64() == 4.0);
    CHECK(fails(editor, "component.add", R"({"entity":"/Light","type":"test.Link"})").kind ==
          ErrorKind::AlreadyExists);
    CHECK(fails(editor, "component.add", R"({"entity":"/Light","type":"illusion.LocalTransform"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "component.add", R"({"entity":"/Light","type":"illusion.Hierarchy"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "component.add", R"({"entity":"/Light","type":"illusion.Disabled","value":{"x":1}})")
              .errors.at(0)
              .pointer == "/value/x");

    const JsonDocument set =
        ok(editor, "component.set", R"({"entity":"/Light","type":"test.Link","value":{"weight":7}})");
    CHECK(set.root().find("changed").asBool() == true);
    CHECK(ok(editor, "component.set", R"({"entity":"/Light","type":"test.Link","value":{"weight":7}})")
              .root()
              .find("changed")
              .asBool() == false);
    const CommandError field =
        fails(editor, "component.set", R"({"entity":"/Light","type":"test.Link","value":{"wieght":1}})");
    CHECK(field.errors.at(0).pointer == "/value/wieght");
    CHECK(field.suggestions.at(0) == "weight");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Link","path":"weight"})")
              .root()
              .find("value")
              .asF64() == 7.0);
    CHECK(fails(editor, "component.set", R"({"entity":"/Light","type":"test.Link","value":{"weight":1000}})")
              .errors.at(0)
              .pointer == "/value/weight");
    CHECK(fails(editor, "component.set", R"({"entity":"/Light","type":"test.Link","value":3})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "component.set", R"({"entity":"/Light","type":"illusion.EntityGuid","value":{}})")
              .kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "component.set", R"({"entity":"/Light","type":"illusion.LocalTransform","value":{}})")
              .kind == ErrorKind::NotFound);

    ok(editor, "component.set_field", R"({"entity":"/Light","type":"test.Link","path":"weight","value":2})");
    CHECK(fails(editor, "component.set_field",
                R"({"entity":"/Light","type":"test.Link","path":"nope","value":2})")
              .kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "component.get", R"({"entity":"/Light","type":"test.Link","path":"nope"})")
              .errors.at(0)
              .pointer == "/path");
    CHECK(ok(editor, "component.reset", R"({"entity":"/Light","type":"test.Link"})")
              .root()
              .find("changed")
              .asBool() == true);
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Link","path":"weight"})")
              .root()
              .find("value")
              .asF64() == 1.0);

    ok(editor, "component.remove", R"({"entity":"/Light","type":"test.Link"})");
    CHECK(fails(editor, "component.remove", R"({"entity":"/Light","type":"test.Link"})").kind ==
          ErrorKind::NotFound);
    ok(editor, "history.undo", R"({"steps":2})");
    CHECK(ok(editor, "component.get", R"({"entity":"/Light","type":"test.Link","path":"weight"})")
              .root()
              .find("value")
              .asF64() == 2.0);

    const JsonDocument types = ok(editor, "component.types", R"({"category":"Core"})");
    CHECK(types.root().find("types").size() >= 6);
    CHECK(ok(editor, "component.schema", R"({"type":"test.Link"})")
              .root()
              .pointer("/schema/properties/weight/maximum")
              .asF64() == 100.0);
    CHECK(fails(editor, "component.schema", R"({"type":"Transform"})").kind == ErrorKind::NotFound);
}

TEST_CASE("100 merged component.set calls form one undo step back to the first value")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();
    CommandOptions drag;
    drag.mergeKey = "drag-weight";
    drag.origin = "gizmo";
    for (int i = 1; i <= 100; ++i)
    {
        ok(editor, "component.set",
           R"({"entity":"/Game/Player","type":"test.Link","value":{"weight":)" + std::to_string(i % 50) +
               "}}",
           drag);
    }
    CHECK(editor.history().steps().size() == 1);
    CHECK(editor.history().steps()[0].origin == "gizmo");
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);
    ok(editor, "history.redo");
    CHECK(ok(editor, "component.get", R"({"entity":"/Game/Player","type":"test.Link","path":"weight"})")
              .root()
              .find("value")
              .asF64() == 0.0);

    // A save seals the step: the next edit with the same key is a new step.
    ok(editor, "scene.save");
    ok(editor, "component.set", R"({"entity":"/Game/Player","type":"test.Link","value":{"weight":3}})", drag);
    CHECK(editor.history().steps().size() == 2);
}

TEST_CASE("hierarchy commands reparent, reorder and refuse cycles")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string before = fixture.scene();

    ok(editor, "hierarchy.set_parent", R"({"entity":"/Light","parent":"/Game","index":0})");
    CHECK(ok(editor, "hierarchy.children", R"({"entity":"/Game"})")
              .root()
              .pointer("/entities/0/name")
              .asString() == "Light");
    ok(editor, "hierarchy.reorder", R"({"entity":"/Game/Light","index":5})");
    CHECK(ok(editor, "hierarchy.children", R"({"entity":"/Game"})")
              .root()
              .pointer("/entities/2/name")
              .asString() == "Light");
    ok(editor, "hierarchy.set_parent", R"({"entity":"/Game/Light","keepWorld":false})");
    CHECK(ok(editor, "hierarchy.children").root().find("entities").size() == 2);
    ok(editor, "history.undo", R"({"steps":3})");
    CHECK(fixture.scene() == before);

    CHECK(fails(editor, "hierarchy.set_parent", R"({"entity":"/Game","parent":"/Game/Board/Cell 1"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "hierarchy.set_parent", R"({"entity":"/Game","parent":"/Game"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "hierarchy.reorder", R"({"entity":"/Light","index":0})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "hierarchy.children", R"({"entity":"0123456789abcdef"})").kind ==
          ErrorKind::NotFound);
    CHECK(fixture.scene() == before);
}

TEST_CASE("selection commands are not undo steps but undo restores the selection")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "selection.set", R"({"entities":["/Game/Board","/Light"]})");
    CHECK(editor.history().steps().empty());
    CHECK(ok(editor, "selection.get").root().pointer("/entities/1/name").asString() == "Light");
    ok(editor, "selection.remove", R"({"entities":["/Light"]})");
    ok(editor, "selection.add", R"({"entities":["/Game/Player","/Game/Board"]})");
    CHECK(ok(editor, "selection.get").root().pointer("/entities/1/name").asString() == "Board");
    CHECK(fails(editor, "entity.rename", R"({"entity":"$selection","name":"X"})").kind ==
          ErrorKind::Ambiguous);
    ok(editor, "selection.clear");
    CHECK(editor.selection().empty());

    ok(editor, "selection.set", R"({"entities":["/Game/Board"]})");
    ok(editor, "entity.rename", R"({"entity":"$selection","name":"Field"})");
    ok(editor, "selection.set", R"({"entities":["/Light"]})");
    ok(editor, "history.undo");
    CHECK(editor.selection().size() == 1);
    CHECK(ok(editor, "selection.get").root().pointer("/entities/0/name").asString() == "Board");
    CHECK(fails(editor, "selection.set", R"({"entities":["/Nope"]})").errors.at(0).pointer == "/entities/0");
}

TEST_CASE("history commands undo, redo, jump, list and clear")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string start = fixture.scene();
    ok(editor, "entity.rename", R"({"entity":"/Light","name":"L1"})");
    ok(editor, "entity.rename", R"({"entity":"/L1","name":"L2"})");
    const std::string end = fixture.scene();
    const JsonDocument list = ok(editor, "history.list");
    CHECK(list.root().find("steps").size() == 2);
    CHECK(list.root().pointer("/steps/0/label").asString() == "Rename entity");
    CHECK(list.root().find("position").asU64() == 2);

    CHECK(ok(editor, "history.undo", R"({"steps":5})").root().find("steps").asU64() == 2);
    CHECK(fixture.scene() == start);
    CHECK(ok(editor, "history.undo").root().find("steps").asU64() == 0);
    ok(editor, "history.jump", R"({"position":2})");
    CHECK(fixture.scene() == end);
    ok(editor, "history.jump", R"({"position":1})");
    CHECK(ok(editor, "history.list").root().pointer("/steps/1/applied").asBool() == false);
    CHECK(fails(editor, "history.jump", R"({"position":3})").errors.at(0).pointer == "/position");
    CHECK(ok(editor, "history.clear").root().find("steps").asU64() == 2);
    CHECK(editor.history().steps().empty());
    CHECK(editor.dirty());
}

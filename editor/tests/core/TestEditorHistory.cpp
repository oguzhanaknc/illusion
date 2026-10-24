#include "EditorCoreTestSupport.hpp"

#include <fstream>
#include <random>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::ok;

namespace
{

// Ids of the live entities, in a stable order.
std::vector<std::string> entityIds(EditorContext& editor)
{
    const JsonDocument tree = ok(editor, "scene.tree");
    std::vector<std::string> ids;
    for (const JsonValue node : tree.root().find("nodes").elements())
    {
        ids.emplace_back(node.find("id").asString().value_or(""));
    }
    return ids;
}

// One random command on the scene; failures (cycles, missing Components) are part of the test. Every random
// draw is a statement of its own, in a fixed order: the order in which the operands of `a + f() + g()` are
// evaluated is unspecified, and compilers differ, which would give each platform different commands.
void randomCommand(EditorContext& editor, std::mt19937& random)
{
    const std::vector<std::string> ids = entityIds(editor);
    const auto pick = [&]() -> std::string {
        return ids.empty() ? std::string("/none") : ids[random() % ids.size()];
    };
    const auto number = [&](u32 range) {
        return std::to_string(static_cast<int>(random() % range) - static_cast<int>(range / 2));
    };
    const auto coin = [&] {
        return random() % 2 == 0;
    };
    std::string name;
    std::string params;
    switch (random() % 15)
    {
    case 0:
    case 1:
    {
        name = "entity.create";
        if (coin())
        {
            const std::string suffix = number(100);
            const std::string parent = pick();
            params = R"({"name":"E)" + suffix + R"(","parent":")" + parent + R"("})";
        }
        else
        {
            params = R"({"name":"R)" + number(100) + R"(","index":0,"components":{"illusion.Transform":{}}})";
        }
        break;
    }
    case 2:
        name = "entity.destroy";
        params = R"({"entities":[")" + pick() + R"("]})";
        break;
    case 3:
    {
        name = "entity.rename";
        const std::string entity = pick();
        const std::string suffix = number(1000);
        params = R"({"entity":")" + entity + R"(","name":"N)" + suffix + R"("})";
        break;
    }
    case 4:
    {
        name = "entity.set_enabled";
        const std::string entity = pick();
        const bool enabled = coin();
        params = R"({"entity":")" + entity + R"(","enabled":)" + (enabled ? "true" : "false") + "}";
        break;
    }
    case 5:
    {
        name = "component.add";
        const std::string entity = pick();
        const std::string target = pick();
        params =
            R"({"entity":")" + entity + R"(","type":"test.Link","value":{"target":")" + target + R"("}})";
        break;
    }
    case 6:
    {
        name = "component.set";
        const std::string entity = pick();
        const std::string x = number(20);
        const std::string y = number(20);
        params = R"({"entity":")" + entity + R"(","type":"illusion.LocalTransform","value":{"position":[)" +
                 x + "," + y + ",0]}}";
        break;
    }
    case 7:
    {
        name = "hierarchy.set_parent";
        const std::string entity = pick();
        const std::string parent = pick();
        const bool keepWorld = coin();
        params = R"({"entity":")" + entity + R"(","parent":")" + parent + R"(","keepWorld":)" +
                 (keepWorld ? "true" : "false") + "}";
        break;
    }
    case 8:
    {
        name = "hierarchy.reorder";
        const std::string entity = pick();
        const std::string index = std::to_string(random() % 4);
        params = R"({"entity":")" + entity + R"(","index":)" + index + "}";
        break;
    }
    case 9:
        name = "entity.duplicate";
        params = R"({"entities":[")" + pick() + R"("]})";
        break;
    case 10:
    {
        // A batch whose later steps use the first one's result; a failing last step reverts all of it.
        name = "editor.batch";
        const bool atomic = coin();
        const std::string suffix = number(100);
        const std::string parent = pick();
        const std::string target = pick();
        const std::string moved = pick();
        params = R"({"atomic":)" + std::string(atomic ? "true" : "false") +
                 R"(,"steps":[{"as":"made","method":"entity.create","params":{"name":"B)" + suffix +
                 R"(","parent":")" + parent + R"("}},{"method":"component.add","params":{"entity":)" +
                 R"("$made.entity.id","type":"test.Link","value":{"target":")" + target +
                 R"("}}},{"method":"hierarchy.set_parent","params":{"entity":")" + moved +
                 R"(","parent":"$made.entity.id"}}]})";
        break;
    }
    case 11:
    {
        constexpr std::string_view spaces[] = {"world", "local", "self"};
        name = "transform.translate";
        const std::string entity = pick();
        const std::string x = number(10);
        const std::string y = number(10);
        const std::string_view space = spaces[random() % 3];
        params = R"({"entities":[")" + entity + R"("],"offset":[)" + x + "," + y + R"(,1],"space":")" +
                 std::string(space) + R"("})";
        break;
    }
    case 12:
    {
        name = "transform.rotate";
        const std::string first = pick();
        const std::string second = pick();
        const std::string angle = number(360);
        const bool center = coin();
        params = R"({"entities":[")" + first + R"(",")" + second + R"("],"euler":[0,)" + angle +
                 R"(,0],"pivot":")" + (center ? "center" : "individual") + R"("})";
        break;
    }
    case 13:
        name = "scene.settings_set";
        params = R"({"type":"test.Difficulty","value":{"enemies":)" + number(100) + "}}";
        break;
    case 14:
        (void)test::run(editor, "clipboard.copy", R"({"entities":[")" + pick() + R"("]})");
        name = "clipboard.paste";
        params = coin() ? std::string("{}") : R"({"parent":")" + pick() + R"("})";
        break;
    default:
    {
        name = coin() ? "component.remove" : "component.reset";
        const std::string entity = pick();
        const bool link = coin();
        params =
            R"({"entity":")" + entity + R"(","type":")" + (link ? "test.Link" : "illusion.Name") + R"("})";
        break;
    }
    }
    const Expected<std::string, CommandError> result = test::run(editor, name, params);
    if (!result)
    {
        CHECK_MESSAGE(result.error().kind != ErrorKind::Internal,
                      (name + " " + params + ": " + result.error().message));
    }
}

} // namespace

TEST_CASE("undoing 2000 random commands restores the scene byte for byte, and redoing them repeats it")
{
    EditorFixture fixture(7);
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string initial = fixture.scene();
    std::mt19937 random(1234);
    for (int i = 0; i < 2000; ++i)
    {
        // Some commands run inside transactions, which end committed or cancelled.
        if (i % 50 == 0)
        {
            ok(editor, "transaction.begin", R"({"label":"Group"})");
        }
        randomCommand(editor, random);
        if (i % 50 == 9)
        {
            ok(editor, random() % 3 == 0 ? "transaction.cancel" : "transaction.commit");
        }
        if (i % 100 == 0)
        {
            editor.endFrame(1.0 / 60.0);
        }
    }
    const std::string final = fixture.scene();
    const usize steps = editor.history().position();
    CHECK(steps > 500);
    REQUIRE(editor.undo(static_cast<u32>(steps)).value() == steps);
    CHECK(fixture.scene() == initial);
    REQUIRE(editor.redo(static_cast<u32>(steps)).value() == steps);
    CHECK(fixture.scene() == final);
}

TEST_CASE("the crash journal of a session recovers its unsaved changes")
{
    EditorFixture fixture(11);
    fixture.buildScene();
    std::mt19937 random(99);
    for (int i = 0; i < 200; ++i)
    {
        randomCommand(*fixture.editor, random);
    }
    ok(*fixture.editor, "history.undo", R"({"steps":10})");
    ok(*fixture.editor, "entity.create", R"({"name":"Last"})");
    fixture.editor->endFrame(0.0);
    const std::string crashed = fixture.scene();

    // Keep a copy of the journal as a crash would leave it; the clean shutdown deletes the original.
    const std::filesystem::path journals = fixture.directory.path / "project" / ".illusion" / "journal";
    const std::filesystem::path journal = journals / (fixture.editor->session() + ".jsonl");
    REQUIRE(std::filesystem::exists(journal));
    std::filesystem::copy_file(journal, journals / "crashed.jsonl");
    fixture.open(12);
    CHECK_FALSE(std::filesystem::exists(journal));

    EditorContext& editor = *fixture.editor;
    REQUIRE(editor.leftoverJournals().size() == 1);
    CHECK(fixture.scene() != crashed);
    const JsonDocument recovered = ok(editor, "project.recover");
    CHECK(recovered.root().find("journal").asString() == "crashed.jsonl");
    CHECK(recovered.root().find("operations").asU64() > 0);
    CHECK(fixture.scene() == crashed);
    CHECK(editor.dirty());
    CHECK(editor.leftoverJournals().empty());
    ok(editor, "history.undo");
    CHECK(fixture.scene() != crashed);
    CHECK(test::fails(editor, "project.recover", "{}").kind == ErrorKind::NotFound);
}

TEST_CASE("a session with the seed of a crashed one keeps the crashed journal and can recover it")
{
    // Deterministic runs (--deterministic, the MCP bridge relaunching after a crash) reuse the session id.
    EditorFixture fixture(11);
    ok(*fixture.editor, "entity.create", R"({"name":"Lost"})");
    fixture.editor->endFrame(0.0);
    const std::filesystem::path journals = fixture.directory.path / "project" / ".illusion" / "journal";
    const std::filesystem::path journal = journals / (fixture.editor->session() + ".jsonl");
    const std::filesystem::path kept = fixture.directory.path / "kept.jsonl";
    std::filesystem::copy_file(journal, kept);
    fixture.editor.reset();
    std::filesystem::rename(kept, journal); // as a crash leaves it

    fixture.open(11);
    EditorContext& editor = *fixture.editor;
    CHECK(editor.session() + ".jsonl" == journal.filename().string());
    const std::vector<std::filesystem::path> leftover = editor.leftoverJournals();
    REQUIRE(leftover.size() == 1);
    CHECK(leftover[0] == journal);
    CHECK(ok(editor, "project.recover").root().find("operations").asU64() > 0);
    CHECK(
        ok(editor, "entity.get", R"({"entity":"name:Lost"})").root().find("entity").find("name").asString() ==
        "Lost");
}

TEST_CASE("journals keep a scene whose entities got new ids when it was opened")
{
    EditorFixture fixture(21);
    // A scene without ids: opening it assigns them, so the journal must carry the loaded scene.
    ok(*fixture.editor, "project.file_write",
       R"({"path":"assets:/scenes/old.scene","text":"{\"format\":\"illusion.scene\",\"version\":1,)"
       R"(\"types\":{\"illusion.Name\":1},\"entities\":[{\"illusion.Name\":{\"value\":\"Old\"}}]}"})");
    ok(*fixture.editor, "scene.open", R"({"path":"assets:/scenes/old.scene","discardChanges":true})");
    ok(*fixture.editor, "entity.rename", R"({"entity":"name:Old","name":"Renamed"})");
    fixture.editor->endFrame(0.0);
    const std::string crashed = fixture.scene();
    const std::filesystem::path journals = fixture.directory.path / "project" / ".illusion" / "journal";
    std::filesystem::copy_file(journals / (fixture.editor->session() + ".jsonl"), journals / "crashed.jsonl");
    fixture.open(22);
    ok(*fixture.editor, "project.recover", R"({"journal":"crashed.jsonl"})");
    CHECK(fixture.scene() == crashed);
    CHECK(test::fails(*fixture.editor, "project.recover", R"({"journal":"../x"})").errors.at(0).pointer ==
          "/journal");
}

TEST_CASE("the undo budget drops the oldest steps and says so")
{
    EditorFixture fixture(5, EditorContextOptions{.undoBudget = 8 * 1024});
    EditorContext& editor = *fixture.editor;
    u32 trimmedEvents = 0;
    editor.events().subscribe("history.trimmed", [&](const EditorEvent&) { ++trimmedEvents; });
    for (int i = 0; i < 100; ++i)
    {
        ok(editor, "entity.create", R"({"name":"E)" + std::to_string(i) + R"("})");
    }
    editor.endFrame(0.0);
    CHECK(trimmedEvents == 1);
    CHECK(editor.history().bytes() <= 8 * 1024);
    CHECK(editor.history().steps().size() < 100);
    CHECK(editor.undo(1000).value() == editor.history().steps().size());
    CHECK(editor.world().aliveCount() > 0); // the dropped steps cannot be undone
    CHECK(editor.dirty());
}

TEST_CASE("autosave writes rotating copies of a dirty scene")
{
    EditorFixture fixture(6, EditorContextOptions{.autosaveInterval = 1.0, .autosaveCopies = 2});
    EditorContext& editor = *fixture.editor;
    u32 autosaves = 0;
    editor.events().subscribe("scene.autosaved", [&](const EditorEvent&) { ++autosaves; });
    const std::filesystem::path directory = fixture.directory.path / "project" / ".illusion" / "autosave";
    editor.endFrame(5.0);
    CHECK_FALSE(std::filesystem::exists(directory)); // a clean scene is not autosaved
    ok(editor, "entity.create", R"({"name":"A"})");
    for (int i = 0; i < 8; ++i)
    {
        editor.endFrame(0.6);
    }
    editor.endFrame(0.0);
    CHECK(autosaves == 4);
    CHECK(std::filesystem::exists(directory / "main.0.scene"));
    CHECK(std::filesystem::exists(directory / "main.1.scene"));
    CHECK_FALSE(std::filesystem::exists(directory / "main.2.scene"));
}

TEST_CASE("change events and the change journal follow commits, undos and redos")
{
    EditorFixture fixture(8);
    EditorContext& editor = *fixture.editor;
    editor.endFrame(0.0); // deliver the events of opening the project
    std::vector<std::string> topics;
    editor.events().subscribe("*", [&](const EditorEvent& event) { topics.push_back(event.topic); });
    const u64 start = editor.revision();
    ok(editor, "entity.create", R"({"name":"A"})");
    ok(editor, "entity.rename", R"({"entity":"name:A","name":"B"})");
    ok(editor, "selection.set", R"({"entities":["name:B"]})");
    editor.endFrame(0.0);
    CHECK(topics == std::vector<std::string>{"scene.changed", "history.changed", "selection.changed"});
    std::vector<EntityChange> changes;
    REQUIRE(editor.changes().changesSince(start, changes));
    REQUIRE(changes.size() == 2);
    CHECK(changes[0].kind == ChangeKind::Created);
    CHECK(changes[1].component == "illusion.Name");
    ok(editor, "history.undo");
    CHECK(editor.revision() == start + 3);
    // A failed or dry-run command makes no revision.
    test::fails(editor, "entity.rename", R"({"entity":"/Nope","name":"x"})");
    ok(editor, "entity.create", R"({"name":"C","dryRun":true})");
    CHECK(editor.revision() == start + 3);
}

TEST_CASE("undoing a move under a rotated parent restores the root's Transform bit for bit")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "entity.create",
       R"({"name":"Parent","components":{"illusion.Transform":{"position":[3.3,1.1,-7.7],)"
       R"("rotation":[0.13,0.61566186,0.05,0.77]}}})");
    for (int i = 0; i < 20; ++i)
    {
        CAPTURE(i);
        const std::string name = "Root " + std::to_string(i);
        ok(editor, "entity.create",
           R"({"name":")" + name + R"(","components":{"illusion.Transform":{"position":[)" +
               std::to_string(2.740831 + i * 0.37) +
               R"(,-14,26.94212],"rotation":[0,-0.9832555,0,-0.18223594]}}})");
        const std::string before = fixture.scene();
        ok(editor, "hierarchy.set_parent", R"({"entity":"/)" + name + R"(","parent":"/Parent"})");
        REQUIRE(editor.undo().value() == 1);
        CHECK(fixture.scene() == before);
    }
}

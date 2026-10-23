#include "EditorCoreTestSupport.hpp"

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

std::string idAt(const JsonDocument& doc, std::string_view pointer)
{
    return std::string(doc.root().pointer(pointer).asString().value_or(""));
}

f64 coordinate(EditorContext& editor, std::string_view entity, usize axis)
{
    const JsonDocument placed = ok(editor, "transform.get", R"({"entity":")" + std::string(entity) + R"("})");
    return placed.root().find("position")[axis].asF64().value_or(0.0);
}

} // namespace

TEST_CASE("clipboard.copy and paste make new subtrees with new ids, inner references and world placement")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;

    const JsonDocument copied = ok(editor, "clipboard.copy", R"({"entities":["/Game"]})");
    CHECK(copied.root().find("entities").asU64() == 5);
    CHECK(copied.root().find("text").asString().value_or("").find("illusion.scene") !=
          std::string_view::npos);
    CHECK(editor.clipboard() == copied.root().find("text").asString());
    CHECK(copied.root().pointer("/_editor/transaction").asU64() == 0); // not an undo step

    const std::string original = idAt(ok(editor, "entity.get", R"({"entity":"/Game"})"), "/entity/id");
    const JsonDocument pasted = ok(editor, "clipboard.paste");
    REQUIRE(pasted.root().find("entities").size() == 1);
    const std::string game = idAt(pasted, "/entities/0/id");
    CHECK(game != original);
    ok(editor, "entity.rename", R"({"entity":")" + game + R"(","name":"Copy"})");
    REQUIRE(editor.selection().size() == 1);
    CHECK(idAt(ok(editor, "selection.get"), "/entities/0/id") == game);

    // The pasted Player's link points to the pasted Board, not to the original one.
    const JsonDocument children = ok(editor, "hierarchy.children", R"({"entity":")" + game + R"("})");
    REQUIRE(children.root().find("entities").size() == 2);
    const std::string board = idAt(children, "/entities/0/id");
    const std::string player = idAt(children, "/entities/1/id");
    const JsonDocument link =
        ok(editor, "component.get", R"({"entity":")" + player + R"(","type":"test.Link","path":"target"})");
    CHECK(link.root().find("value").asString() == board);
    CHECK(
        ok(editor, "hierarchy.children", R"({"entity":")" + board + R"("})").root().find("entities").size() ==
        2);

    // A copied entity whose link points outside the copy loses the link target.
    ok(editor, "clipboard.copy", R"({"entities":["/Game/Player"]})");
    const std::string lone = idAt(ok(editor, "clipboard.paste"), "/entities/0/id");
    CHECK(ok(editor, "component.get", R"({"entity":")" + lone + R"(","type":"test.Link","path":"target"})")
              .root()
              .find("value")
              .isNull());

    // Pasted under a moved parent, a copy stays where the original is in the world.
    ok(editor, "transform.set", R"({"entities":["/Light"],"position":[10,0,0]})");
    ok(editor, "clipboard.copy", R"({"entities":["/Game/Board"]})");
    const std::string copy =
        idAt(ok(editor, "clipboard.paste", R"({"parent":"/Light","index":0})"), "/entities/0/id");
    CHECK(coordinate(editor, copy, 0) == doctest::Approx(1.0));
    CHECK(coordinate(editor, copy, 1) == doctest::Approx(2.0));
    CHECK(ok(editor, "transform.get", R"({"entity":")" + copy + R"(","space":"local"})")
              .root()
              .find("position")[0]
              .asF64() == doctest::Approx(-9.0));
    CHECK(idAt(ok(editor, "hierarchy.children", R"({"entity":"/Light"})"), "/entities/0/id") == copy);

    // Every paste is one undo step.
    const std::string before = fixture.scene();
    ok(editor, "clipboard.paste", R"({"parent":"/Game/Board/Cell 1"})");
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);
}

TEST_CASE("clipboard.cut removes entities and undo brings them back")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string initial = fixture.scene();
    ok(editor, "selection.set", R"({"entities":["/Game/Board/Cell 1","/Light"]})");

    const JsonDocument cut =
        ok(editor, "clipboard.cut", R"({"entities":["/Game/Board","/Game/Board/Cell 1"]})");
    CHECK(cut.root().find("entities").asU64() == 3);
    CHECK(fails(editor, "entity.get", R"({"entity":"/Game/Board"})").kind == ErrorKind::NotFound);
    CHECK(editor.selection().size() == 1); // the cut cell left the selection
    // The Player's link to the cut Board was cleared (and comes back with undo).
    CHECK(ok(editor, "component.get", R"({"entity":"/Game/Player","type":"test.Link","path":"target"})")
              .root()
              .find("value")
              .isNull());

    ok(editor, "history.undo");
    CHECK(fixture.scene() == initial);
    CHECK(editor.selection().size() == 2);
    ok(editor, "clipboard.paste", R"({"parent":"/Game"})");
    CHECK(ok(editor, "hierarchy.children", R"({"entity":"/Game"})").root().find("entities").size() == 3);
}

TEST_CASE("clipboard.paste reports an empty clipboard and bad scene text")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    CHECK(fails(editor, "clipboard.paste", "{}").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "clipboard.paste", R"({"text":""})").errors.at(0).pointer == "/text");
    const CommandError broken = fails(editor, "clipboard.paste", R"({"text":"{"})");
    CHECK(broken.kind == ErrorKind::ValidationFailed);
    CHECK(broken.errors.at(0).pointer.starts_with("/text"));
    CHECK(fails(editor, "clipboard.paste",
                R"({"text":"{\"format\":\"illusion.scene\",\"version\":2,\"entities\":[]}"})")
              .errors.at(0)
              .pointer == "/text");
    ok(editor, "clipboard.copy", R"({"entities":["/Light"]})");
    CHECK(fails(editor, "clipboard.paste", R"({"parent":"/Nope"})").kind == ErrorKind::NotFound);

    // Scene text from elsewhere pastes too, with new ids.
    const std::string text =
        R"({"format":"illusion.scene","version":2,"entities":[{"id":"00000000000000c1","components":)"
        R"({"illusion.Name":{"value":"Given"}}}]})";
    JsonMutDocument doc;
    JsonMutValue params = doc.makeObject();
    params.add("text", doc.makeString(text));
    doc.setRoot(params);
    std::string json;
    REQUIRE(doc.write(json, JsonWriteOptions{.pretty = false}));
    const JsonDocument given = ok(editor, "clipboard.paste", json);
    CHECK(given.root().pointer("/entities/0/path").asString() == "/Given");
    CHECK(given.root().pointer("/entities/0/id").asString() != "00000000000000c1");

    // A dry run copies nothing.
    const std::string clipboard = editor.clipboard();
    ok(editor, "clipboard.copy", R"({"entities":["/Game"],"dryRun":true})");
    CHECK(editor.clipboard() == clipboard);
}

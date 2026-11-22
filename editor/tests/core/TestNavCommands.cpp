// The nav.* commands on the edit World (ADR 0062): baking into `.navmesh` files, changed tiles only,
// undo, paths around walls, the status counters, the debug view and every invalid param.

#include "EditorCoreTestSupport.hpp"

#include <illusion/nav/NavMeshData.hpp>

#include <fstream>
#include <iterator>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

// A 20 m ground and a surface over it: 2 x 2 tiles of 10 m.
void addYard(EditorContext& editor)
{
    ok(editor, "entity.create",
       R"({"name":"Ground","components":{"illusion.Transform":{"position":[0,-0.25,0]},)"
       R"("illusion.RigidBody":{"motionType":"Static"},"illusion.Collider":{"halfExtents":[10,0.25,10]}}})");
    ok(editor, "entity.create",
       R"({"name":"Yard","components":{"illusion.Transform":{"position":[0,1,0]},)"
       R"("illusion.NavMeshSurface":{"size":[20,4,20],"agentRadius":0.5,)"
       R"("build":{"cellSize":0.5,"tileSize":20}}}})");
}

void addWall(EditorContext& editor, std::string_view name, std::string_view position, std::string_view half)
{
    ok(editor, "entity.create",
       "{\"name\":\"" + std::string(name) + R"(","components":{"illusion.Transform":{"position":)" +
           std::string(position) + R"(},"illusion.RigidBody":{"motionType":"Static"},)" +
           R"("illusion.Collider":{"halfExtents":)" + std::string(half) + "}}}");
}

std::vector<char> readBytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

u64 field(const JsonDocument& result, std::string_view pointer)
{
    return result.root().pointer(pointer).asU64().value_or(~u64{0});
}

std::string dataOf(EditorContext& editor)
{
    return std::string(ok(editor, "component.get", R"({"entity":"/Yard","type":"illusion.NavMeshSurface"})")
                           .root()
                           .pointer("/value/data")
                           .asString()
                           .value_or("?"));
}

} // namespace

TEST_CASE("nav.bake writes the navmesh, sets the surface's data, and baking twice changes nothing")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addYard(editor);
    const std::filesystem::path file =
        f.directory.path / "project" / "assets" / "navigation" / "yard.navmesh";

    const JsonDocument first = ok(editor, "nav.bake", "{}");
    REQUIRE(field(first, "/surfaces/0/tiles") == 4);
    CHECK(field(first, "/surfaces/0/rebuiltTiles") == 4);
    CHECK(first.root().pointer("/surfaces/0/path").asString() == "assets:/navigation/yard.navmesh");
    CHECK(first.root().pointer("/surfaces/0/written").asBool() == true);
    CHECK(first.root().pointer("/surfaces/0/dataChanged").asBool() == true);
    CHECK(field(first, "/written") == 1);
    CHECK(dataOf(editor) == "assets:/navigation/yard.navmesh");
    const std::vector<char> bytes = readBytes(file);
    REQUIRE_FALSE(bytes.empty());
    CHECK(field(first, "/surfaces/0/bytes") == bytes.size());
    const Expected<NavMeshData, NavDataError> decoded =
        decodeNavMeshData(std::as_bytes(std::span<const char>(bytes)));
    REQUIRE(decoded.hasValue());
    CHECK(decoded->tiles.size() == 4);

    const JsonDocument second = ok(editor, "nav.bake", R"({"surface":"/Yard"})");
    CHECK(field(second, "/surfaces/0/rebuiltTiles") == 0);
    CHECK(second.root().pointer("/surfaces/0/written").asBool() == false);
    CHECK(second.root().pointer("/surfaces/0/dataChanged").asBool() == false);
    CHECK(readBytes(file) == bytes);

    // Forcing builds every tile again, with the same bytes.
    const JsonDocument forced = ok(editor, "nav.bake", R"({"force":true})");
    CHECK(field(forced, "/surfaces/0/rebuiltTiles") == 4);
    CHECK(forced.root().pointer("/surfaces/0/written").asBool() == false);
    CHECK(readBytes(file) == bytes);

    // The step that set the data is undoable; the file stays.
    ok(editor, "history.undo", "{}");
    CHECK(dataOf(editor).empty());
    CHECK(readBytes(file) == bytes);
}

TEST_CASE("nav.bake builds again only the tiles a moved wall touches")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addYard(editor);
    addWall(editor, "Wall", "[-6,1,-6]", "[0.5,1,0.5]");
    ok(editor, "nav.bake", "{}");
    ok(editor, "transform.set", R"({"entities":["/Wall"],"position":[-7,1,-6]})");
    const JsonDocument moved = ok(editor, "nav.bake", "{}");
    CHECK(field(moved, "/surfaces/0/tiles") == 4);
    CHECK(field(moved, "/surfaces/0/rebuiltTiles") == 1);
    CHECK(moved.root().pointer("/surfaces/0/written").asBool() == true);
    CHECK(field(moved, "/written") == 1);
    CHECK(field(ok(editor, "nav.bake", "{}"), "/rebuiltTiles") == 0);
}

TEST_CASE("nav.bake takes a path for a surface without data and refuses clashes and bad paths")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addYard(editor);
    const JsonDocument baked =
        ok(editor, "nav.bake", R"({"surface":"/Yard","path":"assets:/maps/courtyard.navmesh"})");
    CHECK(baked.root().pointer("/surfaces/0/path").asString() == "assets:/maps/courtyard.navmesh");
    CHECK(dataOf(editor) == "assets:/maps/courtyard.navmesh");

    CHECK(fails(editor, "nav.bake", R"({"path":"assets:/maps/other.navmesh"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.bake", R"({"surface":"/Yard","path":"assets:/maps/other.txt"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.bake", R"({"surface":"/Yard","path":"textures/x.navmesh"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.bake", R"({"surface":"/Yard","path":"assets:/.navmesh"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.bake", R"({"surface":"/Ground"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.bake", R"({"surface":"/Missing"})").kind == ErrorKind::NotFound);

    // "YARD" and "Yard" both default to assets:/navigation/yard.navmesh once the first one has no data.
    ok(editor, "entity.create",
       R"({"name":"YARD","components":{"illusion.Transform":{"position":[40,1,0]},)"
       R"("illusion.NavMeshSurface":{"size":[4,4,4],"agentType":1}}})");
    ok(editor, "component.set_field",
       R"({"entity":"/Yard","type":"illusion.NavMeshSurface","path":"data","value":""})");
    CHECK(fails(editor, "nav.bake", "{}").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("nav.bake of a scene without surfaces bakes nothing")
{
    EditorFixture f;
    const JsonDocument none = ok(*f.editor, "nav.bake", "{}");
    CHECK(none.root().find("surfaces").size() == 0);
    CHECK(field(none, "/written") == 0);
}

TEST_CASE("nav.bake as a dry run writes nothing")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addYard(editor);
    ok(editor, "nav.bake", R"({"dryRun":true})");
    CHECK_FALSE(std::filesystem::exists(f.directory.path / "project" / "assets" / "navigation"));
    CHECK(dataOf(editor).empty());
}

TEST_CASE("nav.path goes around a wall and straight where nothing is in the way")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    addYard(editor);
    // A wall along z from -10 to 3: the way from one side to the other leads round its end.
    addWall(editor, "Wall", "[0,1,-3.5]", "[0.5,1,6.5]");
    const JsonDocument open = ok(editor, "nav.path", R"({"from":[-4,0,5],"to":[4,0,5]})");
    CHECK(open.root().find("partial").asBool() == false);
    CHECK(field(open, "/total") == 2);
    CHECK(open.root().find("length").asF64().value_or(0.0) == doctest::Approx(8.0).epsilon(0.05));

    const JsonDocument around = ok(editor, "nav.path", R"({"from":[-4,0,-5],"to":[4,0,-5]})");
    CHECK(around.root().find("partial").asBool() == false);
    CHECK(field(around, "/total") >= 3);
    CHECK(around.root().find("length").asF64().value_or(0.0) > 14.0);
    CHECK(around.root().find("corners").size() == field(around, "/total"));

    // maxCorners cuts the list, not the count.
    const JsonDocument cut = ok(editor, "nav.path", R"({"from":[-4,0,-5],"to":[4,0,-5],"maxCorners":2})");
    CHECK(cut.root().find("corners").size() == 2);
    CHECK(field(cut, "/total") == field(around, "/total"));
}

TEST_CASE("nav.path rejects points off the navmesh, a missing navmesh and bad params")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,0],"to":[1,0,0]})").kind ==
          ErrorKind::PreconditionFailed);
    addYard(editor);
    const CommandError start = fails(editor, "nav.path", R"({"from":[50,0,0],"to":[1,0,0]})");
    CHECK(start.kind == ErrorKind::ValidationFailed);
    REQUIRE_FALSE(start.errors.empty());
    CHECK(start.errors[0].pointer == "/from");
    const CommandError end = fails(editor, "nav.path", R"({"from":[0,0,0],"to":[0,30,0]})");
    REQUIRE_FALSE(end.errors.empty());
    CHECK(end.errors[0].pointer == "/to");
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,0],"to":[1,0,0],"agentType":3})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,0],"to":[1,0,0],"agentType":8})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,0],"to":[1,0,0],"maxCorners":1})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,0],"to":[1,0,0],"maxCorners":2000})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,2000000],"to":[1,0,0]})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "nav.path", R"({"from":[0,0,0]})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("nav.status lists the surfaces and counts tiles, and nav.debug keeps its state")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    const JsonDocument empty = ok(editor, "nav.status", "{}");
    CHECK(empty.root().find("surfaces").size() == 0);
    CHECK(field(empty, "/tiles") == 0);

    addYard(editor);
    const JsonDocument status = ok(editor, "nav.status", "{}");
    REQUIRE(status.root().find("surfaces").size() == 1);
    CHECK(status.root().pointer("/surfaces/0/ready").asBool() == true);
    CHECK(status.root().pointer("/surfaces/0/data").asString() == "");
    CHECK(field(status, "/tiles") == 4);
    CHECK(field(status, "/polygons") > 0);
    CHECK(field(status, "/pendingTiles") == 0);
    CHECK(field(status, "/failures") == 0);

    // Once baked, the surface loads its file.
    ok(editor, "nav.bake", "{}");
    const JsonDocument loaded = ok(editor, "nav.status", "{}");
    CHECK(loaded.root().pointer("/surfaces/0/data").asString() == "assets:/navigation/yard.navmesh");
    CHECK(loaded.root().pointer("/surfaces/0/ready").asBool() == true);
    CHECK(field(loaded, "/tiles") == 4);
    CHECK(field(loaded, "/staleTiles") == 0);

    const JsonDocument initial = ok(editor, "nav.debug", "{}");
    CHECK(initial.root().find("enabled").asBool() == false);
    CHECK(initial.root().find("navMesh").asBool() == true);
    CHECK(initial.root().find("tiles").asBool() == false);
    const JsonDocument shown = ok(editor, "nav.debug", R"({"enabled":true,"tiles":true,"links":false})");
    CHECK(shown.root().find("enabled").asBool() == true);
    CHECK(shown.root().find("tiles").asBool() == true);
    CHECK(shown.root().find("links").asBool() == false);
    CHECK(shown.root().find("navMesh").asBool() == true);
    ok(editor, "nav.debug", R"({"enabled":false,"dryRun":true})");
    CHECK(ok(editor, "nav.debug", "{}").root().find("enabled").asBool() == true);
    CHECK(fails(editor, "nav.debug", R"({"enabled":"yes"})").kind == ErrorKind::ValidationFailed);
}

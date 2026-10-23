#include "EditorCoreTestSupport.hpp"

#include <cmath>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// The value of a Vector3 at @p pointer of a JSON result.
Vec3 vec3(const JsonDocument& doc, std::string_view pointer)
{
    const JsonValue value = doc.root().pointer(pointer);
    REQUIRE_MESSAGE(value.size() == 3, std::string(pointer));
    return Vec3(static_cast<f32>(value[0].asF64().value_or(0.0)),
                static_cast<f32>(value[1].asF64().value_or(0.0)),
                static_cast<f32>(value[2].asF64().value_or(0.0)));
}

void checkNear(Vec3 actual, Vec3 expected)
{
    CHECK(actual.x == doctest::Approx(expected.x).epsilon(1e-4));
    CHECK(actual.y == doctest::Approx(expected.y).epsilon(1e-4));
    CHECK(actual.z == doctest::Approx(expected.z).epsilon(1e-4));
}

Vec3 worldPosition(EditorContext& editor, std::string_view entity)
{
    return vec3(ok(editor, "transform.get", R"({"entity":")" + std::string(entity) + R"("})"), "/position");
}

Vec3 field(EditorContext& editor, std::string_view entity, std::string_view type, std::string_view path)
{
    const JsonDocument value = ok(editor, "component.get",
                                  R"({"entity":")" + std::string(entity) + R"(","type":")" +
                                      std::string(type) + R"(","path":")" + std::string(path) + R"("})");
    return vec3(value, "/value");
}

} // namespace

TEST_CASE("transform.get reports world and local placements with Euler angles")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    checkNear(worldPosition(editor, "/Game/Board/Cell 1"), Vec3(1, 2, 0));
    checkNear(vec3(ok(editor, "transform.get", R"({"entity":"/Game/Board","space":"local"})"), "/position"),
              Vec3(0, 2, 0));
    ok(editor, "transform.set", R"({"entities":["/Game"],"euler":[0,90,0]})");
    const JsonDocument game = ok(editor, "transform.get", R"({"entity":"/Game"})");
    checkNear(vec3(game, "/euler"), Vec3(0, 90, 0));
    CHECK(game.root().pointer("/entity/path").asString() == "/Game");
    // The child turned with its parent: (0, 2, 0) is on the turning axis, (1, 0, 0) is not.
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 2, 0));
    ok(editor, "transform.set", R"({"entities":["/Game/Board"],"space":"local","position":[1,0,0]})");
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 0, -1));
    CHECK(fails(editor, "transform.get", R"({"entity":"/Game","space":"self"})").errors.at(0).pointer ==
          "/space");
    CHECK(fails(editor, "transform.get", R"({"entity":"/Nope"})").kind == ErrorKind::NotFound);
}

TEST_CASE("transform.set places entities in world or parent space and children follow")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const std::string initial = fixture.scene();

    ok(editor, "transform.set", R"({"entities":["/Game/Board"],"position":[5,5,5]})");
    checkNear(field(editor, "/Game/Board", "illusion.LocalTransform", "position"), Vec3(4, 5, 5));
    checkNear(field(editor, "/Game/Board", "illusion.Transform", "position"), Vec3(5, 5, 5));
    // Children's world Transforms are derived again after every command.
    checkNear(field(editor, "/Game/Board/Cell 1", "illusion.Transform", "position"), Vec3(5, 5, 5));

    ok(editor, "transform.set", R"({"entities":["/Game/Board"],"space":"local","position":[0,1,0]})");
    CHECK(field(editor, "/Game/Board", "illusion.LocalTransform", "position") == Vec3(0, 1, 0));
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 1, 0));

    ok(editor, "transform.set", R"({"entities":["/Light"],"scale":[2,2,2]})");
    CHECK(field(editor, "/Light", "illusion.Transform", "scale") == Vec3(2, 2, 2));

    // A parent and its child set together: the parent first, the child lands where it was asked.
    ok(editor, "transform.set", R"({"entities":["/Game/Board","/Game"],"position":[3,0,0]})");
    checkNear(worldPosition(editor, "/Game"), Vec3(3, 0, 0));
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(3, 0, 0));

    // A root without a placement gets a Transform.
    ok(editor, "entity.create", R"({"name":"Empty"})");
    ok(editor, "transform.set", R"({"entities":["/Empty"],"position":[0,0,7]})");
    CHECK(field(editor, "/Empty", "illusion.Transform", "position") == Vec3(0, 0, 7));

    REQUIRE(editor.undo(10).value() == 6);
    CHECK(fixture.scene() == initial);
}

TEST_CASE("transform.translate moves along world, parent and own axes, once per moved subtree")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "transform.set", R"({"entities":["/Game"],"euler":[0,90,0]})");

    ok(editor, "transform.translate", R"({"entities":["/Game/Board"],"offset":[1,0,0],"space":"local"})");
    CHECK(field(editor, "/Game/Board", "illusion.LocalTransform", "position") == Vec3(1, 2, 0));
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 2, -1));

    ok(editor, "transform.translate", R"({"entities":["/Game/Board"],"offset":[0,0,1]})");
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 2, 0));

    // The Board is listed with its parent: it moves with it, not twice.
    ok(editor, "transform.translate", R"({"entities":["/Game","/Game/Board"],"offset":[1,0,0]})");
    checkNear(worldPosition(editor, "/Game"), Vec3(2, 0, 0));
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(2, 2, 0));

    // Along its own axes: forward (-z) of a light turned 90 degrees about +y points along -x.
    ok(editor, "transform.set", R"({"entities":["/Light"],"euler":[0,90,0]})");
    ok(editor, "transform.translate", R"({"entities":["/Light"],"offset":[0,0,-1],"space":"self"})");
    checkNear(worldPosition(editor, "/Light"), Vec3(-1, 0, 0));

    CHECK(fails(editor, "transform.translate", R"({"entities":[],"offset":[1,0,0]})").errors.at(0).pointer ==
          "/entities");
    ok(editor, "transform.translate", R"({"entities":["/Light"],"offset":[3e38,0,0]})");
    CHECK(fails(editor, "transform.translate", R"({"entities":["/Light"],"offset":[3e38,0,0]})")
              .errors.at(0)
              .pointer == "/offset");
}

TEST_CASE("transform.rotate turns entities about themselves, their center or a point")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "entity.create", R"({"name":"A","components":{"illusion.Transform":{"position":[2,0,0]}}})");
    ok(editor, "entity.create", R"({"name":"B","components":{"illusion.Transform":{"position":[-2,0,0]}}})");

    ok(editor, "transform.rotate", R"({"entities":["/A","/B"],"euler":[0,180,0],"pivot":"center"})");
    checkNear(worldPosition(editor, "/A"), Vec3(-2, 0, 0));
    checkNear(worldPosition(editor, "/B"), Vec3(2, 0, 0));
    const Vec3 turned = vec3(ok(editor, "transform.get", R"({"entity":"/A"})"), "/euler");
    CHECK(std::abs(turned.y) == doctest::Approx(180.0f).epsilon(1e-4)); // 180 and -180 are the same turn

    ok(editor, "transform.rotate", R"({"entities":["/A"],"euler":[0,-180,0]})");
    checkNear(worldPosition(editor, "/A"), Vec3(-2, 0, 0)); // individual: about its own position
    ok(editor, "transform.rotate",
       R"({"entities":["/A"],"rotation":[0,0.70710678,0,0.70710678],"point":[0,0,0]})");
    checkNear(worldPosition(editor, "/A"), Vec3(0, 0, 2));

    // About its own axes: pitch after yaw.
    ok(editor, "transform.set", R"({"entities":["/B"],"euler":[0,90,0]})");
    ok(editor, "transform.rotate", R"({"entities":["/B"],"euler":[30,0,0],"space":"self"})");
    checkNear(vec3(ok(editor, "transform.get", R"({"entity":"/B"})"), "/euler"), Vec3(30, 90, 0));

    CHECK(fails(editor, "transform.rotate", R"({"entities":["/A"]})").errors.at(0).pointer == "/rotation");
    CHECK(fails(editor, "transform.rotate", R"({"entities":["/A"],"rotation":[0,0,0,1],"euler":[0,0,0]})")
              .errors.at(0)
              .pointer == "/euler");
    CHECK(fails(editor, "transform.rotate", R"({"entities":["/A"],"rotation":[0,0,0,0]})")
              .errors.at(0)
              .pointer == "/rotation");
    CHECK(fails(editor, "transform.rotate",
                R"({"entities":["/A"],"euler":[0,1,0],"pivot":"center","point":[0,0,0]})")
              .errors.at(0)
              .pointer == "/point");
    CHECK(fails(editor, "transform.rotate", R"({"entities":["/A"],"euler":[0,1,0],"pivot":"middle"})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("transform.scale multiplies scales and spreads positions from a pivot")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "transform.scale", R"({"entities":["/Game"],"factor":[2,2,2]})");
    checkNear(field(editor, "/Game", "illusion.Transform", "scale"), Vec3(2, 2, 2));
    checkNear(worldPosition(editor, "/Game"), Vec3(1, 0, 0));
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 4, 0)); // the child's offset scales with it

    ok(editor, "transform.set", R"({"entities":["/Light"],"position":[3,0,0]})");
    ok(editor, "transform.scale", R"({"entities":["/Light"],"factor":[2,1,1],"point":[0,0,0]})");
    checkNear(worldPosition(editor, "/Light"), Vec3(6, 0, 0));
    checkNear(field(editor, "/Light", "illusion.Transform", "scale"), Vec3(2, 1, 1));

    // A child scaled about its own position keeps its LocalTransform position exactly.
    ok(editor, "transform.scale", R"({"entities":["/Game/Board"],"factor":[3,3,3]})");
    CHECK(field(editor, "/Game/Board", "illusion.LocalTransform", "position") == Vec3(0, 2, 0));
    CHECK(field(editor, "/Game/Board", "illusion.LocalTransform", "scale") == Vec3(3, 3, 3));
}

TEST_CASE("transform.reset resets placements relative to the parent")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "transform.set",
       R"({"entities":["/Game/Board"],"space":"local","euler":[0,45,0],"scale":[2,2,2]})");
    ok(editor, "transform.reset", R"({"entities":["/Game/Board"],"scale":false})");
    CHECK(field(editor, "/Game/Board", "illusion.LocalTransform", "position") == Vec3(0, 0, 0));
    CHECK(field(editor, "/Game/Board", "illusion.LocalTransform", "scale") == Vec3(2, 2, 2));
    checkNear(vec3(ok(editor, "transform.get", R"({"entity":"/Game/Board","space":"local"})"), "/euler"),
              Vec3(0, 0, 0));
    checkNear(worldPosition(editor, "/Game/Board"), Vec3(1, 0, 0));
    ok(editor, "transform.reset", R"({"entities":["/Game"]})");
    checkNear(worldPosition(editor, "/Game"), Vec3(0, 0, 0));
    CHECK(field(editor, "/Game", "illusion.LocalTransform", "position") ==
          Vec3(0, 0, 0)); // roots keep both equal
    CHECK(fails(editor, "transform.set", R"({"entities":["/Game"]})").errors.at(0).pointer == "/position");
    CHECK(fails(editor, "transform.set", R"({"entities":["/Game"],"space":"self","position":[0,0,0]})")
              .errors.at(0)
              .pointer == "/space");
}

TEST_CASE("reparenting right after opening a scene keeps world placements")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "scene.open", R"({"path":"assets:/scenes/main.scene"})");
    // Saved scenes leave out the derived Transforms of children; opening derives them.
    checkNear(field(editor, "/Game/Board/Cell 1", "illusion.Transform", "position"), Vec3(1, 2, 0));
    ok(editor, "transform.set", R"({"entities":["/Light"],"position":[10,0,0]})");
    ok(editor, "hierarchy.set_parent", R"({"entity":"/Game/Board/Cell 1","parent":"/Light"})");
    checkNear(worldPosition(editor, "/Light/Cell 1"), Vec3(1, 2, 0));
    checkNear(field(editor, "/Light/Cell 1", "illusion.LocalTransform", "position"), Vec3(-9, 2, 0));
}

TEST_CASE("Transforms derived after loading stay out of undo")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    // A child placed in world space without a Transform: adding a LocalTransform must not add one for good.
    ok(editor, "project.file_write",
       R"({"path":"assets:/scenes/bare.scene","text":"{\"format\":\"illusion.scene\",\"version\":2,\"entities\":[)"
       R"({\"id\":\"00000000000000d1\",\"components\":{\"illusion.Name\":{\"value\":\"P\"},\"illusion.Hierarchy\":)"
       R"({\"parent\":null,\"order\":0}}},{\"id\":\"00000000000000d2\",\"components\":{\"illusion.Name\":)"
       R"({\"value\":\"C\"},\"illusion.Hierarchy\":{\"parent\":\"00000000000000d1\",\"order\":0}}}]}"})");
    ok(editor, "scene.open", R"({"path":"assets:/scenes/bare.scene","discardChanges":true})");
    const std::string opened = fixture.scene();
    ok(editor, "component.add", R"({"entity":"/P/C","type":"illusion.LocalTransform"})");
    ok(editor, "history.undo");
    CHECK(fixture.scene() == opened);
}

TEST_CASE("pasted children get their world Transforms, so reparenting keeps them in place")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "transform.set", R"({"entities":["/Light"],"position":[10,0,0]})");
    ok(editor, "clipboard.copy", R"({"entities":["/Game/Board"]})");
    const JsonDocument pasted = ok(editor, "clipboard.paste", R"({"parent":"/Light"})");
    const std::string board(pasted.root().pointer("/entities/0/id").asString().value_or(""));
    const JsonDocument cells = ok(editor, "hierarchy.children", R"({"entity":")" + board + R"("})");
    const std::string cell(cells.root().pointer("/entities/0/id").asString().value_or(""));
    checkNear(field(editor, cell, "illusion.Transform", "position"), Vec3(1, 2, 0));
    ok(editor, "hierarchy.set_parent", R"({"entity":")" + cell + R"("})");
    checkNear(worldPosition(editor, cell), Vec3(1, 2, 0));
}

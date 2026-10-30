#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/core/Arrange.hpp>

#include <array>
#include <format>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

// The arranging math (align, distribute, resting heights) and the transform.align, transform.distribute,
// transform.drop_to_ground and transform.align_to_view commands.

namespace
{

Aabb box(Vec3 low, Vec3 high)
{
    return Aabb{low, high};
}

// Creates a root entity @p name at @p position and returns it.
Entity create(EditorContext& editor, std::string_view name, Vec3 position)
{
    ok(editor, "entity.create",
       std::format(R"({{"name":"{}","components":{{"illusion.Transform":{{"position":[{},{},{}]}}}}}})", name,
                   position.x, position.y, position.z));
    Expected<Entity, CommandError> entity =
        editor.entities().resolve(std::string("/") + std::string(name), editor.selection());
    REQUIRE(entity.hasValue());
    return *entity;
}

Vec3 positionOf(EditorContext& editor, std::string_view path)
{
    const JsonDocument placed = ok(editor, "transform.get", std::format(R"({{"entity":"{}"}})", path));
    const JsonValue position = placed.root().find("position");
    return Vec3(static_cast<f32>(position[0].asF64().value_or(0.0)),
                static_cast<f32>(position[1].asF64().value_or(0.0)),
                static_cast<f32>(position[2].asF64().value_or(0.0)));
}

} // namespace

TEST_CASE("align offsets line up the min, centre or max edges of boxes with a target")
{
    const std::array boxes = {box(Vec3(0.0f), Vec3(1.0f)),
                              box(Vec3(3.0f, 0.0f, 0.0f), Vec3(5.0f, 2.0f, 1.0f)), Aabb{}};
    const Aabb target = box(Vec3(-1.0f), Vec3(6.0f));
    std::array<f32, 3> out{};
    alignOffsets(boxes, 0, AlignEdge::Min, target, out);
    CHECK(out == std::array{-1.0f, -4.0f, 0.0f});
    alignOffsets(boxes, 0, AlignEdge::Max, target, out);
    CHECK(out == std::array{5.0f, 1.0f, 0.0f});
    alignOffsets(boxes, 1, AlignEdge::Center, target, out);
    CHECK(out == std::array{2.0f, 1.5f, 0.0f});
    // Nothing moves without a target or along an axis that does not exist.
    alignOffsets(boxes, 0, AlignEdge::Min, Aabb{}, out);
    CHECK(out == std::array{0.0f, 0.0f, 0.0f});
    alignOffsets(boxes, 3, AlignEdge::Min, target, out);
    CHECK(out == std::array{0.0f, 0.0f, 0.0f});
    alignOffsets({}, 0, AlignEdge::Min, target, {});
}

TEST_CASE("distribute offsets space boxes evenly by centres or gaps and keep the outer ones in place")
{
    // Out of order on purpose: the middle box is listed last.
    const std::array boxes = {box(Vec3(0.0f), Vec3(1.0f)),
                              box(Vec3(9.0f, 0.0f, 0.0f), Vec3(10.0f, 1.0f, 1.0f)),
                              box(Vec3(2.0f, 0.0f, 0.0f), Vec3(5.0f, 1.0f, 1.0f))};
    std::array<f32, 3> out{};
    distributeOffsets(boxes, 0, DistributeMode::Centers, out);
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == 0.0f);
    CHECK(out[2] == doctest::Approx(1.5f)); // centre 3.5 -> 5
    distributeOffsets(boxes, 0, DistributeMode::Gaps, out);
    CHECK(out[2] == doctest::Approx(1.5f)); // gaps of (10 - 0 - 5) / 2 = 2.5: min at 3.5
    const std::array four = {box(Vec3(0.0f), Vec3(1.0f)), box(Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 1.0f, 1.0f)),
                             box(Vec3(2.0f, 0.0f, 0.0f), Vec3(4.0f, 1.0f, 1.0f)),
                             box(Vec3(10.0f, 0.0f, 0.0f), Vec3(11.0f, 1.0f, 1.0f))};
    std::array<f32, 4> gaps{};
    distributeOffsets(four, 0, DistributeMode::Gaps, gaps);
    // Sizes 1 + 1 + 2 + 1 = 5 in a span of 11: gaps of 2.
    CHECK(gaps[1] == doctest::Approx(2.0f));
    CHECK(gaps[2] == doctest::Approx(4.0f));
    CHECK(gaps[3] == 0.0f);

    // Two boxes, invalid boxes or a bad axis: nothing moves.
    std::array<f32, 2> two{1.0f, 1.0f};
    distributeOffsets(std::span(boxes).first(2), 0, DistributeMode::Centers, two);
    CHECK(two == std::array{0.0f, 0.0f});
    const std::array invalid = {boxes[0], Aabb{}, boxes[1]};
    distributeOffsets(invalid, 0, DistributeMode::Centers, out);
    CHECK(out == std::array{0.0f, 0.0f, 0.0f});
    distributeOffsets(boxes, 5, DistributeMode::Centers, out);
    CHECK(out == std::array{0.0f, 0.0f, 0.0f});
}

TEST_CASE("resting heights find the highest surface below a box, or else the ground plane")
{
    const Aabb falling = box(Vec3(0.0f, 5.0f, 0.0f), Vec3(1.0f, 6.0f, 1.0f));
    const std::array surfaces = {
        box(Vec3(-1.0f, 0.0f, -1.0f), Vec3(2.0f, 1.0f, 2.0f)), // a table below
        box(Vec3(-1.0f, 2.0f, -1.0f), Vec3(2.0f, 2.5f, 2.0f)), // a higher shelf below
        box(Vec3(5.0f, 3.0f, 5.0f), Vec3(6.0f, 4.0f, 6.0f)),   // beside it
        box(Vec3(-1.0f, 7.0f, -1.0f), Vec3(2.0f, 8.0f, 2.0f)), // above it
        box(Vec3(1.0f, 4.0f, 0.0f), Vec3(2.0f, 4.5f, 1.0f)),   // only touching its side
        Aabb{},
    };
    CHECK(restingHeight(falling, surfaces, 0.0f) == 2.5f);
    CHECK(restingHeight(falling, std::span(surfaces).first(1), 0.0f) == 1.0f);
    CHECK(restingHeight(falling, {}, 0.0f) == 0.0f);
    // A floor below the plane wins over it: the plane only catches what nothing else does.
    const std::array basement = {box(Vec3(-5.0f, -3.0f, -5.0f), Vec3(5.0f, -2.0f, 5.0f))};
    CHECK(restingHeight(falling, basement, 0.0f) == -2.0f);
    // A box resting slightly inside a surface stays on it.
    const Aabb resting = box(Vec3(0.0f, 2.4995f, 0.0f), Vec3(1.0f, 3.0f, 1.0f));
    CHECK(restingHeight(resting, surfaces, 0.0f) == 2.5f);
    // Below the ground with nothing below it: up to the plane; a point counts too.
    CHECK(restingHeight(box(Vec3(0.0f, -3.0f, 0.0f), Vec3(1.0f, -2.0f, 1.0f)), {}, 0.0f) == 0.0f);
    CHECK(restingHeight(box(Vec3(0.5f, 9.0f, 0.5f), Vec3(0.5f, 9.0f, 0.5f)), surfaces, 0.0f) == 8.0f);
    CHECK(restingHeight(Aabb{}, surfaces, -1.0f) == -1.0f);
}

TEST_CASE("transform.align and transform.distribute move entities by their boxes in one undo step each")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const Entity a = create(editor, "A", Vec3(0.5f, 0.0f, 0.0f));
    const Entity b = create(editor, "B", Vec3(4.0f, 0.0f, 0.0f));
    const Entity c = create(editor, "C", Vec3(10.0f, 0.0f, 0.0f));
    create(editor, "Empty", Vec3(7.0f, 3.0f, 0.0f));
    fixture.viewports.meshes = {
        ViewportBounds{a, box(Vec3(0.0f, -0.5f, -0.5f), Vec3(1.0f, 0.5f, 0.5f))},
        ViewportBounds{b, box(Vec3(3.0f, -1.0f, -1.0f), Vec3(5.0f, 1.0f, 1.0f))},
        ViewportBounds{c, box(Vec3(9.5f, -0.5f, -0.5f), Vec3(10.5f, 0.5f, 0.5f))},
    };
    const std::string before = fixture.scene();
    const usize steps = editor.history().steps().size();

    ok(editor, "transform.align", R"({"entities":["/A","/B"],"axis":"x","edge":"min"})");
    CHECK(positionOf(editor, "/B").x == doctest::Approx(1.0f));
    CHECK(positionOf(editor, "/A").x == doctest::Approx(0.5f));
    CHECK(editor.history().steps().size() == steps + 1);
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);

    // To the active (last) entity's box; an entity without a Mesh counts as its position.
    ok(editor, "transform.align", R"({"entities":["/Empty","/B"],"axis":"y","edge":"max","to":"active"})");
    CHECK(positionOf(editor, "/Empty").y == doctest::Approx(1.0f));
    CHECK(positionOf(editor, "/B").y == doctest::Approx(0.0f));
    ok(editor, "history.undo");

    ok(editor, "transform.distribute", R"({"entities":["/C","/A","/B"],"axis":"x"})");
    CHECK(positionOf(editor, "/B").x == doctest::Approx(5.25f)); // centre 4 -> (0.5 + 10) / 2
    ok(editor, "history.undo");
    ok(editor, "transform.distribute", R"({"entities":["/A","/B","/C"],"axis":"x","mode":"gaps"})");
    // Sizes 1 + 2 + 1 in a span of 10.5: gaps of 3.25, B's min at 4.25.
    CHECK(positionOf(editor, "/B").x == doctest::Approx(5.25f));
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);

    CHECK(fails(editor, "transform.distribute", R"({"entities":["/A","/B"],"axis":"x"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "transform.align", R"({"entities":["/A"],"axis":3})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "transform.align", R"({"entities":["/A"],"axis":"x","edge":9})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "transform.align", R"({"entities":["/A"],"axis":"x","to":4})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(
        fails(editor, "transform.distribute", R"({"entities":["/A","/B","/C"],"axis":"x","mode":2})").kind ==
        ErrorKind::ValidationFailed);
    CHECK(fails(editor, "transform.align", R"({"entities":[],"axis":"x"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fixture.scene() == before);
}

TEST_CASE("transform.drop_to_ground lets entities fall onto boxes below them or onto the ground plane")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const Entity table = create(editor, "Table", Vec3(0.0f, 0.5f, 0.0f));
    const Entity crate = create(editor, "Crate", Vec3(0.0f, 5.0f, 0.0f));
    ok(editor, "entity.create",
       R"({"name":"Lid","parent":"/Crate","components":{"illusion.Transform":{},"illusion.LocalTransform":{"position":[0,1,0]}}})");
    const Entity lid = *editor.entities().resolve("/Crate/Lid", editor.selection());
    create(editor, "Lamp", Vec3(3.0f, 4.0f, 3.0f));
    fixture.viewports.meshes = {
        ViewportBounds{table, box(Vec3(-1.0f, 0.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f))},
        ViewportBounds{crate, box(Vec3(-0.5f, 4.5f, -0.5f), Vec3(0.5f, 5.5f, 0.5f))},
        ViewportBounds{lid, box(Vec3(-0.5f, 5.5f, -0.5f), Vec3(0.5f, 6.0f, 0.5f))},
    };
    const std::string before = fixture.scene();

    // The crate with its lid lands on the table (the lid's box never counts as a surface); the lamp, with
    // nothing below it, lands on the plane.
    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate","/Crate/Lid","/Lamp"]})");
    CHECK(positionOf(editor, "/Crate").y == doctest::Approx(1.5f));
    CHECK(positionOf(editor, "/Crate/Lid").y == doctest::Approx(2.5f));
    CHECK(positionOf(editor, "/Lamp").y == doctest::Approx(0.0f));
    ok(editor, "history.undo");
    CHECK(fixture.scene() == before);

    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"],"onto":"plane"})");
    CHECK(positionOf(editor, "/Crate").y == doctest::Approx(0.5f));
    ok(editor, "history.undo");
    CHECK(fails(editor, "transform.drop_to_ground", R"({"entities":["/Crate"],"onto":2})").kind ==
          ErrorKind::ValidationFailed);

    // Without viewports every entity is a point.
    editor.setViewports(nullptr);
    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"]})");
    CHECK(positionOf(editor, "/Crate").y == doctest::Approx(0.0f));
}

TEST_CASE("transform.align_to_view turns entities to the viewport camera and can move them to its eye")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    create(editor, "Spot", Vec3(1.0f, 2.0f, 3.0f));
    fixture.viewports.camera.yaw = 90.0f;
    fixture.viewports.camera.pitch = -30.0f;
    const Quat expected = viewportRotation(fixture.viewports.camera);
    ok(editor, "transform.align_to_view", R"({"entities":["/Spot"]})");
    const JsonDocument placed = ok(editor, "transform.get", R"({"entity":"/Spot"})");
    const JsonValue rotation = placed.root().find("rotation");
    const Quat turned(static_cast<f32>(rotation[3].asF64().value_or(0.0)),
                      static_cast<f32>(rotation[0].asF64().value_or(0.0)),
                      static_cast<f32>(rotation[1].asF64().value_or(0.0)),
                      static_cast<f32>(rotation[2].asF64().value_or(0.0)));
    CHECK(std::abs(std::abs(glm::dot(turned, expected)) - 1.0f) < 1e-5f);
    CHECK(glm::length(positionOf(editor, "/Spot") - Vec3(1.0f, 2.0f, 3.0f)) < 1e-5f);

    ok(editor, "transform.align_to_view", R"({"entities":["/Spot"],"move":true})");
    CHECK(glm::length(positionOf(editor, "/Spot") - viewportEye(fixture.viewports.camera)) < 1e-4f);
    CHECK(fails(editor, "transform.align_to_view", R"({"entities":["/Spot"],"viewport":"nope"})").kind ==
          ErrorKind::NotFound);
    editor.setViewports(nullptr);
    CHECK(fails(editor, "transform.align_to_view", R"({"entities":["/Spot"]})").kind ==
          ErrorKind::Unsupported);
}

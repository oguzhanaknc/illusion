#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/core/UiActions.hpp>
#include <illusion/editor/core/ViewportHost.hpp>

#include <cmath>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

bool nearlyEqual(Vec3 a, Vec3 b, f32 tolerance = 1e-4f)
{
    return glm::length(a - b) <= tolerance;
}

Vec3 vec3At(const JsonValue& value)
{
    return Vec3(static_cast<f32>(value[0].asF64().value_or(0.0)),
                static_cast<f32>(value[1].asF64().value_or(0.0)),
                static_cast<f32>(value[2].asF64().value_or(0.0)));
}

bool missing(const JsonValue& value)
{
    return value.isAbsent() || value.isNull();
}

} // namespace

TEST_CASE("the viewport camera math places the eye behind the pivot and projects the pivot to the centre")
{
    ViewportCamera camera;
    camera.pivot = Vec3(1.0f, 2.0f, 3.0f);
    camera.distance = 5.0f;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    CHECK(nearlyEqual(viewportForward(camera), Vec3(0.0f, 0.0f, -1.0f)));
    CHECK(nearlyEqual(viewportEye(camera), Vec3(1.0f, 2.0f, 8.0f)));

    camera.pitch = -90.0f;
    CHECK(nearlyEqual(viewportForward(camera), Vec3(0.0f, -1.0f, 0.0f)));
    camera.pitch = -30.0f;
    camera.yaw = 90.0f;
    // Yaw 90 turns the view direction from -z towards -x.
    CHECK(viewportForward(camera).x < -0.8f);

    for (const ViewportProjection projection :
         {ViewportProjection::Perspective, ViewportProjection::Orthographic})
    {
        camera.projection = projection;
        const ViewportPoint centre = projectToViewport(camera, UVec2(640, 360), camera.pivot);
        CHECK(centre.inside);
        CHECK(std::abs(centre.pixel.x - 320.0f) < 0.01f);
        CHECK(std::abs(centre.pixel.y - 180.0f) < 0.01f);
        CHECK(centre.depth > 0.0f);
        CHECK(centre.depth <= 1.0f);
    }
    camera.projection = ViewportProjection::Perspective;
    // Behind the eye.
    const ViewportPoint behind =
        projectToViewport(camera, UVec2(640, 360), viewportEye(camera) - viewportForward(camera) * 2.0f);
    CHECK_FALSE(behind.inFront);
    CHECK_FALSE(behind.inside);
    // Up in the world is up in the image.
    const ViewportPoint above =
        projectToViewport(camera, UVec2(640, 360), camera.pivot + Vec3(0.0f, 0.5f, 0.0f));
    CHECK(above.pixel.y < 180.0f);
    CHECK(projectToViewport(camera, UVec2(0, 0), camera.pivot).inside == false);
}

TEST_CASE("standard views turn around the pivot and keep the apparent size")
{
    ViewportCamera camera;
    camera.distance = 10.0f;
    camera.fieldOfView = 60.0f;
    const ViewportCamera top = viewportPreset(camera, ViewportView::Top);
    CHECK(top.projection == ViewportProjection::Orthographic);
    CHECK(nearlyEqual(viewportForward(top), Vec3(0.0f, -1.0f, 0.0f)));
    CHECK(top.orthographicHeight == doctest::Approx(2.0 * 10.0 * std::tan(glm::radians(30.0))));
    CHECK(nearlyEqual(top.pivot, camera.pivot));

    CHECK(nearlyEqual(viewportForward(viewportPreset(camera, ViewportView::Bottom)), Vec3(0.0f, 1.0f, 0.0f)));
    CHECK(nearlyEqual(viewportForward(viewportPreset(camera, ViewportView::Front)), Vec3(0.0f, 0.0f, -1.0f)));
    CHECK(nearlyEqual(viewportForward(viewportPreset(camera, ViewportView::Back)), Vec3(0.0f, 0.0f, 1.0f)));
    CHECK(nearlyEqual(viewportForward(viewportPreset(camera, ViewportView::Left)), Vec3(1.0f, 0.0f, 0.0f)));
    CHECK(nearlyEqual(viewportForward(viewportPreset(camera, ViewportView::Right)), Vec3(-1.0f, 0.0f, 0.0f)));

    ViewportCamera zoomed = top;
    zoomed.orthographicHeight *= 2.0f;
    const ViewportCamera back = viewportPreset(zoomed, ViewportView::Perspective);
    CHECK(back.projection == ViewportProjection::Perspective);
    CHECK(back.distance == doctest::Approx(20.0));
}

TEST_CASE("aiming a viewport camera puts its eye at the point and keeps the pivot")
{
    ViewportCamera camera;
    camera.pivot = Vec3(0.0f, 1.0f, 0.0f);
    const std::optional<ViewportCamera> aimed = aimViewportCamera(camera, Vec3(6.0f, 4.0f, 6.0f));
    REQUIRE(aimed.has_value());
    CHECK(nearlyEqual(viewportEye(*aimed), Vec3(6.0f, 4.0f, 6.0f), 1e-3f));
    CHECK(nearlyEqual(aimed->pivot, camera.pivot));
    CHECK_FALSE(aimViewportCamera(camera, camera.pivot).has_value());
    // Straight down keeps the yaw.
    camera.yaw = 33.0f;
    const std::optional<ViewportCamera> down = aimViewportCamera(camera, Vec3(0.0f, 9.0f, 0.0f));
    REQUIRE(down.has_value());
    CHECK(down->yaw == doctest::Approx(33.0));
    CHECK(down->pitch == doctest::Approx(-90.0));
}

TEST_CASE("viewport commands fail with code 1400 without viewports")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    editor.setViewports(nullptr);
    for (const std::string_view name : {"viewport.list", "viewport.camera_get", "viewport.frame",
                                        "viewport.set_mode", "viewport.set_overlays"})
    {
        INFO(name);
        const CommandError error = fails(editor, name, "{}");
        CHECK(error.kind == ErrorKind::Unsupported);
        CHECK(static_cast<int>(error.kind) == 1400);
    }
    CHECK(fails(editor, "viewport.pick", R"({"x":1,"y":1})").kind == ErrorKind::Unsupported);
    CHECK(fails(editor, "viewport.project", R"({"point":[0,0,0]})").kind == ErrorKind::Unsupported);
    CHECK(fails(editor, "viewport.view", R"({"view":"top"})").kind == ErrorKind::Unsupported);
}

TEST_CASE("viewport.camera_set changes the given fields and can aim the eye")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const JsonDocument set =
        ok(editor, "viewport.camera_set", R"({"pivot":[1,2,3],"yaw":10,"animate":false})");
    CHECK(fixture.viewports.camera.pivot == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(fixture.viewports.camera.yaw == 10.0f);
    CHECK(fixture.viewports.camera.distance == ViewportCamera{}.distance);
    CHECK_FALSE(fixture.viewports.animated);
    CHECK(set.root().find("viewport").asString() == std::optional<std::string_view>("scene"));
    CHECK(nearlyEqual(vec3At(set.root().find("eye")), viewportEye(fixture.viewports.camera)));

    const JsonDocument aimed = ok(editor, "viewport.camera_set", R"({"eye":[1,2,13]})");
    CHECK(fixture.viewports.animated);
    CHECK(nearlyEqual(vec3At(aimed.root().find("eye")), Vec3(1.0f, 2.0f, 13.0f), 1e-3f));
    CHECK(nearlyEqual(vec3At(aimed.root().find("forward")), Vec3(0.0f, 0.0f, -1.0f), 1e-3f));

    CHECK(fails(editor, "viewport.camera_set", R"({"eye":[1,2,3]})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.camera_set", R"({"eye":[0,0,9],"yaw":4})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.camera_set", R"({"pitch":91})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.camera_set", R"({"distance":0})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.camera_set", R"({"pivot":[1e9,0,0]})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.camera_get", R"({"viewport":"game"})").kind == ErrorKind::NotFound);

    // A dry run reports the camera without moving it.
    const ViewportCamera before = fixture.viewports.camera;
    REQUIRE(
        test::run(editor, "viewport.camera_set", R"({"yaw":77})", CommandOptions{.dryRun = true}).hasValue());
    CHECK(fixture.viewports.camera.yaw == before.yaw);
}

TEST_CASE("viewport.frame frames the given entities, else the selection, else everything")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "viewport.frame", R"({"entities":["/Game/Board","/Light"],"animate":false})");
    CHECK(fixture.viewports.framed.size() == 2);
    CHECK(fixture.viewports.camera.distance == 3.0f);

    ok(editor, "selection.set", R"({"entities":["/Game/Player"]})");
    ok(editor, "viewport.frame");
    REQUIRE(fixture.viewports.framed.size() == 1);
    CHECK(fixture.viewports.framed[0] ==
          editor.entities().resolve("/Game/Player", editor.selection()).value());

    ok(editor, "viewport.frame", R"({"entities":[]})");
    CHECK(fixture.viewports.framed.empty());
    ok(editor, "selection.clear");
    ok(editor, "viewport.frame");
    CHECK(fixture.viewports.framed.empty());
    CHECK(fails(editor, "viewport.frame", R"({"entities":["/Missing"]})").kind == ErrorKind::NotFound);
}

TEST_CASE("viewport.view, set_mode and set_overlays change only what they name")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "viewport.view", R"({"view":"front","animate":false})");
    CHECK(fixture.viewports.camera.projection == ViewportProjection::Orthographic);
    CHECK(fixture.viewports.camera.yaw == 0.0f);

    const JsonDocument mode = ok(editor, "viewport.set_mode", R"({"shading":"albedo"})");
    CHECK(fixture.viewports.settings.shading == ViewportShading::Albedo);
    CHECK_FALSE(fixture.viewports.settings.wireframe);
    CHECK(mode.root().pointer("/settings/shading").asString() == std::optional<std::string_view>("albedo"));
    ok(editor, "viewport.set_overlays", R"({"grid":false})");
    CHECK_FALSE(fixture.viewports.settings.grid);
    CHECK(fixture.viewports.settings.selectionOutline);
    CHECK(fixture.viewports.settings.shading == ViewportShading::Albedo);
    CHECK(fails(editor, "viewport.set_mode", R"({"shading":"toon"})").kind == ErrorKind::ValidationFailed);

    const JsonDocument list = ok(editor, "viewport.list");
    REQUIRE(list.root().find("viewports").size() == 1);
    CHECK(list.root().pointer("/viewports/0/width").asU64() == 1280u);
    CHECK(list.root().pointer("/viewports/0/settings/grid").asBool() == std::optional<bool>(false));
}

TEST_CASE("viewport.project returns the pixel of an entity's position or of a point")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    ok(editor, "viewport.camera_set", R"({"pivot":[1,0,0],"animate":false})");
    const JsonDocument game = ok(editor, "viewport.project", R"({"entity":"/Game"})");
    CHECK(game.root().find("x").asF64().value_or(0.0) == doctest::Approx(640.0).epsilon(1e-3));
    CHECK(game.root().find("y").asF64().value_or(0.0) == doctest::Approx(360.0).epsilon(1e-3));
    CHECK(game.root().find("inside").asBool() == std::optional<bool>(true));
    // The child's world position comes through its parent.
    const JsonDocument board = ok(editor, "viewport.project", R"({"entity":"/Game/Board"})");
    CHECK(board.root().find("y").asF64().value_or(1e9) < 360.0);

    CHECK(fails(editor, "viewport.project", "{}").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.project", R"({"entity":"/Game","point":[0,0,0]})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("viewport.pick returns the hit and changes the selection as asked")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const Entity board = editor.entities().resolve("/Game/Board", editor.selection()).value();
    const Entity light = editor.entities().resolve("/Light", editor.selection()).value();

    fixture.viewports.hit = ViewportHit{board, 0, 2};
    const JsonDocument picked = ok(editor, "viewport.pick", R"({"x":10.5,"y":20,"cycle":true})");
    CHECK(fixture.viewports.pickedAt == Vec2(10.5f, 20.0f));
    CHECK(fixture.viewports.cycled);
    CHECK(picked.root().pointer("/entity/path").asString() == std::optional<std::string_view>("/Game/Board"));
    CHECK(picked.root().find("candidates").asU64() == 2u);
    CHECK(editor.selection().empty());

    ok(editor, "viewport.pick", R"({"x":1,"y":1,"select":"replace"})");
    CHECK(editor.selection().primary() == editor.entities().guidOf(board));
    fixture.viewports.hit = ViewportHit{light, 1, 2};
    ok(editor, "viewport.pick", R"({"x":1,"y":1,"select":"add"})");
    CHECK(editor.selection().size() == 2);
    ok(editor, "viewport.pick", R"({"x":1,"y":1,"select":"toggle"})");
    CHECK(editor.selection().size() == 1);
    ok(editor, "viewport.pick", R"({"x":1,"y":1,"select":"toggle"})");
    CHECK(editor.selection().size() == 2);

    // The background clears the selection only with "replace".
    fixture.viewports.hit.reset();
    const JsonDocument none = ok(editor, "viewport.pick", R"({"x":1,"y":1,"select":"add"})");
    CHECK(missing(none.root().find("entity")));
    CHECK(editor.selection().size() == 2);
    ok(editor, "viewport.pick", R"({"x":1,"y":1,"select":"replace"})");
    CHECK(editor.selection().empty());

    // A hit that died before the pick completed counts as the background.
    fixture.viewports.hit = ViewportHit{Entity{12345, 7}, 0, 1};
    CHECK(missing(ok(editor, "viewport.pick", R"({"x":1,"y":1})").root().find("entity")));

    CHECK(fails(editor, "viewport.pick", R"({"x":1280,"y":1})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.pick", R"({"x":-1,"y":1})").kind == ErrorKind::ValidationFailed);
    fixture.viewports.size = UVec2(0);
    CHECK(fails(editor, "viewport.pick", R"({"x":1,"y":1})").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("the frame actions run viewport.frame on the selection or on everything")
{
    const UiAction* selected = findUiAction("view.frame_selected");
    const UiAction* all = findUiAction("view.frame_all");
    REQUIRE(selected != nullptr);
    REQUIRE(all != nullptr);
    CHECK(selected->command == "viewport.frame");
    CHECK(selected->params.empty());
    CHECK(selected->milestone.empty());
    CHECK(all->command == "viewport.frame");
    CHECK(all->params == R"({"entities":[]})");
}

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/core/UiActions.hpp>
#include <illusion/editor/core/ViewCube.hpp>
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
    CHECK(fails(editor, "viewport.camera_get", R"({"viewport":"game"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "viewport.camera_get", R"({"viewport":"scene-9"})").kind == ErrorKind::NotFound);

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
    REQUIRE(list.root().find("viewports").size() == 2);
    CHECK(list.root().pointer("/viewports/1/id").asString() == std::optional<std::string_view>("game"));
    CHECK(list.root().pointer("/viewports/1/kind").asString() == std::optional<std::string_view>("game"));
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

TEST_CASE("the image of a viewport fills its panel, keeps an aspect inside it or has a fixed resolution")
{
    ViewportSettings settings;
    CHECK(viewportImageSize(UVec2(800, 600), settings) == UVec2(800, 600));
    CHECK(viewportImageSize(UVec2(0, 600), settings) == UVec2(0));
    settings.aspect = ViewportAspect::Wide16x9;
    CHECK(viewportImageSize(UVec2(800, 600), settings) == UVec2(800, 450));
    CHECK(viewportImageSize(UVec2(1600, 450), settings) == UVec2(800, 450));
    settings.aspect = ViewportAspect::Square;
    CHECK(viewportImageSize(UVec2(800, 600), settings) == UVec2(600, 600));
    // A thin panel still gives at least a pixel.
    settings.aspect = ViewportAspect::Ultrawide21x9;
    CHECK(viewportImageSize(UVec2(1, 600), settings).y >= 1u);
    settings.aspect = ViewportAspect::Fixed;
    settings.width = 640;
    settings.height = 360;
    CHECK(viewportImageSize(UVec2(100, 100), settings) == UVec2(640, 360));
}

TEST_CASE("projecting through a view-projection matrix matches projecting through the camera")
{
    ViewportCamera camera;
    camera.pivot = Vec3(1.0f, 2.0f, 3.0f);
    const UVec2 size(1280, 720);
    const Mat4 viewProjection =
        viewportProjectionMatrix(camera, 1280.0f / 720.0f) * viewportViewMatrix(camera);
    for (const Vec3 point : {Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 0.0f, -2.0f), Vec3(-3.0f, 5.0f, 1.0f)})
    {
        const ViewportPoint a = projectToViewport(camera, size, point);
        const ViewportPoint b = projectToViewport(viewProjection, size, point);
        CHECK(glm::length(a.pixel - b.pixel) < 1e-3f);
        CHECK(a.inside == b.inside);
    }
    CHECK(projectToViewport(viewProjection, size, camera.pivot).inside);
    CHECK_FALSE(projectToViewport(viewProjection, UVec2(0), camera.pivot).inside);
}

TEST_CASE("viewport.create opens up to three extra viewports and viewport.close closes them")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    fixture.viewports.camera.yaw = 12.0f;
    const JsonDocument created = ok(editor, "viewport.create");
    CHECK(created.root().find("id").asString() == std::optional<std::string_view>("scene-2"));
    CHECK(created.root().pointer("/camera/yaw").asF64() == doctest::Approx(12.0));
    ok(editor, "viewport.create");
    ok(editor, "viewport.create");
    CHECK(fails(editor, "viewport.create", "{}").kind == ErrorKind::LimitExceeded);
    CHECK(ok(editor, "viewport.list").root().find("viewports").size() == 5);

    // Secondary viewports take the camera commands; "scene" and "game" stay open.
    ok(editor, "viewport.camera_set", R"({"viewport":"scene-3","yaw":40,"animate":false})");
    CHECK(fixture.viewports.find("scene-3").camera.yaw == 40.0f);
    CHECK(fixture.viewports.camera.yaw == 12.0f);
    CHECK(fails(editor, "viewport.close", R"({"viewport":"scene"})").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "viewport.close", R"({"viewport":"game"})").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "viewport.close", R"({"viewport":"scene-9"})").kind == ErrorKind::NotFound);
    ok(editor, "viewport.close", R"({"viewport":"scene-3"})");
    CHECK(fixture.viewports.secondary.size() == 2);
}

TEST_CASE("the game view refuses editor camera commands and reports the Camera it looks through")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    for (const std::string_view command :
         {"viewport.camera_get", "viewport.frame", "camera.bookmark_set", "camera.bookmark_go"})
    {
        CAPTURE(command);
        CHECK(fails(editor, command, R"({"viewport":"game"})").kind == ErrorKind::PreconditionFailed);
    }
    CHECK(fails(editor, "viewport.view", R"({"viewport":"game","view":"top"})").kind ==
          ErrorKind::PreconditionFailed);
    // Without a Camera there is nothing to capture.
    CHECK(fails(editor, "viewport.screenshot", R"({"viewport":"game"})").kind ==
          ErrorKind::PreconditionFailed);
    CHECK(missing(ok(editor, "viewport.list").root().pointer("/viewports/1/sceneCamera")));

    fixture.viewports.gameCamera = editor.entities().resolve("/Light", editor.selection()).value();
    CHECK(ok(editor, "viewport.list").root().pointer("/viewports/1/sceneCamera/path").asString() ==
          std::optional<std::string_view>("/Light"));
    ok(editor, "viewport.screenshot", R"({"viewport":"game"})");
}

TEST_CASE("viewport.set_aspect sets presets and fixed resolutions and validates them")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "viewport.set_aspect", R"({"aspect":"16:9"})");
    CHECK(fixture.viewports.settings.aspect == ViewportAspect::Wide16x9);
    const JsonDocument fixed =
        ok(editor, "viewport.set_aspect", R"({"viewport":"game","aspect":"fixed","width":640,"height":360})");
    CHECK(fixture.viewports.gameSettings.aspect == ViewportAspect::Fixed);
    CHECK(fixture.viewports.gameSettings.width == 640u);
    CHECK(fixed.root().pointer("/settings/aspect").asString() == std::optional<std::string_view>("fixed"));
    // Leaving "fixed" forgets the resolution.
    ok(editor, "viewport.set_aspect", R"({"viewport":"game","aspect":"free"})");
    CHECK(fixture.viewports.gameSettings.width == 0u);

    CHECK(fails(editor, "viewport.set_aspect", R"({"aspect":"fixed"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.set_aspect", R"({"aspect":"fixed","width":8,"height":360})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.set_aspect", R"({"aspect":"fixed","width":640,"height":9000})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.set_aspect", R"({"aspect":"4:3","width":640})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.set_aspect", R"({"aspect":"5:4"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.set_aspect", R"({"aspect":9})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("viewport.sample averages the pixels around a point and reports the hue")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    fixture.viewports.color = {0, 255, 0, 255};
    const JsonDocument green = ok(editor, "viewport.sample", R"({"x":10,"y":20,"radius":2})");
    CHECK(green.root().find("pixels").asU64() == 25u);
    CHECK(green.root().find("hue").asF64() == doctest::Approx(120.0));
    CHECK(green.root().find("saturation").asF64() == doctest::Approx(1.0));
    CHECK(green.root().pointer("/color/1").asF64() == doctest::Approx(1.0));
    CHECK(fixture.viewports.readOrigin == UVec2(8, 18));
    CHECK(fixture.viewports.readExtent == UVec2(5, 5));

    // The square is clipped at the edges of the image.
    fixture.viewports.color = {128, 128, 128, 255};
    const JsonDocument grey = ok(editor, "viewport.sample", R"({"x":0,"y":719,"radius":3})");
    CHECK(fixture.viewports.readOrigin == UVec2(0, 716));
    CHECK(fixture.viewports.readExtent == UVec2(4, 4));
    CHECK(grey.root().find("saturation").asF64() == doctest::Approx(0.0));
    CHECK(grey.root().find("hue").asF64() == doctest::Approx(0.0));

    fixture.viewports.color = {0, 0, 255, 255};
    CHECK(ok(editor, "viewport.sample", R"({"x":1,"y":1})").root().find("hue").asF64() ==
          doctest::Approx(240.0));
    fixture.viewports.color = {255, 0, 128, 255};
    CHECK(ok(editor, "viewport.sample", R"({"x":1,"y":1})").root().find("hue").asF64() ==
          doctest::Approx(329.9).epsilon(0.01));

    CHECK(fails(editor, "viewport.sample", R"({"x":1280,"y":1})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.sample", R"({"x":1,"y":1,"radius":9})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("viewport.pick_rect returns the entities in a rectangle and changes the selection")
{
    EditorFixture fixture;
    fixture.buildScene();
    EditorContext& editor = *fixture.editor;
    const Entity board = editor.entities().resolve("/Game/Board", editor.selection()).value();
    const Entity light = editor.entities().resolve("/Light", editor.selection()).value();
    fixture.viewports.entities = {board, light, Entity{54321, 3}};

    const JsonDocument found =
        ok(editor, "viewport.pick_rect", R"({"x":100,"y":80,"width":400,"height":300})");
    CHECK(found.root().find("entities").size() == 2); // the dead entity is left out
    CHECK(editor.selection().empty());
    CHECK(fixture.viewports.readChannel == ViewportChannel::Entities);
    CHECK(fixture.viewports.readOrigin == UVec2(100, 80));
    CHECK(fixture.viewports.readExtent == UVec2(400, 300));

    ok(editor, "viewport.pick_rect", R"({"x":0,"y":0,"width":10,"height":10,"select":"replace"})");
    CHECK(editor.selection().size() == 2);
    fixture.viewports.entities = {board};
    ok(editor, "viewport.pick_rect", R"({"x":0,"y":0,"width":10,"height":10,"select":"toggle"})");
    CHECK(editor.selection().size() == 1);
    CHECK(editor.selection().primary() == editor.entities().guidOf(light));
    fixture.viewports.entities.clear();
    ok(editor, "viewport.pick_rect", R"({"x":0,"y":0,"width":10,"height":10,"select":"replace"})");
    CHECK(editor.selection().empty());

    // The rectangle is clipped to the image.
    ok(editor, "viewport.pick_rect", R"({"x":1200,"y":700,"width":500,"height":500})");
    CHECK(fixture.viewports.readExtent == UVec2(80, 20));
    CHECK(fails(editor, "viewport.pick_rect", R"({"x":1300,"y":0,"width":5,"height":5})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.pick_rect", R"({"x":0,"y":0,"width":0,"height":5})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("viewport.screenshot returns a PNG of the viewport's size")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const JsonDocument shot = ok(editor, "viewport.screenshot");
    CHECK(shot.root().find("width").asU64() == 1280u);
    CHECK(shot.root().find("height").asU64() == 720u);
    CHECK(shot.root().pointer("/_images/0/mimeType").asString() ==
          std::optional<std::string_view>("image/png"));
    // "\x89PNG" in base64.
    CHECK(shot.root().pointer("/_images/0/data").asString().value_or("").starts_with("iVBORw0KGgo"));
    fixture.viewports.size = UVec2(0);
    CHECK(fails(editor, "viewport.screenshot", "{}").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("camera bookmarks store a viewport camera and move to it until the scene is replaced")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    fixture.viewports.camera.yaw = 33.0f;
    ok(editor, "camera.bookmark_set", R"({"slot":3})");
    fixture.viewports.camera.yaw = 90.0f;
    const JsonDocument went = ok(editor, "camera.bookmark_go", R"({"slot":3,"animate":false})");
    CHECK(fixture.viewports.camera.yaw == 33.0f);
    CHECK_FALSE(fixture.viewports.animated);
    CHECK(went.root().pointer("/camera/yaw").asF64() == doctest::Approx(33.0));
    CHECK(fails(editor, "camera.bookmark_go", R"({"slot":4})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "camera.bookmark_set", R"({"slot":0})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(editor, "camera.bookmark_set", R"({"slot":10})").kind == ErrorKind::ValidationFailed);

    // Bookmarks are shared by the Scene viewports.
    ok(editor, "viewport.create");
    ok(editor, "camera.bookmark_go", R"({"viewport":"scene-2","slot":3,"animate":false})");
    CHECK(fixture.viewports.find("scene-2").camera.yaw == 33.0f);

    ok(editor, "scene.new", R"({"discardChanges":true})");
    CHECK(fails(editor, "camera.bookmark_go", R"({"slot":3})").kind == ErrorKind::NotFound);
}

TEST_CASE("render.stats, gpu_info and diagnostics report the viewports' GPU, or nothing without one")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    fixture.viewports.stats.visible = 7;
    fixture.viewports.stats.frames = 2;
    const JsonDocument stats = ok(editor, "render.stats");
    CHECK(stats.root().pointer("/stats/visible").asU64() == 7u);
    CHECK(stats.root().find("width").asU64() == 1280u);
    CHECK(ok(editor, "render.gpu_info").root().pointer("/gpu/name").asString() ==
          std::optional<std::string_view>("Fake GPU"));
    const JsonDocument diagnostics = ok(editor, "render.diagnostics");
    CHECK(diagnostics.root().find("available").asBool() == std::optional<bool>(true));
    CHECK(diagnostics.root().find("validationErrors").asU64() == 0u);

    editor.setViewports(nullptr);
    CHECK(ok(editor, "render.gpu_info").root().find("available").asBool() == std::optional<bool>(false));
    CHECK(ok(editor, "render.diagnostics").root().find("available").asBool() == std::optional<bool>(false));
    CHECK(fails(editor, "render.stats", "{}").kind == ErrorKind::Unsupported);
}

TEST_CASE("viewport.set_overlays changes the named overlays, visualiser kinds and scope only")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    const ViewportSettings defaults;
    CHECK(defaults.hover);
    CHECK(defaults.icons);
    CHECK(defaults.viewCube);
    CHECK_FALSE(defaults.stats);
    CHECK(defaults.visualiserScope == ViewportVisualiserScope::Selected);

    const JsonDocument result = ok(editor, "viewport.set_overlays",
                                   R"({"stats":true,"icons":false,"visualisers":{"colliders":false},)"
                                   R"("visualiserScope":"all"})");
    const ViewportSettings& settings = fixture.viewports.settings;
    CHECK(settings.stats);
    CHECK_FALSE(settings.icons);
    CHECK(settings.hover);
    CHECK(settings.viewCube);
    CHECK(settings.cameraPreview);
    CHECK_FALSE(settings.visualisers.colliders);
    CHECK(settings.visualisers.lights);
    CHECK(settings.visualisers.audio);
    CHECK(settings.visualiserScope == ViewportVisualiserScope::All);
    CHECK(result.root().pointer("/settings/visualisers/colliders").asBool() == std::optional<bool>(false));
    CHECK(result.root().pointer("/settings/visualiserScope").asString() ==
          std::optional<std::string_view>("all"));

    ok(editor, "viewport.set_overlays", R"({"hover":false,"viewCube":false,"cameraPreview":false})");
    CHECK_FALSE(settings.hover);
    CHECK_FALSE(settings.viewCube);
    CHECK_FALSE(settings.cameraPreview);
    CHECK(settings.stats);
    CHECK_FALSE(settings.visualisers.colliders);
    CHECK(fails(editor, "viewport.set_overlays", R"({"visualiserScope":"some"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "viewport.set_overlays", R"({"visualisers":{"meshes":true}})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("an agent's selection is recorded so that the Scene view can follow it")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    ok(editor, "entity.create", R"({"name":"A"})");
    ok(editor, "entity.create", R"({"name":"B"})");
    CHECK(editor.agentSelectionVersion() == 0);

    CommandOptions agent;
    agent.origin = "rpc:claude";
    ok(editor, "selection.set", R"({"entities":["/A"]})", agent);
    const u64 followed = editor.agentSelectionVersion();
    CHECK(followed != 0);
    CHECK(followed == editor.selection().version());

    // A person's change leaves the agent's version behind; a command that keeps the selection changes
    // nothing.
    ok(editor, "selection.set", R"({"entities":["/B"]})");
    CHECK(editor.agentSelectionVersion() == followed);
    CHECK(editor.selection().version() != followed);
    ok(editor, "selection.get", "{}", agent);
    CHECK(editor.agentSelectionVersion() == followed);
}

TEST_CASE("the view cube shows the faces towards the camera, farthest first")
{
    ViewportCamera camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    std::array<ViewCubeFace, 6> faces{};
    // Looking along -z: only the front face.
    REQUIRE(viewCubeFaces(viewportRotation(camera), faces) == 1);
    CHECK(faces[0].normal == Vec3(0.0f, 0.0f, 1.0f));
    CHECK(faces[0].view == ViewportView::Front);
    CHECK(faces[0].facing == doctest::Approx(1.0f));
    CHECK(faces[0].center.x == doctest::Approx(0.0f).epsilon(1e-5));
    // The front face's corners span the cube: x right, y down.
    CHECK(faces[0].corners[0].x == doctest::Approx(-1.0f));
    CHECK(faces[0].corners[0].y == doctest::Approx(1.0f));

    camera.yaw = 30.0f;
    camera.pitch = -20.0f;
    const u32 count = viewCubeFaces(viewportRotation(camera), faces);
    REQUIRE(count == 3);
    for (u32 i = 1; i < count; ++i)
    {
        CHECK(faces[i - 1].facing <= faces[i].facing);
    }
    // Looking down from the front right: the top, front and right faces.
    bool top = false;
    bool front = false;
    bool right = false;
    for (u32 i = 0; i < count; ++i)
    {
        top = top || faces[i].view == ViewportView::Top;
        front = front || faces[i].view == ViewportView::Front;
        right = right || faces[i].view == ViewportView::Right;
    }
    CHECK((top && front && right));

    const std::array<ViewAxisEnd, 6> ends = viewAxisEnds(viewportRotation(camera));
    for (usize i = 1; i < ends.size(); ++i)
    {
        CHECK(ends[i - 1].depth <= ends[i].depth);
    }
}

TEST_CASE("picking the view cube finds faces, edges and corners and turns the camera to look from them")
{
    ViewportCamera camera;
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    const Quat rotation = viewportRotation(camera);
    CHECK(viewCubePick(rotation, Vec2(0.0f)) == std::optional<Vec3>(Vec3(0.0f, 0.0f, 1.0f)));
    // y is down in the image: the upper right corner of the front face.
    CHECK(viewCubePick(rotation, Vec2(0.95f, -0.95f)) == std::optional<Vec3>(Vec3(1.0f, 1.0f, 1.0f)));
    CHECK(viewCubePick(rotation, Vec2(0.0f, 0.9f)) == std::optional<Vec3>(Vec3(0.0f, -1.0f, 1.0f)));
    CHECK_FALSE(viewCubePick(rotation, Vec2(1.5f, 0.0f)).has_value());
    CHECK_FALSE(viewCubePick(rotation, Vec2(std::nanf(""), 0.0f)).has_value());

    CHECK(viewCubeView(Vec3(0.0f, 1.0f, 0.0f)) == std::optional<ViewportView>(ViewportView::Top));
    CHECK(viewCubeView(Vec3(-1.0f, 0.0f, 0.0f)) == std::optional<ViewportView>(ViewportView::Left));
    CHECK_FALSE(viewCubeView(Vec3(1.0f, 1.0f, 0.0f)).has_value());

    // Each face's view looks along -normal, like the camera viewCubeCamera() turns to.
    for (const Vec3 normal : {Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f),
                              Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)})
    {
        const ViewportCamera preset = viewportPreset(camera, *viewCubeView(normal));
        CHECK(nearlyEqual(viewportForward(preset), -normal));
        CHECK(nearlyEqual(viewportForward(viewCubeCamera(camera, normal)), -normal));
    }
    // A corner looks diagonally down, keeping the pivot and distance.
    camera.pivot = Vec3(2.0f, 1.0f, 0.0f);
    const ViewportCamera corner = viewCubeCamera(camera, Vec3(1.0f, 1.0f, 1.0f));
    CHECK(corner.yaw == doctest::Approx(45.0f));
    CHECK(corner.pitch == doctest::Approx(-35.2644f).epsilon(1e-4));
    CHECK(corner.pivot == camera.pivot);
    CHECK(corner.distance == camera.distance);
    CHECK(nearlyEqual(viewportForward(corner), -glm::normalize(Vec3(1.0f))));
    // A zero direction keeps the camera.
    CHECK(viewCubeCamera(camera, Vec3(0.0f)).yaw == camera.yaw);
}

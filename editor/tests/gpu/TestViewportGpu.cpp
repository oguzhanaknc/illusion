#include "core/EditorCoreTestSupport.hpp"
#include "gpu/GoldenSupport.hpp"

#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/ReadbackRing.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <cmath>
#include <cstring>
#include <format>

using namespace illusion;
using namespace illusion::editor;

// GPU tests of the editor viewports: picking through viewport.project and viewport.pick, cycling, hidden
// and locked entities, the selection outline and an overlay golden image.

namespace
{

constexpr UVec2 kSize{320, 180};

struct GpuViewports
{
    std::unique_ptr<RenderDevice> device;
    test::EditorFixture fixture;
    ui::UiContext context;
    std::unique_ptr<ui::OffscreenUi> offscreen;
    std::unique_ptr<viewport::ViewportService> service;
    std::unique_ptr<ReadbackRing> readbacks;

    GpuViewports()
        : device(RenderDevice::create(test::headlessDeviceDesc()))
    {
        REQUIRE(device != nullptr);
        reflectRenderComponents();
        offscreen = ui::OffscreenUi::create(*device, context, kSize);
        REQUIRE(offscreen != nullptr);
        service = viewport::ViewportService::create(*device, *fixture.editor, offscreen->renderer());
        REQUIRE(service != nullptr);
        CHECK(fixture.editor->viewports() == service.get());
        readbacks = ReadbackRing::create(*device, 1);
        REQUIRE(readbacks != nullptr);
    }

    ~GpuViewports()
    {
        service.reset();
        offscreen.reset();
        if (device)
        {
            device->waitIdle();
            CHECK(device->validationErrorCount() == 0);
        }
    }

    GpuViewports(const GpuViewports&) = delete;
    GpuViewports& operator=(const GpuViewports&) = delete;

    EditorContext& editor() { return *fixture.editor; }

    // One frame of the Scene panel: the viewport is drawn at kSize and rendered.
    void frame()
    {
        ViewportInput input;
        input.deltaSeconds = 1.0f / 60.0f;
        const ViewportFrame drawn = service->frame("scene", kSize, input, ViewportStyle{});
        CHECK(drawn.texture != 0);
        service->render(true);
        device->waitIdle();
        service->waitIdle();
    }

    // Runs @p name (an async command) to completion, rendering a frame between steps.
    JsonDocument run(std::string_view name, std::string_view params)
    {
        const JsonDocument doc = test::parseJson(params);
        Expected<std::unique_ptr<RunningCommand>, CommandError> started =
            editor().startJson(name, doc.root());
        REQUIRE_MESSAGE(started.hasValue(), (started ? std::string() : started.error().message));
        for (int i = 0; i < 16; ++i)
        {
            Expected<std::optional<std::string>, CommandError> updated = started.value()->update();
            REQUIRE_MESSAGE(updated.hasValue(), (updated ? std::string() : updated.error().message));
            if (updated->has_value())
            {
                return test::parseJson(**updated);
            }
            frame();
        }
        FAIL("the command did not finish");
        return JsonDocument{};
    }

    std::string pickAt(f64 x, f64 y, bool cycle)
    {
        const JsonDocument picked = run(
            "viewport.pick", std::format(R"({{"x":{},"y":{},"cycle":{}}})", x, y, cycle ? "true" : "false"));
        return std::string(picked.root().pointer("/entity/path").asString().value_or(""));
    }

    Image capture()
    {
        nvrhi::ITexture* target = service->target("scene");
        REQUIRE(target != nullptr);
        const std::optional<u64> ticket = readbacks->request(target);
        REQUIRE(ticket.has_value());
        Image image;
        image.mips.push_back(ImageMip{kSize.x, kSize.y, 0, static_cast<usize>(kSize.x) * kSize.y * 4});
        image.data.resize(image.mips[0].size);
        REQUIRE(readbacks->read(*ticket, image.data, true) == ReadbackStatus::Ready);
        return image;
    }
};

// Two cubes on the view axis (Front in front of Back) and one to the side, seen from +z.
void buildScene(GpuViewports& gpu)
{
    EditorContext& editor = gpu.editor();
    const auto cube = [&](std::string_view name, Vec3 position) {
        test::ok(editor, "entity.create",
                 std::format(R"({{"name":"{}","components":{{"illusion.Transform":{{"position":[{},{},{}]}},)"
                             R"("illusion.MeshRenderer":{{"mesh":"builtin:/mesh/cube"}}}}}})",
                             name, position.x, position.y, position.z));
    };
    cube("Front", Vec3(0.0f, 0.0f, 0.0f));
    cube("Back", Vec3(0.0f, 0.0f, -4.0f));
    cube("Side", Vec3(3.0f, 0.0f, 0.0f));
    test::ok(editor, "viewport.camera_set",
             R"({"pivot":[0,0,0],"yaw":0,"pitch":0,"distance":8,"animate":false})");
    test::ok(editor, "viewport.set_overlays", R"({"grid":false})");
    // The first frames give the viewport its size and resolve the meshes.
    for (int i = 0; i < 3; ++i)
    {
        gpu.frame();
    }
}

} // namespace

TEST_CASE("viewport.project then viewport.pick returns the same entity and repeated clicks cycle")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    const JsonDocument list = test::ok(editor, "viewport.list");
    CHECK(list.root().pointer("/viewports/0/width").asU64() == u64{kSize.x});

    for (const std::string_view name : {"/Front", "/Side"})
    {
        INFO(name);
        const JsonDocument projected =
            test::ok(editor, "viewport.project", std::format(R"({{"entity":"{}"}})", name));
        REQUIRE(projected.root().find("inside").asBool() == std::optional<bool>(true));
        CHECK(gpu.pickAt(projected.root().find("x").asF64().value_or(0.0),
                         projected.root().find("y").asF64().value_or(0.0), false) == name);
    }

    const JsonDocument front = test::ok(editor, "viewport.project", R"({"entity":"/Front"})");
    const f64 x = front.root().find("x").asF64().value_or(0.0);
    const f64 y = front.root().find("y").asF64().value_or(0.0);
    CHECK(gpu.pickAt(x, y, true) == "/Front");
    // Again at the same spot: the cube behind, then round to the front.
    CHECK(gpu.pickAt(x, y, true) == "/Back");
    CHECK(gpu.pickAt(x + 1.0, y, true) == "/Front");
    // Within the 5 x 5 tolerance next to the cube's edge.
    const JsonDocument side = test::ok(editor, "viewport.project", R"({"entity":"/Side"})");
    CHECK(gpu.pickAt(x, 2.0, false).empty());

    // Locked and hidden entities are skipped.
    test::ok(editor, "view.set_entities", R"({"entities":["/Front"],"locked":true})");
    CHECK(gpu.pickAt(x, y, false) == "/Back");
    test::ok(editor, "view.set_entities", R"({"entities":["/Front"],"locked":false,"hidden":true})");
    CHECK(gpu.pickAt(x, y, false) == "/Back");
    test::ok(editor, "view.set_entities", R"({"entities":["/Back"],"hidden":true})");
    CHECK(gpu.pickAt(x, y, false).empty());

    // Picking selects when asked.
    test::ok(editor, "view.set_entities", R"({"entities":["/Front","/Back"],"hidden":false})");
    gpu.run("viewport.pick", std::format(R"({{"x":{},"y":{},"select":"replace"}})",
                                         side.root().find("x").asF64().value_or(0.0),
                                         side.root().find("y").asF64().value_or(0.0)));
    REQUIRE(editor.selection().size() == 1);
    CHECK(editor.entities().pathOf(editor.entities().find(editor.selection().primary())) == "/Side");
}

TEST_CASE("the selection outline appears only around the selection and renders are repeatable")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    const Image first = gpu.capture();
    gpu.frame();
    const Image second = gpu.capture();
    CHECK(first.data == second.data);

    test::ok(editor, "selection.set", R"({"entities":["/Side"]})");
    gpu.frame();
    const Image selected = gpu.capture();
    // The pixels that changed lie around the Side cube, and some have the outline colour.
    const JsonDocument side = test::ok(editor, "viewport.project", R"({"entity":"/Side"})");
    const f64 cx = side.root().find("x").asF64().value_or(0.0);
    const f64 cy = side.root().find("y").asF64().value_or(0.0);
    const ViewportStyle style;
    usize changed = 0;
    usize outline = 0;
    usize stray = 0;
    for (u32 py = 0; py < kSize.y; ++py)
    {
        for (u32 px = 0; px < kSize.x; ++px)
        {
            const usize i = (static_cast<usize>(py) * kSize.x + px) * 4;
            if (std::memcmp(&first.data[i], &selected.data[i], 4) == 0)
            {
                continue;
            }
            ++changed;
            // A unit cube 8 units away spans about 22 pixels at 180 pixels and 60 degrees.
            if (std::abs(px - cx) > 24.0 || std::abs(py - cy) > 24.0)
            {
                ++stray;
            }
            const auto channel = [&](usize c) {
                return static_cast<f32>(selected.data[i + c]) / 255.0f;
            };
            if (std::abs(channel(0) - style.selection.x) < 0.02f &&
                std::abs(channel(1) - style.selection.y) < 0.02f &&
                std::abs(channel(2) - style.selection.z) < 0.02f)
            {
                ++outline;
            }
        }
    }
    CHECK(changed > 20);
    CHECK(outline > 20);
    CHECK(stray == 0);

    test::ok(editor, "viewport.set_overlays", R"({"selectionOutline":false})");
    gpu.frame();
    CHECK(gpu.capture().data == first.data);
}

TEST_CASE("the viewport overlays match the golden image")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    test::ok(editor, "viewport.set_overlays", R"({"grid":true})");
    test::ok(editor, "viewport.set_mode", R"({"shading":"unlit"})");
    test::ok(editor, "viewport.camera_set",
             R"({"pivot":[1,0,-1],"yaw":30,"pitch":-25,"distance":12,"animate":false})");
    test::ok(editor, "selection.set", R"({"entities":["/Front"]})");
    gpu.frame();
    test::checkGolden("viewport-overlays", gpu.capture());

    test::ok(editor, "viewport.view", R"({"view":"top","animate":false})");
    gpu.frame();
    test::checkGolden("viewport-overlays-top", gpu.capture());
}

TEST_CASE(
    "the game view looks through the scene Camera at a fixed resolution and reads back colours and entities")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    test::ok(
        editor, "material.create",
        R"({"path":"assets:/materials/red.material","value":{"material":{"name":"Red","baseColorFactor":[1,0,0,1]}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"Red","components":{"illusion.Transform":{"position":[0,0,2]},)"
        R"("illusion.MeshRenderer":{"mesh":"builtin:/mesh/cube","material":"assets:/materials/red.material"}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"Camera","components":{"illusion.Transform":{"position":[0,0,10]},"illusion.Camera":{}}})");
    test::ok(editor, "viewport.set_aspect",
             R"({"viewport":"game","aspect":"fixed","width":160,"height":90})");
    test::ok(editor, "viewport.set_mode", R"({"viewport":"game","shading":"unlit"})");

    const JsonDocument list = test::ok(editor, "viewport.list");
    CHECK(list.root().pointer("/viewports/1/width").asU64() == 160u);
    CHECK(list.root().pointer("/viewports/1/sceneCamera/path").asString() ==
          std::optional<std::string_view>("/Camera"));

    // The panel is hidden: the readbacks render the viewport themselves.
    const JsonDocument shot = gpu.run("viewport.screenshot", R"({"viewport":"game"})");
    CHECK(shot.root().find("width").asU64() == 160u);
    CHECK(shot.root().find("height").asU64() == 90u);

    const JsonDocument projected =
        test::ok(editor, "viewport.project", R"({"viewport":"game","entity":"/Red"})");
    CHECK(projected.root().find("inside").asBool() == std::optional<bool>(true));
    const f64 x = projected.root().find("x").asF64().value_or(0.0);
    const f64 y = projected.root().find("y").asF64().value_or(0.0);
    CHECK(x == doctest::Approx(80.0).epsilon(0.02));
    CHECK(y == doctest::Approx(45.0).epsilon(0.02));
    const JsonDocument sample =
        gpu.run("viewport.sample", std::format(R"({{"viewport":"game","x":{},"y":{},"radius":2}})", x, y));
    const f64 hue = sample.root().find("hue").asF64().value_or(180.0);
    CHECK((hue < 25.0 || hue > 335.0));
    CHECK(sample.root().find("saturation").asF64().value_or(0.0) > 0.5);
    const JsonDocument picked =
        gpu.run("viewport.pick", std::format(R"({{"viewport":"game","x":{},"y":{}}})", x, y));
    CHECK(picked.root().pointer("/entity/path").asString() == std::optional<std::string_view>("/Red"));

    const JsonDocument boxed =
        gpu.run("viewport.pick_rect",
                R"({"viewport":"game","x":0,"y":0,"width":160,"height":90,"select":"replace"})");
    bool red = false;
    for (const JsonValue entity : boxed.root().find("entities").elements())
    {
        red = red || entity.find("path").asString() == std::optional<std::string_view>("/Red");
    }
    CHECK(red);
    CHECK(editor.selection().size() == boxed.root().find("entities").size());

    const JsonDocument stats = test::ok(editor, "render.stats", R"({"viewport":"game"})");
    CHECK(stats.root().pointer("/stats/visible").asU64().value_or(0) >= 1u);
    CHECK(stats.root().pointer("/stats/frames").asU64().value_or(0) >= 1u);
    CHECK(test::ok(editor, "render.diagnostics").root().find("validationErrors").asU64() == 0u);

    // Without a Camera there is no game view.
    test::ok(editor, "entity.destroy", R"({"entities":["/Camera"]})");
    CHECK(test::fails(editor, "viewport.screenshot", R"({"viewport":"game"})").kind ==
          ErrorKind::PreconditionFailed);
}

TEST_CASE("secondary viewports render with their own camera and close cleanly")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    const JsonDocument created = test::ok(editor, "viewport.create");
    const std::string id(created.root().find("id").asString().value_or(""));
    CHECK(id == "scene-2");
    test::ok(editor, "viewport.set_aspect",
             std::format(R"({{"viewport":"{}","aspect":"fixed","width":64,"height":48}})", id));
    test::ok(editor, "viewport.camera_set",
             std::format(R"({{"viewport":"{}","yaw":90,"animate":false}})", id));
    const JsonDocument shot = gpu.run("viewport.screenshot", std::format(R"({{"viewport":"{}"}})", id));
    CHECK(shot.root().find("width").asU64() == 64u);
    CHECK(test::ok(editor, "render.stats", std::format(R"({{"viewport":"{}"}})", id))
              .root()
              .pointer("/stats/frames")
              .asU64()
              .value_or(0) >= 1u);
    const u64 version = gpu.service->viewportsVersion();
    test::ok(editor, "viewport.close", std::format(R"({{"viewport":"{}"}})", id));
    CHECK(gpu.service->viewportsVersion() != version);
    gpu.frame();
    CHECK(test::ok(editor, "viewport.list").root().find("viewports").size() == 2);
}
TEST_CASE("readbacks finish while assets stream in without anyone waiting for the streamer")
{
    GpuViewports gpu;
    EditorContext& editor = gpu.editor();
    test::ok(
        editor, "material.create",
        R"({"path":"assets:/materials/red.material","value":{"material":{"name":"Red","baseColorFactor":[1,0,0,1]}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"Red","components":{"illusion.Transform":{"position":[0,0,0]},)"
        R"("illusion.MeshRenderer":{"mesh":"builtin:/mesh/cube","material":"assets:/materials/red.material"}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"Camera","components":{"illusion.Transform":{"position":[0,0,8]},"illusion.Camera":{}}})");
    test::ok(editor, "viewport.set_aspect", R"({"viewport":"game","aspect":"fixed","width":64,"height":36})");
    test::ok(editor, "viewport.set_mode", R"({"viewport":"game","shading":"unlit"})");

    // Regression: the viewports published streamed loads only when something called waitIdle(), so the
    // .material stayed Loading and readbacks (which wait for loads) never finished in a running editor.
    const JsonDocument doc = test::parseJson(R"({"viewport":"game","x":32,"y":18,"radius":1})");
    Expected<std::unique_ptr<RunningCommand>, CommandError> started =
        editor.startJson("viewport.sample", doc.root());
    REQUIRE(started.hasValue());
    std::optional<std::string> result;
    for (int i = 0; i < 100000 && !result; ++i)
    {
        gpu.service->render(false);
        gpu.device->waitIdle();
        Expected<std::optional<std::string>, CommandError> updated = started.value()->update();
        REQUIRE(updated.hasValue());
        result = *updated;
    }
    REQUIRE(result.has_value());
    const JsonDocument sample = test::parseJson(*result);
    const f64 hue = sample.root().find("hue").asF64().value_or(180.0);
    CHECK((hue < 25.0 || hue > 335.0));
    CHECK(sample.root().find("saturation").asF64().value_or(0.0) > 0.5);
}

TEST_CASE("closing a viewport with readbacks in flight frees their slots for the other viewports")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    const JsonDocument created = test::ok(editor, "viewport.create");
    const std::string id(created.root().find("id").asString().value_or(""));
    test::ok(editor, "viewport.set_aspect",
             std::format(R"({{"viewport":"{}","aspect":"fixed","width":32,"height":32}})", id));
    // Fill the readback ring (four slots) with copies of the secondary viewport, then close it.
    const JsonDocument params = test::parseJson(std::format(R"({{"viewport":"{}"}})", id));
    std::vector<std::unique_ptr<RunningCommand>> shots;
    for (int i = 0; i < 4; ++i)
    {
        Expected<std::unique_ptr<RunningCommand>, CommandError> started =
            editor.startJson("viewport.screenshot", params.root());
        REQUIRE(started.hasValue());
        shots.push_back(std::move(started.value()));
    }
    gpu.service->render(false);
    test::ok(editor, "viewport.close", std::format(R"({{"viewport":"{}"}})", id));
    for (const std::unique_ptr<RunningCommand>& shot : shots)
    {
        // Each one has finished: with the image copied before the close, or failed by it.
        const Expected<std::optional<std::string>, CommandError> updated = shot->update();
        CHECK((!updated.hasValue() || updated->has_value()));
    }
    const JsonDocument shot = gpu.run("viewport.screenshot", R"({"viewport":"scene"})");
    CHECK(shot.root().find("width").asU64().value_or(0) > 0u);
}

TEST_CASE("the entity under a resting cursor is highlighted and reported, the background clears it")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    const JsonDocument projected = test::ok(editor, "viewport.project", R"({"entity":"/Side"})");
    const Vec2 pixel(static_cast<f32>(projected.root().find("x").asF64().value_or(0.0)),
                     static_cast<f32>(projected.root().find("y").asF64().value_or(0.0)));
    const auto hover = [&](Vec2 cursor) {
        ViewportFrame drawn;
        for (int i = 0; i < 4; ++i)
        {
            ViewportInput input;
            input.hovered = true;
            input.cursor = cursor;
            input.deltaSeconds = 1.0f / 60.0f;
            drawn = gpu.service->frame("scene", kSize, input, ViewportStyle{});
            gpu.service->render(true);
            gpu.device->waitIdle();
            gpu.service->waitIdle();
        }
        return drawn;
    };
    const Image before = gpu.capture();
    const ViewportFrame over = hover(pixel);
    REQUIRE_FALSE(over.hovered.isNull());
    CHECK(editor.entities().pathOf(over.hovered) == "/Side");
    // The highlight draws an outline the image did not have.
    CHECK(gpu.capture().data != before.data);
    CHECK(hover(Vec2(2.0f, 2.0f)).hovered.isNull());

    // Hover off: nothing is read.
    test::ok(editor, "viewport.set_overlays", R"({"hover":false})");
    CHECK(hover(pixel).hovered.isNull());
}

TEST_CASE("a selected Camera shows its frustum, icon and picture-in-picture view")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    test::ok(
        editor, "entity.create",
        R"({"name":"Eye","components":{"illusion.Transform":{"position":[0,0,6]},"illusion.Camera":{}}})");
    test::ok(editor, "entity.create",
             R"({"name":"Lamp","components":{"illusion.Transform":{"position":[-2,1,0]},)"
             R"("illusion.PointLight":{"range":2}}})");
    test::ok(editor, "viewport.camera_set",
             R"({"pivot":[0,0,0],"yaw":90,"pitch":-20,"distance":16,"animate":false})");
    gpu.frame();
    CHECK(gpu.service->stats().visualisers == 0);
    CHECK(gpu.service->stats().previews == 0);
    const Image unselected = gpu.capture();

    test::ok(editor, "selection.set", R"({"entities":["/Eye"]})");
    gpu.frame();
    CHECK(gpu.service->stats().visualisers == 1);
    CHECK(gpu.service->stats().previews == 1);
    const ViewportFrame drawn = gpu.service->frame("scene", kSize, ViewportInput{}, ViewportStyle{});
    CHECK(drawn.preview != 0);
    CHECK(drawn.previewSize.x >= 128u);
    CHECK(editor.entities().pathOf(drawn.previewCamera) == "/Eye");
    bool eye = false;
    bool lamp = false;
    for (const ViewportIcon& icon : drawn.icons)
    {
        const std::string path = editor.entities().pathOf(icon.entity);
        eye = eye || (path == "/Eye" && icon.kind == ViewportIconKind::Camera && icon.selected);
        lamp = lamp || (path == "/Lamp" && icon.kind == ViewportIconKind::PointLight && !icon.selected);
    }
    CHECK((eye && lamp));
    gpu.service->render(true);
    gpu.device->waitIdle();
    CHECK(gpu.capture().data != unselected.data);

    // Every entity's visualisers, then none; icons off.
    test::ok(editor, "viewport.set_overlays",
             R"({"visualiserScope":"all","icons":false,"cameraPreview":false})");
    gpu.frame();
    CHECK(gpu.service->stats().visualisers == 2);
    CHECK(gpu.service->stats().previews == 0);
    CHECK(gpu.service->frame("scene", kSize, ViewportInput{}, ViewportStyle{}).icons.empty());
    gpu.service->render(true);
    test::ok(editor, "viewport.set_overlays", R"({"visualisers":{"lights":false,"cameras":false}})");
    gpu.frame();
    CHECK(gpu.service->stats().visualisers == 0);
}

TEST_CASE("mesh bounds follow the scene and drop to ground lands an entity on the floor below it")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    test::ok(
        editor, "entity.create",
        R"({"name":"Floor","components":{"illusion.Transform":{"position":[0,-3,0],"scale":[20,0.5,20]},)"
        R"("illusion.MeshRenderer":{"mesh":"builtin:/mesh/cube"}}})");
    test::ok(
        editor, "entity.create",
        R"({"name":"Crate","components":{"illusion.Transform":{"position":[6,5,1],)"
        R"("rotation":[0,0.3826834,0,0.9238795]},"illusion.MeshRenderer":{"mesh":"builtin:/mesh/cube"}}})");
    for (int i = 0; i < 3; ++i)
    {
        gpu.frame();
    }
    const auto boundsOf = [&](std::string_view path) {
        std::vector<ViewportBounds> meshes;
        gpu.service->meshBounds(meshes);
        CHECK(meshes.size() == 5);
        for (const ViewportBounds& mesh : meshes)
        {
            if (editor.entities().pathOf(mesh.entity) == path)
            {
                return mesh.bounds;
            }
        }
        FAIL("no bounds for " << path);
        return Aabb{};
    };
    const Aabb front = boundsOf("/Front");
    CHECK(front.isValid());
    CHECK(glm::length(front.center()) < 1e-4f);

    test::ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"]})");
    const Aabb floor = boundsOf("/Floor");
    const Aabb crate = boundsOf("/Crate");
    CHECK(crate.min.y == doctest::Approx(floor.max.y).epsilon(1e-4));
    CHECK(crate.center().x == doctest::Approx(6.0f));

    // Hidden entities have no bounds: with the floor hidden the crate falls to the ground plane.
    test::ok(editor, "view.set_entities", R"({"entities":["/Floor"],"hidden":true})");
    test::ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"]})");
    std::vector<ViewportBounds> meshes;
    gpu.service->meshBounds(meshes);
    CHECK(meshes.size() == 4);
    for (const ViewportBounds& mesh : meshes)
    {
        if (editor.entities().pathOf(mesh.entity) == "/Crate")
        {
            CHECK(std::abs(mesh.bounds.min.y) < 1e-4f);
        }
    }
    gpu.frame();
}

TEST_CASE("surface snap lands on a tilted plane with its normal, and vertex snap on a cube corner")
{
    GpuViewports gpu;
    buildScene(gpu);
    EditorContext& editor = gpu.editor();
    // A ramp through the origin turned 30 degrees about z, under the Front cube; seen from above the front.
    test::ok(
        editor, "entity.create",
        R"({"name":"Ramp","components":{"illusion.Transform":{"scale":[10,1,10],)"
        R"("rotation":[0,0,0.2588190,0.9659258]},"illusion.MeshRenderer":{"mesh":"builtin:/mesh/plane"}}})");
    test::ok(editor, "entity.create",
             R"({"name":"Crate","components":{"illusion.Transform":{"position":[0,4,0]}}})");
    test::ok(editor, "viewport.camera_set",
             R"({"pivot":[0,0,0],"yaw":0,"pitch":-40,"distance":8,"animate":false})");
    for (int i = 0; i < 3; ++i)
    {
        gpu.frame();
    }
    const Vec3 normal(-0.5f, 0.8660254f, 0.0f);
    const auto vec3Of = [](const JsonValue& value) {
        return Vec3(static_cast<f32>(value[0].asF64().value_or(0.0)),
                    static_cast<f32>(value[1].asF64().value_or(0.0)),
                    static_cast<f32>(value[2].asF64().value_or(0.0)));
    };

    // Under the centre of the image lies the Front cube; left out, the ramp is below it.
    const JsonDocument covered = gpu.run("viewport.snap", R"({"x":160,"y":90,"to":"surface"})");
    CHECK(covered.root().pointer("/entity/path").asString().value_or("") == "/Front");
    const JsonDocument snapped =
        gpu.run("viewport.snap", R"({"x":160,"y":90,"to":"surface","exclude":["/Front"]})");
    const JsonValue root = snapped.root();
    REQUIRE(root.find("hit").asBool().value_or(false));
    CHECK(root.find("kind").asString().value_or("") == "surface");
    CHECK(root.pointer("/entity/path").asString().value_or("") == "/Ramp");
    const Vec3 position = vec3Of(root.find("position"));
    CHECK(std::abs(glm::dot(position, normal)) < 2e-3f);
    CHECK(glm::length(position) < 0.1f);
    CHECK(glm::dot(vec3Of(root.find("normal")), normal) > 0.999f);

    // Placing an entity with the snapped position and rotation lands it on the plane, standing on its normal.
    const JsonValue rotation = root.find("rotation");
    test::ok(editor, "transform.set",
             std::format(R"({{"entities":["/Crate"],"position":[{},{},{}],"rotation":[{},{},{},{}]}})",
                         position.x, position.y, position.z, rotation[0].asF64().value_or(0.0),
                         rotation[1].asF64().value_or(0.0), rotation[2].asF64().value_or(0.0),
                         rotation[3].asF64().value_or(1.0)));
    const JsonDocument placed = test::ok(editor, "transform.get", R"({"entity":"/Crate"})");
    const JsonValue turned = placed.root().find("rotation");
    const Quat crate(
        static_cast<f32>(turned[3].asF64().value_or(1.0)), static_cast<f32>(turned[0].asF64().value_or(0.0)),
        static_cast<f32>(turned[1].asF64().value_or(0.0)), static_cast<f32>(turned[2].asF64().value_or(0.0)));
    CHECK(glm::dot(crate * Vec3(0.0f, 1.0f, 0.0f), normal) > 0.999f);
    CHECK(std::abs(glm::dot(vec3Of(placed.root().find("position")), normal)) < 2e-3f);

    // The top front right corner of the Side cube, a few pixels off.
    const std::vector<ViewportInfo> views = gpu.service->viewports();
    const ViewportInfo& scene = views.front();
    REQUIRE(scene.id == "scene");
    const Vec3 corner(3.5f, 0.5f, 0.5f);
    const ViewportPoint point = projectToViewport(scene.projection * scene.view, scene.size, corner);
    REQUIRE(point.inside);
    const JsonDocument vertex =
        gpu.run("viewport.snap", std::format(R"({{"x":{},"y":{},"to":"vertex"}})", point.pixel.x - 3.0f,
                                             point.pixel.y + 2.0f));
    REQUIRE(vertex.root().find("hit").asBool().value_or(false));
    CHECK(vertex.root().pointer("/entity/path").asString().value_or("") == "/Side");
    CHECK(glm::length(vec3Of(vertex.root().find("position")) - corner) < 1e-4f);
    // Far from every vertex there is none, and "any" then finds the surface.
    const JsonDocument none = gpu.run("viewport.snap", R"({"x":160,"y":170,"to":"vertex","tolerance":2})");
    CHECK(!none.root().find("hit").asBool().value_or(true));
    const JsonDocument any =
        gpu.run("viewport.snap", R"({"x":160,"y":170,"exclude":["/Front"],"tolerance":2})");
    CHECK(any.root().find("kind").asString().value_or("") == "surface");
    CHECK(any.root().pointer("/entity/path").asString().value_or("") == "/Ramp");
    gpu.frame();
}

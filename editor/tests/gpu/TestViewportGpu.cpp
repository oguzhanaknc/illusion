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

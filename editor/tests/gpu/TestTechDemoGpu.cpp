// GPU golden of the tech demo's pause menu (samples/techdemo): the module instantiates the committed UI scene
// in Play mode, Escape opens the menu and its transition settles, over the plain background of a scene with
// only a Camera (the forest needs downloaded assets).

#include "GoldenSupport.hpp"
#include "core/EditorCoreTestSupport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/editor/client/ImageCompare.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/core/Project.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr UVec2 kPanelSize{320, 180};

// A project from the empty template with the tech demo's UI scene, string tables and font, opened in an
// editor with GPU viewports.
struct TechDemoGpu
{
    TechDemoGpu()
        : device(RenderDevice::create(test::headlessDeviceDesc()))
    {
        REQUIRE(device != nullptr);
        test::reflectEditorTestComponents();
        reflectRenderComponents();
        const std::filesystem::path project = directory.path / "techdemo";
        Expected<Project, std::string> created =
            Project::create(project, "Tech Demo", test::editorDataDirectory() / "templates" / "empty");
        REQUIRE_MESSAGE(created.hasValue(), (created ? std::string() : created.error()));
        const std::filesystem::path source = utf8Path(ILLUSION_SAMPLES_DIR) / "techdemo" / "assets";
        std::filesystem::copy_file(source / "scenes" / "ui.scene",
                                   project / "assets" / "scenes" / "ui.scene");
        for (const std::string_view folder : {"locales", "fonts"})
        {
            std::filesystem::copy(source / folder, project / "assets" / folder,
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing);
        }
        EditorContextOptions options;
        options.workspace = directory.path;
        editor = std::make_unique<EditorContext>(options);
        const Expected<void, CommandError> opened = editor->openProject(project);
        REQUIRE_MESSAGE(opened.hasValue(), (opened ? std::string() : opened.error().message));
        offscreen = ui::OffscreenUi::create(*device, context, kPanelSize);
        REQUIRE(offscreen != nullptr);
        service = viewport::ViewportService::create(*device, *editor, offscreen->renderer());
        REQUIRE(service != nullptr);
    }

    ~TechDemoGpu()
    {
        service.reset();
        offscreen.reset();
        editor.reset();
        if (device)
        {
            device->waitIdle();
            CHECK(device->validationErrorCount() == 0);
        }
    }

    TechDemoGpu(const TechDemoGpu&) = delete;
    TechDemoGpu& operator=(const TechDemoGpu&) = delete;

    // Runs the async command @p name to completion, rendering between its steps.
    JsonDocument run(std::string_view name, std::string_view params)
    {
        const JsonDocument doc = test::parseJson(params);
        Expected<std::unique_ptr<RunningCommand>, CommandError> started = editor->startJson(name, doc.root());
        REQUIRE_MESSAGE(started.hasValue(), (started ? std::string() : started.error().message));
        for (int i = 0; i < 1000; ++i)
        {
            Expected<std::optional<std::string>, CommandError> updated = started.value()->update();
            REQUIRE_MESSAGE(updated.hasValue(), (updated ? std::string() : updated.error().message));
            if (updated->has_value())
            {
                return test::parseJson(**updated);
            }
            service->render(false);
            device->waitIdle();
            service->waitIdle();
        }
        FAIL("the command did not finish");
        return JsonDocument{};
    }

    test::TempDirectory directory;
    std::unique_ptr<RenderDevice> device;
    std::unique_ptr<EditorContext> editor;
    ui::UiContext context;
    std::unique_ptr<ui::OffscreenUi> offscreen;
    std::unique_ptr<viewport::ViewportService> service;
};

} // namespace

TEST_CASE("the tech demo's pause menu in Play mode matches the golden image")
{
    TechDemoGpu gpu;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    AssetStreamer playAssets(gpu.editor->files(), jobs, AssetStreamerDesc{.cacheMount = ""});
    PlaySession play(
        *gpu.editor,
        PlaySessionDesc{.jobs = &jobs, .streamer = &playAssets, .audioOutput = AudioOutput::Offline});
    const Expected<std::string, CommandError> loaded = play.loadModule(TECHDEMO_MODULE);
    REQUIRE_MESSAGE(loaded.hasValue(), (loaded ? std::string() : loaded.error().message));
    test::ok(*gpu.editor, "viewport.set_aspect",
             R"({"viewport":"game","aspect":"fixed","width":960,"height":540})");
    // A Camera with nothing to see, for the Game view.
    test::ok(*gpu.editor, "entity.create",
             R"({"name":"Camera","components":{"illusion.Transform":{},"illusion.Camera":{}}})");
    test::ok(*gpu.editor, "play.start", R"({"fixedStep":true})");
    test::ok(*gpu.editor, "play.pause", R"({"paused":true})");
    // The Game view draws every step, as in Play mode: the game's UI asks for its fonts in the update after a
    // render. Escape on the second step opens the menu; its transition takes 15 steps.
    JsonDocument shot;
    for (int step = 0; step < 60; ++step)
    {
        if (step == 1 || step == 2)
        {
            test::ok(*gpu.editor, "input.inject",
                     step == 1 ? R"({"keys":[{"key":"Escape","down":true}]})"
                               : R"({"keys":[{"key":"Escape","down":false}]})");
        }
        test::ok(*gpu.editor, "play.step", R"({"steps":1})");
        shot = gpu.run("viewport.screenshot", R"({"viewport":"game"})");
    }
    const std::optional<std::string_view> data = shot.root().find("_images")[0].find("data").asString();
    REQUIRE(data.has_value());
    const std::optional<std::vector<std::byte>> png = client::decodeBase64(*data);
    REQUIRE(png.has_value());
    const std::optional<Image> image = decodeImage(*png, true);
    REQUIRE(image.has_value());
    REQUIRE(image->width() == 960u);
    test::checkGolden("techdemo-pause-menu", *image);
    CHECK(test::ok(*gpu.editor, "render.diagnostics").root().find("validationErrors").asU64() == 0u);
    test::ok(*gpu.editor, "play.stop");
    gpu.service->render(false);
    gpu.device->waitIdle();
}

#include "gpu/GoldenSupport.hpp"
#include "shell/ShellTestSupport.hpp"

#include <illusion/editor/client/RpcClient.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>

#include <imgui.h>

using namespace illusion;
using namespace illusion::editor;

// GPU tests of the editor shell: golden images at 1280x720 and editor.screenshot of a headless editor.

namespace
{

// The shell rendered into an offscreen target.
struct GpuShell
{
    std::unique_ptr<RenderDevice> device;
    test::ShellFixture fixture;
    std::unique_ptr<ui::OffscreenUi> offscreen;

    GpuShell()
        : device(RenderDevice::create(test::headlessDeviceDesc()))
        , fixture(test::ShellSetup{.headless = false})
    {
        REQUIRE(device != nullptr);
        offscreen = ui::OffscreenUi::create(
            *device, fixture.context,
            UVec2(static_cast<u32>(test::kShellFrameSize.x), static_cast<u32>(test::kShellFrameSize.y)));
        REQUIRE(offscreen != nullptr);
    }

    ~GpuShell()
    {
        offscreen.reset();
        if (device)
        {
            device->waitIdle();
        }
    }

    GpuShell(const GpuShell&) = delete;
    GpuShell& operator=(const GpuShell&) = delete;

    // Runs @p frames frames and returns the capture of the last one.
    Image capture(int frames)
    {
        std::optional<Image> image;
        for (int i = 0; i < frames; ++i)
        {
            fixture.beginFrame();
            fixture.shell->draw(fixture.time, 1.0 / 60.0);
            ImGui::Render();
            fixture.editor->endFrame(1.0 / 60.0);
            fixture.time += 1.0 / 60.0;
            image = offscreen->render(ImGui::GetDrawData(), fixture.shell->clearColor());
            REQUIRE(image.has_value());
        }
        return std::move(*image);
    }
};

struct ShellGolden
{
    const char* name;
    const char* preferences;
};

constexpr ShellGolden kShellGoldens[] = {
    {"shell-dark-100", R"({"theme":"dark"})"},
    {"shell-light-100", R"({"theme":"light"})"},
    {"shell-dark-100-en-XA", R"({"theme":"dark","locale":"en-XA"})"},
};

} // namespace

TEST_CASE("shell captures match the golden images")
{
    for (const ShellGolden& golden : kShellGoldens)
    {
        CAPTURE(golden.name);
        GpuShell gpu;
        EditorContext& editor = *gpu.fixture.editor;
        test::ok(editor, "preferences.set", std::string(R"({"value":)") + golden.preferences + "}");
        test::ok(editor, "entity.create", R"({"name":"Board"})");
        // The History tab shows the step; the log is cleared so that nothing machine-specific shows.
        test::ok(editor, "ui.show_panel", R"({"panel":"panel.history"})");
        (void)gpu.capture(10);
        test::ok(editor, "log.clear");
        const Image actual = gpu.capture(4);
        test::checkGolden(golden.name, actual);
        CHECK(gpu.device->validationErrorCount() == 0);
    }
}

TEST_CASE("the shell renders its windows and the palette without validation errors")
{
    GpuShell gpu;
    EditorContext& editor = *gpu.fixture.editor;
    for (const std::string_view panel :
         {"window.preferences", "window.welcome", "window.about", "window.licenses", "window.notifications",
          "panel.console", "panel.ai_activity"})
    {
        test::ok(editor, "ui.show_panel", std::string(R"({"panel":")") + std::string(panel) + "\"}");
    }
    test::ok(editor, "ui.palette", R"({"query":"save"})");
    test::ok(editor, "ui.notify", R"({"message":"Board built","level":"success"})");
    const Image image = gpu.capture(8);
    test::writeBinaryFile(test::captureDirectory() / "shell-windows.png", encodePng(image));
    CHECK(gpu.device->validationErrorCount() == 0);
}

TEST_CASE("editor.screenshot of a headless editor returns a PNG of the requested size")
{
    test::TempDirectory workspace;
    client::LaunchOptions launch;
    launch.editor = utf8Path(ILLUSION_EDITOR_EXECUTABLE);
    const std::u8string root = workspace.path.u8string();
    launch.arguments = {"--workspace", std::string(reinterpret_cast<const char*>(root.data()), root.size()),
                        "--deterministic"};
    Expected<client::RpcClient, std::string> rpc = client::RpcClient::launch(launch);
    REQUIRE_MESSAGE(rpc.hasValue(), (rpc ? std::string() : rpc.error()));
    Expected<std::string, client::RpcError> shot =
        rpc->call("editor.screenshot", R"({"width":640,"height":360})");
    REQUIRE_MESSAGE(shot.hasValue(), (shot ? std::string() : shot.error().message));
    const JsonDocument result = test::parseJson(*shot);
    CHECK(result.root().find("width").asU64() == 640u);
    CHECK(result.root().find("height").asU64() == 360u);
    const std::optional<std::string_view> data = result.root().find("_images")[0].find("data").asString();
    REQUIRE(data.has_value());
    // "iVBORw0KGgo" is the base64 of the PNG signature.
    CHECK(data->starts_with("iVBORw0KGgo"));
    CHECK(rpc->close() == std::optional<i32>(0));
}

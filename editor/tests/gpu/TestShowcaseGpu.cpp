// A GPU golden of the showcase project (samples/showcase): the everything scene through its main Camera, as
// viewport.screenshot gives it to an agent.

#include "GoldenSupport.hpp"
#include "core/EditorCoreTestSupport.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/editor/core/EditorContext.hpp>
#include <illusion/editor/ui/OffscreenUi.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/viewport/ViewportService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderDevice.hpp>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace illusion;
using namespace illusion::editor;

namespace
{

constexpr UVec2 kPanelSize{320, 180};

// The bytes of the base64 text @p text (RFC 4648, with padding).
std::vector<std::byte> decodeBase64(std::string_view text)
{
    std::array<i32, 256> values{};
    values.fill(-1);
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (usize i = 0; i < kAlphabet.size(); ++i)
    {
        values[static_cast<u8>(kAlphabet[i])] = static_cast<i32>(i);
    }
    std::vector<std::byte> bytes;
    bytes.reserve(text.size() / 4 * 3);
    u32 bits = 0;
    i32 count = 0;
    for (const char c : text)
    {
        const i32 value = values[static_cast<u8>(c)];
        if (value < 0)
        {
            REQUIRE(c == '=');
            break;
        }
        bits = (bits << 6) | static_cast<u32>(value);
        count += 6;
        if (count >= 8)
        {
            count -= 8;
            bytes.push_back(static_cast<std::byte>((bits >> count) & 0xFFu));
        }
    }
    return bytes;
}

// The showcase project, copied and opened in an editor with GPU viewports.
struct ShowcaseGpu
{
    ShowcaseGpu()
        : device(RenderDevice::create(test::headlessDeviceDesc()))
    {
        REQUIRE(device != nullptr);
        test::reflectEditorTestComponents();
        reflectRenderComponents();
        reflectPhysicsComponents();
        reflectAudioComponents();
        const std::filesystem::path project = directory.path / "showcase";
        std::filesystem::copy(utf8Path(ILLUSION_SAMPLES_DIR) / "showcase", project,
                              std::filesystem::copy_options::recursive);
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

    ~ShowcaseGpu()
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

    ShowcaseGpu(const ShowcaseGpu&) = delete;
    ShowcaseGpu& operator=(const ShowcaseGpu&) = delete;

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

TEST_CASE("the showcase scene through its main Camera matches the golden image")
{
    ShowcaseGpu gpu;
    test::ok(*gpu.editor, "viewport.set_aspect",
             R"({"viewport":"game","aspect":"fixed","width":480,"height":270})");
    // The screenshot waits for the scene's assets; the second one sees the shadows and the sky settled.
    static_cast<void>(gpu.run("viewport.screenshot", R"({"viewport":"game"})"));
    const JsonDocument shot = gpu.run("viewport.screenshot", R"({"viewport":"game"})");
    CHECK(shot.root().find("width").asU64() == 480u);
    CHECK(shot.root().find("height").asU64() == 270u);
    const std::optional<std::string_view> data = shot.root().find("_images")[0].find("data").asString();
    REQUIRE(data.has_value());
    const std::optional<Image> image = decodeImage(decodeBase64(*data), true);
    REQUIRE(image.has_value());
    REQUIRE(image->width() == 480u);
    test::checkGolden("showcase-game", *image);
    CHECK(test::ok(*gpu.editor, "render.diagnostics").root().find("validationErrors").asU64() == 0u);
}

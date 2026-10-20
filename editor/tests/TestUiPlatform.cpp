#include "Gallery.hpp"
#include "HeadlessUi.hpp"

#include <illusion/core/EventBus.hpp>
#include <illusion/editor/ui/FontManager.hpp>
#include <illusion/editor/ui/UiContext.hpp>
#include <illusion/editor/ui/UiPlatformBackend.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <doctest/doctest.h>

#include <array>
#include <imgui.h>
#include <string>

using namespace illusion;
using namespace illusion::editor;

namespace
{

// A headless window with the UI backend attached; frames run without a GPU.
struct PlatformFixture
{
    EventBus events;
    std::unique_ptr<Platform> platform;
    std::unique_ptr<Window> window;
    VirtualFileSystem vfs;
    ui::UiContext context;
    std::unique_ptr<ui::FontManager> fonts;
    std::unique_ptr<ui::UiPlatformBackend> backend;
    std::unique_ptr<gallery::HeadlessUi> headless;

    PlatformFixture()
    {
        platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
        REQUIRE(platform != nullptr);
        WindowDesc desc;
        desc.width = 800;
        desc.height = 600;
        window = Window::create(*platform, desc, &events);
        REQUIRE(window != nullptr);
        backend = std::make_unique<ui::UiPlatformBackend>(context, *window, events);
        headless = std::make_unique<gallery::HeadlessUi>(context);
        // Shortcuts below use Ctrl on every OS.
        ImGui::GetIO().ConfigMacOSXBehaviors = false;
        // Apply every queued event in the frame it arrives (Dear ImGui otherwise spreads a press and a
        // release of the same frame over two frames).
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
        REQUIRE(gallery::mountEditorData(vfs));
        auto loaded = ui::FontManager::load(context, vfs, "editor:/fonts");
        REQUIRE(loaded.hasValue());
        fonts = std::move(loaded.value());
    }

    ~PlatformFixture()
    {
        headless.reset();
        backend.reset();
        fonts.reset();
    }

    PlatformFixture(const PlatformFixture&) = delete;
    PlatformFixture& operator=(const PlatformFixture&) = delete;

    // Delivers queued events, then runs a frame that draws @p ui.
    template <typename F>
    void frame(F&& ui)
    {
        events.dispatchQueued();
        backend->newFrame(1.0 / 60.0);
        ImGui::NewFrame();
        ui();
        headless->endFrame();
    }

    void frame()
    {
        frame([] {});
    }

    void key(Key key, bool down, KeyModifiers modifiers = {})
    {
        events.enqueue(KeyEvent{window->id(), key, down, false, modifiers});
    }
};

} // namespace

TEST_CASE("window input reaches Dear ImGui in order")
{
    PlatformFixture fixture;
    fixture.frame();
    CHECK(ImGui::GetIO().DisplaySize.x == doctest::Approx(800.0f));
    CHECK(ImGui::GetIO().DisplaySize.y == doctest::Approx(600.0f));

    fixture.events.enqueue(MouseMovedEvent{fixture.window->id(), Vec2(120.0f, 45.0f)});
    fixture.key(Key::A, true);
    fixture.key(Key::LeftShift, true, KeyModifiers{.shift = true});
    fixture.frame([] {
        CHECK(ImGui::GetIO().MousePos.x == doctest::Approx(120.0f));
        CHECK(ImGui::GetIO().MousePos.y == doctest::Approx(45.0f));
        CHECK(ImGui::IsKeyDown(ImGuiKey_A));
        CHECK(ImGui::IsKeyPressed(ImGuiKey_A, false));
        CHECK(ImGui::IsKeyDown(ImGuiKey_LeftShift));
        CHECK(ImGui::GetIO().KeyShift);
    });

    fixture.key(Key::A, false);
    fixture.key(Key::LeftShift, false);
    fixture.events.enqueue(
        MouseButtonEvent{fixture.window->id(), MouseButton::Right, true, {}, Vec2(10.0f, 20.0f)});
    fixture.events.enqueue(MouseScrolledEvent{fixture.window->id(), Vec2(0.0f, -2.0f)});
    fixture.frame([] {
        CHECK_FALSE(ImGui::IsKeyDown(ImGuiKey_A));
        CHECK_FALSE(ImGui::GetIO().KeyShift);
        CHECK(ImGui::IsMouseDown(ImGuiMouseButton_Right));
        CHECK(ImGui::GetIO().MousePos.x == doctest::Approx(10.0f));
        CHECK(ImGui::GetIO().MouseWheel == doctest::Approx(-2.0f));
    });

    fixture.events.enqueue(CursorEnteredEvent{fixture.window->id(), false});
    fixture.frame([] { CHECK_FALSE(ImGui::IsMousePosValid()); });
}

TEST_CASE("events of other windows are ignored")
{
    PlatformFixture fixture;
    WindowDesc desc;
    auto other = Window::create(*fixture.platform, desc, &fixture.events);
    REQUIRE(other != nullptr);
    fixture.frame();
    fixture.events.enqueue(KeyEvent{other->id(), Key::B, true, false, {}});
    fixture.events.enqueue(TextInputEvent{other->id(), U'x'});
    fixture.frame([] {
        CHECK_FALSE(ImGui::IsKeyDown(ImGuiKey_B));
        CHECK(ImGui::GetIO().InputQueueCharacters.Size == 0);
    });
}

TEST_CASE("typed text goes into the focused text field and copying reaches the window's clipboard")
{
    PlatformFixture fixture;
    std::array<char, 64> buffer{};
    const auto ui = [&] {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::Begin("###test.window");
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::InputText("###test.field", buffer.data(), buffer.size());
        ImGui::End();
    };
    fixture.frame(ui);
    fixture.frame(ui);
    for (const char32_t c : std::u32string_view(U"Ağaç 🌲"))
    {
        fixture.events.enqueue(TextInputEvent{fixture.window->id(), c});
    }
    fixture.frame(ui);
    fixture.frame(ui);
    CHECK(std::string_view(buffer.data()) == "Ağaç \xF0\x9F\x8C\xB2");

    const KeyModifiers control{.control = true};
    fixture.key(Key::LeftControl, true, control);
    fixture.key(Key::A, true, control);
    fixture.frame(ui);
    fixture.key(Key::A, false, control);
    fixture.key(Key::C, true, control);
    fixture.frame(ui);
    fixture.key(Key::C, false, control);
    fixture.key(Key::LeftControl, false);
    fixture.frame(ui);
    CHECK(fixture.window->clipboardText() == "Ağaç \xF0\x9F\x8C\xB2");
}

TEST_CASE("Dear ImGui's clipboard functions use the window's clipboard")
{
    PlatformFixture fixture;
    fixture.frame();
    ImGui::SetClipboardText("from ui");
    CHECK(fixture.window->clipboardText() == "from ui");
    fixture.window->setClipboardText("from os ✓");
    CHECK(std::string_view(ImGui::GetClipboardText()) == "from os ✓");
}

TEST_CASE("the backend applies the cursor shape the UI asks for")
{
    PlatformFixture fixture;
    fixture.frame([] { ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput); });
    fixture.frame();
    CHECK(fixture.window->cursorShape() == CursorShape::TextInput);
    fixture.frame([] { ImGui::SetMouseCursor(ImGuiMouseCursor_None); });
    fixture.frame();
    CHECK(fixture.window->cursorMode() == CursorMode::Hidden);
    fixture.frame([] { ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow); });
    fixture.frame();
    CHECK(fixture.window->cursorMode() == CursorMode::Normal);
    CHECK(fixture.window->cursorShape() == CursorShape::Arrow);
}

TEST_CASE("a content scale change is reported once")
{
    PlatformFixture fixture;
    CHECK_FALSE(fixture.backend->consumeContentScaleChange());
    fixture.events.enqueue(ContentScaleChangedEvent{fixture.window->id(), Vec2(1.5f, 1.5f)});
    fixture.frame();
    CHECK(fixture.backend->consumeContentScaleChange());
    CHECK_FALSE(fixture.backend->consumeContentScaleChange());
}

#include <illusion/core/EventBus.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <doctest/doctest.h>

#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace illusion;

// These tests use the headless backend so they run on CI machines without a display.

TEST_CASE("a headless platform creates windows with the requested properties")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    CHECK(platform->isHeadless());
    CHECK(platform->backendName() == "null");

    WindowDesc desc;
    desc.title = "Test window";
    desc.width = 640;
    desc.height = 480;
    auto window = Window::create(*platform, desc);
    REQUIRE(window != nullptr);
    CHECK(window->title() == "Test window");
    CHECK(window->size() == UVec2(640, 480));
    CHECK(window->framebufferSize().x > 0);
    CHECK(window->contentScale().x > 0.0f);
    CHECK(window->nativeHandle() != nullptr);
    CHECK_FALSE(window->shouldClose());

    window->setTitle("Renamed");
    CHECK(window->title() == "Renamed");

    window->setShouldClose(true);
    CHECK(window->shouldClose());
    window->setShouldClose(false);
    CHECK_FALSE(window->shouldClose());

    window->setCursorMode(CursorMode::Captured);
    CHECK(window->cursorMode() == CursorMode::Captured);
    window->setCursorMode(CursorMode::Normal);

    platform->pollEvents();
    platform->waitEvents(0.0);
}

TEST_CASE("windows get unique ids")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    auto first = Window::create(*platform, WindowDesc{});
    auto second = Window::create(*platform, WindowDesc{});
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->id() != WindowId::Invalid);
    CHECK(first->id() != second->id());
}

TEST_CASE("losing focus releases held keys and reports the releases as events")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    EventBus events;
    auto first = Window::create(*platform, WindowDesc{}, &events);
    auto second = Window::create(*platform, WindowDesc{}, &events);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    first->focus();
    platform->pollEvents();
    events.dispatchQueued();

    first->input().setKey(Key::W, true);
    first->input().setMouseButton(MouseButton::Left, true);

    std::vector<Key> releasedKeys;
    bool releasedButton = false;
    bool lostFocus = false;
    Subscription keys = events.subscribe<KeyEvent>([&](const KeyEvent& e) {
        if (e.window == first->id() && !e.down)
        {
            releasedKeys.push_back(e.key);
        }
    });
    Subscription buttons = events.subscribe<MouseButtonEvent>([&](const MouseButtonEvent& e) {
        releasedButton = releasedButton || (e.window == first->id() && !e.down);
    });
    Subscription focus = events.subscribe<WindowFocusEvent>(
        [&](const WindowFocusEvent& e) { lostFocus = lostFocus || (e.window == first->id() && !e.focused); });

    second->focus();
    platform->pollEvents();
    events.dispatchQueued();

    CHECK(lostFocus);
    CHECK(releasedKeys == std::vector<Key>{Key::W});
    CHECK(releasedButton);
    CHECK_FALSE(first->input().isDown(Key::W));
    CHECK_FALSE(first->input().isDown(MouseButton::Left));
}

TEST_CASE("invalid window sizes are rejected")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    WindowDesc desc;
    desc.width = 0;
    CHECK(Window::create(*platform, desc) == nullptr);
}

TEST_CASE("pollEvents starts a new input frame for every window")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    EventBus events;
    auto first = Window::create(*platform, WindowDesc{}, &events);
    auto second = Window::create(*platform, WindowDesc{}, &events);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    first->input().setKey(Key::A, true);
    second->input().addScroll(Vec2(0.0f, 1.0f));
    platform->pollEvents();
    CHECK(first->input().isDown(Key::A));
    CHECK_FALSE(first->input().wasPressed(Key::A));
    CHECK(second->input().scrollDelta() == Vec2(0.0f));
}

TEST_CASE("resizing a window enqueues resize events")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    EventBus events;
    auto window = Window::create(*platform, WindowDesc{}, &events);
    REQUIRE(window != nullptr);

    UVec2 resized{0, 0};
    Subscription sub = events.subscribe<WindowResizedEvent>([&](const WindowResizedEvent& e) {
        CHECK(e.window == window->id());
        resized = e.size;
    });
    window->setSize(UVec2(800, 600));
    platform->pollEvents();
    events.dispatchQueued();
    CHECK(window->size() == UVec2(800, 600));
    CHECK(resized == UVec2(800, 600));
}

TEST_CASE("the clipboard round-trips UTF-8 text")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    auto window = Window::create(*platform, WindowDesc{});
    REQUIRE(window != nullptr);
    window->setClipboardText("Ağaç \xF0\x9F\x8C\xB2 tree");
    CHECK(window->clipboardText() == "Ağaç \xF0\x9F\x8C\xB2 tree");
    window->setClipboardText("");
    CHECK(window->clipboardText().empty());
}

TEST_CASE("windows remember their cursor shape and can move the cursor")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    auto first = Window::create(*platform, WindowDesc{});
    auto second = Window::create(*platform, WindowDesc{});
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->cursorShape() == CursorShape::Arrow);
    for (usize i = 0; i < kCursorShapeCount; ++i)
    {
        const auto shape = static_cast<CursorShape>(i);
        first->setCursorShape(shape);
        second->setCursorShape(shape); // cursors are shared between windows
        CHECK(first->cursorShape() == shape);
    }
    CHECK(first->setCursorPosition(Vec2(10.0f, 20.0f)));
}

TEST_CASE("windows maximize, restore and iconify")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    auto window = Window::create(*platform, WindowDesc{});
    REQUIRE(window != nullptr);
    CHECK_FALSE(window->isMaximized());
    window->maximize();
    CHECK(window->isMaximized());
    window->restore();
    CHECK_FALSE(window->isMaximized());
    window->iconify();
    CHECK(window->isMinimized());
    window->restore();
    CHECK_FALSE(window->isMinimized());

    WindowDesc desc;
    desc.maximized = true;
    auto maximized = Window::create(*platform, desc);
    REQUIRE(maximized != nullptr);
    CHECK(maximized->isMaximized());
}

TEST_CASE("moving a window enqueues a moved event")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    EventBus events;
    auto window = Window::create(*platform, WindowDesc{}, &events);
    REQUIRE(window != nullptr);
    std::vector<IVec2> moves;
    Subscription sub = events.subscribe<WindowMovedEvent>([&](const WindowMovedEvent& e) {
        CHECK(e.window == window->id());
        moves.push_back(e.position);
    });
    CHECK(window->setPosition(IVec2(100, 200)));
    CHECK(window->setPosition(IVec2(-5, 40)));
    REQUIRE(window->position().has_value());
    CHECK(*window->position() == IVec2(-5, 40));
    events.dispatchQueued();
    CHECK(moves == std::vector<IVec2>{IVec2(100, 200), IVec2(-5, 40)});
}

TEST_CASE("a window that needs redrawing enqueues a refresh event")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    EventBus events;
    auto window = Window::create(*platform, WindowDesc{}, &events);
    REQUIRE(window != nullptr);
    int refreshes = 0;
    Subscription sub = events.subscribe<WindowRefreshEvent>([&](const WindowRefreshEvent& e) {
        CHECK(e.window == window->id());
        ++refreshes;
    });
    window->setSize(UVec2(300, 200));
    events.dispatchQueued();
    CHECK(refreshes == 1);
}

TEST_CASE("dropped files are delivered in order with their paths until the next poll")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    EventBus events;
    auto window = Window::create(*platform, WindowDesc{}, &events);
    REQUIRE(window != nullptr);

    struct Drop
    {
        std::vector<std::string> paths;
        Vec2 position;
    };
    std::vector<Drop> drops;
    Subscription sub = events.subscribe<FilesDroppedEvent>([&](const FilesDroppedEvent& e) {
        CHECK(e.window == window->id());
        Drop drop{{}, e.position};
        for (u32 i = 0; i < e.count; ++i)
        {
            drop.paths.emplace_back(window->droppedPath(e.first + i));
        }
        drops.push_back(std::move(drop));
    });

    const std::string_view firstDrop[] = {"C:/assets/crate.png", "/home/ayşe/sahne.json"};
    const std::string_view secondDrop[] = {"music.ogg"};
    window->dropFiles(firstDrop, Vec2(10.0f, 20.0f));
    window->dropFiles(secondDrop, Vec2(30.0f, 40.0f));
    window->dropFiles({}, Vec2(0.0f)); // nothing dropped, no event
    CHECK(window->droppedPathCount() == 3);
    events.dispatchQueued();
    REQUIRE(drops.size() == 2);
    CHECK(drops[0].paths == std::vector<std::string>{"C:/assets/crate.png", "/home/ayşe/sahne.json"});
    CHECK(drops[0].position == Vec2(10.0f, 20.0f));
    CHECK(drops[1].paths == std::vector<std::string>{"music.ogg"});
    CHECK(drops[1].position == Vec2(30.0f, 40.0f));

    platform->pollEvents();
    CHECK(window->droppedPathCount() == 0);
}

TEST_CASE("window icons are validated")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    auto window = Window::create(*platform, WindowDesc{});
    REQUIRE(window != nullptr);
    const std::vector<u8> pixels(2 * 2 * 4, 0xFF);
    const WindowIcon icon{UVec2(2, 2), pixels};
    CHECK(window->setIcon(std::span(&icon, 1)));
    const WindowIcon wrongSize{UVec2(3, 2), pixels};
    CHECK_FALSE(window->setIcon(std::span(&wrongSize, 1)));
    const WindowIcon empty{UVec2(0, 0), {}};
    CHECK_FALSE(window->setIcon(std::span(&empty, 1)));
    CHECK(window->setIcon({}));                                  // back to the default icon
    window->setImeRect(Vec2(10.0f, 10.0f), Vec2(100.0f, 20.0f)); // no input method without a display
}

TEST_CASE("the headless platform reports one simulated monitor")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    const std::vector<MonitorInfo> monitors = platform->monitors();
    REQUIRE(monitors.size() == 1);
    const MonitorInfo& monitor = monitors[0];
    CHECK_FALSE(monitor.name.empty());
    CHECK(monitor.primary);
    CHECK(monitor.size.x > 0);
    CHECK(monitor.size.y > 0);
    CHECK(monitor.workSize.y <= monitor.size.y);
    CHECK(monitor.workPosition.y >= monitor.position.y);
    CHECK(monitor.contentScale == Vec2(1.0f));
}

TEST_CASE("wake can be called from any thread")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    std::thread other([&] { platform->wake(); });
    other.join();
    platform->wake();
    platform->waitEvents(0.0);
}

TEST_CASE("key display names follow the layout and skip keys that type no character")
{
    auto platform = Platform::create(PlatformDesc{PlatformBackend::Headless});
    REQUIRE(platform != nullptr);
    CHECK(platform->keyDisplayName(Key::A) == "a");
    CHECK(platform->keyDisplayName(Key::Semicolon) == ";");
    CHECK(platform->keyDisplayName(Key::Num7) == "7");
    for (const Key key : {Key::Space, Key::LeftShift, Key::F1, Key::Up, Key::Enter, Key::Unknown})
    {
        INFO(keyName(key));
        CHECK_FALSE(platform->keyDisplayName(key).has_value());
    }
}

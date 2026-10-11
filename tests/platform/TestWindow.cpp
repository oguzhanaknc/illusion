#include <illusion/core/EventBus.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <doctest/doctest.h>

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

// Tests that need a real window system (CTest label "display"). CI runs them on X11 under Xvfb in the gpu
// job; locally they use the desktop.

#include <illusion/core/Log.hpp>
#include <illusion/core/Time.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <thread>
#include <vector>

using namespace illusion;

namespace
{

struct DisplayFixture
{
    std::shared_ptr<MemorySink> sink = std::make_shared<MemorySink>(256);
    std::unique_ptr<Platform> platform;
    std::unique_ptr<Window> window;

    DisplayFixture()
    {
        Logger::instance().addSink(sink);
        platform = Platform::create();
        REQUIRE_MESSAGE(platform != nullptr, "no display: run the display tests under a window system");
        WindowDesc desc;
        desc.title = "illusion display test";
        desc.width = 320;
        desc.height = 240;
        desc.visible = false;
        window = Window::create(*platform, desc);
        REQUIRE(window != nullptr);
    }

    ~DisplayFixture()
    {
        window.reset();
        platform.reset();
        Logger::instance().removeSink(sink.get());
    }

    DisplayFixture(const DisplayFixture&) = delete;
    DisplayFixture& operator=(const DisplayFixture&) = delete;

    [[nodiscard]] usize errorCount() const
    {
        usize errors = 0;
        for (const MemorySink::Entry& entry : sink->entries())
        {
            if (entry.level >= LogLevel::Error)
            {
                ++errors;
            }
        }
        return errors;
    }
};

} // namespace

TEST_CASE("the system clipboard round-trips UTF-8 text")
{
    DisplayFixture fixture;
    fixture.window->setClipboardText("illusion \xC3\xA7\xC4\x9F\xC3\xBC \xF0\x9F\x8E\xAE");
    fixture.platform->pollEvents();
    CHECK(fixture.window->clipboardText() == "illusion \xC3\xA7\xC4\x9F\xC3\xBC \xF0\x9F\x8E\xAE");
    CHECK(fixture.errorCount() == 0);
}

TEST_CASE("every cursor shape can be shown, missing ones as the arrow")
{
    DisplayFixture fixture;
    for (usize i = 0; i < kCursorShapeCount; ++i)
    {
        const auto shape = static_cast<CursorShape>(i);
        fixture.window->setCursorShape(shape);
        CHECK(fixture.window->cursorShape() == shape);
    }
    fixture.window->setCursorShape(CursorShape::Arrow);
    fixture.platform->pollEvents();
    CHECK(fixture.errorCount() == 0);
}

TEST_CASE("wake from another thread ends a long wait")
{
    DisplayFixture fixture;
    // If the wake arrives before the wait starts, the wait finds its empty event queued and returns too.
    std::thread waker([&] { fixture.platform->wake(); });
    const f64 start = monotonicSeconds();
    fixture.platform->waitEvents(30.0); // the timeout is only a failure ceiling
    const f64 waited = monotonicSeconds() - start;
    waker.join();
    CHECK(waited < 10.0);
}

TEST_CASE("the window system reports monitors, the primary one first")
{
    DisplayFixture fixture;
    const std::vector<MonitorInfo> monitors = fixture.platform->monitors();
    REQUIRE_FALSE(monitors.empty());
    CHECK(monitors[0].primary);
    for (const MonitorInfo& monitor : monitors)
    {
        CHECK(monitor.size.x > 0);
        CHECK(monitor.contentScale.x > 0.0f);
    }
}

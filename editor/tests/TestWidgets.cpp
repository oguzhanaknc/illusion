#include "EditorTestSupport.hpp"

#include <illusion/editor/ui/Icons.hpp>
#include <illusion/editor/ui/UiDriver.hpp>
#include <illusion/editor/ui/Widgets.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace illusion;
using namespace illusion::editor;

TEST_CASE("ease-out runs from 0 to 1 and is instant with reduced motion")
{
    CHECK(ui::easeOut(0.0, 0.0, 0.15, false) == doctest::Approx(0.0f));
    CHECK(ui::easeOut(0.075, 0.0, 0.15, false) == doctest::Approx(0.875f));
    CHECK(ui::easeOut(0.15, 0.0, 0.15, false) == doctest::Approx(1.0f));
    CHECK(ui::easeOut(5.0, 0.0, 0.15, false) == doctest::Approx(1.0f));
    CHECK(ui::easeOut(-1.0, 0.0, 0.15, false) == doctest::Approx(0.0f));
    CHECK(ui::easeOut(0.0, 0.0, 0.15, true) == doctest::Approx(1.0f));
    CHECK(ui::easeOut(0.0, 0.0, 0.0, false) == doctest::Approx(1.0f));
}

TEST_CASE("toasts keep the newest when full and expire after their duration")
{
    test::GalleryFixture fixture;
    ui::FrameText& text = fixture.galleryPage->text();
    ui::UiKit kit(fixture.theme, fixture.fonts.get(), text);
    ui::ToastQueue toasts;
    for (int i = 0; i < 6; ++i)
    {
        toasts.push(ui::icons::kInfo, ui::ThemeToken::Info, "message " + std::to_string(i), 0.0, 1.0 + i);
    }
    CHECK(toasts.size() == ui::ToastQueue::kCapacity);
    CHECK(toasts.message(0) == "message 2");

    const auto drawAt = [&](f64 now) {
        fixture.headless->beginFrame(test::kFrameSize);
        text.reset();
        const std::optional<u32> action = toasts.draw(kit, now);
        fixture.headless->endFrame();
        return action;
    };
    CHECK_FALSE(drawAt(0.5).has_value());
    CHECK(toasts.size() == 4);
    // The two oldest (durations 1 and 2) were dropped when full; the rest last 3 to 6 seconds.
    drawAt(3.5);
    CHECK(toasts.size() == 3);
    drawAt(10.0);
    CHECK(toasts.size() == 0);
}

TEST_CASE("long toast text is cut at a UTF-8 character boundary")
{
    test::GalleryFixture fixture;
    ui::FrameText& text = fixture.galleryPage->text();
    ui::UiKit kit(fixture.theme, fixture.fonts.get(), text);
    ui::ToastQueue toasts;
    // 'ğ' takes two bytes; 127 bytes of text fit, which ends in the middle of one.
    std::string message(126, 'a');
    message += "ğğ";
    toasts.push(ui::icons::kInfo, ui::ThemeToken::Info, message, 0.0);
    ui::UiDriver driver(fixture.context, [&] {
        fixture.headless->beginFrame(test::kFrameSize);
        text.reset();
        (void)toasts.draw(kit, 1.0);
        fixture.headless->endFrame();
    });
    driver.runFrames(2);
    CHECK(toasts.size() == 1);
    CHECK(toasts.message(0) == std::string(126, 'a'));
    CHECK(driver.find("toast.0.close").has_value());
}

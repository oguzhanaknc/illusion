#include <illusion/platform/Input.hpp>

#include <doctest/doctest.h>

using namespace illusion;

TEST_CASE("key presses produce pressed, held and released states across frames")
{
    InputState input;
    input.beginFrame();
    input.setKey(Key::W, true);
    CHECK(input.isDown(Key::W));
    CHECK(input.wasPressed(Key::W));
    CHECK_FALSE(input.wasReleased(Key::W));

    input.beginFrame();
    CHECK(input.isDown(Key::W));
    CHECK_FALSE(input.wasPressed(Key::W));

    input.setKey(Key::W, true); // OS key repeat: no new edge
    CHECK_FALSE(input.wasPressed(Key::W));

    input.beginFrame();
    input.setKey(Key::W, false);
    CHECK_FALSE(input.isDown(Key::W));
    CHECK(input.wasReleased(Key::W));
}

TEST_CASE("a tap within one frame reports both edges")
{
    InputState input;
    input.setKey(Key::Space, true);
    input.setKey(Key::Space, false);
    CHECK(input.wasPressed(Key::Space));
    CHECK(input.wasReleased(Key::Space));
    CHECK_FALSE(input.isDown(Key::Space));
}

TEST_CASE("unknown keys are ignored")
{
    InputState input;
    input.setKey(Key::Unknown, true);
    CHECK_FALSE(input.isDown(Key::Unknown));
    CHECK_FALSE(input.wasPressed(Key::Unknown));
}

TEST_CASE("mouse buttons, movement and scroll accumulate per frame")
{
    InputState input;
    input.setMousePosition(Vec2(100.0f, 50.0f)); // first position: no delta
    CHECK(input.mouseDelta() == Vec2(0.0f));

    input.beginFrame();
    input.setMousePosition(Vec2(110.0f, 45.0f));
    input.setMousePosition(Vec2(120.0f, 40.0f));
    CHECK(input.mousePosition() == Vec2(120.0f, 40.0f));
    CHECK(input.mouseDelta() == Vec2(20.0f, -10.0f));

    input.addScroll(Vec2(0.0f, 1.0f));
    input.addScroll(Vec2(0.0f, 2.0f));
    CHECK(input.scrollDelta() == Vec2(0.0f, 3.0f));

    input.setMouseButton(MouseButton::Right, true);
    CHECK(input.isDown(MouseButton::Right));
    CHECK(input.wasPressed(MouseButton::Right));

    input.beginFrame();
    CHECK(input.mouseDelta() == Vec2(0.0f));
    CHECK(input.scrollDelta() == Vec2(0.0f));
    CHECK(input.isDown(MouseButton::Right));
    CHECK_FALSE(input.wasPressed(MouseButton::Right));
}

TEST_CASE("resetMouseTracking suppresses the jump after a cursor mode change")
{
    InputState input;
    input.setMousePosition(Vec2(0.0f));
    input.resetMouseTracking();
    input.setMousePosition(Vec2(500.0f, 500.0f));
    CHECK(input.mouseDelta() == Vec2(0.0f));
}

TEST_CASE("text input is stored in order and capped per frame")
{
    InputState input;
    input.addText(U'h');
    input.addText(U'ı'); // Turkish dotless i
    CHECK(input.text() == std::u32string_view(U"hı"));
    for (usize i = 0; i < 2 * InputState::kMaxTextPerFrame; ++i)
    {
        input.addText(U'x');
    }
    CHECK(input.text().size() == InputState::kMaxTextPerFrame);
    input.beginFrame();
    CHECK(input.text().empty());
}

TEST_CASE("releaseAll releases every held key and button")
{
    InputState input;
    input.setKey(Key::A, true);
    input.setMouseButton(MouseButton::Left, true);
    input.setModifiers(KeyModifiers{true, false, false, false});
    input.beginFrame();
    input.releaseAll();
    CHECK_FALSE(input.isDown(Key::A));
    CHECK(input.wasReleased(Key::A));
    CHECK_FALSE(input.isDown(MouseButton::Left));
    CHECK(input.wasReleased(MouseButton::Left));
    CHECK(input.modifiers() == KeyModifiers{});
}

TEST_CASE("key names round-trip for every key")
{
    for (u16 i = 0; i < static_cast<u16>(Key::Count); ++i)
    {
        const auto key = static_cast<Key>(i);
        const std::string_view name = keyName(key);
        CHECK_FALSE(name.empty());
        const auto parsed = keyFromName(name);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == key);
    }
    CHECK(keyName(Key::LeftControl) == "LeftControl");
    CHECK_FALSE(keyFromName("NotAKey").has_value());
    CHECK(mouseButtonName(MouseButton::Middle) == "Middle");
}

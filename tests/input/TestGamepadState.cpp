#include <illusion/input/Gamepad.hpp>

#include <doctest/doctest.h>

using namespace illusion;

TEST_CASE("gamepad buttons report held state and per-frame edges")
{
    GamepadState pad;
    pad.setButton(GamepadButton::South, true);
    CHECK(pad.isDown(GamepadButton::South));
    CHECK(pad.wasPressed(GamepadButton::South));
    pad.beginFrame();
    CHECK(pad.isDown(GamepadButton::South));
    CHECK_FALSE(pad.wasPressed(GamepadButton::South));
    pad.setButton(GamepadButton::South, false);
    CHECK(pad.wasReleased(GamepadButton::South));
    CHECK_FALSE(pad.isDown(GamepadButton::South));
}

TEST_CASE("axes are clamped to their ranges")
{
    GamepadState pad;
    pad.setAxis(GamepadAxis::LeftX, 2.0f);
    pad.setAxis(GamepadAxis::LeftY, -3.0f);
    pad.setAxis(GamepadAxis::LeftTrigger, -0.5f);
    CHECK(pad.axis(GamepadAxis::LeftX) == 1.0f);
    CHECK(pad.axis(GamepadAxis::LeftY) == -1.0f);
    CHECK(pad.axis(GamepadAxis::LeftTrigger) == 0.0f);
}

TEST_CASE("the stick dead zone is radial and rescales the remaining range")
{
    GamepadState pad;
    pad.setStickDeadZone(0.2f);
    pad.setAxis(GamepadAxis::LeftX, 0.1f);
    pad.setAxis(GamepadAxis::LeftY, 0.1f); // length 0.14 < 0.2
    CHECK(pad.stick(GamepadStick::Left) == Vec2(0.0f));

    pad.setAxis(GamepadAxis::LeftX, 0.6f);
    pad.setAxis(GamepadAxis::LeftY, 0.0f);
    CHECK(pad.stick(GamepadStick::Left).x == doctest::Approx((0.6 - 0.2) / 0.8));

    pad.setAxis(GamepadAxis::LeftX, 1.0f);
    CHECK(pad.stick(GamepadStick::Left).x == doctest::Approx(1.0));

    // Direction is preserved on diagonals.
    pad.setAxis(GamepadAxis::LeftX, 0.5f);
    pad.setAxis(GamepadAxis::LeftY, 0.5f);
    const Vec2 diagonal = pad.stick(GamepadStick::Left);
    CHECK(diagonal.x == doctest::Approx(diagonal.y));
}

TEST_CASE("the trigger dead zone rescales to [0, 1]")
{
    GamepadState pad;
    pad.setTriggerDeadZone(0.1f);
    pad.setAxis(GamepadAxis::RightTrigger, 0.05f);
    CHECK(pad.trigger(GamepadTrigger::Right) == 0.0f);
    pad.setAxis(GamepadAxis::RightTrigger, 0.55f);
    CHECK(pad.trigger(GamepadTrigger::Right) == doctest::Approx(0.5));
    pad.setAxis(GamepadAxis::RightTrigger, 1.0f);
    CHECK(pad.trigger(GamepadTrigger::Right) == doctest::Approx(1.0));
}

TEST_CASE("releaseAll releases buttons and centers axes")
{
    GamepadState pad;
    pad.setButton(GamepadButton::North, true);
    pad.setAxis(GamepadAxis::RightX, 0.9f);
    pad.setFinger(0, TouchpadFinger{true, Vec2(0.5f), 1.0f});
    pad.beginFrame();
    pad.releaseAll();
    CHECK(pad.wasReleased(GamepadButton::North));
    CHECK(pad.axis(GamepadAxis::RightX) == 0.0f);
    CHECK_FALSE(pad.finger(0).down);
}

TEST_CASE("button and axis names round-trip")
{
    for (u8 i = 0; i < static_cast<u8>(GamepadButton::Count); ++i)
    {
        const auto button = static_cast<GamepadButton>(i);
        CHECK(gamepadButtonFromName(gamepadButtonName(button)) == button);
    }
    for (u8 i = 0; i < static_cast<u8>(GamepadAxis::Count); ++i)
    {
        const auto axis = static_cast<GamepadAxis>(i);
        CHECK(gamepadAxisFromName(gamepadAxisName(axis)) == axis);
    }
    CHECK_FALSE(gamepadButtonFromName("Jump").has_value());
}

TEST_CASE("gamepad families group controller models")
{
    CHECK(gamepadFamily(GamepadType::XboxOne) == GamepadFamily::Xbox);
    CHECK(gamepadFamily(GamepadType::PS5) == GamepadFamily::PlayStation);
    CHECK(gamepadFamily(GamepadType::SwitchPro) == GamepadFamily::Nintendo);
    CHECK(gamepadFamily(GamepadType::Generic) == GamepadFamily::Generic);
    CHECK(gamepadTypeName(GamepadType::PS5) == "PS5");
}

TEST_CASE("DualSense trigger effects encode to the documented byte layout")
{
    CHECK(encodeDualSenseTriggerEffect(TriggerEffect::off())[0] == 0x05);

    const auto resistance = encodeDualSenseTriggerEffect(TriggerEffect::resistance(0.0f, 110.0f / 255.0f));
    CHECK(resistance[0] == 0x01);
    CHECK(resistance[1] == 0);
    CHECK(resistance[2] == 110);

    const auto weapon = encodeDualSenseTriggerEffect(TriggerEffect::weapon(0.2f, 0.6f, 1.0f));
    CHECK(weapon[0] == 0x02);
    CHECK(weapon[1] == 51);
    CHECK(weapon[2] == 153);
    CHECK(weapon[3] == 255);

    // End before start is corrected rather than sent to the controller.
    const auto backwards = encodeDualSenseTriggerEffect(TriggerEffect::weapon(0.6f, 0.2f, 1.0f));
    CHECK(backwards[2] >= backwards[1]);

    // Matches SDL testcontroller's "resistance and vibration" example: { 0x06, 15, 63, 128 }.
    const auto vibration =
        encodeDualSenseTriggerEffect(TriggerEffect::vibration(128.0f / 255.0f, 63.0f / 255.0f, 15));
    CHECK(vibration[0] == 0x06);
    CHECK(vibration[1] == 15);
    CHECK(vibration[2] == 63);
    CHECK(vibration[3] == 128);
    for (usize i = 4; i < vibration.size(); ++i)
    {
        CHECK(vibration[i] == 0);
    }
}

TEST_CASE("analog activation is reported once when an axis crosses half range")
{
    GamepadState pad;
    pad.setAxis(GamepadAxis::LeftX, 0.3f);
    CHECK_FALSE(pad.wasAnalogActivated());
    pad.setAxis(GamepadAxis::LeftX, 0.8f);
    CHECK(pad.wasAnalogActivated());
    pad.beginFrame();
    pad.setAxis(GamepadAxis::LeftX, 0.9f); // still deflected: no new activation
    CHECK_FALSE(pad.wasAnalogActivated());
    pad.setAxis(GamepadAxis::RightTrigger, 1.0f);
    CHECK(pad.wasAnalogActivated());
}

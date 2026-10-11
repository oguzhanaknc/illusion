#include <illusion/input/InputActions.hpp>

#include <doctest/doctest.h>

#include <array>

using namespace illusion;

namespace
{

struct Fixture
{
    InputState keyboard;
    GamepadState pad;
    std::array<const GamepadState*, 1> pads{&pad};
    InputActions actions;

    void frame()
    {
        actions.update(&keyboard, pads);
        keyboard.beginFrame();
        pad.beginFrame();
    }
};

} // namespace

TEST_CASE("button actions combine keyboard and gamepad bindings")
{
    Fixture f;
    const ActionId jump = f.actions.addAction("Jump", ActionType::Button);
    f.actions.addBinding(jump, InputBinding::key(Key::Space));
    f.actions.addBinding(jump, InputBinding::gamepadButton(GamepadButton::South));

    f.keyboard.setKey(Key::Space, true);
    f.frame();
    CHECK(f.actions.isDown(jump));
    CHECK(f.actions.wasPressed(jump));
    CHECK(f.actions.lastDevice() == InputDevice::KeyboardMouse);

    // The gamepad joining in does not re-trigger the press.
    f.pad.setButton(GamepadButton::South, true);
    f.frame();
    CHECK(f.actions.isDown(jump));
    CHECK_FALSE(f.actions.wasPressed(jump));

    f.keyboard.setKey(Key::Space, false);
    f.frame();
    CHECK(f.actions.isDown(jump)); // still held on the gamepad
    f.pad.setButton(GamepadButton::South, false);
    f.frame();
    CHECK_FALSE(f.actions.isDown(jump));
    CHECK(f.actions.wasReleased(jump));
}

TEST_CASE("a tap shorter than a frame is still seen as press and release")
{
    Fixture f;
    const ActionId fire = f.actions.addAction("Fire", ActionType::Button);
    f.actions.addBinding(fire, InputBinding::mouseButton(MouseButton::Left));
    f.keyboard.setMouseButton(MouseButton::Left, true);
    f.keyboard.setMouseButton(MouseButton::Left, false);
    f.frame();
    CHECK(f.actions.wasPressed(fire));
    CHECK(f.actions.wasReleased(fire));
    CHECK_FALSE(f.actions.isDown(fire));
}

TEST_CASE("triggers and stick directions can drive button actions")
{
    Fixture f;
    const ActionId shoot = f.actions.addAction("Shoot", ActionType::Button);
    const ActionId crouch = f.actions.addAction("Crouch", ActionType::Button);
    f.actions.addBinding(shoot, InputBinding::gamepadAxis(GamepadAxis::RightTrigger));
    f.actions.addBinding(crouch, InputBinding::gamepadAxis(GamepadAxis::LeftY, -1.0f));

    f.pad.setAxis(GamepadAxis::RightTrigger, 0.3f);
    f.frame();
    CHECK_FALSE(f.actions.isDown(shoot));
    f.pad.setAxis(GamepadAxis::RightTrigger, 0.9f);
    f.pad.setAxis(GamepadAxis::LeftY, -1.0f); // stick down
    f.frame();
    CHECK(f.actions.wasPressed(shoot));
    CHECK(f.actions.isDown(crouch));
    CHECK(f.actions.lastDevice() == InputDevice::Gamepad);
}

TEST_CASE("axis actions take the strongest binding")
{
    Fixture f;
    const ActionId throttle = f.actions.addAction("Throttle", ActionType::Axis);
    f.actions.addBinding(throttle, InputBinding::key(Key::W));
    f.actions.addBinding(throttle, InputBinding::key(Key::S, -1.0f));
    f.actions.addBinding(throttle, InputBinding::gamepadAxis(GamepadAxis::RightTrigger));

    f.keyboard.setKey(Key::S, true);
    f.frame();
    CHECK(f.actions.value(throttle) == -1.0f);

    f.keyboard.setKey(Key::S, false);
    f.pad.setAxis(GamepadAxis::RightTrigger, 1.0f);
    f.frame();
    CHECK(f.actions.value(throttle) == doctest::Approx(1.0));
}

TEST_CASE("vector actions from WASD and sticks are limited to length 1")
{
    Fixture f;
    const ActionId move = f.actions.addAction("Move", ActionType::Vector2);
    f.actions.addBinding(move, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
    f.actions.addBinding(move, InputBinding::gamepadStick(GamepadStick::Left));

    f.keyboard.setKey(Key::W, true);
    f.keyboard.setKey(Key::D, true);
    f.frame();
    const Vec2 diagonal = f.actions.vector(move);
    CHECK(glm::length(diagonal) == doctest::Approx(1.0));
    CHECK(diagonal.x > 0.0f);
    CHECK(diagonal.y > 0.0f); // W is up

    f.keyboard.setKey(Key::W, false);
    f.keyboard.setKey(Key::D, false);
    f.pad.setAxis(GamepadAxis::LeftX, -1.0f);
    f.frame();
    CHECK(f.actions.vector(move).x == doctest::Approx(-1.0));

    f.pad.setAxis(GamepadAxis::LeftX, 0.05f); // inside the dead zone
    f.frame();
    CHECK(f.actions.vector(move) == Vec2(0.0f));
    CHECK_FALSE(f.actions.isDown(move));
}

TEST_CASE("bindings round-trip through text")
{
    const InputBinding bindings[] = {
        InputBinding::key(Key::Space),
        InputBinding::key(Key::S, -1.0f),
        InputBinding::mouseButton(MouseButton::Right),
        InputBinding::gamepadButton(GamepadButton::RightShoulder),
        InputBinding::gamepadAxis(GamepadAxis::LeftY, -1.0f),
        InputBinding::gamepadAxis(GamepadAxis::RightTrigger, 0.5f),
        InputBinding::gamepadStick(GamepadStick::Right),
        InputBinding::keyVector(Key::Up, Key::Down, Key::Left, Key::Right),
    };
    for (const InputBinding& binding : bindings)
    {
        const std::string text = bindingToString(binding);
        const auto parsed = bindingFromString(text);
        REQUIRE_MESSAGE(parsed.has_value(), text);
        CHECK_MESSAGE(*parsed == binding, text);
    }
    CHECK(bindingToString(InputBinding::key(Key::Space)) == "Key.Space");
    CHECK(bindingToString(InputBinding::gamepadAxis(GamepadAxis::LeftY, -1.0f)) == "Axis.LeftY*-1");
    for (const char* bad : {"", "Key", "Key.NotAKey", "Gamepad.Jump", "Axis.LeftX*abc", "Axis.LeftX*0",
                            "Keys.W.S.A", "Stick.Middle", "Key.Unknown"})
    {
        CHECK_MESSAGE(!bindingFromString(bad).has_value(), bad);
    }
}

TEST_CASE("serialize and deserialize restore bindings and report problems")
{
    InputActions original;
    const ActionId jump = original.addAction("Jump", ActionType::Button);
    const ActionId move = original.addAction("Move", ActionType::Vector2);
    original.addBinding(jump, InputBinding::key(Key::Space));
    original.addBinding(jump, InputBinding::gamepadButton(GamepadButton::South));
    original.addBinding(move, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
    const std::string text = original.serialize();

    InputActions restored;
    const ActionId jump2 = restored.addAction("Jump", ActionType::Button);
    const ActionId move2 = restored.addAction("Move", ActionType::Vector2);
    std::vector<std::string> errors;
    CHECK(restored.deserialize(text, &errors));
    CHECK(errors.empty());
    CHECK(restored.bindings(jump2).size() == 2);
    CHECK(restored.bindings(move2).size() == 1);
    CHECK(restored.serialize() == text);

    CHECK_FALSE(restored.deserialize("Jump = Key.Enter, Key.Bogus\nDance = Key.D\nnonsense\n", &errors));
    CHECK(errors.size() == 3);
    REQUIRE(restored.bindings(jump2).size() == 1); // the valid binding was applied
    CHECK(restored.bindings(jump2)[0] == InputBinding::key(Key::Enter));

    CHECK(restored.deserialize("Jump =\n"));
    CHECK(restored.bindings(jump2).empty());
}

TEST_CASE("binding management finds conflicts and avoids duplicates")
{
    InputActions actions;
    const ActionId jump = actions.addAction("Jump", ActionType::Button);
    const ActionId interact = actions.addAction("Interact", ActionType::Button);
    actions.addBinding(jump, InputBinding::key(Key::Space));
    actions.addBinding(jump, InputBinding::key(Key::Space));
    CHECK(actions.bindings(jump).size() == 1);
    actions.addBinding(interact, InputBinding::key(Key::E));

    CHECK(actions.findConflicts(InputBinding::key(Key::Space), interact) == std::vector<ActionId>{jump});
    CHECK(actions.findConflicts(InputBinding::key(Key::Space), jump).empty());
    CHECK(actions.removeBinding(jump, InputBinding::key(Key::Space)));
    CHECK_FALSE(actions.removeBinding(jump, InputBinding::key(Key::Space)));
    CHECK(actions.findAction("Interact") == interact);
    CHECK_FALSE(actions.findAction("Fly").has_value());
    CHECK(actions.actionName(interact) == "Interact");
    CHECK(actions.actionType(interact) == ActionType::Button);
}

TEST_CASE("glyphs follow the controller family")
{
    const InputBinding south = InputBinding::gamepadButton(GamepadButton::South);
    CHECK(glyphId(south, GamepadFamily::Xbox) == "xbox_a");
    CHECK(glyphId(south, GamepadFamily::PlayStation) == "ps_cross");
    CHECK(glyphId(south, GamepadFamily::Nintendo) == "switch_b");
    CHECK(glyphId(south, GamepadFamily::Generic) == "gamepad_south");
    CHECK(glyphLabel(south, GamepadFamily::PlayStation) == "Cross");

    const InputBinding trigger = InputBinding::gamepadAxis(GamepadAxis::LeftTrigger);
    CHECK(glyphId(trigger, GamepadFamily::Xbox) == "xbox_lt");
    CHECK(glyphLabel(trigger, GamepadFamily::PlayStation) == "L2");
    CHECK(glyphLabel(trigger, GamepadFamily::Nintendo) == "ZL");

    CHECK(glyphId(InputBinding::key(Key::LeftControl), GamepadFamily::Xbox) == "key_left_control");
    CHECK(glyphLabel(InputBinding::key(Key::Space), GamepadFamily::Xbox) == "Space");
    CHECK(glyphLabel(InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D), GamepadFamily::Xbox) == "WSAD");
    CHECK(glyphId(InputBinding::mouseButton(MouseButton::Left), GamepadFamily::Xbox) == "mouse_left");
    CHECK(glyphId(InputBinding::gamepadStick(GamepadStick::Right), GamepadFamily::PlayStation) ==
          "ps_right_stick");
}

TEST_CASE("BindingCapture ignores inputs held when it starts and returns the next one")
{
    InputState keyboard;
    GamepadState pad;
    const std::array<const GamepadState*, 1> pads{&pad};

    keyboard.setKey(Key::Enter, true); // the key that opened the rebinding menu
    BindingCapture capture(BindingCapture::Devices::Any, ActionType::Button);
    CHECK_FALSE(capture.poll(&keyboard, pads).has_value());
    keyboard.beginFrame();
    CHECK_FALSE(capture.poll(&keyboard, pads).has_value()); // still held
    keyboard.setKey(Key::Enter, false);
    keyboard.beginFrame();
    CHECK_FALSE(capture.poll(&keyboard, pads).has_value()); // arms now

    pad.setAxis(GamepadAxis::RightTrigger, 1.0f);
    const auto binding = capture.poll(&keyboard, pads);
    REQUIRE(binding.has_value());
    CHECK(*binding == InputBinding::gamepadAxis(GamepadAxis::RightTrigger, 1.0f));
}

TEST_CASE("BindingCapture respects the device filter and captures sticks for vector actions")
{
    InputState keyboard;
    GamepadState pad;
    const std::array<const GamepadState*, 1> pads{&pad};

    BindingCapture gamepadOnly(BindingCapture::Devices::Gamepad, ActionType::Vector2);
    CHECK_FALSE(gamepadOnly.poll(&keyboard, pads).has_value()); // arm
    keyboard.setKey(Key::K, true);
    CHECK_FALSE(gamepadOnly.poll(&keyboard, pads).has_value());
    pad.setAxis(GamepadAxis::RightX, 1.0f);
    const auto stick = gamepadOnly.poll(&keyboard, pads);
    REQUIRE(stick.has_value());
    CHECK(*stick == InputBinding::gamepadStick(GamepadStick::Right));

    InputState keys;
    BindingCapture keyboardOnly(BindingCapture::Devices::KeyboardMouse, ActionType::Button);
    CHECK_FALSE(keyboardOnly.poll(&keys, pads).has_value()); // pad axis is held but filtered out: arms
    keys.setKey(Key::F, true);
    const auto key = keyboardOnly.poll(&keys, pads);
    REQUIRE(key.has_value());
    CHECK(*key == InputBinding::key(Key::F));
}

TEST_CASE("a stick resting deflected does not keep claiming the last used device")
{
    Fixture f;
    f.actions.addAction("Jump", ActionType::Button);
    f.pad.setAxis(GamepadAxis::LeftX, 1.0f);
    f.frame();
    CHECK(f.actions.lastDevice() == InputDevice::Gamepad);
    f.keyboard.setKey(Key::Space, true);
    f.frame();
    CHECK(f.actions.lastDevice() == InputDevice::KeyboardMouse);
    f.frame(); // the stick is still held, but it did not move
    CHECK(f.actions.lastDevice() == InputDevice::KeyboardMouse);
}

TEST_CASE("BindingCapture catches a tap on the frame it arms")
{
    InputState keyboard;
    const std::array<const GamepadState*, 0> noPads{};
    BindingCapture capture(BindingCapture::Devices::KeyboardMouse, ActionType::Button);
    keyboard.setKey(Key::Q, true);
    keyboard.setKey(Key::Q, false); // pressed and released within the frame
    const auto binding = capture.poll(&keyboard, noPads);
    REQUIRE(binding.has_value());
    CHECK(*binding == InputBinding::key(Key::Q));
}

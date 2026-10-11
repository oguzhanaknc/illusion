#include "Budget.hpp"

#include <illusion/input/InputActions.hpp>

#include <array>
#include <string>

using namespace illusion;

ILL_BENCHMARK("input/actions-update", 6000.0)
{
    // A typical game: 32 actions with keyboard and gamepad bindings, two gamepads connected.
    InputActions actions;
    for (u32 i = 0; i < 32; ++i)
    {
        const ActionType type = i % 4 == 0 ? ActionType::Vector2 : ActionType::Button;
        const ActionId id = actions.addAction("Action" + std::to_string(i), type);
        if (type == ActionType::Vector2)
        {
            actions.addBinding(id, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
            actions.addBinding(id, InputBinding::gamepadStick(GamepadStick::Left));
        }
        else
        {
            actions.addBinding(id, InputBinding::key(static_cast<Key>(static_cast<u16>(Key::A) + i % 26)));
            actions.addBinding(id, InputBinding::gamepadButton(static_cast<GamepadButton>(i % 15)));
        }
    }
    InputState keyboard;
    keyboard.setKey(Key::W, true);
    GamepadState first;
    GamepadState second;
    first.setAxis(GamepadAxis::LeftX, 0.7f);
    second.setButton(GamepadButton::South, true);
    const std::array<const GamepadState*, 2> pads{&first, &second};

    bench.unit("frame").run("InputActions::update (32 actions, 2 gamepads)", [&] {
        actions.update(&keyboard, pads);
        ankerl::nanobench::doNotOptimizeAway(actions.vector(0));
    });
}

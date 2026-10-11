// input_viewer: shows how actions, gamepads and device features work. Connect an Xbox or PlayStation
// controller (or use the keyboard) and watch the log.
//
//   Jump      Space / South (A, Cross)  -> logs, rumbles the gamepad
//   Move      WASD / left stick         -> logged when it changes direction
//   Fire      left mouse / right trigger -> on a DualSense, toggles the right trigger's weapon effect
//   Escape closes the window. --frames N exits after N frames (smoke test).

#include <illusion/core/EventBus.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/input/GamepadSystem.hpp>
#include <illusion/input/InputActions.hpp>
#include <illusion/platform/Events.hpp>
#include <illusion/platform/Platform.hpp>
#include <illusion/platform/Window.hpp>

#include <charconv>
#include <cmath>
#include <string_view>
#include <vector>

using namespace illusion;

int main(int argc, char** argv)
{
    u64 frameLimit = 0;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::string_view(argv[i]) == "--frames")
        {
            const std::string_view value = argv[i + 1];
            std::from_chars(value.data(), value.data() + value.size(), frameLimit);
        }
    }

    EventBus events;
    auto platform = Platform::create();
    if (!platform)
    {
        return 1;
    }
    WindowDesc windowDesc;
    windowDesc.title = "illusion - input viewer";
    windowDesc.width = 640;
    windowDesc.height = 360;
    auto window = Window::create(*platform, windowDesc, &events);
    auto gamepads = GamepadSystem::create(&events);
    if (!window || !gamepads)
    {
        return 1;
    }

    InputActions actions;
    const ActionId jump = actions.addAction("Jump", ActionType::Button);
    const ActionId move = actions.addAction("Move", ActionType::Vector2);
    const ActionId fire = actions.addAction("Fire", ActionType::Button);
    actions.addBinding(jump, InputBinding::key(Key::Space));
    actions.addBinding(jump, InputBinding::gamepadButton(GamepadButton::South));
    actions.addBinding(move, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
    actions.addBinding(move, InputBinding::gamepadStick(GamepadStick::Left));
    actions.addBinding(fire, InputBinding::mouseButton(MouseButton::Left));
    actions.addBinding(fire, InputBinding::gamepadAxis(GamepadAxis::RightTrigger));

    Subscription onConnect = events.subscribe<GamepadConnectedEvent>([&](const GamepadConnectedEvent& e) {
        if (Gamepad* pad = gamepads->find(e.gamepad))
        {
            const GamepadCapabilities& caps = pad->capabilities();
            ILL_LOG_INFO(
                "Sample",
                "{} connected as player {} (rumble {}, LED {}, motion {}, touchpad {}, adaptive triggers {})",
                pad->name(), e.playerIndex + 1, caps.rumble, caps.led, caps.motionSensors, caps.touchpad,
                caps.adaptiveTriggers);
            pad->setLed(40, 80, 255);
        }
    });

    std::vector<const GamepadState*> states;
    bool weaponEffect = false;
    int lastDirection = -1;
    u64 frames = 0;
    while (!window->shouldClose() && (frameLimit == 0 || frames < frameLimit))
    {
        platform->waitEvents(1.0 / 120.0);
        gamepads->update(window->isFocused());
        events.dispatchQueued();
        if (window->input().wasPressed(Key::Escape))
        {
            window->setShouldClose(true);
        }

        states.clear();
        for (Gamepad* pad : gamepads->gamepads())
        {
            states.push_back(&pad->state());
        }
        actions.update(&window->input(), states);
        const GamepadFamily family = actions.lastDevice() == InputDevice::Gamepad
                                         ? gamepadFamily(actions.lastGamepadType())
                                         : GamepadFamily::Generic;

        if (actions.wasPressed(jump))
        {
            const auto bindings = actions.bindings(jump);
            const InputBinding& shown =
                actions.lastDevice() == InputDevice::Gamepad ? bindings[1] : bindings[0];
            ILL_LOG_INFO("Sample", "Jump ({} / {})", glyphLabel(shown, family), glyphId(shown, family));
            for (Gamepad* pad : gamepads->gamepads())
            {
                pad->rumble(0.4f, 0.8f, 150);
            }
        }
        if (actions.wasPressed(fire))
        {
            ILL_LOG_INFO("Sample", "Fire");
            weaponEffect = !weaponEffect;
            for (Gamepad* pad : gamepads->gamepads())
            {
                pad->setTriggerEffect(GamepadTrigger::Right, weaponEffect
                                                                 ? TriggerEffect::weapon(0.3f, 0.6f, 1.0f)
                                                                 : TriggerEffect::off());
            }
        }
        const Vec2 direction = actions.vector(move);
        const int octant =
            glm::length(direction) < 0.5f
                ? -1
                : static_cast<int>(std::lround(std::atan2(direction.y, direction.x) / 0.785398f) + 8) % 8;
        if (octant != lastDirection)
        {
            lastDirection = octant;
            ILL_LOG_INFO("Sample", "Move ({:.2f}, {:.2f})", direction.x, direction.y);
        }
        ++frames;
    }
    return 0;
}

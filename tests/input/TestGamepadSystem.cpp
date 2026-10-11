#include <illusion/core/EventBus.hpp>
#include <illusion/input/GamepadSystem.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace illusion;

// End-to-end tests through SDL using virtual gamepads; no hardware needed.

namespace
{

/// Runs update() until @p predicate holds (SDL may deliver a change over a couple of pumps).
template <class Predicate>
bool updateUntil(GamepadSystem& system, Predicate predicate)
{
    for (int i = 0; i < 10; ++i)
    {
        system.update();
        if (predicate())
        {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("virtual gamepads connect, report input and disconnect")
{
    EventBus events;
    auto system = GamepadSystem::create(&events);
    REQUIRE(system != nullptr);

    std::vector<GamepadConnectedEvent> connected;
    std::vector<GamepadDisconnectedEvent> disconnected;
    Subscription onConnect = events.subscribe<GamepadConnectedEvent>(
        [&](const GamepadConnectedEvent& e) { connected.push_back(e); });
    Subscription onDisconnect = events.subscribe<GamepadDisconnectedEvent>(
        [&](const GamepadDisconnectedEvent& e) { disconnected.push_back(e); });

    VirtualGamepadDesc desc;
    desc.name = "Test pad";
    desc.type = GamepadType::XboxOne;
    auto virtualPad = system->attachVirtualGamepad(desc);
    REQUIRE(virtualPad != nullptr);
    REQUIRE(updateUntil(*system, [&] { return system->find(virtualPad->id()) != nullptr; }));
    events.dispatchQueued();

    Gamepad* pad = system->find(virtualPad->id());
    REQUIRE(pad != nullptr);
    CHECK(pad->type() == GamepadType::XboxOne);
    CHECK(pad->playerIndex() == 0);
    CHECK(system->findByPlayer(0) == pad);
    CHECK(system->gamepads().size() == 1);
    REQUIRE(connected.size() == 1);
    CHECK(connected[0].gamepad == pad->id());

    SUBCASE("buttons produce edges")
    {
        virtualPad->setButton(GamepadButton::South, true);
        REQUIRE(updateUntil(*system, [&] { return pad->state().isDown(GamepadButton::South); }));
        CHECK(pad->state().wasPressed(GamepadButton::South));
        system->update();
        CHECK_FALSE(pad->state().wasPressed(GamepadButton::South));
        virtualPad->setButton(GamepadButton::South, false);
        REQUIRE(updateUntil(*system, [&] { return !pad->state().isDown(GamepadButton::South); }));
        CHECK(pad->state().wasReleased(GamepadButton::South));
    }

    SUBCASE("axes follow the engine conventions")
    {
        virtualPad->setAxis(GamepadAxis::LeftY, 1.0f); // stick pushed up
        virtualPad->setAxis(GamepadAxis::LeftX, -0.5f);
        virtualPad->setAxis(GamepadAxis::RightTrigger, 1.0f);
        REQUIRE(updateUntil(*system, [&] { return pad->state().axis(GamepadAxis::RightTrigger) > 0.99f; }));
        CHECK(pad->state().axis(GamepadAxis::LeftY) == doctest::Approx(1.0).epsilon(0.001));
        CHECK(pad->state().axis(GamepadAxis::LeftX) == doctest::Approx(-0.5).epsilon(0.001));
        CHECK(pad->state().axis(GamepadAxis::LeftTrigger) == doctest::Approx(0.0).epsilon(0.001));
    }

    SUBCASE("rumble reaches the device")
    {
        CHECK(pad->capabilities().rumble);
        CHECK(pad->rumble(0.25f, 1.0f, 100));
        CHECK(virtualPad->outputs().lowFrequencyRumble == doctest::Approx(0.25).epsilon(0.001));
        CHECK(virtualPad->outputs().highFrequencyRumble == doctest::Approx(1.0));
        CHECK(pad->capabilities().triggerRumble);
        CHECK(pad->rumbleTriggers(0.5f, 0.0f, 100));
        CHECK(virtualPad->outputs().leftTriggerRumble == doctest::Approx(0.5).epsilon(0.001));
        CHECK_FALSE(pad->setTriggerEffect(GamepadTrigger::Right, TriggerEffect::resistance(0.0f, 1.0f)));
    }

    virtualPad.reset();
    REQUIRE(updateUntil(*system, [&] { return system->gamepads().empty(); }));
    events.dispatchQueued();
    REQUIRE(disconnected.size() == 1);
    CHECK(disconnected[0].gamepad == connected[0].gamepad);
}

TEST_CASE("a virtual DualSense supports LED, adaptive triggers, touchpad and motion")
{
    auto system = GamepadSystem::create();
    REQUIRE(system != nullptr);
    VirtualGamepadDesc desc;
    desc.type = GamepadType::PS5;
    auto virtualPad = system->attachVirtualGamepad(desc);
    REQUIRE(virtualPad != nullptr);
    REQUIRE(updateUntil(*system, [&] { return system->find(virtualPad->id()) != nullptr; }));
    Gamepad* pad = system->find(virtualPad->id());
    REQUIRE(pad != nullptr);
    CHECK(pad->type() == GamepadType::PS5);
    CHECK(gamepadFamily(pad->type()) == GamepadFamily::PlayStation);

    const GamepadCapabilities& caps = pad->capabilities();
    CHECK(caps.led);
    CHECK(caps.touchpad);
    CHECK(caps.motionSensors);
    CHECK(caps.adaptiveTriggers);

    CHECK(pad->setLed(10, 20, 30));
    CHECK(virtualPad->outputs().led == std::array<u8, 3>{10, 20, 30});

    REQUIRE(pad->setTriggerEffect(GamepadTrigger::Right, TriggerEffect::weapon(0.2f, 0.6f, 1.0f)));
    const std::vector<u8>& report = virtualPad->outputs().lastEffect;
    REQUIRE(report.size() == 47);
    CHECK(report[0] == 0x04);  // modify right trigger
    CHECK(report[10] == 0x02); // weapon mode in the right trigger block
    CHECK(report[21] == 0x00); // left trigger block untouched
    REQUIRE(pad->setTriggerEffect(GamepadTrigger::Left, TriggerEffect::off()));
    CHECK(virtualPad->outputs().lastEffect[0] == 0x08);
    CHECK(virtualPad->outputs().lastEffect[21] == 0x05);

    virtualPad->setTouchpadFinger(0, TouchpadFinger{true, Vec2(0.25f, 0.75f), 1.0f});
    REQUIRE(updateUntil(*system, [&] { return pad->state().finger(0).down; }));
    CHECK(pad->state().finger(0).position.x == doctest::Approx(0.25));
    CHECK(pad->state().finger(0).position.y == doctest::Approx(0.75));

    CHECK(pad->setMotionSensorsEnabled(true));
    CHECK(virtualPad->outputs().motionSensorsEnabled);
    virtualPad->setMotion(Vec3(0.1f, 0.2f, 0.3f), Vec3(0.0f, 9.81f, 0.0f));
    REQUIRE(updateUntil(*system, [&] { return pad->state().accelerometer().y > 9.0f; }));
    CHECK(pad->state().gyroscope().z == doctest::Approx(0.3));
}

TEST_CASE("player slots are reused after disconnection")
{
    auto system = GamepadSystem::create();
    REQUIRE(system != nullptr);
    auto first = system->attachVirtualGamepad();
    auto second = system->attachVirtualGamepad();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(updateUntil(*system, [&] { return system->gamepads().size() == 2; }));
    CHECK(system->find(first->id())->playerIndex() == 0);
    CHECK(system->find(second->id())->playerIndex() == 1);

    first.reset();
    REQUIRE(updateUntil(*system, [&] { return system->gamepads().size() == 1; }));
    auto third = system->attachVirtualGamepad();
    REQUIRE(third != nullptr);
    REQUIRE(updateUntil(*system, [&] { return system->gamepads().size() == 2; }));
    CHECK(system->find(third->id())->playerIndex() == 0);
}

TEST_CASE("gamepads read as released while input is not accepted")
{
    auto system = GamepadSystem::create();
    REQUIRE(system != nullptr);
    auto virtualPad = system->attachVirtualGamepad();
    REQUIRE(virtualPad != nullptr);
    REQUIRE(updateUntil(*system, [&] { return system->find(virtualPad->id()) != nullptr; }));
    Gamepad* pad = system->find(virtualPad->id());

    virtualPad->setButton(GamepadButton::East, true);
    virtualPad->setAxis(GamepadAxis::LeftX, 1.0f);
    REQUIRE(updateUntil(*system, [&] { return pad->state().isDown(GamepadButton::East); }));

    system->update(false); // the window lost focus
    CHECK_FALSE(pad->state().isDown(GamepadButton::East));
    CHECK(pad->state().wasReleased(GamepadButton::East));
    CHECK(pad->state().axis(GamepadAxis::LeftX) == 0.0f);
    system->update(false);
    CHECK_FALSE(pad->state().wasReleased(GamepadButton::East)); // reported once
    CHECK(system->gamepads().size() == 1);                      // still connected
}

TEST_CASE("every connected gamepad gets a unique player slot")
{
    auto system = GamepadSystem::create();
    REQUIRE(system != nullptr);
    std::vector<std::unique_ptr<VirtualGamepad>> pads;
    for (int i = 0; i < 10; ++i)
    {
        pads.push_back(system->attachVirtualGamepad());
        REQUIRE(pads.back() != nullptr);
    }
    REQUIRE(updateUntil(*system, [&] { return system->gamepads().size() == 10; }));
    std::vector<bool> seen(10, false);
    for (Gamepad* pad : system->gamepads())
    {
        REQUIRE(pad->playerIndex() < 10);
        CHECK_FALSE(seen[pad->playerIndex()]);
        seen[pad->playerIndex()] = true;
        CHECK(system->findByPlayer(pad->playerIndex()) == pad);
    }
}

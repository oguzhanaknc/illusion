// EditorGamepads over a real GamepadSystem with virtual gamepads: the gamepad.* commands read and drive
// the devices Play mode reads.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/editor/play/EditorGamepads.hpp>
#include <illusion/input/Gamepad.hpp>
#include <illusion/input/GamepadSystem.hpp>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

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

TEST_CASE("EditorGamepads lists a virtual DualSense and sends it rumble, light, trigger effects and motion")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    std::unique_ptr<GamepadSystem> system = GamepadSystem::create();
    REQUIRE(system != nullptr);
    {
        EditorGamepads gamepads(editor, *system);
        CHECK(editor.gamepads() == &gamepads);
        CHECK(ok(editor, "gamepad.list").root().find("gamepads").size() == 0);

        VirtualGamepadDesc desc;
        desc.name = "Virtual DualSense";
        desc.type = GamepadType::PS5;
        std::unique_ptr<VirtualGamepad> pad = system->attachVirtualGamepad(desc);
        REQUIRE(pad != nullptr);
        REQUIRE(updateUntil(*system, [&] { return system->find(pad->id()) != nullptr; }));
        pad->setButton(GamepadButton::East, true);
        pad->setAxis(GamepadAxis::LeftTrigger, 1.0f);
        REQUIRE(updateUntil(*system,
                            [&] { return system->find(pad->id())->state().isDown(GamepadButton::East); }));
        REQUIRE(updateUntil(
            *system, [&] { return system->find(pad->id())->state().axis(GamepadAxis::LeftTrigger) > 0.9f; }));

        const JsonDocument listed = ok(editor, "gamepad.list");
        REQUIRE(listed.root().find("gamepads").size() == 1);
        const JsonValue entry = listed.root().find("gamepads")[0];
        const u64 id = static_cast<u64>(pad->id());
        CHECK(entry.find("id").asU64() == std::optional<u64>(id));
        CHECK(entry.find("type").asString() == "PS5");
        CHECK(entry.find("buttons").size() == 1);
        CHECK(entry.find("buttons")[0].asString() == "East");
        CHECK(entry.find("axes")[4].find("value").asF64().value_or(0.0) > 0.9);
        CHECK(entry.find("fingers").size() == kGamepadFingers);

        ok(editor, "gamepad.rumble", R"({"low":0.5,"high":1,"durationMs":200})");
        CHECK(pad->outputs().lowFrequencyRumble == doctest::Approx(0.5).epsilon(0.01));
        CHECK(pad->outputs().highFrequencyRumble == doctest::Approx(1.0).epsilon(0.01));
        ok(editor, "gamepad.set_led", R"({"color":[0,0.5,1]})");
        CHECK(pad->outputs().led == std::array<u8, 3>{0, 128, 255});
        ok(editor, "gamepad.set_trigger_effect",
           R"({"trigger":"right","mode":"weapon","start":0.2,"end":0.6})");
        REQUIRE(pad->outputs().lastEffect.size() == 47);
        CHECK(pad->outputs().lastEffect[0] == 0x04);  // modify the right trigger
        CHECK(pad->outputs().lastEffect[10] == 0x02); // weapon mode in its block
        const JsonDocument motion = ok(editor, "gamepad.set_motion", R"({"enabled":true})");
        CHECK(motion.root().find("motion").asBool() == std::optional<bool>(true));
        CHECK(pad->outputs().motionSensorsEnabled);
        // An Xbox-only feature.
        CHECK(fails(editor, "gamepad.rumble", R"({"left":1})").kind == ErrorKind::Unsupported);
    }
    CHECK(editor.gamepads() == nullptr);
}

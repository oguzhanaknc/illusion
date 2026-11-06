#include "EditorCoreTestSupport.hpp"

#include <string>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

// The names of a JSON array of strings.
std::vector<std::string> strings(const JsonValue& array)
{
    std::vector<std::string> out;
    for (const JsonValue value : array.elements())
    {
        out.emplace_back(value.asString().value_or(""));
    }
    return out;
}

} // namespace

TEST_CASE("gamepad.list reports each gamepad's model, features, held buttons, axes, touchpad and motion")
{
    EditorFixture fixture;
    GamepadSnapshot& pad = fixture.gamepads.pads.front();
    pad.player = 1;
    pad.buttons[0] = true; // South
    pad.buttons[9] = true; // LeftShoulder
    pad.axes[1] = 0.5f;    // LeftY
    pad.axes[5] = 1.0f;    // RightTrigger
    pad.fingers[0] = {true, Vec2(0.25f, 0.75f), 1.0f};
    pad.gyroscope = Vec3(0.0f, 1.5f, 0.0f);

    const JsonDocument result = ok(*fixture.editor, "gamepad.list");
    REQUIRE(result.root().find("gamepads").size() == 1);
    const JsonValue entry = result.root().find("gamepads")[0];
    CHECK(entry.find("id").asU64() == std::optional<u64>(7));
    CHECK(entry.find("type").asString() == "PS5");
    CHECK(entry.find("player").asU64() == std::optional<u64>(1));
    CHECK(strings(entry.find("features")) ==
          std::vector<std::string>{"rumble", "led", "motion", "touchpad", "adaptive_triggers"});
    CHECK(strings(entry.find("buttons")) == std::vector<std::string>{"South", "LeftShoulder"});
    REQUIRE(entry.find("axes").size() == kGamepadAxes);
    CHECK(entry.find("axes")[1].find("name").asString() == "LeftY");
    CHECK(entry.find("axes")[1].find("value").asF64() == 0.5);
    CHECK(entry.find("axes")[5].find("name").asString() == "RightTrigger");
    REQUIRE(entry.find("fingers").size() == kGamepadFingers);
    CHECK(entry.find("fingers")[0].find("down").asBool() == std::optional<bool>(true));
    CHECK(entry.find("fingers")[0].find("position")[1].asF64() == 0.75);
    CHECK(entry.find("motion").asBool() == std::optional<bool>(false));
    CHECK(entry.find("gyroscope")[1].asF64() == 1.5);

    // Without a touchpad there are no fingers.
    fixture.gamepads.pads.front().features.touchpad = false;
    CHECK(ok(*fixture.editor, "gamepad.list").root().find("gamepads")[0].find("fingers").size() == 0);
}

TEST_CASE("the gamepad output commands drive the first or the named gamepad and report its state")
{
    EditorFixture fixture;
    test::FakeGamepads& gamepads = fixture.gamepads;
    GamepadSnapshot& second = gamepads.pads.emplace_back();
    second.id = 9;
    second.name = "Fake Xbox";
    second.type = "XboxOne";
    second.features = {.rumble = true, .triggerRumble = true};

    // Rumble: the first gamepad by default, half a second by default.
    JsonDocument result = ok(*fixture.editor, "gamepad.rumble", R"({"low":0.5,"high":0.25})");
    CHECK(result.root().find("id").asU64() == std::optional<u64>(7));
    CHECK(gamepads.rumbled.gamepad == 7);
    CHECK(gamepads.rumbled.low == 0.5f);
    CHECK(gamepads.rumbled.high == 0.25f);
    CHECK(gamepads.rumbled.durationMs == 500);
    ok(*fixture.editor, "gamepad.rumble", R"({"gamepad":9,"left":1,"durationMs":100})");
    CHECK(gamepads.rumbled.gamepad == 9);
    CHECK(gamepads.rumbled.left == 1.0f);
    CHECK(gamepads.rumbled.right == 0.0f);
    CHECK(gamepads.rumbled.durationMs == 100);

    // Light, trigger effect and motion sensors.
    ok(*fixture.editor, "gamepad.set_led", R"({"color":[1,0.5,0]})");
    CHECK(gamepads.led == std::array<u8, 3>{255, 128, 0});
    ok(*fixture.editor, "gamepad.set_trigger_effect",
       R"({"trigger":"right","mode":"weapon","start":0.25,"end":0.75,"strength":0.5})");
    CHECK(gamepads.effectSide == GamepadTriggerSide::Right);
    CHECK(gamepads.effect.mode == GamepadTriggerMode::Weapon);
    CHECK(gamepads.effect.start == 0.25f);
    CHECK(gamepads.effect.end == 0.75f);
    CHECK(gamepads.effect.strength == 0.5f);
    ok(*fixture.editor, "gamepad.set_trigger_effect", R"({"trigger":"left","mode":"vibration"})");
    CHECK(gamepads.effect.mode == GamepadTriggerMode::Vibration);
    CHECK(gamepads.effect.amplitude == 1.0f);
    CHECK(gamepads.effect.frequency == 30);
    result = ok(*fixture.editor, "gamepad.set_motion", R"({"enabled":true})");
    CHECK(result.root().find("motion").asBool() == std::optional<bool>(true));
    CHECK(gamepads.pads.front().motionEnabled);
}

TEST_CASE("the gamepad commands refuse missing gamepads, missing features and values out of range")
{
    EditorFixture fixture;
    EditorContext& editor = *fixture.editor;
    test::FakeGamepads& gamepads = fixture.gamepads;
    const auto kindOf = [&](std::string_view name, std::string_view params) {
        return fails(editor, name, params).kind;
    };

    CHECK(kindOf("gamepad.rumble", R"({"gamepad":3,"low":1})") == ErrorKind::NotFound);
    // The DualSense has no trigger motors; a gamepad without a light or adaptive triggers refuses them.
    CHECK(kindOf("gamepad.rumble", R"({"right":1})") == ErrorKind::Unsupported);
    gamepads.pads.front().features = {};
    CHECK(kindOf("gamepad.rumble", R"({"low":1})") == ErrorKind::Unsupported);
    CHECK(kindOf("gamepad.set_led", R"({"color":[1,1,1]})") == ErrorKind::Unsupported);
    CHECK(kindOf("gamepad.set_trigger_effect", R"({"trigger":"left","mode":"resistance"})") ==
          ErrorKind::Unsupported);
    CHECK(kindOf("gamepad.set_motion", R"({"enabled":true})") == ErrorKind::Unsupported);
    CHECK(gamepads.outputs == 0);

    CHECK(kindOf("gamepad.rumble", R"({})") == ErrorKind::ValidationFailed);
    CHECK(kindOf("gamepad.rumble", R"({"low":1.5})") == ErrorKind::ValidationFailed);
    CHECK(kindOf("gamepad.rumble", R"({"low":1,"durationMs":20000})") == ErrorKind::ValidationFailed);
    CHECK(kindOf("gamepad.set_led", R"({"color":[2,0,0]})") == ErrorKind::ValidationFailed);
    CHECK(
        kindOf("gamepad.set_trigger_effect", R"({"trigger":"left","mode":"weapon","start":0.6,"end":0.5})") ==
        ErrorKind::ValidationFailed);
    CHECK(kindOf("gamepad.set_trigger_effect", R"({"trigger":"left","mode":"vibration","frequency":0})") ==
          ErrorKind::ValidationFailed);
    CHECK(kindOf("gamepad.set_trigger_effect", R"({"trigger":"left","mode":"rumble"})") ==
          ErrorKind::ValidationFailed);
    CHECK(kindOf("gamepad.set_motion", R"({})") == ErrorKind::ValidationFailed);

    // Nothing connected, then no gamepad support at all.
    gamepads.pads.clear();
    CHECK(ok(editor, "gamepad.list").root().find("gamepads").size() == 0);
    CHECK(kindOf("gamepad.rumble", R"({"low":1})") == ErrorKind::NotFound);
    editor.setGamepads(nullptr);
    CHECK(kindOf("gamepad.list", "{}") == ErrorKind::Unsupported);
    CHECK(kindOf("gamepad.set_led", R"({"color":[1,1,1]})") == ErrorKind::Unsupported);
}

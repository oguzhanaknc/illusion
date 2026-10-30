#include "TestSupport.hpp"

#include <illusion/input/InputActions.hpp>
#include <illusion/input/InputActionsAsset.hpp>
#include <illusion/input/InputActionsService.hpp>
#include <illusion/platform/FileSystem.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace illusion;

namespace
{

InputActions sampleActions()
{
    InputActions actions;
    const ActionId jump = actions.addAction("Jump", ActionType::Button);
    actions.addBinding(jump, InputBinding::key(Key::Space));
    actions.addBinding(jump, InputBinding::gamepadButton(GamepadButton::South));
    const ActionId zoom = actions.addAction("Zoom", ActionType::Axis);
    actions.addBinding(zoom, InputBinding::gamepadAxis(GamepadAxis::RightY, -1.0f));
    const ActionId move = actions.addAction("Move", ActionType::Vector2);
    actions.addBinding(move, InputBinding::keyVector(Key::W, Key::S, Key::A, Key::D));
    actions.addBinding(move, InputBinding::gamepadStick(GamepadStick::Left));
    actions.addAction("Unbound", ActionType::Button);
    return actions;
}

std::string asset(std::string_view actions)
{
    return R"({"format": "illusion.input", "version": 1, "actions": )" + std::string(actions) + "}";
}

} // namespace

TEST_CASE("actions can be renamed, removed and cleared")
{
    InputActions actions = sampleActions();
    const ActionId zoom = *actions.findAction("Zoom");
    CHECK(actions.renameAction(zoom, "Zoom Camera"));
    CHECK(actions.actionName(zoom) == "Zoom Camera");
    CHECK(actions.renameAction(zoom, "Zoom Camera")); // its own name
    CHECK_FALSE(actions.renameAction(zoom, "Jump"));  // taken
    CHECK_FALSE(actions.renameAction(zoom, ""));
    CHECK_FALSE(actions.renameAction(zoom, "A = B"));
    CHECK_FALSE(actions.renameAction(zoom, " Padded"));
    CHECK(actions.actionName(zoom) == "Zoom Camera");

    actions.removeAction(*actions.findAction("Jump"));
    CHECK(actions.actionCount() == 3);
    CHECK_FALSE(actions.findAction("Jump"));
    REQUIRE(actions.findAction("Move"));
    const ActionId move = *actions.findAction("Move");
    CHECK(move == 1); // later ids moved down
    CHECK(actions.bindings(move).size() == 2);

    actions.clear();
    CHECK(actions.actionCount() == 0);
    CHECK_FALSE(actions.findAction("Move"));
    actions.addAction("Move", ActionType::Vector2); // the names are free again
}

TEST_CASE("removing or renaming an invalid action id asserts")
{
    test::ScopedAssertCatcher asserts;
    InputActions actions = sampleActions();
    CHECK_THROWS_AS(actions.removeAction(9), test::AssertionFailure);
    CHECK_THROWS_AS(static_cast<void>(actions.renameAction(9, "Other")), test::AssertionFailure);
}

TEST_CASE("action names reject the separators of the bindings text")
{
    CHECK(InputActions::isValidActionName("Jump"));
    CHECK(InputActions::isValidActionName("Move Left"));
    CHECK_FALSE(InputActions::isValidActionName(""));
    CHECK_FALSE(InputActions::isValidActionName("A,B"));
    CHECK_FALSE(InputActions::isValidActionName("#Jump"));
    CHECK_FALSE(InputActions::isValidActionName("Jump\n"));
    CHECK_FALSE(InputActions::isValidActionName("Jump "));

    InputActions actions;
    InputActionsService service(actions);
    InputService& api = service;
    CHECK_FALSE(api.addAction("Bad=Name", ActionType::Button)); // modules cannot reach the assert
    CHECK(actions.actionCount() == 0);
}

TEST_CASE("an input actions asset round-trips names, types and bindings")
{
    const InputActions original = sampleActions();
    const std::string text = writeInputActions(original);
    CHECK(text.back() == '\n');
    CHECK(text.find("\"Keys.W.S.A.D\"") != std::string::npos);

    InputActions read;
    read.addAction("Old", ActionType::Axis);
    REQUIRE(readInputActions(text, read).hasValue());
    REQUIRE(read.actionCount() == original.actionCount());
    CHECK_FALSE(read.findAction("Old")); // replaced, not merged
    for (ActionId id = 0; id < original.actionCount(); ++id)
    {
        CHECK(read.actionName(id) == original.actionName(id));
        CHECK(read.actionType(id) == original.actionType(id));
        REQUIRE(read.bindings(id).size() == original.bindings(id).size());
        for (usize i = 0; i < original.bindings(id).size(); ++i)
        {
            CHECK(read.bindings(id)[i] == original.bindings(id)[i]);
        }
    }
    CHECK(writeInputActions(read) == text); // the same actions give the same text
}

TEST_CASE("invalid input actions assets say where and leave the actions unchanged")
{
    InputActions actions = sampleActions();
    const auto fails = [&](const std::string& json, std::string_view expected) {
        const Expected<void, std::string> read = readInputActions(json, actions);
        REQUIRE_FALSE(read.hasValue());
        CHECK_MESSAGE(read.error().find(expected) != std::string::npos, read.error());
        CHECK(actions.actionCount() == 4);
    };
    fails("{", "not valid JSON");
    fails(R"({"format": "illusion.settings", "version": 1, "actions": []})", "not an input actions asset");
    fails(R"({"format": "illusion.input", "actions": []})", "\"version\"");
    fails(R"({"format": "illusion.input", "version": 2, "actions": []})", "newer");
    fails(R"({"format": "illusion.input", "version": 1, "actions": {}})", "'/actions' must be an array");
    fails(asset(R"([{"type": "Button"}])"), "'/actions/0/name'");
    fails(asset(R"([{"name": "A,B", "type": "Button"}])"), "'/actions/0/name'");
    fails(asset(R"([{"name": "A", "type": "Button"}, {"name": "A", "type": "Axis"}])"),
          "repeats the action 'A'");
    fails(asset(R"([{"name": "A", "type": "Trigger"}])"), "'/actions/0/type'");
    fails(asset(R"([{"name": "A", "type": "Button", "bindings": "Key.Space"}])"),
          "'/actions/0/bindings' must");
    fails(asset(R"([{"name": "A", "type": "Button", "bindings": ["Key.Space", "Key.Nope"]}])"),
          "'/actions/0/bindings/1'");

    REQUIRE(readInputActions(asset(R"([{"name": "A", "type": "Axis"}])"), actions).hasValue());
    CHECK(actions.actionCount() == 1);
    CHECK(actions.bindings(0).empty()); // bindings are optional
}

TEST_CASE("input actions assets load from the virtual file system")
{
    test::TempDirectory directory;
    VirtualFileSystem files;
    REQUIRE(files.mount("assets", directory.path, MountAccess::ReadWrite) == FileError::None);
    REQUIRE(files.writeText("assets:/game.input", writeInputActions(sampleActions())) == FileError::None);
    InputActions actions;
    REQUIRE(loadInputActions(files, "assets:/game.input", actions).hasValue());
    CHECK(actions.actionCount() == 4);

    const Expected<void, std::string> missing = loadInputActions(files, "assets:/none.input", actions);
    REQUIRE_FALSE(missing.hasValue());
    CHECK(missing.error().find("assets:/none.input: cannot read") == 0);
    REQUIRE(files.writeText("assets:/bad.input", "{}") == FileError::None);
    const Expected<void, std::string> bad = loadInputActions(files, "assets:/bad.input", actions);
    REQUIRE_FALSE(bad.hasValue());
    CHECK(bad.error().find("assets:/bad.input: is not") == 0);
    CHECK(actions.actionCount() == 4);
}

TEST_CASE("action types have stable names")
{
    CHECK(actionTypeName(ActionType::Vector2) == "Vector2");
    CHECK(actionTypeFromName("Axis") == ActionType::Axis);
    CHECK_FALSE(actionTypeFromName("axis"));
}

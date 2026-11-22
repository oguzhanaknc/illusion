// The ai.* commands (ADR 0062): the status of the agents of a play session, writing blackboard keys, noises
// that perceivers hear, checking `.behavior` files, and every invalid param.

#include "EditorCoreTestSupport.hpp"

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

constexpr std::string_view kGuardTree = R"({"format":"illusion.behavior","version":1,)"
                                        R"("blackboard":[{"name":"alert","type":"Bool"},)"
                                        R"({"name":"ammo","type":"Int","default":3},)"
                                        R"({"name":"speed","type":"Float"},)"
                                        R"({"name":"target","type":"Entity"},)"
                                        R"({"name":"home","type":"Vector3"}],)"
                                        R"("root":{"name":"idle","wait":{"seconds":60}}})";

void writeText(EditorContext& editor, std::string_view path, std::string_view text)
{
    JsonMutDocument doc;
    JsonMutValue params = doc.makeObject();
    params.add("path", doc.makeString(path));
    params.add("text", doc.makeString(text));
    doc.setRoot(params);
    std::string json;
    REQUIRE(doc.write(json, JsonWriteOptions{.pretty = false}));
    ok(editor, "project.file_write", json);
}

// A guard that perceives and runs kGuardTree, and a player; the play session started.
void startGame(EditorContext& editor)
{
    writeText(editor, "assets:/ai/guard.behavior", kGuardTree);
    ok(editor, "entity.create",
       R"({"name":"Guard","components":{"illusion.Transform":{},"illusion.AiPerception":{"hearing":1},)"
       R"("illusion.BehaviorAgent":{"tree":"assets:/ai/guard.behavior"}}})");
    ok(editor, "entity.create", R"({"name":"Player","components":{"illusion.Transform":{}}})");
    ok(editor, "play.start");
}

JsonValue findKey(const JsonDocument& status, std::string_view name)
{
    const JsonValue keys = status.root().pointer("/agents/0/blackboard");
    for (usize i = 0; i < keys.size(); ++i)
    {
        if (keys[i].find("name").asString() == name)
        {
            return keys[i];
        }
    }
    return {};
}

} // namespace

TEST_CASE("ai.status reports the tree, its running node, the blackboard and the counters")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    startGame(editor);
    ok(editor, "play.step", R"({"steps":2})");

    const JsonDocument status = ok(editor, "ai.status", "{}");
    CHECK(status.root().find("total").asU64() == 1);
    REQUIRE(status.root().find("agents").size() == 1);
    CHECK(status.root().pointer("/agents/0/entity/path").asString() == "/Guard");
    CHECK(status.root().pointer("/agents/0/tree").asString() == "assets:/ai/guard.behavior");
    CHECK(status.root().pointer("/agents/0/loaded").asBool() == true);
    CHECK(status.root().pointer("/agents/0/running").asString() == "idle");
    CHECK(status.root().pointer("/agents/0/ticks").asU64() >= 1);
    CHECK(status.root().find("pending").asU64() == 0);
    CHECK(status.root().find("failures").asU64() == 0);
    CHECK(status.root().pointer("/agents/0/blackboard").size() == 5);
    CHECK(findKey(status, "alert").find("value").asString() == "false");
    CHECK(findKey(status, "ammo").find("type").asString() == "Int");
    CHECK(findKey(status, "ammo").find("value").asString() == "3");
    CHECK(findKey(status, "target").find("set").asBool() == false);
    CHECK(findKey(status, "home").find("value").asString() == "");

    const JsonDocument one = ok(editor, "ai.status", R"({"entity":"/Guard","wait":false})");
    CHECK(one.root().find("agents").size() == 1);
}

TEST_CASE("ai.status waits for a tree that has not loaded and names a tree that fails")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "entity.create",
       R"({"name":"Lost","components":{"illusion.Transform":{},)"
       R"("illusion.BehaviorAgent":{"tree":"assets:/ai/missing.behavior"}}})");
    ok(editor, "play.start");
    const JsonDocument status = ok(editor, "ai.status", "{}");
    CHECK(status.root().pointer("/agents/0/loaded").asBool() == false);
    CHECK(status.root().pointer("/agents/0/running").asString() == "");
    CHECK(status.root().find("failures").asU64() == 1);
}

TEST_CASE("ai.status rejects a missing entity, an entity without AI and the edit World")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    CHECK(fails(editor, "ai.status", "{}").kind == ErrorKind::PreconditionFailed);
    startGame(editor);
    CHECK(fails(editor, "ai.status", R"({"entity":"/Nobody"})").kind == ErrorKind::NotFound);
    const CommandError none = fails(editor, "ai.status", R"({"entity":"/Player"})");
    CHECK(none.kind == ErrorKind::ValidationFailed);
    REQUIRE_FALSE(none.errors.empty());
    CHECK(none.errors[0].pointer == "/entity");
    CHECK(fails(editor, "ai.status", R"({"world":"edit"})").kind == ErrorKind::PreconditionFailed);
}

TEST_CASE("ai.setValue writes every type of key and the status shows it")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    startGame(editor);

    const JsonDocument alert = ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"alert","value":true})");
    CHECK(alert.root().pointer("/key/value").asString() == "true");
    ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"ammo","value":7})");
    ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"speed","value":1.5})");
    ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"home","value":[1,0,-2]})");
    ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"target","value":"/Player"})");

    const JsonDocument status = ok(editor, "ai.status", R"({"entity":"/Guard"})");
    CHECK(findKey(status, "alert").find("value").asString() == "true");
    CHECK(findKey(status, "ammo").find("value").asString() == "7");
    CHECK(findKey(status, "speed").find("value").asString() == "1.5");
    CHECK(findKey(status, "home").find("value").asString() == "[1, 0, -2]");
    CHECK(findKey(status, "target").find("value").asString() == "/Player");
    CHECK(findKey(status, "target").find("set").asBool() == true);

    ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"target","value":null})");
    ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"home","value":null})");
    const JsonDocument cleared = ok(editor, "ai.status", R"({"entity":"/Guard"})");
    CHECK(findKey(cleared, "target").find("set").asBool() == false);
    CHECK(findKey(cleared, "home").find("set").asBool() == false);
}

TEST_CASE("ai.setValue as a dry run changes nothing")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    startGame(editor);
    const JsonDocument dry =
        ok(editor, "ai.setValue", R"({"entity":"/Guard","key":"ammo","value":9,"dryRun":true})");
    CHECK(dry.root().pointer("/key/value").asString() == "9");
    CHECK(findKey(ok(editor, "ai.status", R"({"entity":"/Guard"})"), "ammo").find("value").asString() == "3");
}

TEST_CASE("ai.setValue refuses what does not fit the key")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    CHECK(fails(editor, "ai.setValue", R"({"entity":"/Guard","key":"alert","value":true})").kind ==
          ErrorKind::PreconditionFailed);
    startGame(editor);

    const auto pointerOf = [&](std::string_view params) {
        const CommandError error = fails(editor, "ai.setValue", params);
        CHECK(error.kind == ErrorKind::ValidationFailed);
        return error.errors.empty() ? std::string() : error.errors[0].pointer;
    };
    CHECK(pointerOf(R"({"entity":"/Player","key":"alert","value":true})") == "/entity");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"alrt","value":true})") == "/key");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"alert","value":1})") == "/value");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"alert","value":null})") == "/value");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"ammo","value":1.5})") == "/value");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"ammo","value":4294967296})") == "/value");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"speed","value":"fast"})") == "/value");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"home","value":[1,2]})") == "/value");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"home","value":[1,2,"x"]})") == "/value/2");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"home","value":[1,2,1e9]})") == "/value/2");
    CHECK(pointerOf(R"({"entity":"/Guard","key":"target","value":5})") == "/value");
    CHECK(fails(editor, "ai.setValue", R"({"entity":"/Guard","key":"target","value":"/Nobody"})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("ai.noise is heard by a perceiver at the next step, not by the one that made it")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    startGame(editor);
    ok(editor, "play.step", "{}");

    const JsonDocument reported =
        ok(editor, "ai.noise", R"({"position":[3,0,0],"radius":6,"instigator":"/Player"})");
    CHECK(reported.root().find("listeners").asU64() == 1);
    CHECK(reported.root().find("team").asU64() == 0);
    ok(editor, "play.step", "{}");
    const JsonDocument status = ok(editor, "ai.status", R"({"entity":"/Guard"})");
    REQUIRE(status.root().pointer("/agents/0/perceived").size() == 1);
    CHECK(status.root().pointer("/agents/0/perceived/0/entity/path").asString() == "/Player");
    CHECK(status.root().pointer("/agents/0/perceived/0/sense").asString() == "Hearing");
    CHECK(status.root().pointer("/agents/0/perceived/0/current").asBool() == true);

    // Out of reach, from the guard itself, and in a team it does not notice: nobody listens.
    CHECK(ok(editor, "ai.noise", R"({"position":[50,0,0],"radius":6})").root().find("listeners").asU64() ==
          0);
    CHECK(ok(editor, "ai.noise", R"({"position":[0,0,0],"instigator":"/Guard"})")
              .root()
              .find("listeners")
              .asU64() == 0);
    ok(editor, "component.set_field",
       R"({"entity":"/Guard","type":"illusion.AiPerception","path":"detectTeams","value":1})");
    CHECK(ok(editor, "ai.noise", R"({"position":[0,0,0],"team":3})").root().find("listeners").asU64() == 0);
}

TEST_CASE("ai.noise as a dry run reports nothing and rejects invalid params")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    CHECK(fails(editor, "ai.noise", R"({"position":[0,0,0]})").kind == ErrorKind::PreconditionFailed);
    startGame(editor);
    ok(editor, "ai.noise", R"({"position":[1,0,0],"dryRun":true})");
    ok(editor, "play.step", "{}");
    CHECK(ok(editor, "ai.status", "{}").root().pointer("/agents/0/perceived").size() == 0);

    const auto pointerOf = [&](std::string_view params) {
        const CommandError error = fails(editor, "ai.noise", params);
        CHECK(error.kind == ErrorKind::ValidationFailed);
        return error.errors.empty() ? std::string() : error.errors[0].pointer;
    };
    CHECK(pointerOf(R"({"position":[1e9,0,0]})") == "/position");
    CHECK(pointerOf(R"({"position":[0,0,0],"radius":-1})") == "/radius");
    CHECK(pointerOf(R"({"position":[0,0,0],"radius":2e6})") == "/radius");
    CHECK(pointerOf(R"({"position":[0,0,0],"team":32})") == "/team");
    CHECK(fails(editor, "ai.noise", R"({"position":[0,0,0],"instigator":"/Nobody"})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("ai.noise refuses more noises than an update keeps")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    startGame(editor);
    ErrorKind last = ErrorKind::Internal;
    for (u32 i = 0; i < 300; ++i)
    {
        Expected<std::string, CommandError> result = run(editor, "ai.noise", R"({"position":[1,0,0]})");
        if (!result)
        {
            last = result.error().kind;
            break;
        }
    }
    CHECK(last == ErrorKind::PreconditionFailed);
}

TEST_CASE("ai.validate counts a valid tree and names the JSON path of the first error")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    writeText(editor, "assets:/ai/guard.behavior", kGuardTree);
    const JsonDocument valid = ok(editor, "ai.validate", R"({"path":"assets:/ai/guard.behavior"})");
    CHECK(valid.root().find("valid").asBool() == true);
    CHECK(valid.root().find("nodes").asU64() == 1);
    CHECK(valid.root().find("keys").asU64() == 5);
    CHECK(valid.root().find("error").asString() == "");

    writeText(editor, "assets:/ai/typo.behavior",
              R"({"format":"illusion.behavior","version":1,"root":{"selector":[{"guard":{"key":"targt",)"
              R"("compare":"IsSet","child":{"stop":{}}}}]}})");
    const JsonDocument typo = ok(editor, "ai.validate", R"({"path":"assets:/ai/typo.behavior"})");
    CHECK(typo.root().find("valid").asBool() == false);
    CHECK(typo.root().find("errorPath").asString() == "root.selector[0].guard.key");
    CHECK(std::string(typo.root().find("error").asString().value_or("")).find("targt") != std::string::npos);
    CHECK(typo.root().find("nodes").asU64() == 0);

    writeText(editor, "assets:/ai/broken.behavior", "{\"format\": ");
    const JsonDocument broken = ok(editor, "ai.validate", R"({"path":"assets:/ai/broken.behavior"})");
    CHECK(broken.root().find("valid").asBool() == false);
    CHECK(std::string(broken.root().find("error").asString().value_or("")).starts_with("invalid JSON"));
    CHECK(broken.root().find("errorPath").asString() == "");
}

TEST_CASE("ai.validate rejects other paths and files that do not exist")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    for (const char* path : {"assets:/ai/guard.txt", "ai/guard.behavior", "assets:/.behavior",
                             "settings:/guard.behavior", "assets:/../guard.behavior"})
    {
        INFO(path);
        const CommandError error = fails(editor, "ai.validate", std::string(R"({"path":")") + path + R"("})");
        CHECK(error.kind == ErrorKind::ValidationFailed);
    }
    CHECK(fails(editor, "ai.validate", R"({"path":"assets:/ai/none.behavior"})").kind == ErrorKind::NotFound);
}

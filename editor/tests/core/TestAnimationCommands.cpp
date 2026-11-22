// The animation commands (ADR 0059-0061): the skins and clips of a model, the status of the Animators in the
// edit and the play World, driving a graph of the play session, scrubbing an Animator in the edit World with
// undo, checking `.animgraph` and `.ragdoll` files, and every invalid param.

#include "EditorCoreTestSupport.hpp"

#include <illusion/animation/AnimationWorld.hpp>
#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/JobSystem.hpp>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

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

// The showcase column with its graph, an Animator without a graph and an Entity without an Animator.
void buildScene(EditorContext& editor)
{
    installAnimationAssets(editor);
    ok(editor, "entity.create",
       R"({"name":"Column","components":{"illusion.Transform":{},"illusion.Animator":{)"
       R"("skeleton":"assets:/models/column.gltf#skin/0","clip":"assets:/models/column.gltf#animation/sway",)"
       R"("graph":"assets:/animations/column.animgraph","speed":0.8,"time":0.4,"loop":false}}})");
    ok(editor, "entity.create",
       R"({"name":"Bare","components":{"illusion.Transform":{},"illusion.Animator":{)"
       R"("skeleton":"assets:/models/column.gltf#skin/0","clip":"assets:/models/column.gltf#animation/sway"}}})");
    ok(editor, "entity.create", R"({"name":"Plain","components":{"illusion.Transform":{}}})");
}

JsonValue findNamed(const JsonValue& list, std::string_view name)
{
    for (usize i = 0; i < list.size(); ++i)
    {
        if (list[i].find("name").asString() == name)
        {
            return list[i];
        }
    }
    return {};
}

} // namespace

TEST_CASE("animation.list reports the skins and clips of a model")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    installAnimationAssets(editor);
    const JsonDocument list = ok(editor, "animation.list", R"({"path":"assets:/models/column.gltf"})");
    CHECK(list.root().find("path").asString() == "assets:/models/column.gltf");
    REQUIRE(list.root().find("skins").size() == 1);
    CHECK(list.root().pointer("/skins/0/ref").asString() == "assets:/models/column.gltf#skin/0");
    CHECK(list.root().pointer("/skins/0/name").asString() == "column");
    CHECK(list.root().pointer("/skins/0/joints").asU64() == 2);
    REQUIRE(list.root().find("clips").size() == 2);
    const JsonValue sway = findNamed(list.root().find("clips"), "sway");
    CHECK(sway.find("ref").asString() == "assets:/models/column.gltf#animation/sway");
    CHECK(sway.find("duration").asF64().value_or(0.0) > 0.0);
    CHECK(sway.find("channels").asU64() == 1);
    CHECK(findNamed(list.root().find("clips"), "lean").find("channels").asU64() == 1);
}

TEST_CASE("animation.list rejects bad paths, a missing model and a file that is no model")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    writeText(editor, "assets:/models/broken.gltf", "this is not glTF");
    for (const char* path : {"", "models/column.gltf", "assets:/models/column.png", "assets:/models/.gltf",
                             "assets:/models/column.gltf#skin/0", "game:/models/column.gltf"})
    {
        CAPTURE(std::string(path));
        const CommandError error =
            fails(editor, "animation.list", std::string(R"({"path":")") + path + R"("})");
        CHECK(error.kind == ErrorKind::ValidationFailed);
        REQUIRE_FALSE(error.errors.empty());
        CHECK(error.errors[0].pointer == "/path");
    }
    CHECK(fails(editor, "animation.list", R"({"path":"assets:/models/none.glb"})").kind ==
          ErrorKind::NotFound);
    CHECK(fails(editor, "animation.list", R"({"path":"assets:/models/broken.gltf"})").kind ==
          ErrorKind::IoError);
}

TEST_CASE("animation.status in the edit World shows the saved fields and says it has no runtime")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    const JsonDocument status = ok(editor, "animation.status", "{}");
    CHECK(status.root().find("world").asString() == "edit");
    CHECK(status.root().find("live").asBool() == false);
    CHECK_FALSE(status.root().find("note").asString().value_or("").empty());
    CHECK(status.root().find("total").asU64() == 2);
    REQUIRE(status.root().find("animators").size() == 2);
    const JsonValue column = status.root().pointer("/animators/0");
    CHECK(column.pointer("/entity/path").asString() == "/Column");
    CHECK(column.find("skeleton").asString() == "assets:/models/column.gltf#skin/0");
    CHECK(column.find("clip").asString() == "assets:/models/column.gltf#animation/sway");
    CHECK(column.find("graph").asString() == "assets:/animations/column.animgraph");
    CHECK(column.find("speed").asF64().value_or(0.0) == doctest::Approx(0.8));
    CHECK(column.find("time").asF64().value_or(0.0) == doctest::Approx(0.4));
    CHECK(column.find("loop").asBool() == false);
    CHECK(column.find("posed").asBool() == false);
    CHECK(column.find("layers").size() == 0);
    CHECK(status.root().find("events").size() == 0);

    const JsonDocument one = ok(editor, "animation.status", R"({"entity":"/Bare"})");
    REQUIRE(one.root().find("animators").size() == 1);
    CHECK(one.root().pointer("/animators/0/graph").asString() == "");
    CHECK(one.root().find("total").asU64() == 2);
}

TEST_CASE("animation.status in the edit World reports the poses a viewport gives it")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    JobSystem jobs;
    AssetStreamer streamer(editor.files(), jobs, AssetStreamerDesc{.cacheMount = ""});
    AnimationWorld poses(editor.world(), streamer);
    f.viewports.animation = &poses;
    poses.waitIdle();
    for (int i = 0; i < 4; ++i)
    {
        poses.update(0.0f, nullptr, false);
    }

    const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(status.root().find("world").asString() == "edit");
    CHECK(status.root().find("live").asBool() == true);
    CHECK(status.root().find("note").asString() == "");
    const JsonValue column = status.root().pointer("/animators/0");
    CHECK(column.find("posed").asBool() == true);
    CHECK(column.find("joints").asU64() == 2);
    CHECK(column.find("time").asF64().value_or(0.0) == doctest::Approx(0.4));
    CHECK(column.find("graphLoaded").asBool() == true);
    CHECK(column.find("layers").size() == 1);

    f.viewports.animation = nullptr;
    const JsonDocument without = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(without.root().find("live").asBool() == false);
    CHECK(without.root().pointer("/animators/0/posed").asBool() == false);
}

TEST_CASE("animation.status in a play session reports the layers, parameters and states of the graph")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    ok(editor, "play.start");
    ok(editor, "play.step", R"({"steps":2})");

    const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(status.root().find("world").asString() == "play");
    CHECK(status.root().find("live").asBool() == true);
    CHECK(status.root().find("note").asString() == "");
    CHECK(status.root().find("pending").asU64() == 0);
    CHECK(status.root().find("failures").asU64() == 0);
    const JsonValue column = status.root().pointer("/animators/0");
    CHECK(column.find("graphLoaded").asBool() == true);
    REQUIRE(column.find("layers").size() == 1);
    CHECK(column.pointer("/layers/0/name").asString() == "base");
    CHECK(column.pointer("/layers/0/state").asString() == "hold");
    CHECK(column.pointer("/layers/0/previous").asString() == "");
    CHECK(column.pointer("/layers/0/fade").asF64().value_or(1.0) == doctest::Approx(0.0));
    REQUIRE(column.find("parameters").size() == 4);
    const JsonValue lean = findNamed(column.find("parameters"), "lean");
    CHECK(lean.find("type").asString() == "Float");
    CHECK(lean.find("value").asF64().value_or(0.0) == doctest::Approx(0.5));
    CHECK(findNamed(column.find("parameters"), "calm").find("type").asString() == "Bool");
    CHECK(findNamed(column.find("parameters"), "mood").find("type").asString() == "Int");
    CHECK(findNamed(column.find("parameters"), "bow").find("type").asString() == "Trigger");

    const JsonDocument bare = ok(editor, "animation.status", R"({"entity":"/Bare"})");
    CHECK(bare.root().pointer("/animators/0/graphLoaded").asBool() == false);

    const JsonDocument edit = ok(editor, "animation.status", R"({"world":"edit"})");
    CHECK(edit.root().find("world").asString() == "edit");
    CHECK(edit.root().find("live").asBool() == false);
}

TEST_CASE("animation.status rejects a missing entity and an entity without an Animator")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    CHECK(fails(editor, "animation.status", R"({"entity":"/Nobody"})").kind == ErrorKind::NotFound);
    const CommandError none = fails(editor, "animation.status", R"({"entity":"/Plain"})");
    CHECK(none.kind == ErrorKind::ValidationFailed);
    REQUIRE_FALSE(none.errors.empty());
    CHECK(none.errors[0].pointer == "/entity");
}

TEST_CASE("animation.set_parameter writes every type of parameter and the status shows it after a step")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    ok(editor, "play.start");
    ok(editor, "play.step");

    const JsonDocument lean =
        ok(editor, "animation.set_parameter", R"({"entity":"/Column","name":"lean","value":0.9})");
    CHECK(lean.root().pointer("/entity/path").asString() == "/Column");
    CHECK(lean.root().pointer("/parameter/type").asString() == "Float");
    CHECK(lean.root().pointer("/parameter/value").asF64().value_or(0.0) == doctest::Approx(0.9));
    ok(editor, "animation.set_parameter", R"({"entity":"/Column","name":"calm","value":true})");
    const JsonDocument mood =
        ok(editor, "animation.set_parameter", R"({"entity":"/Column","name":"mood","value":3})");
    CHECK(mood.root().pointer("/parameter/type").asString() == "Int");
    ok(editor, "play.step");

    const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
    const JsonValue parameters = status.root().pointer("/animators/0/parameters");
    CHECK(findNamed(parameters, "lean").find("value").asF64().value_or(0.0) == doctest::Approx(0.9));
    CHECK(findNamed(parameters, "calm").find("value").asF64().value_or(0.0) == doctest::Approx(1.0));
    CHECK(findNamed(parameters, "mood").find("value").asF64().value_or(0.0) == doctest::Approx(3.0));
}

TEST_CASE("animation.set_parameter changes nothing in a dry run")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    ok(editor, "play.start");
    ok(editor, "play.step");
    const JsonDocument dry = ok(editor, "animation.set_parameter",
                                R"({"entity":"/Column","name":"lean","value":0.9,"dryRun":true})");
    CHECK(dry.root().pointer("/parameter/value").asF64().value_or(0.0) == doctest::Approx(0.9));
    ok(editor, "play.step");
    const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(findNamed(status.root().pointer("/animators/0/parameters"), "lean")
              .find("value")
              .asF64()
              .value_or(0.0) == doctest::Approx(0.5));
}

TEST_CASE("animation.set_parameter rejects bad values, names and entities")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    CHECK(fails(editor, "animation.set_parameter", R"({"entity":"/Column","name":"lean","value":1})").kind ==
          ErrorKind::PreconditionFailed);
    ok(editor, "play.start");
    ok(editor, "play.step");
    CHECK(fails(editor, "animation.set_parameter",
                R"({"entity":"/Column","name":"lean","value":1,"world":"edit"})")
              .kind == ErrorKind::PreconditionFailed);

    const auto invalid = [&](std::string_view params, std::string_view pointer) {
        CAPTURE(params);
        const CommandError error = fails(editor, "animation.set_parameter", params);
        CHECK(error.kind == ErrorKind::ValidationFailed);
        REQUIRE_FALSE(error.errors.empty());
        CHECK(error.errors[0].pointer == pointer);
    };
    invalid(R"({"entity":"/Column","name":"lean","value":"fast"})", "/value");
    invalid(R"({"entity":"/Column","name":"lean","value":null})", "/value");
    invalid(R"({"entity":"/Column","name":"calm","value":1})", "/value");
    invalid(R"({"entity":"/Column","name":"mood","value":1.5})", "/value");
    invalid(R"({"entity":"/Column","name":"mood","value":99999999})", "/value");
    invalid(R"({"entity":"/Column","name":"bow","value":1})", "/name");
    invalid(R"({"entity":"/Plain","name":"lean","value":1})", "/entity");
    invalid(R"({"entity":"/Bare","name":"lean","value":1})", "/entity");

    const CommandError unknown =
        fails(editor, "animation.set_parameter", R"({"entity":"/Column","name":"lan","value":1})");
    CHECK(unknown.kind == ErrorKind::NotFound);
    REQUIRE_FALSE(unknown.suggestions.empty());
    CHECK(unknown.suggestions[0] == "lean");
    CHECK(fails(editor, "animation.set_parameter", R"({"entity":"/Nobody","name":"lean","value":1})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("animation.trigger fires a transition and rejects what is no Trigger")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    CHECK(fails(editor, "animation.trigger", R"({"entity":"/Column","name":"bow"})").kind ==
          ErrorKind::PreconditionFailed);
    ok(editor, "play.start");
    ok(editor, "play.step");

    const JsonDocument trigger = ok(editor, "animation.trigger", R"({"entity":"/Column","name":"bow"})");
    CHECK(trigger.root().pointer("/entity/path").asString() == "/Column");
    CHECK(trigger.root().find("name").asString() == "bow");
    ok(editor, "play.step");
    const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(status.root().pointer("/animators/0/layers/0/state").asString() == "swing");
    CHECK(status.root().pointer("/animators/0/layers/0/previous").asString() == "hold");
    ok(editor, "play.step", R"({"steps":3})");
    const JsonDocument fading = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(fading.root().pointer("/animators/0/layers/0/fade").asF64().value_or(0.0) > 0.0);

    CHECK(fails(editor, "animation.trigger", R"({"entity":"/Column","name":"lean"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "animation.trigger", R"({"entity":"/Column","name":"bowl"})").kind ==
          ErrorKind::NotFound);
    CHECK(fails(editor, "animation.trigger", R"({"entity":"/Plain","name":"bow"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "animation.trigger", R"({"entity":"/Bare","name":"bow"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "animation.trigger", R"({"entity":"/Nobody","name":"bow"})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("animation.trigger changes nothing in a dry run")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    ok(editor, "play.start");
    ok(editor, "play.step");
    ok(editor, "animation.trigger", R"({"entity":"/Column","name":"bow","dryRun":true})");
    ok(editor, "play.step");
    const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(status.root().pointer("/animators/0/layers/0/state").asString() == "hold");
}

TEST_CASE("animation.scrub sets Animator time in the edit World and undoes")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    const JsonDocument scrub = ok(editor, "animation.scrub", R"({"entity":"/Column","time":0.6})");
    CHECK(scrub.root().pointer("/entity/path").asString() == "/Column");
    CHECK(scrub.root().find("previous").asF64().value_or(0.0) == doctest::Approx(0.4));
    CHECK(scrub.root().find("time").asF64().value_or(0.0) == doctest::Approx(0.6));
    CHECK(scrub.root().find("changed").asBool() == true);
    const auto timeOf = [&] {
        const JsonDocument status = ok(editor, "animation.status", R"({"entity":"/Column"})");
        return status.root().pointer("/animators/0/time").asF64().value_or(-1.0);
    };
    CHECK(timeOf() == doctest::Approx(0.6));

    CHECK(
        ok(editor, "animation.scrub", R"({"entity":"/Column","time":0.6})").root().find("changed").asBool() ==
        false);

    ok(editor, "history.undo");
    CHECK(timeOf() == doctest::Approx(0.4));
    ok(editor, "history.redo");
    CHECK(timeOf() == doctest::Approx(0.6));
}

TEST_CASE("animation.scrub acts on the edit World while the game plays")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    ok(editor, "play.start");
    ok(editor, "animation.scrub", R"({"entity":"/Column","time":1.5})");
    const JsonDocument edit = ok(editor, "animation.status", R"({"entity":"/Column","world":"edit"})");
    CHECK(edit.root().pointer("/animators/0/time").asF64().value_or(0.0) == doctest::Approx(1.5));
    const JsonDocument play = ok(editor, "animation.status", R"({"entity":"/Column"})");
    CHECK(play.root().pointer("/animators/0/time").asF64().value_or(0.0) != doctest::Approx(1.5));
}

TEST_CASE("animation.scrub rejects bad times and entities")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    buildScene(editor);
    for (const char* time : {"-0.5", "1000001", "1e30"})
    {
        CAPTURE(time);
        const CommandError error =
            fails(editor, "animation.scrub", std::string(R"({"entity":"/Column","time":)") + time + "}");
        CHECK(error.kind == ErrorKind::ValidationFailed);
    }
    const CommandError none = fails(editor, "animation.scrub", R"({"entity":"/Plain","time":1})");
    CHECK(none.kind == ErrorKind::ValidationFailed);
    REQUIRE_FALSE(none.errors.empty());
    CHECK(none.errors[0].pointer == "/entity");
    CHECK(fails(editor, "animation.scrub", R"({"entity":"/Nobody","time":1})").kind == ErrorKind::NotFound);
}

TEST_CASE("animation.validate checks graphs and ragdoll profiles")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    installAnimationAssets(editor);
    const JsonDocument graph =
        ok(editor, "animation.validate", R"({"path":"assets:/animations/column.animgraph"})");
    CHECK(graph.root().find("kind").asString() == "AnimationGraph");
    CHECK(graph.root().find("valid").asBool() == true);
    CHECK(graph.root().find("error").asString() == "");
    CHECK(graph.root().find("parameters").asU64() == 4);
    CHECK(graph.root().find("layers").asU64() == 1);
    CHECK(graph.root().find("states").asU64() == 2);
    CHECK(graph.root().find("parts").asU64() == 0);

    const JsonDocument ragdoll =
        ok(editor, "animation.validate", R"({"path":"assets:/animations/strider.ragdoll"})");
    CHECK(ragdoll.root().find("kind").asString() == "RagdollProfile");
    CHECK(ragdoll.root().find("valid").asBool() == true);
    CHECK(ragdoll.root().find("parts").asU64() > 0);
    CHECK(ragdoll.root().find("layers").asU64() == 0);
}

TEST_CASE("animation.validate names the JSON path of the first error in a graph or a profile")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    writeText(
        editor, "assets:/animations/bad.animgraph",
        R"({"format":"illusion.animgraph","version":1,"parameters":[],"layers":[{"name":"base",)"
        R"("entry":"idle","states":[{"name":"other","clip":"assets:/models/column.gltf#animation/sway"}],)"
        R"("transitions":[]}]})");
    const JsonDocument graph =
        ok(editor, "animation.validate", R"({"path":"assets:/animations/bad.animgraph"})");
    CHECK(graph.root().find("valid").asBool() == false);
    CHECK_FALSE(graph.root().find("error").asString().value_or("").empty());
    CHECK(graph.root().find("errorPath").asString().value_or("").starts_with("layers[0]"));
    CHECK(graph.root().find("layers").asU64() == 0);

    writeText(editor, "assets:/animations/bad.ragdoll",
              R"({"format":"illusion.ragdoll","version":1,"parts":[]})");
    const JsonDocument ragdoll =
        ok(editor, "animation.validate", R"({"path":"assets:/animations/bad.ragdoll"})");
    CHECK(ragdoll.root().find("kind").asString() == "RagdollProfile");
    CHECK(ragdoll.root().find("valid").asBool() == false);
    CHECK_FALSE(ragdoll.root().find("error").asString().value_or("").empty());

    writeText(editor, "assets:/animations/garbage.animgraph", "not json at all");
    const JsonDocument garbage =
        ok(editor, "animation.validate", R"({"path":"assets:/animations/garbage.animgraph"})");
    CHECK(garbage.root().find("valid").asBool() == false);
    CHECK_FALSE(garbage.root().find("error").asString().value_or("").empty());
}

TEST_CASE("animation.validate rejects bad paths and a missing file")
{
    EditorFixture f;
    EditorContext& editor = *f.editor;
    for (const char* path : {"", "animations/hero.animgraph", "assets:/animations/hero.json",
                             "assets:/animations/.animgraph", "game:/animations/hero.animgraph"})
    {
        CAPTURE(std::string(path));
        const CommandError error =
            fails(editor, "animation.validate", std::string(R"({"path":")") + path + R"("})");
        CHECK(error.kind == ErrorKind::ValidationFailed);
        REQUIRE_FALSE(error.errors.empty());
        CHECK(error.errors[0].pointer == "/path");
    }
    CHECK(fails(editor, "animation.validate", R"({"path":"assets:/animations/none.animgraph"})").kind ==
          ErrorKind::NotFound);
    CHECK(fails(editor, "animation.validate", R"({"path":"assets:/animations/none.ragdoll"})").kind ==
          ErrorKind::NotFound);
}

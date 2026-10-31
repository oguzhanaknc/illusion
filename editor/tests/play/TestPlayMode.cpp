// Play mode (ADR 0042): play sessions on a copy of the edit World, command routing, keep_changes, edit
// hooks, input and the module commands, through the editor's PlaySession.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/runtime/GameSession.hpp>

#include <fstream>
#include <sstream>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

// A project with an open editor and its game runtime.
struct PlayFixture
{
    PlayFixture()
    {
        reflectPhysicsComponents();
        reflectAudioComponents();
        play = std::make_unique<PlaySession>(
            *base.editor, PlaySessionDesc{.jobs = &jobs, .audioOutput = AudioOutput::Offline});
    }

    EditorContext& editor() { return *base.editor; }

    // A value of the play World read through entity.get.
    f64 playNumber(std::string_view entity, std::string_view pointer)
    {
        const JsonDocument got =
            ok(editor(), "component.get",
               R"({"entity":")" + std::string(entity) + R"(","type":"illusion.Transform"})");
        return got.root().pointer(pointer).asF64().value_or(-1000.0);
    }

    // A falling sphere 10 m up.
    void addBall()
    {
        ok(editor(), "entity.create",
           R"({"name":"Ball","components":{"illusion.Transform":{"position":[0,10,0]},)"
           R"("illusion.RigidBody":{"motionType":"Dynamic"},"illusion.Collider":{"shape":"Sphere","radius":0.5}}})");
    }

    EditorFixture base;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    std::unique_ptr<PlaySession> play;
};

} // namespace

TEST_CASE("stopping a play session leaves the edit scene exactly as it was")
{
    PlayFixture f;
    f.base.buildScene();
    const std::string before = f.base.scene();
    const usize steps = f.editor().history().steps().size();
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    CHECK(f.editor().playing());
    ok(f.editor(), "transform.translate", R"({"entities":["/Game"],"offset":[3,0,0]})");
    ok(f.editor(), "entity.create", R"({"name":"Spawned"})");
    ok(f.editor(), "entity.destroy", R"({"entities":["/Light"]})");
    ok(f.editor(), "play.step", R"({"steps":5})");
    ok(f.editor(), "play.stop");
    CHECK_FALSE(f.editor().playing());
    CHECK(f.base.scene() == before);
    CHECK(f.editor().history().steps().size() == steps);
    CHECK(ok(f.editor(), "play.status").root().find("state").asString() == "stopped");
}

TEST_CASE("while playing, entity commands act on the play World unless they ask for the edit World")
{
    PlayFixture f;
    f.base.buildScene();
    ok(f.editor(), "play.start");
    ok(f.editor(), "entity.rename", R"({"entity":"/Light","name":"Lamp"})");
    CHECK(ok(f.editor(), "entity.get", R"({"entity":"/Lamp"})").root().pointer("/entity/name").asString() ==
          "Lamp");
    CHECK(fails(f.editor(), "entity.get", R"({"entity":"/Lamp","world":"edit"})").kind ==
          ErrorKind::NotFound);
    ok(f.editor(), "entity.get", R"({"entity":"/Light","world":"edit"})");

    // Edit-World edits while playing are undoable as usual; play edits are not.
    ok(f.editor(), "entity.rename", R"({"entity":"/Game","name":"Root","world":"edit"})");
    CHECK(f.editor().history().steps().size() == 1);
    ok(f.editor(), "entity.get", R"({"entity":"/Game"})");
    ok(f.editor(), "play.stop");
    ok(f.editor(), "entity.get", R"({"entity":"/Root"})");
    ok(f.editor(), "history.undo");
    ok(f.editor(), "entity.get", R"({"entity":"/Game"})");
}

TEST_CASE("play commands check the session state and the world option")
{
    PlayFixture f;
    CHECK(fails(f.editor(), "play.stop", "{}").kind == ErrorKind::WrongMode);
    CHECK(fails(f.editor(), "play.step", "{}").kind == ErrorKind::WrongMode);
    CHECK(fails(f.editor(), "input.read", "{}").kind == ErrorKind::WrongMode);
    CHECK(fails(f.editor(), "entity.create", R"({"world":"play"})").kind == ErrorKind::WrongMode);
    CHECK(fails(f.editor(), "entity.create", R"({"world":"moon"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "play.start", R"({"mode":"fly"})").kind == ErrorKind::ValidationFailed);
    CHECK(ok(f.editor(), "play.start", R"({"dryRun":true})").root().find("state").asString() == "stopped");
    CHECK_FALSE(f.editor().playing());
    ok(f.editor(), "play.start");
    CHECK(fails(f.editor(), "play.start", "{}").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(f.editor(), "scene.save", R"({"world":"play"})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "play.step", R"({"steps":0})").kind == ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "play.set_time_scale", R"({"scale":-1})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("play.keep_changes copies play values to the edit World as one undoable step")
{
    PlayFixture f;
    f.base.buildScene();
    ok(f.editor(), "selection.set", R"({"entities":["/Game"]})");
    ok(f.editor(), "play.start");
    ok(f.editor(), "component.set",
       R"({"entity":"/Game","type":"illusion.Transform","value":{"position":[5,0,0]}})");
    ok(f.editor(), "entity.create", R"({"name":"Spawned"})");
    // The selection holds ids, so it names the same entity in both Worlds.
    const JsonDocument kept =
        ok(f.editor(), "play.keep_changes",
           R"({"entities":["$selection","/Spawned"],"components":["illusion.Transform"]})");
    CHECK(kept.root().find("entities").asU64() == 1);
    CHECK(kept.root().find("components").asU64() == 1);
    CHECK(kept.root().pointer("/_editor/warnings/0").asString().value_or("").find("Spawned") !=
          std::string::npos);
    ok(f.editor(), "play.stop");
    CHECK(f.playNumber("/Game", "/value/position/0") == doctest::Approx(5.0));
    CHECK(f.editor().history().steps().size() == 1);
    ok(f.editor(), "history.undo");
    CHECK(f.playNumber("/Game", "/value/position/0") == doctest::Approx(1.0));
}

TEST_CASE("a simulate session moves dynamic bodies and applies physics edits at once")
{
    PlayFixture f;
    f.addBall();
    ok(f.editor(), "play.start", R"({"mode":"simulate","fixedStep":true})");
    CHECK(ok(f.editor(), "play.status").root().find("mode").asString() == "simulate");
    ok(f.editor(), "play.step", R"({"steps":30})");
    const f64 falling = f.playNumber("/Ball", "/value/position/1");
    CHECK(falling < 9.5);
    // An edit hook rebuilds the body: a static body stops falling.
    ok(f.editor(), "component.set_field",
       R"({"entity":"/Ball","type":"illusion.RigidBody","path":"motionType","value":"Static"})");
    ok(f.editor(), "play.step", R"({"steps":30})");
    CHECK(f.playNumber("/Ball", "/value/position/1") == doctest::Approx(falling));
    ok(f.editor(), "play.stop");
    CHECK(f.playNumber("/Ball", "/value/position/1") == doctest::Approx(10.0));
}

TEST_CASE("pause, steps and the time scale control how the play World advances")
{
    PlayFixture f;
    ok(f.editor(), "play.start");
    f.play->update(1.0 / 60.0);
    CHECK(f.play->status().steps == 1);
    CHECK(ok(f.editor(), "play.pause", R"({"paused":true})").root().find("state").asString() == "paused");
    f.play->update(1.0 / 60.0);
    CHECK(f.play->status().steps == 1);
    CHECK(ok(f.editor(), "play.step", R"({"steps":3})").root().find("steps").asU64() == 4);
    ok(f.editor(), "play.pause", R"({"paused":false})");
    ok(f.editor(), "play.set_time_scale", R"({"scale":0})");
    f.play->update(1.0 / 60.0);
    CHECK(f.play->status().steps == 4);
    ok(f.editor(), "play.set_time_scale", R"({"scale":0.5})");
    f.play->update(1.0 / 30.0);
    CHECK(f.play->status().steps == 5);
    CHECK(f.play->status().time == doctest::Approx(4.0 / 60.0 + 1.0 / 60.0));
}

TEST_CASE("play.run_until steps until the condition holds or maxSteps pass")
{
    PlayFixture f;
    f.addBall();
    ok(f.editor(), "play.start", R"({"mode":"simulate","fixedStep":true})");
    const JsonDocument now =
        ok(f.editor(), "play.run_until", R"({"maxSteps":10,"entity":"/Ball","exists":true})");
    CHECK(now.root().find("met").asBool() == true);
    CHECK(now.root().find("steps").asU64() == 0);
    const JsonDocument never =
        ok(f.editor(), "play.run_until", R"({"maxSteps":10,"entity":"/Ball","exists":false})");
    CHECK(never.root().find("met").asBool() == false);
    CHECK(never.root().find("steps").asU64() == 10);
    CHECK(never.root().pointer("/status/steps").asU64() == 10);
    const JsonDocument value = ok(f.editor(), "play.run_until",
                                  R"({"maxSteps":10,"entity":"/Ball","component":"illusion.RigidBody",)"
                                  R"("field":"/motionType","equals":"Dynamic"})");
    CHECK(value.root().find("met").asBool() == true);
    CHECK(fails(f.editor(), "play.run_until", R"({"maxSteps":10,"entity":"/Ball"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "play.run_until", R"({"maxSteps":0,"entity":"/Ball","exists":true})").kind ==
          ErrorKind::ValidationFailed);
}

TEST_CASE("input asset commands edit the .input file and undo")
{
    PlayFixture f;
    constexpr std::string_view asset = R"("asset":"assets:/input/game.input")";
    const JsonDocument defined =
        ok(f.editor(), "input.action_define",
           "{" + std::string(asset) + R"(,"name":"Jump","type":"Button","bindings":["Key.Space"]})");
    CHECK(defined.root().pointer("/actions/0/bindings/0").asString() == "Key.Space");
    ok(f.editor(), "input.action_define",
       "{" + std::string(asset) + R"(,"name":"Move","type":"Vector2","bindings":["Keys.W.S.A.D"]})");
    ok(f.editor(), "input.bind", "{" + std::string(asset) + R"(,"action":"Jump","binding":"Gamepad.South"})");
    ok(f.editor(), "input.action_rename", "{" + std::string(asset) + R"(,"name":"Jump","newName":"Hop"})");
    const JsonDocument listed = ok(f.editor(), "input.actions", "{" + std::string(asset) + "}");
    CHECK(listed.root().find("actions").size() == 2);
    CHECK(listed.root().pointer("/actions/0/name").asString() == "Hop");
    CHECK(listed.root().pointer("/actions/0/bindings").size() == 2);
    ok(f.editor(), "input.unbind", "{" + std::string(asset) + R"(,"action":"Hop","binding":"Key.Space"})");
    ok(f.editor(), "input.action_remove", "{" + std::string(asset) + R"(,"name":"Move"})");
    CHECK(ok(f.editor(), "input.actions", "{" + std::string(asset) + "}").root().find("actions").size() == 1);
    ok(f.editor(), "history.undo");
    ok(f.editor(), "history.undo");
    ok(f.editor(), "history.undo");
    CHECK(ok(f.editor(), "input.actions", "{" + std::string(asset) + "}")
              .root()
              .pointer("/actions/0/name")
              .asString() == "Jump");

    CHECK(fails(f.editor(), "input.bind",
                "{" + std::string(asset) + R"(,"action":"Jump","binding":"Key.Nope"})")
              .kind == ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "input.bind", "{" + std::string(asset) + R"(,"action":"Jupm","binding":"Key.A"})")
              .suggestions.front() == "Jump");
    CHECK(fails(f.editor(), "input.action_define",
                "{" + std::string(asset) + R"(,"name":"X","type":"Trigger"})")
              .kind == ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "input.actions", R"({"asset":"assets:/input/game.json"})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "input.actions", R"({"asset":"assets:/input/none.input"})").kind ==
          ErrorKind::NotFound);
}

TEST_CASE("input.inject reaches the actions of the running game")
{
    PlayFixture f;
    ok(f.editor(), "input.action_define",
       R"({"asset":"assets:/input/game.input","name":"Jump","type":"Button","bindings":["Key.Space"]})");
    ok(f.editor(), "project.settings_set",
       R"({"type":"illusion.InputSettings","value":{"actions":"assets:/input/game.input"}})");
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    ok(f.editor(), "input.inject", R"({"keys":[{"key":"Space","down":true}],"mousePosition":[10,20]})");
    ok(f.editor(), "play.step");
    const JsonDocument read = ok(f.editor(), "input.read", R"({"actions":["Jump"]})");
    CHECK(read.root().pointer("/actions/0/down").asBool() == true);
    CHECK(read.root().pointer("/actions/0/pressed").asBool() == true);
    CHECK(read.root().pointer("/actions/0/type").asString() == "Button");
    CHECK(f.play->game()->keyboardMouse().mousePosition().x == doctest::Approx(10.0f));
    CHECK(fails(f.editor(), "input.read", R"({"actions":["Fly"]})").kind == ErrorKind::NotFound);
    CHECK(fails(f.editor(), "input.inject", R"({"keys":[{"key":"Nope"}]})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "input.inject", R"({"mouseButtons":[{"button":"Thumb"}]})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(f.editor(), "input.inject", "{}").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("module commands report what is loaded and why a module cannot load")
{
    PlayFixture f;
    CHECK(ok(f.editor(), "module.list").root().find("modules").size() == 0);
    const CommandError error = fails(f.editor(), "module.load", R"({"path":"no/such/module.dll"})");
    CHECK(error.kind == ErrorKind::ModuleError);
    CHECK_FALSE(error.message.empty());
    CHECK(fails(f.editor(), "module.unload", R"({"name":"tetris"})").kind == ErrorKind::NotFound);
    CHECK(fails(f.editor(), "module.load", R"({"path":""})").kind == ErrorKind::ValidationFailed);
}

TEST_CASE("play commands fail cleanly in an editor without a game runtime")
{
    reflectEditorTestComponents();
    EditorContext editor(EditorContextOptions{.seed = 1});
    CHECK(fails(editor, "play.start", "{}").kind == ErrorKind::Unsupported);
    CHECK(fails(editor, "play.status", "{}").kind == ErrorKind::Unsupported);
    CHECK(fails(editor, "module.list", "{}").kind == ErrorKind::Unsupported);
    CHECK(fails(editor, "input.inject", R"({"keys":[{"key":"A"}]})").kind == ErrorKind::WrongMode);
    CHECK(fails(editor, "play.keep_changes", R"({"entities":["/A"]})").kind == ErrorKind::WrongMode);
}

TEST_CASE("the runtime settings are project settings of the editor")
{
    PlayFixture f;
    const SettingsType* physics =
        f.editor().settings().find("illusion.PhysicsSettings", SettingsScope::Project);
    REQUIRE(physics != nullptr);
    CHECK(physics->file == "settings:/physics.json");
    ok(f.editor(), "project.settings_set",
       R"({"type":"illusion.PhysicsSettings","value":{"stepSeconds":0.02}})");
    ok(f.editor(), "play.start", R"({"fixedStep":true})");
    ok(f.editor(), "play.step", R"({"steps":10})");
    CHECK(f.play->status().time == doctest::Approx(0.2));
}

TEST_CASE("destroying the PlaySession stops its session and unregisters it")
{
    PlayFixture f;
    ok(f.editor(), "play.start");
    f.play.reset();
    CHECK_FALSE(f.editor().playing());
    CHECK(f.editor().play() == nullptr);
}

#if defined(ILLUSION_MODULE_SDK)
TEST_CASE("module.build compiles a project module with the module SDK of this build and plays it")
{
    EditorFixture base(1, EditorContextOptions{.moduleSdk = std::filesystem::path(
                                                   std::u8string_view(u8"" ILLUSION_MODULE_SDK))});
    reflectPhysicsComponents();
    reflectAudioComponents();
    JobSystem jobs{JobSystemDesc{2, 1024}};
    PlaySession play(*base.editor, PlaySessionDesc{.jobs = &jobs, .audioOutput = AudioOutput::Offline});
    EditorContext& editor = *base.editor;
    // The fixture's module has no source; start from module.create alone.
    std::filesystem::remove_all(base.directory.path / "project" / "modules");
    ok(editor, "module.create", R"({"name":"spinner"})");

    const JsonDocument built = ok(editor, "module.build");
    INFO(built.root().find("output").asString().value_or(""));
    REQUIRE(built.root().find("succeeded").asBool() == true);
    REQUIRE(built.root().find("loaded").size() == 1);
    CHECK(built.root().find("loaded")[0].asString() == "spinner");
    CHECK(ok(editor, "module.list").root().find("modules")[0].find("name").asString() == "spinner");

    // A compile error comes back with the module's virtual path and the line.
    const std::filesystem::path source =
        base.directory.path / "project" / "modules" / "spinner" / "spinner.cpp";
    std::string original;
    {
        std::ifstream in(source, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        original = text.str();
    }
    std::ofstream(source, std::ios::binary) << original << "int broken = undefined_name;\n";
    const JsonDocument failed = ok(editor, "module.build");
    CHECK(failed.root().find("succeeded").asBool() == false);
    REQUIRE(failed.root().find("errors").asU64().value_or(0) >= 1u);
    const JsonValue error = failed.root().find("diagnostics")[0];
    CHECK(error.find("file").asString() == "modules:/spinner/spinner.cpp");
    CHECK(error.find("severity").asString() == "error");
    CHECK(error.find("line").asU64() == 26u);

    // The fixed module is hot-reloaded and runs in a play session.
    std::ofstream(source, std::ios::binary) << original << "// fixed\n";
    const JsonDocument fixed = ok(editor, "module.build", R"({"modules":["spinner"]})");
    INFO(fixed.root().find("output").asString().value_or(""));
    REQUIRE(fixed.root().find("succeeded").asBool() == true);
    REQUIRE(fixed.root().find("reloaded").size() == 1);
    ok(editor, "play.start", R"({"fixedStep":true})");
    ok(editor, "play.step", R"({"steps":3})");
    ok(editor, "play.stop");
    CHECK(ok(editor, "module.list").root().find("modules")[0].find("reloads").asU64() == 1u);
}
#endif

TEST_CASE("play.start autosaves an unsaved scene before the game runs")
{
    PlayFixture f;
    const std::filesystem::path autosaves = f.editor().project()->stateDirectory() / "autosave";
    const auto count = [&] {
        std::error_code code;
        usize files = 0;
        for (std::filesystem::directory_iterator it(autosaves, code), end; !code && it != end;
             it.increment(code))
        {
            ++files;
        }
        return files;
    };

    // A saved scene has nothing to keep.
    ok(f.editor(), "play.start");
    ok(f.editor(), "play.stop");
    CHECK(count() == 0);

    ok(f.editor(), "entity.create", R"({"name":"Unsaved"})");
    REQUIRE(f.editor().dirty());
    ok(f.editor(), "play.start", R"({"dryRun":true})");
    CHECK(count() == 0);
    ok(f.editor(), "play.start");
    CHECK(count() == 1);
    ok(f.editor(), "play.stop");

    // Autosave off (interval 0) means never, also before Play.
    ok(f.editor(), "preferences.set", R"({"value":{"autosaveInterval":0}})");
    ok(f.editor(), "play.start");
    CHECK(count() == 1);
}

TEST_CASE("while playing, viewport commands see the play World and refuse the edit World")
{
    PlayFixture f;
    f.base.buildScene();
    ok(f.editor(), "play.start");
    ok(f.editor(), "entity.rename", R"({"entity":"/Light","name":"Lamp"})");
    EntityIndex* play = f.editor().playEntities();
    REQUIRE(play != nullptr);
    const Entity lamp = play->resolve("/Lamp", f.editor().selection()).value();
    f.base.viewports.hit = ViewportHit{lamp, 0, 1};

    const JsonDocument picked = ok(f.editor(), "viewport.pick", R"({"x":1,"y":1,"select":"replace"})");
    CHECK(picked.root().pointer("/entity/path").asString() == std::optional<std::string_view>("/Lamp"));
    CHECK(f.editor().selection().primary() == play->guidOf(lamp));
    CHECK(fails(f.editor(), "viewport.pick", R"({"x":1,"y":1,"world":"edit"})").kind ==
          ErrorKind::PreconditionFailed);
    ok(f.editor(), "play.stop");
    ok(f.editor(), "viewport.camera_get", R"({"world":"edit"})");
}

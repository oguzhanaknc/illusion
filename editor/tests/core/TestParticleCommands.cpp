// The particles.* commands on the edit World, whose emitters EditorContext previews every frame (ADR 0047).

#include "EditorCoreTestSupport.hpp"

#include <illusion/particles/ParticleComponents.hpp>

using namespace illusion;
using namespace illusion::editor;
using namespace illusion::test;

namespace
{

// Advances the editor (and its particle preview) by @p frames frames of 1/60 s.
void frames(EditorContext& editor, int count)
{
    for (int i = 0; i < count; ++i)
    {
        editor.endFrame(1.0 / 60.0);
    }
}

u64 field(const JsonDocument& result, std::string_view name)
{
    return result.root().find(name).asU64().value_or(~u64{0});
}

} // namespace

TEST_CASE("the edit World previews particle emitters and particles.* plays, stops, bursts and clears them")
{
    reflectParticleComponents();
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "entity.create",
       R"({"name":"Sparks","components":{"illusion.Transform":{},"illusion.ParticleEmitter":)"
       R"({"rate":60,"lifetimeMin":10,"lifetimeMax":10,"maxParticles":100}}})");
    const std::string hash(ok(editor, "scene.info").root().find("hash").asString().value_or(""));
    frames(editor, 30);
    // Particles are runtime state: the preview does not change the scene.
    CHECK(ok(editor, "scene.info").root().find("hash").asString() == hash);
    const JsonDocument previewed = ok(editor, "particles.stats", R"({"entity":"/Sparks"})");
    CHECK(field(previewed, "emitters") == 1);
    CHECK(field(previewed, "playing") == 1);
    CHECK(field(previewed, "alive") == 30); // half a second at 60 per second

    const JsonDocument stopped = ok(editor, "particles.stop", R"({"entity":"/Sparks"})");
    CHECK(stopped.root().find("playing").asBool() == false);
    frames(editor, 30);
    CHECK(field(ok(editor, "particles.stats"), "alive") == 30); // nothing new while stopped

    const JsonDocument burst = ok(editor, "particles.emit", R"({"entity":"/Sparks","count":100})");
    CHECK(field(burst, "emitted") == 70); // up to maxParticles
    CHECK(field(burst, "alive") == 100);

    const JsonDocument cleared = ok(editor, "particles.clear", R"({"entity":"/Sparks"})");
    CHECK(field(cleared, "alive") == 0);

    const JsonDocument played = ok(editor, "particles.play", R"({"entity":"/Sparks"})");
    CHECK(played.root().find("playing").asBool() == true);
    frames(editor, 6);
    CHECK(field(ok(editor, "particles.stats"), "alive") == 6);

    // A dry run validates without changing anything.
    ok(editor, "particles.clear", R"({"entity":"/Sparks","dryRun":true})");
    CHECK(field(ok(editor, "particles.stats"), "alive") == 6);
}

TEST_CASE("particles.* rejects entities without an emitter and counts outside their range")
{
    reflectParticleComponents();
    EditorFixture f;
    EditorContext& editor = *f.editor;
    ok(editor, "entity.create", R"({"name":"Plain","components":{"illusion.Transform":{}}})");
    ok(editor, "entity.create",
       R"({"name":"Sparks","components":{"illusion.Transform":{},"illusion.ParticleEmitter":{}}})");
    CHECK(fails(editor, "particles.play", R"({"entity":"/Plain"})").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "particles.stats", R"({"entity":"/Plain"})").kind == ErrorKind::PreconditionFailed);
    CHECK(fails(editor, "particles.stop", R"({"entity":"/Missing"})").kind == ErrorKind::NotFound);
    CHECK(fails(editor, "particles.emit", R"({"entity":"/Sparks","count":0})").kind ==
          ErrorKind::ValidationFailed);
    CHECK(fails(editor, "particles.emit", R"({"entity":"/Sparks","count":70000})").kind ==
          ErrorKind::ValidationFailed);
    const JsonDocument none = ok(editor, "particles.stats");
    CHECK(field(none, "emitters") == 1);
    CHECK(field(none, "alive") == 0);
}

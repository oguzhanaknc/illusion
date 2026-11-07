// EditorPhysicsQueries: transform.drop_to_ground onto the Colliders of the edit World with Jolt.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/audio/AudioComponents.hpp>
#include <illusion/core/JobSystem.hpp>
#include <illusion/editor/play/EditorPhysicsQueries.hpp>
#include <illusion/editor/play/PlaySession.hpp>
#include <illusion/particles/ParticleComponents.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <filesystem>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;
using test::fails;
using test::ok;

namespace
{

struct PhysicsFixture
{
    PhysicsFixture() { reflectPhysicsComponents(); }

    EditorContext& editor() { return *base.editor; }

    void create(std::string_view json) { ok(editor(), "entity.create", json); }

    f64 height(std::string_view path)
    {
        const JsonDocument placed =
            ok(editor(), "transform.get", R"({"entity":")" + std::string(path) + R"("})");
        return placed.root().find("position")[1].asF64().value_or(-1000.0);
    }

    EditorFixture base;
    JobSystem jobs{JobSystemDesc{2, 1024}};
    EditorPhysicsQueries queries{*base.editor, jobs};
};

} // namespace

TEST_CASE("drop to ground onto colliders sweeps colliders down onto other colliders or the ground plane")
{
    PhysicsFixture fixture;
    EditorContext& editor = fixture.editor();
    CHECK(editor.physicsQueries() == &fixture.queries);
    // A floor 10 m wide with its top at y = 1: a Collider without a RigidBody is a surface too.
    fixture.create(R"({"name":"Floor","components":{"illusion.Transform":{"position":[0,0.5,0]},)"
                   R"("illusion.Collider":{"shape":"Box","halfExtents":[5,0.5,5]}}})");
    fixture.create(
        R"({"name":"Crate","components":{"illusion.Transform":{"position":[0,5,0]},)"
        R"("illusion.RigidBody":{},"illusion.Collider":{"shape":"Box","halfExtents":[0.5,0.5,0.5]}}})");
    fixture.create(R"({"name":"Lid","parent":"/Crate","components":{"illusion.Transform":{},)"
                   R"("illusion.LocalTransform":{"position":[0,1,0]}}})");
    // A sphere scaled to radius 1.
    fixture.create(
        R"({"name":"Ball","components":{"illusion.Transform":{"position":[3,6,0],"scale":[2,2,2]},)"
        R"("illusion.Collider":{"shape":"Sphere","radius":0.5}}})");
    // A trigger is not a surface: the box above it falls through it onto the floor.
    fixture.create(R"({"name":"Trigger","components":{"illusion.Transform":{"position":[-3,3,0]},)"
                   R"("illusion.RigidBody":{"motionType":"Static","sensor":true},)"
                   R"("illusion.Collider":{"shape":"Box","halfExtents":[1,1,1]}}})");
    fixture.create(R"({"name":"Box","components":{"illusion.Transform":{"position":[-3,7,0]},)"
                   R"("illusion.Collider":{"shape":"Box","halfExtents":[0.5,0.5,0.5]}}})");
    // Beyond the floor only the ground plane is below; without a Collider a ray falls from the position.
    fixture.create(R"({"name":"Far","components":{"illusion.Transform":{"position":[20,4,0]},)"
                   R"("illusion.Collider":{"shape":"Capsule","radius":0.5,"halfHeight":0.5}}})");
    fixture.create(R"({"name":"Marker","components":{"illusion.Transform":{"position":[0,8,4]}}})");
    const std::string before = fixture.base.scene();

    ok(editor, "transform.drop_to_ground",
       R"({"entities":["/Crate","/Ball","/Box","/Far","/Marker"],"onto":"colliders"})");
    CHECK(fixture.height("/Crate") == doctest::Approx(1.5).epsilon(0.001));
    CHECK(fixture.height("/Crate/Lid") == doctest::Approx(2.5).epsilon(0.001));
    CHECK(fixture.height("/Ball") == doctest::Approx(2.0).epsilon(0.001));
    CHECK(fixture.height("/Box") == doctest::Approx(1.5).epsilon(0.001));
    CHECK(fixture.height("/Far") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(fixture.height("/Marker") == doctest::Approx(1.0).epsilon(0.001));
    CHECK(fixture.height("/Floor") == doctest::Approx(0.5));

    // One step undoes the drop.
    ok(editor, "history.undo");
    CHECK(fixture.base.scene() == before);

    // A resting entity stays.
    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"],"onto":"colliders"})");
    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"],"onto":"colliders"})");
    CHECK(fixture.height("/Crate") == doctest::Approx(1.5).epsilon(0.001));

    // A disabled floor is no surface: the crate falls to the plane.
    ok(editor, "entity.set_enabled", R"({"entity":"/Floor","enabled":false})");
    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate"],"onto":"colliders"})");
    CHECK(fixture.height("/Crate") == doctest::Approx(0.5).epsilon(0.001));
}

TEST_CASE("drop to ground onto colliders needs physics queries")
{
    EditorFixture fixture;
    ok(*fixture.editor, "entity.create", R"({"name":"Crate","components":{"illusion.Transform":{}}})");
    CHECK(fails(*fixture.editor, "transform.drop_to_ground", R"({"entities":["/Crate"],"onto":"colliders"})")
              .kind == ErrorKind::Unsupported);
}

namespace
{

// A 10 m quad lying flat at y = 2 as a triangle Mesh Collider from a glTF file of the project (the quad of
// tests/data/gltf/binary.glb stands in the XY plane: turned -90 degrees about X it faces up), with the
// Components in @p body as well.
void addQuadFloor(PhysicsFixture& fixture, std::string_view body = "")
{
    const std::filesystem::path models = fixture.base.directory.path / "project" / "assets" / "models";
    std::filesystem::create_directories(models);
    std::filesystem::copy_file(utf8Path(ILLUSION_ENGINE_TEST_DATA) / "gltf" / "binary.glb",
                               models / "quad.glb", std::filesystem::copy_options::overwrite_existing);
    fixture.create(R"({"name":"Quad","components":{"illusion.Transform":{"position":[0,2,0],)"
                   R"("rotation":[-0.70710678,0,0,0.70710678],"scale":[5,5,5]},)" +
                   std::string(body) +
                   R"("illusion.Collider":{"shape":"Mesh","mesh":"assets:/models/quad.glb#mesh/0"}}})");
}

} // namespace

TEST_CASE("drop to ground onto colliders loads mesh assets: triangle mesh surfaces and falling convex hulls")
{
    PhysicsFixture fixture;
    EditorContext& editor = fixture.editor();
    addQuadFloor(fixture);
    fixture.create(R"({"name":"Crate","components":{"illusion.Transform":{"position":[3,6,0]},)"
                   R"("illusion.Collider":{"shape":"Box","halfExtents":[0.5,0.5,0.5]}}})");
    // A convex hull of the built-in unit cube, twice the size: its bottom is 1 m below its position.
    fixture.create(
        R"({"name":"Rock","components":{"illusion.Transform":{"position":[2,8,2],"scale":[2,2,2]},)"
        R"("illusion.Collider":{"shape":"ConvexHull","mesh":"builtin:/mesh/cube"}}})");

    ok(editor, "transform.drop_to_ground", R"({"entities":["/Crate","/Rock"],"onto":"colliders"})");
    CHECK(fixture.height("/Crate") == doctest::Approx(2.5).epsilon(0.001));
    CHECK(fixture.height("/Rock") == doctest::Approx(3.0).epsilon(0.001));

    // The quad itself is a triangle mesh: it cannot be swept, so it falls from its position by a ray.
    ok(editor, "transform.drop_to_ground", R"({"entities":["/Quad"],"onto":"colliders"})");
    CHECK(fixture.height("/Quad") == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("Play mode gives mesh asset colliders their shape when the session has a streamer")
{
    PhysicsFixture fixture;
    reflectAudioComponents();
    reflectParticleComponents();
    EditorContext& editor = fixture.editor();
    // Play mode simulates bodies only: the floor needs a static RigidBody.
    addQuadFloor(fixture, R"("illusion.RigidBody":{"motionType":"Static"},)");
    fixture.create(
        R"({"name":"Ball","components":{"illusion.Transform":{"position":[0,4,0]},)"
        R"("illusion.RigidBody":{"motionType":"Dynamic"},"illusion.Collider":{"shape":"Sphere","radius":0.5}}})");
    AssetStreamer streamer(editor.files(), fixture.jobs, AssetStreamerDesc{.cacheMount = ""});
    PlaySession play(
        editor,
        PlaySessionDesc{.jobs = &fixture.jobs, .streamer = &streamer, .audioOutput = AudioOutput::Offline});
    ok(editor, "play.start", R"({"fixedStep":true})");
    ok(editor, "play.step", R"({"steps":180})");
    // The ball rests on the quad instead of falling through it to the ground.
    CHECK(fixture.height("/Ball") == doctest::Approx(2.5).epsilon(0.01));
    ok(editor, "play.stop");
}

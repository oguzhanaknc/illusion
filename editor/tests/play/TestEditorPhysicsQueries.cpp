// EditorPhysicsQueries: transform.drop_to_ground onto the Colliders of the edit World with Jolt.

#include "core/EditorCoreTestSupport.hpp"

#include <illusion/core/JobSystem.hpp>
#include <illusion/editor/play/EditorPhysicsQueries.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

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

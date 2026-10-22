#include "assets/AssetTestProject.hpp"
#include "physics/PhysicsTestSupport.hpp"

#include <illusion/assets/AssetStreamer.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/physics/PhysicsAssetResolver.hpp>
#include <illusion/physics/PhysicsComponents.hpp>

#include <doctest/doctest.h>

#include <numbers>

using namespace illusion;
using namespace illusion::test;

namespace
{

Collider meshCollider(ColliderShape shape, std::string_view asset)
{
    Collider collider;
    collider.shape = shape;
    collider.mesh = AssetRef(asset);
    return collider;
}

} // namespace

TEST_CASE("mesh and hull colliders from assets collide after a scene reload")
{
    reflectCoreComponents();
    reflectPhysicsComponents();
    AssetProject project;
    JobSystem jobs(JobSystemDesc{2, 256});
    AssetStreamer streamer(project.files, jobs);

    std::string json;
    {
        World source;
        // Ground: the glTF quad (2 x 2, facing +z) turned to face up and scaled to 20 x 20.
        Transform ground;
        ground.rotation = glm::angleAxis(-std::numbers::pi_v<f32> / 2.0f, Vec3(1.0f, 0.0f, 0.0f));
        ground.scale = Vec3(10.0f);
        source.create(ground, body(MotionType::Static),
                      meshCollider(ColliderShape::Mesh, "game:/binary.glb#mesh/0"));
        // A falling box: the convex hull of the built-in cube (1 x 1 x 1).
        source.create(at(Vec3(0.0f, 3.0f, 0.0f)), body(MotionType::Dynamic),
                      meshCollider(ColliderShape::ConvexHull, "builtin:/mesh/cube"));
        assignMissingGuids(source);
        const SceneSaveResult saved = saveScene(source);
        REQUIRE(saved.ok());
        CHECK(saved.json.find("\"mesh\": \"game:/binary.glb#mesh/0\"") != std::string::npos);
        json = saved.json;
    }

    World world;
    const SceneLoadResult loaded = loadScene(world, json);
    REQUIRE(loaded.ok());
    Entity box;
    world.query<const RigidBody>().each([&](Entity entity, const RigidBody& rigidBody) {
        if (rigidBody.motionType == MotionType::Dynamic)
        {
            box = entity;
        }
    });
    REQUIRE_FALSE(box.isNull());

    PhysicsWorld physics(world, jobs);
    PhysicsAssetResolver colliders(physics, streamer);
    physics.update(kStep);
    CHECK(physics.bodyCount() == 0); // waiting for the cooked geometry

    colliders.waitIdle();
    CHECK(colliders.statistics().geometries == 2);
    CHECK(colliders.statistics().pending == 0);
    CHECK(colliders.statistics().failures == 0);
    for (int step = 0; step < 180; ++step)
    {
        colliders.update();
        physics.update(kStep);
    }
    CHECK(physics.bodyCount() == 2);
    CHECK(world.get<Transform>(box).position.y == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("colliders share cooked geometry and rebuild when their asset changes")
{
    reflectPhysicsComponents();
    AssetProject project;
    JobSystem jobs(JobSystemDesc{2, 256});
    AssetStreamer streamer(project.files, jobs);
    World world;
    PhysicsWorld physics(world, jobs);
    PhysicsAssetResolver colliders(physics, streamer);
    createGround(world);
    const Entity first = world.create(at(Vec3(-2.0f, 1.0f, 0.0f)), body(MotionType::Dynamic),
                                      meshCollider(ColliderShape::ConvexHull, "builtin:/mesh/sphere"));
    world.create(at(Vec3(2.0f, 1.0f, 0.0f)), body(MotionType::Dynamic),
                 meshCollider(ColliderShape::ConvexHull, "builtin:/mesh/sphere"));
    const Entity missing = world.create(at(Vec3(0.0f, 1.0f, 4.0f)), body(MotionType::Static),
                                        meshCollider(ColliderShape::Mesh, "game:/missing.glb#mesh/0"));
    colliders.waitIdle();
    physics.update(kStep);
    CHECK(colliders.statistics().geometries == 1); // shared by both spheres
    CHECK(colliders.statistics().failures == 1);
    CHECK(physics.bodyCount() == 3); // the missing mesh gets no body
    CHECK_FALSE(world.get<Collider>(missing).geometry.valid());

    // A new asset is cooked and the body rebuilt with it.
    const PhysicsMeshHandle sphere = world.get<Collider>(first).geometry;
    world.get<Collider>(first).mesh = AssetRef("builtin:/mesh/cylinder");
    colliders.waitIdle();
    physics.update(kStep);
    CHECK(colliders.statistics().geometries == 2);
    CHECK(world.get<Collider>(first).geometry != sphere);
    CHECK(physics.bodyCount() == 3);
}

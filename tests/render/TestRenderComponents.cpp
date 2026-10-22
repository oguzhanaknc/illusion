#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/render/Components.hpp>

#include <doctest/doctest.h>

using namespace illusion;

TEST_CASE("cameras and lights are saved in scenes; GPU-backed components are not")
{
    reflectCoreComponents();
    reflectRenderComponents();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    CHECK(registry.findByStableName("illusion.Camera") == componentId<Camera>());
    CHECK(registry.findByStableName("illusion.DirectionalLight") == componentId<DirectionalLight>());
    CHECK(registry.findByStableName("illusion.PointLight") == componentId<PointLight>());
    CHECK(registry.reflection(componentId<MeshRenderer>()) == nullptr);
    CHECK(registry.reflection(componentId<SkyLight>()) == nullptr);

    World world;
    Camera camera;
    camera.fovY = 0.8f;
    camera.exposure = 2.0f;
    DirectionalLight sun;
    sun.color = Vec3(1.0f, 0.9f, 0.8f);
    sun.castShadows = false;
    PointLight lamp;
    lamp.range = 25.0f;
    world.create(Transform{}, camera);
    world.create(Transform{}, sun);
    world.create(Transform{}, lamp, MeshRenderer{});

    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world, {.order = SceneOrder::AsGiven});
    REQUIRE(saved.ok());
    CHECK(saved.skippedComponents == 1); // MeshRenderer

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 3);
    CHECK(loaded.get<Camera>(result.entities[0]).fovY == 0.8f);
    CHECK(loaded.get<Camera>(result.entities[0]).exposure == 2.0f);
    CHECK(loaded.get<DirectionalLight>(result.entities[1]).color == sun.color);
    CHECK_FALSE(loaded.get<DirectionalLight>(result.entities[1]).castShadows);
    CHECK(loaded.get<PointLight>(result.entities[2]).range == 25.0f);
    CHECK_FALSE(loaded.has<MeshRenderer>(result.entities[2]));
}

#include <illusion/assets/Model.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

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

TEST_CASE("spawnModel keeps the node hierarchy and node names")
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    const std::optional<ModelData> model = loadGltf(files, "test:/gltf/scene/two_nodes.gltf", &error);
    REQUIRE_MESSAGE(model.has_value(), error);
    ModelHandles handles;
    handles.meshes.push_back(MeshHandle{1});
    handles.materials.push_back(MaterialHandle{2});

    World world;
    world.create(Hierarchy{}, Transform{}); // an existing root
    const std::vector<Entity> entities =
        spawnModel(world, *model, handles, glm::translate(Mat4(1.0f), Vec3(0.0f, 10.0f, 0.0f)));
    REQUIRE(entities.size() == 2);
    const Entity root = entities[0];
    const Entity child = entities[1];
    CHECK(world.get<Name>(root).value == "root");
    CHECK(world.get<Name>(child).value == "child");
    CHECK(parentOf(world, root).isNull());
    CHECK(world.get<Hierarchy>(root).order == 1); // after the existing root
    CHECK(parentOf(world, child) == root);
    CHECK_FALSE(world.has<MeshRenderer>(root));
    REQUIRE(world.has<MeshRenderer>(child));
    CHECK(world.get<MeshRenderer>(child).mesh == MeshHandle{1});
    CHECK(world.get<MeshRenderer>(child).material == MaterialHandle{2});

    // root: translation (1, 2, 3) under the spawn matrix; child: scale 2 relative to it.
    CHECK(world.get<Transform>(root).position.y == doctest::Approx(12.0));
    CHECK(world.get<LocalTransform>(child).scale.x == doctest::Approx(2.0));
    CHECK(world.get<Transform>(child).position.y == doctest::Approx(12.0));
    CHECK(world.get<Transform>(child).scale.x == doctest::Approx(2.0));

    // The derived Transforms agree with the hierarchy.
    const Transform before = world.get<Transform>(child);
    HierarchyIndex index;
    updateTransformHierarchy(world, index);
    CHECK(world.get<Transform>(child).position.y == doctest::Approx(before.position.y));
    CHECK(world.get<Transform>(child).scale.x == doctest::Approx(before.scale.x));
}

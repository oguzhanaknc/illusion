#include <illusion/assets/Model.hpp>
#include <illusion/core/AssetRef.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/render/Components.hpp>
#include <illusion/render/RenderAssets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace illusion;

TEST_CASE("render components with asset references are saved in scenes; runtime ones are not")
{
    reflectCoreComponents();
    reflectRenderComponents();
    const ComponentRegistry& registry = ComponentRegistry::instance();
    CHECK(registry.findByStableName("illusion.Camera") == componentId<Camera>());
    CHECK(registry.findByStableName("illusion.DirectionalLight") == componentId<DirectionalLight>());
    CHECK(registry.findByStableName("illusion.PointLight") == componentId<PointLight>());
    CHECK(registry.findByStableName("illusion.MeshRenderer") == componentId<MeshRenderer>());
    CHECK(registry.findByStableName("illusion.SkyLight") == componentId<SkyLight>());
    CHECK(registry.findByStableName("illusion.ProceduralSky") == componentId<ProceduralSky>());
    CHECK(registry.reflection(componentId<RenderMesh>())->meta.transient);
    CHECK(registry.reflection(componentId<RenderSky>())->meta.transient);
    CHECK(registry.reflection(componentId<MeshRenderer>())->field("mesh")->meta.assetKind == AssetKind::Mesh);

    World world;
    Camera camera;
    camera.fovY = 0.8f;
    camera.exposure = 2.0f;
    DirectionalLight sun;
    sun.color = Vec3(1.0f, 0.9f, 0.8f);
    sun.castShadows = false;
    PointLight lamp;
    lamp.range = 25.0f;
    MeshRenderer renderer;
    renderer.mesh = AssetRef("builtin:/mesh/cube");
    renderer.material = AssetRef("game:/materials/missing.material");
    renderer.castShadows = false;
    ProceduralSky sky;
    sky.resolution = 64;
    world.create(Transform{}, camera);
    world.create(Transform{}, sun);
    world.create(Transform{}, lamp, renderer, RenderMesh{MeshHandle{3}});
    world.create(Transform{}, SkyLight{.environment = AssetRef("builtin:/sky/procedural"), .intensity = 2.0f},
                 sky, RenderSky{EnvironmentHandle{1}});

    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world, {.order = SceneOrder::AsGiven});
    REQUIRE(saved.ok());
    CHECK(saved.skippedComponents == 2); // RenderMesh, RenderSky
    CHECK(saved.json.find("\"mesh\": \"builtin:/mesh/cube\"") != std::string::npos);

    World loaded;
    SceneLoadOptions options;
    options.assetExists = [](std::string_view) {
        return false;
    };
    const SceneLoadResult result = loadScene(loaded, saved.json, options);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 4);
    CHECK(loaded.get<Camera>(result.entities[0]).fovY == 0.8f);
    CHECK(loaded.get<Camera>(result.entities[0]).exposure == 2.0f);
    CHECK(loaded.get<DirectionalLight>(result.entities[1]).color == sun.color);
    CHECK_FALSE(loaded.get<DirectionalLight>(result.entities[1]).castShadows);
    CHECK(loaded.get<PointLight>(result.entities[2]).range == 25.0f);
    const MeshRenderer& copy = loaded.get<MeshRenderer>(result.entities[2]);
    CHECK(copy.mesh == renderer.mesh);
    CHECK(copy.material == renderer.material);
    CHECK_FALSE(copy.castShadows);
    CHECK_FALSE(loaded.has<RenderMesh>(result.entities[2]));
    CHECK(loaded.get<SkyLight>(result.entities[3]).intensity == 2.0f);
    CHECK(loaded.get<ProceduralSky>(result.entities[3]).resolution == 64);
    // Built-ins exist without files; the material file does not.
    CHECK(result.missingAssets == std::vector<std::string>{"game:/materials/missing.material"});

    // A missing asset keeps its reference: saving writes it again.
    const SceneSaveResult again = saveScene(loaded, {.order = SceneOrder::AsGiven});
    REQUIRE(again.ok());
    CHECK(again.json == saved.json);
}

TEST_CASE("the source key of a MeshRenderer changes with every field")
{
    MeshRenderer renderer;
    renderer.mesh = AssetRef("builtin:/mesh/sphere");
    const u64 key = renderSourceKey(renderer);
    CHECK(key != 0);
    CHECK(renderSourceKey(MeshRenderer{}) != 0);
    std::vector<u64> keys{key};
    MeshRenderer changed = renderer;
    changed.mesh = AssetRef("builtin:/mesh/cube");
    keys.push_back(renderSourceKey(changed));
    changed = renderer;
    changed.submesh = 1;
    keys.push_back(renderSourceKey(changed));
    changed = renderer;
    changed.material = AssetRef("builtin:/material/default");
    keys.push_back(renderSourceKey(changed));
    changed = renderer;
    changed.layers = 2;
    keys.push_back(renderSourceKey(changed));
    changed = renderer;
    changed.castShadows = false;
    keys.push_back(renderSourceKey(changed));
    changed = renderer;
    changed.visible = false;
    keys.push_back(renderSourceKey(changed));
    std::sort(keys.begin(), keys.end());
    CHECK(std::unique(keys.begin(), keys.end()) == keys.end());
}

TEST_CASE("spawnModel keeps the node hierarchy and node names")
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    std::string error;
    const std::optional<ModelData> model = loadGltf(files, "test:/gltf/scene/two_nodes.gltf", &error);
    REQUIRE_MESSAGE(model.has_value(), error);

    World world;
    world.create(Hierarchy{}, Transform{}); // an existing root
    const std::vector<Entity> entities = spawnModel(world, *model, "test:/gltf/scene/./two_nodes.gltf",
                                                    glm::translate(Mat4(1.0f), Vec3(0.0f, 10.0f, 0.0f)));
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
    CHECK(world.get<MeshRenderer>(child).mesh.path() == "test:/gltf/scene/two_nodes.gltf#mesh/0");
    CHECK(world.get<MeshRenderer>(child).material.empty()); // the model's material

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

TEST_CASE("spawnModel with GPU handles writes RenderMesh Components")
{
    VirtualFileSystem files;
    REQUIRE(files.mount("test", utf8Path(ILLUSION_TEST_DATA_DIR)) == FileError::None);
    const std::optional<ModelData> model = loadGltf(files, "test:/gltf/scene/two_nodes.gltf");
    REQUIRE(model.has_value());
    ModelHandles handles;
    handles.meshes.push_back(MeshHandle{1});
    handles.materials.push_back(MaterialHandle{2});

    World world;
    const std::vector<Entity> entities = spawnModel(world, *model, handles);
    REQUIRE(entities.size() == 2);
    CHECK_FALSE(world.has<MeshRenderer>(entities[1]));
    REQUIRE(world.has<RenderMesh>(entities[1]));
    CHECK(world.get<RenderMesh>(entities[1]).mesh == MeshHandle{1});
    CHECK(world.get<RenderMesh>(entities[1]).material == MaterialHandle{2});
    CHECK(world.get<RenderMesh>(entities[1]).sourceKey == 0);
}

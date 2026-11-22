// The navigation Components' reflection (ADR 0062): units, hints, asset kinds and scene round trips.

#include <illusion/ecs/EntityGuid.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/SceneSerializer.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/nav/NavComponents.hpp>

#include <doctest/doctest.h>

#include <string_view>

using namespace illusion;

namespace
{

const TypeReflection& reflectionOf(std::string_view name)
{
    const ComponentRegistry& registry = ComponentRegistry::instance();
    const TypeReflection* type = registry.reflection(registry.findByStableName(name));
    REQUIRE(type != nullptr);
    return *type;
}

} // namespace

TEST_CASE("the navigation Components are reflected with units, hints, asset kinds and manual anchors")
{
    reflectCoreComponents();
    registerNavComponents(ComponentRegistry::instance());
    registerNavComponents(ComponentRegistry::instance());

    const TypeReflection& surface = reflectionOf("illusion.NavMeshSurface");
    CHECK(surface.meta.docsAnchor == "manual/navigation.md#surfaces");
    CHECK(surface.field("data")->meta.assetKind == AssetKind::NavMesh);
    CHECK(surface.field("agentMaxSlope")->meta.hint == FieldHint::Angle);
    CHECK(surface.field("agentMaxSlope")->meta.displayUnit == Unit::Degrees);
    CHECK(surface.field("layerMask")->meta.hint == FieldHint::LayerMask);
    CHECK(surface.field("size")->meta.unit == Unit::Meters);
    CHECK(surface.field("areaCosts")->meta.min == 1.0);
    CHECK(surface.field("build")->type == FieldType::Struct);
    CHECK(surface.field("agentType")->meta.max == 7.0);

    CHECK(reflectionOf("illusion.NavArea").meta.docsAnchor == "manual/navigation.md#areas");
    CHECK(reflectionOf("illusion.NavArea").field("area")->meta.max == 15.0);
    CHECK(reflectionOf("illusion.NavLink").meta.docsAnchor == "manual/navigation.md#links");
    CHECK(reflectionOf("illusion.NavLink").field("seconds")->meta.unit == Unit::Seconds);
    CHECK(reflectionOf("illusion.NavObstacle").meta.docsAnchor == "manual/navigation.md#obstacles");
    const TypeReflection& agent = reflectionOf("illusion.NavAgent");
    CHECK(agent.meta.docsAnchor == "manual/navigation.md#agents");
    CHECK(agent.field("maxSpeed")->meta.unit == Unit::MetersPerSecond);
    CHECK(agent.field("destination")->type == FieldType::Optional);
    CHECK(reflectionOf("illusion.NavAgentState").meta.transient);
    for (const std::string_view name : {"illusion.NavMeshSurface", "illusion.NavArea", "illusion.NavLink",
                                        "illusion.NavObstacle", "illusion.NavAgent"})
    {
        CAPTURE(name);
        const TypeReflection& type = reflectionOf(name);
        CHECK(type.meta.category == "Navigation");
        CHECK_FALSE(type.meta.icon.empty());
        CHECK_FALSE(type.meta.description.empty());
        CHECK_FALSE(type.meta.transient);
    }
}

TEST_CASE("navigation Components with non-default values survive a scene round trip")
{
    reflectCoreComponents();
    registerNavComponents(ComponentRegistry::instance());
    World world;
    NavMeshSurface surface;
    surface.size = Vec3(64.0f, 12.0f, 32.0f);
    surface.agentType = 2;
    surface.agentMaxSlope = 0.6f;
    surface.build.tileSize = 128;
    surface.build.cellSize = 0.2f;
    surface.layerMask = 0b1011u;
    surface.areaCosts[4] = 3.0f;
    surface.data = AssetRef("game:/navigation/forest.navmesh");
    surface.rebuildAtRuntime = true;
    NavAgent agent;
    agent.drive = NavDrive::Transform;
    agent.avoidance = NavAvoidance::High;
    agent.speedParameter = "speed";
    agent.destination = Vec3(1.0f, 2.0f, 3.0f);
    NavLink link;
    link.kind = NavLinkKind::Door;
    link.end = Vec3(0.0f, 0.0f, 5.0f);
    NavObstacle obstacle;
    obstacle.shape = NavObstacleShape::Cylinder;
    obstacle.carve = true;
    world.create(Transform{}, surface, agent, link, obstacle,
                 NavArea{.area = 6, .walkable = false, .volume = true, .size = Vec3(2.0f)});
    assignMissingGuids(world);
    const SceneSaveResult saved = saveScene(world);
    REQUIRE(saved.ok());

    World loaded;
    const SceneLoadResult result = loadScene(loaded, saved.json);
    REQUIRE(result.ok());
    REQUIRE(result.entities.size() == 1);
    const Entity entity = result.entities[0];
    const NavMeshSurface& s = loaded.get<NavMeshSurface>(entity);
    CHECK(s.size == surface.size);
    CHECK(s.agentType == 2);
    CHECK(s.agentMaxSlope == doctest::Approx(0.6f));
    CHECK(s.build.tileSize == 128);
    CHECK(s.build.cellSize == 0.2f);
    CHECK(s.layerMask == 0b1011u);
    CHECK(s.areaCosts[4] == 3.0f);
    CHECK(s.data == surface.data);
    CHECK(s.rebuildAtRuntime);
    const NavAgent& a = loaded.get<NavAgent>(entity);
    CHECK(a.drive == NavDrive::Transform);
    CHECK(a.avoidance == NavAvoidance::High);
    CHECK(a.speedParameter == "speed");
    REQUIRE(a.destination.has_value());
    CHECK(*a.destination == Vec3(1.0f, 2.0f, 3.0f));
    CHECK(loaded.get<NavLink>(entity).kind == NavLinkKind::Door);
    CHECK(loaded.get<NavObstacle>(entity).shape == NavObstacleShape::Cylinder);
    CHECK(loaded.get<NavObstacle>(entity).carve);
    CHECK(loaded.get<NavArea>(entity).area == 6);
    CHECK_FALSE(loaded.get<NavArea>(entity).walkable);
}

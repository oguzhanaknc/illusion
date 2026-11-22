#include <illusion/ecs/StructRegistry.hpp>
#include <illusion/nav/NavComponents.hpp>

namespace illusion
{
namespace
{

constexpr std::string_view kRequiresTransform[] = {"illusion.Transform"};
constexpr f64 kMaxAgentType = static_cast<f64>(kMaxNavAgentTypes - 1);
constexpr f64 kMaxArea = static_cast<f64>(kMaxNavAreas - 1);
constexpr f64 kRightAngle = 1.5707963267948966;

void reflectBuildSettings(StructRegistry& structs)
{
    reflectStruct<NavBuildSettings>("illusion.NavBuildSettings", 1, structs)
        .meta({.displayName = "Navmesh build settings", .docsAnchor = "manual/navigation.md#settings"})
        .field("cellSize", &NavBuildSettings::cellSize,
               {.description = "Horizontal voxel size; smaller follows walls closer and bakes slower.",
                .unit = Unit::Meters,
                .min = 0.01})
        .field("cellHeight", &NavBuildSettings::cellHeight,
               {.description = "Vertical voxel size.", .unit = Unit::Meters, .min = 0.01})
        .field("tileSize", &NavBuildSettings::tileSize,
               {.description = "Tile width in cells.", .min = 16.0, .max = 1024.0})
        .field("minRegionArea", &NavBuildSettings::minRegionArea,
               {.description = "Islands smaller than this (m²) are dropped.", .min = 0.0, .advanced = true})
        .field("mergeRegionArea", &NavBuildSettings::mergeRegionArea,
               {.description = "Regions smaller than this (m²) merge into their neighbours.",
                .min = 0.0,
                .advanced = true})
        .field("maxEdgeLength", &NavBuildSettings::maxEdgeLength,
               {.description = "Longest polygon edge along the border; 0 for no limit.",
                .unit = Unit::Meters,
                .min = 0.0,
                .advanced = true})
        .field("maxSimplificationError", &NavBuildSettings::maxSimplificationError,
               {.description = "Largest distance of a simplified border from the voxels, in cells.",
                .min = 0.0,
                .advanced = true})
        .field("detailSampleDistance", &NavBuildSettings::detailSampleDistance,
               {.description = "Spacing of the detail mesh's height samples in cells; 0 for none.",
                .min = 0.0,
                .advanced = true})
        .field("detailSampleMaxError", &NavBuildSettings::detailSampleMaxError,
               {.description = "Largest height error of the detail mesh, in cells.",
                .min = 0.0,
                .advanced = true});
}

} // namespace

void registerNavComponents(ComponentRegistry& registry)
{
    reflectBuildSettings(registry.structs());

    reflect<NavMeshSurface>("illusion.NavMeshSurface", 1, registry)
        .meta({.displayName = "Navmesh surface",
               .category = "Navigation",
               .icon = "map",
               .description = "Bakes the walkable surface of the colliders inside a box for one agent type.",
               .docsAnchor = "manual/navigation.md#surfaces",
               .requiredComponents = kRequiresTransform})
        .field("size", &NavMeshSurface::size,
               {.description = "Full size of the box around the Entity's position (rotation ignored).",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("agentType", &NavMeshSurface::agentType,
               {.description = "The agents that walk this navmesh.", .min = 0.0, .max = kMaxAgentType})
        .field(
            "agentRadius", &NavMeshSurface::agentRadius,
            {.description = "Clearance from walls.", .category = "Agent", .unit = Unit::Meters, .min = 0.0})
        .field("agentHeight", &NavMeshSurface::agentHeight,
               {.description = "Headroom needed.", .category = "Agent", .unit = Unit::Meters, .min = 0.0})
        .field(
            "agentMaxClimb", &NavMeshSurface::agentMaxClimb,
            {.description = "Highest step walked up.", .category = "Agent", .unit = Unit::Meters, .min = 0.0})
        .field("agentMaxSlope", &NavMeshSurface::agentMaxSlope,
               {.description = "Steepest walkable slope.",
                .category = "Agent",
                .unit = Unit::Radians,
                .displayUnit = Unit::Degrees,
                .min = 0.0,
                .max = kRightAngle,
                .hint = FieldHint::Angle})
        .field("build", &NavMeshSurface::build, {.description = "Voxel and simplification settings."})
        .field("layerMask", &NavMeshSurface::layerMask,
               {.description = "Physics layers of the colliders it reads.", .hint = FieldHint::LayerMask})
        .field("areaCosts", &NavMeshSurface::areaCosts,
               {.description = "Cost of walking one metre on each area (at least 1; higher is avoided more).",
                .min = 1.0,
                .advanced = true})
        .field("data", &NavMeshSurface::data,
               {.description = "The baked .navmesh file; none bakes it when the World loads.",
                .assetKind = AssetKind::NavMesh})
        .field("rebuildAtRuntime", &NavMeshSurface::rebuildAtRuntime,
               {.description = "Rebuild the tiles of sources that change while the game runs."});

    reflect<NavArea>("illusion.NavArea", 1, registry)
        .meta(
            {.displayName = "Navmesh area",
             .category = "Navigation",
             .icon = "texture",
             .description = "Marks the Entity's Collider, or a box around it, as an area or as not walkable.",
             .docsAnchor = "manual/navigation.md#areas",
             .requiredComponents = kRequiresTransform})
        .field("area", &NavArea::area, {.description = "Area type.", .min = 0.0, .max = kMaxArea})
        .field("walkable", &NavArea::walkable, {.description = "Off cuts it out of the navmesh."})
        .field("volume", &NavArea::volume,
               {.description = "Use a box of the size below instead of the Collider (rotation ignored)."})
        .field("size", &NavArea::size,
               {.description = "Volume: full size of the box.", .unit = Unit::Meters, .min = 0.0});

    reflect<NavLink>("illusion.NavLink", 1, registry)
        .meta({.displayName = "Navmesh link",
               .category = "Navigation",
               .icon = "conversion_path",
               .description = "An off-mesh link from the Entity's position to a point near it: a jump, a "
                              "ladder, a drop or a door.",
               .docsAnchor = "manual/navigation.md#links",
               .requiredComponents = kRequiresTransform})
        .field("end", &NavLink::end,
               {.description = "End point in the Entity's local space.", .unit = Unit::Meters})
        .field("kind", &NavLink::kind, {.description = "How agents cross it."})
        .field("bidirectional", &NavLink::bidirectional,
               {.description = "Can be crossed from the end back to the start."})
        .field("radius", &NavLink::radius,
               {.description = "Distance within which both ends snap to the navmesh.",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("height", &NavLink::height,
               {.description = "Jump: height of the parabola above the higher end.",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("seconds", &NavLink::seconds,
               {.description = "Time to cross; 0 derives it from the agent's speed.",
                .unit = Unit::Seconds,
                .min = 0.0})
        .field("area", &NavLink::area, {.description = "Area type.", .min = 0.0, .max = kMaxArea})
        .field("agentTypeMask", &NavLink::agentTypeMask,
               {.description = "Agent types whose navmesh gets it (bit i = type i).", .advanced = true});

    reflect<NavObstacle>("illusion.NavObstacle", 1, registry)
        .meta({.displayName = "Navmesh obstacle",
               .category = "Navigation",
               .icon = "block",
               .description = "Something agents steer around, optionally cut out of the navmesh.",
               .docsAnchor = "manual/navigation.md#obstacles",
               .requiredComponents = kRequiresTransform})
        .field("shape", &NavObstacle::shape)
        .field("size", &NavObstacle::size,
               {.description = "Box: full size, rotated about +y.", .unit = Unit::Meters, .min = 0.0})
        .field("radius", &NavObstacle::radius,
               {.description = "Cylinder: radius.", .unit = Unit::Meters, .min = 0.0})
        .field("height", &NavObstacle::height,
               {.description = "Cylinder: height.", .unit = Unit::Meters, .min = 0.0})
        .field("carve", &NavObstacle::carve,
               {.description = "Cut it out of the tiles it covers once it has moved and stood still."})
        .field("moveThreshold", &NavObstacle::moveThreshold,
               {.description = "Carve: movement that rebuilds its tiles.", .unit = Unit::Meters, .min = 0.0})
        .field("settleSeconds", &NavObstacle::settleSeconds,
               {.description = "Carve: time it must stand still first.", .unit = Unit::Seconds, .min = 0.0});

    reflect<NavAgent>("illusion.NavAgent", 1, registry)
        .meta(
            {.displayName = "Navmesh agent",
             .category = "Navigation",
             .icon = "person_pin_circle",
             .description = "Walks the navmesh of its agent type to its destination, avoiding other agents.",
             .docsAnchor = "manual/navigation.md#agents",
             .requiredComponents = kRequiresTransform})
        .field("agentType", &NavAgent::agentType,
               {.description = "The navmesh it walks.", .min = 0.0, .max = kMaxAgentType})
        .field("radius", &NavAgent::radius,
               {.description = "Avoidance radius.", .unit = Unit::Meters, .min = 0.0})
        .field("height", &NavAgent::height,
               {.description = "Avoidance height.", .unit = Unit::Meters, .min = 0.0})
        .field("maxSpeed", &NavAgent::maxSpeed, {.unit = Unit::MetersPerSecond, .min = 0.0})
        .field("maxAcceleration", &NavAgent::maxAcceleration, {.description = "In m/s².", .min = 0.0})
        .field("turnSpeed", &NavAgent::turnSpeed,
               {.description = "Turning of the facing in rad/s.", .min = 0.0})
        .field("stoppingDistance", &NavAgent::stoppingDistance,
               {.description = "Distance from the destination that counts as arrived.",
                .unit = Unit::Meters,
                .min = 0.0})
        .field("avoidance", &NavAgent::avoidance, {.description = "Quality of the local avoidance."})
        .field("priority", &NavAgent::priority,
               {.description = "Lower values are avoided by higher ones.",
                .min = 0.0,
                .max = 99.0,
                .advanced = true})
        .field("separationWeight", &NavAgent::separationWeight,
               {.description = "Strength of keeping apart from neighbours.", .min = 0.0, .advanced = true})
        .field("areaMask", &NavAgent::areaMask,
               {.description = "Areas it may walk (bit i = area i).", .advanced = true})
        .field("drive", &NavAgent::drive, {.description = "Who moves it."})
        .field("faceMovement", &NavAgent::faceMovement,
               {.description = "Turn about +y towards the direction of movement."})
        .field("speedParameter", &NavAgent::speedParameter,
               {.description =
                    "Float parameter of the animation graph set to its speed in m/s; empty for none."})
        .field("destination", &NavAgent::destination,
               {.description = "Where it goes in world space; none stands still.", .unit = Unit::Meters});

    reflectRuntime<NavAgentState>("illusion.NavAgentState", {.displayName = "Navmesh agent state"}, registry);
}

} // namespace illusion

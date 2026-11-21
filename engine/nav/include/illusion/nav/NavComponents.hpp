#pragma once

/// @file
/// The Components of navigation (docs/adr/0062-navigation-and-ai.md): the saved NavMeshSurface, NavArea,
/// NavLink, NavObstacle and NavAgent, and the runtime NavAgentState that NavWorld adds.

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Base.hpp>
#include <illusion/core/Math.hpp>
#include <illusion/ecs/Entity.hpp>
#include <illusion/ecs/Reflection.hpp>

#include <array>
#include <optional>
#include <string>

namespace illusion
{

/// Agent types a World can have navmeshes for (NavMeshSurface::agentType, NavAgent::agentType).
inline constexpr u32 kMaxNavAgentTypes = 8;

/// Area types of a navmesh (NavArea::area, NavLink::area); area 0 is the default walkable ground.
inline constexpr u32 kMaxNavAreas = 16;

/// Recast settings of a NavMeshSurface. Cells are the voxels the bake works in.
struct NavBuildSettings
{
    f32 cellSize = 0.15f;              ///< Horizontal voxel size in m.
    f32 cellHeight = 0.1f;             ///< Vertical voxel size in m.
    u32 tileSize = 64;                 ///< Tile width in cells, in [16, 1024].
    f32 minRegionArea = 1.0f;          ///< Smaller islands are dropped, in m².
    f32 mergeRegionArea = 4.0f;        ///< Smaller regions merge into their neighbours, in m².
    f32 maxEdgeLength = 12.0f;         ///< Longest polygon edge along the border in m; 0 for no limit.
    f32 maxSimplificationError = 1.3f; ///< Largest distance of a simplified border from the voxels, in cells.
    f32 detailSampleDistance = 6.0f; ///< Spacing of height samples of the detail mesh, in cells; 0 for none.
    f32 detailSampleMaxError = 1.0f; ///< Largest height error of the detail mesh, in cells.
};

/// One navmesh: the walkable surface for one agent type of the colliders inside a box around the Entity's
/// position (its rotation is ignored). Baked by the editor into NavMeshSurface::data, or at load without it.
struct NavMeshSurface
{
    Vec3 size{100.0f, 20.0f, 100.0f}; ///< Full size of the box in m.
    u32 agentType = 0;                ///< The agents that use it, in [0, kMaxNavAgentTypes).
    f32 agentRadius = 0.3f;           ///< Clearance from walls in m.
    f32 agentHeight = 1.8f;           ///< Headroom needed in m.
    f32 agentMaxClimb = 0.35f;        ///< Highest step walked up in m.
    f32 agentMaxSlope = 0.87266463f;  ///< Steepest walkable slope in radians (50°).
    NavBuildSettings build{};         ///< Voxel and simplification settings.
    u32 layerMask = 0xFFFFFFFFu;      ///< Physics layers of the colliders it reads (bit i = layer i).
    /// Cost multiplier of walking one metre on each area, at least 1 (higher is avoided more).
    std::array<f32, kMaxNavAreas> areaCosts = [] {
        std::array<f32, kMaxNavAreas> costs{};
        costs.fill(1.0f);
        return costs;
    }();
    AssetRef data;                 ///< The baked `.navmesh` file; none bakes it when the World loads.
    bool rebuildAtRuntime = false; ///< Rebuild the tiles of sources that change while the game runs.
};

/// Marks the triangles of the Entity's Collider, or a box around the Entity, as an area or as not walkable.
struct NavArea
{
    u8 area = 0;          ///< Area type in [0, kMaxNavAreas).
    bool walkable = true; ///< False cuts the triangles or the box out of the navmesh.
    bool volume = false;  ///< Use the box size instead of the Collider (rotation ignored).
    Vec3 size{1.0f};      ///< Volume: full size of the box in m.
};

/// How an agent crosses a NavLink.
enum class NavLinkKind : u8
{
    Jump,   ///< Along a parabola.
    Ladder, ///< Vertically, then horizontally.
    Drop,   ///< Straight, usually downwards.
    Door,   ///< Straight; the game opens it on the LinkEntered event.
    Custom, ///< Straight; the game animates it on the events.
};

/// An off-mesh link from the Entity's position to a point near it, baked into the navmesh.
struct NavLink
{
    Vec3 end{0.0f, 0.0f, 2.0f};           ///< End point in the Entity's local space.
    NavLinkKind kind = NavLinkKind::Jump; ///< How it is crossed.
    bool bidirectional = true;            ///< Can be crossed from the end back to the start.
    f32 radius = 0.5f;                    ///< Distance in m within which the ends snap to the navmesh.
    f32 height = 1.0f;                    ///< Jump: height of the parabola above the higher end in m.
    f32 seconds = 0.0f;                   ///< Time to cross; 0 derives it from the agent's speed.
    u8 area = 0;                          ///< Area type in [0, kMaxNavAreas).
    u32 agentTypeMask = 0xFFu;            ///< Agent types whose navmesh gets it (bit i = type i).
};

/// Shape of a NavObstacle.
enum class NavObstacleShape : u8
{
    Box,      ///< NavObstacle::size around the Entity, rotated about +y.
    Cylinder, ///< NavObstacle::radius and NavObstacle::height, upright.
};

/// Something agents avoid: by steering always, and by cutting it out of the navmesh with carve.
struct NavObstacle
{
    NavObstacleShape shape = NavObstacleShape::Box; ///< Its shape.
    Vec3 size{1.0f};                                ///< Box: full size in m.
    f32 radius = 0.5f;                              ///< Cylinder: radius in m.
    f32 height = 1.0f;                              ///< Cylinder: height in m.
    /// Cut it out of the tiles it covers (surfaces with rebuildAtRuntime), once it has moved more than
    /// moveThreshold and stood still for settleSeconds.
    bool carve = false;
    f32 moveThreshold = 0.1f; ///< Carve: movement in m that rebuilds its tiles.
    f32 settleSeconds = 0.5f; ///< Carve: time it must stand still first.
};

/// Who moves a NavAgent.
enum class NavDrive : u8
{
    Character,  ///< Its CharacterController, with the agent's desired velocity.
    Transform,  ///< Navigation, on the navmesh surface (no physics body).
    RootMotion, ///< The Animator's root motion; navigation turns it and writes the speed.
    None,       ///< Game code; navigation plans the path only.
};

/// Quality of the local avoidance of a NavAgent.
enum class NavAvoidance : u8
{
    None,   ///< No avoidance (separation only).
    Low,    ///< Few velocity samples.
    Medium, ///< The default.
    High,   ///< Most samples, for important agents.
};

/// Something that walks the navmesh of its agent type to NavAgent::destination, avoiding other agents.
struct NavAgent
{
    u32 agentType = 0;           ///< Navmesh it walks, in [0, kMaxNavAgentTypes).
    f32 radius = 0.3f;           ///< Avoidance radius in m.
    f32 height = 1.8f;           ///< Avoidance height in m.
    f32 maxSpeed = 3.5f;         ///< In m/s.
    f32 maxAcceleration = 8.0f;  ///< In m/s².
    f32 turnSpeed = 6.2831853f;  ///< Turning of the facing in rad/s.
    f32 stoppingDistance = 0.1f; ///< Distance from the destination that counts as arrived, in m.
    NavAvoidance avoidance = NavAvoidance::Medium; ///< Avoidance quality.
    u8 priority = 50;                              ///< Lower values are avoided by higher ones, in [0, 99].
    f32 separationWeight = 2.0f;                   ///< Strength of keeping apart from neighbours.
    u32 areaMask = 0xFFFFFFFFu;                    ///< Areas it may walk (bit i = area i).
    NavDrive drive = NavDrive::Character;          ///< Who moves it.
    bool faceMovement = true;                      ///< Turn about +y towards the direction of movement.
    /// Float parameter of the Entity's animation graph set to its speed in m/s every update; empty for none.
    std::string speedParameter;
    std::optional<Vec3> destination; ///< Where it goes in world space; none stands still.
};

/// What a NavAgent is doing.
enum class NavAgentStatus : u8
{
    Idle,    ///< No destination.
    Pending, ///< Waiting for a path (or for its navmesh to load).
    Moving,  ///< Following a path.
    Partial, ///< Following a path to the reachable point nearest the destination.
    Arrived, ///< Within stoppingDistance of the destination.
    Failed,  ///< No path: the agent or the destination is not on the navmesh.
    OffMesh, ///< Too far from the navmesh to steer; it replans when it returns.
};

/// The navigation state of a NavAgent, added and kept by NavWorld (runtime only, never saved).
struct NavAgentState
{
    u32 slot = 0;                                 ///< The agent's crowd slot in its NavWorld.
    NavAgentStatus status = NavAgentStatus::Idle; ///< What it is doing.
    Vec3 velocity{0.0f};          ///< Desired velocity of the last update in m/s (horizontal).
    f32 speed = 0.0f;             ///< Length of velocity.
    f32 remainingDistance = 0.0f; ///< Along the path to the destination in m.
    Entity link;                  ///< The NavLink it crosses, or null.
};

/// Registers NavMeshSurface, NavArea, NavLink, NavObstacle and NavAgent (saved, prefixed "illusion.") and
/// NavAgentState (runtime) with @p registry.
void registerNavComponents(ComponentRegistry& registry);

} // namespace illusion

/// Names of NavLinkKind values in scene files.
template <>
struct illusion::EnumReflection<illusion::NavLinkKind>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Jump", 0}, illusion::EnumValue{"Ladder", 1},
                                        illusion::EnumValue{"Drop", 2}, illusion::EnumValue{"Door", 3},
                                        illusion::EnumValue{"Custom", 4}};
};

/// Names of NavObstacleShape values in scene files.
template <>
struct illusion::EnumReflection<illusion::NavObstacleShape>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Box", 0}, illusion::EnumValue{"Cylinder", 1}};
};

/// Names of NavDrive values in scene files.
template <>
struct illusion::EnumReflection<illusion::NavDrive>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Character", 0},
                                        illusion::EnumValue{"Transform", 1},
                                        illusion::EnumValue{"RootMotion", 2}, illusion::EnumValue{"None", 3}};
};

/// Names of NavAvoidance values in scene files.
template <>
struct illusion::EnumReflection<illusion::NavAvoidance>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"None", 0}, illusion::EnumValue{"Low", 1},
                                        illusion::EnumValue{"Medium", 2}, illusion::EnumValue{"High", 3}};
};

/// Names of NavAgentStatus values (runtime state shown by tools).
template <>
struct illusion::EnumReflection<illusion::NavAgentStatus>
{
    /// Value names.
    static constexpr std::array kValues{illusion::EnumValue{"Idle", 0},    illusion::EnumValue{"Pending", 1},
                                        illusion::EnumValue{"Moving", 2},  illusion::EnumValue{"Partial", 3},
                                        illusion::EnumValue{"Arrived", 4}, illusion::EnumValue{"Failed", 5},
                                        illusion::EnumValue{"OffMesh", 6}};
};

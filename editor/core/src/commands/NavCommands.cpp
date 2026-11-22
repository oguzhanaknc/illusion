// The nav.* commands (ADR 0062): bake the navmeshes of the edited scene into `.navmesh` files, report them,
// ask for a path, and show the navigation in the Scene views. The edit World's NavWorld (EditorNavigation)
// is never advanced.

#include "CommandSupport.hpp"

#include <illusion/core/AssetRef.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/core/Profiler.hpp>
#include <illusion/ecs/Hierarchy.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/editor/core/ReferenceIndex.hpp>
#include <illusion/nav/NavBuild.hpp>
#include <illusion/nav/NavMeshData.hpp>
#include <illusion/nav/NavWorld.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace illusion::editor::detail
{

namespace
{

namespace strings = illusion::editor::strings;

constexpr std::string_view kNavigationDirectory = "assets:/navigation/";
constexpr u32 kDefaultCorners = 256;
constexpr f32 kMaxCoordinate = 1.0e6f;

Expected<void, CommandError> checkNavMeshPath(CommandContext& context, std::string_view path,
                                              std::string_view pointer)
{
    std::filesystem::path real;
    if (!path.starts_with(kAssetsRoot) || lowerExtension(path) != kNavMeshExtension ||
        path.size() <= kAssetsRoot.size() + kNavMeshExtension.size() ||
        context.editor().files().resolve(path, real) != FileError::None)
    {
        return unexpected(CommandError::invalid(
            std::string(pointer),
            "must be a .navmesh file in the assets: mount, e.g. \"assets:/navigation/yard.navmesh\""));
    }
    return {};
}

// "assets:/navigation/<name>.navmesh" for a surface called @p name.
std::string defaultNavMeshPath(std::string_view name)
{
    std::string stem;
    for (const char c : name)
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
        {
            stem.push_back(c);
        }
        else if (c >= 'A' && c <= 'Z')
        {
            stem.push_back(static_cast<char>(c - 'A' + 'a'));
        }
        else if (c == ' ' && !stem.empty() && stem.back() != '-')
        {
            stem.push_back('-');
        }
    }
    while (!stem.empty() && stem.back() == '-')
    {
        stem.pop_back();
    }
    if (stem.empty())
    {
        stem = "surface";
    }
    return std::string(kNavigationDirectory) + stem + std::string(kNavMeshExtension);
}

// Every NavMeshSurface of the edit World, in Entity order.
std::vector<Entity> allSurfaces(World& world)
{
    std::vector<Entity> out;
    world.query<const NavMeshSurface>().without<Inactive>().each(
        [&](Entity entity, const NavMeshSurface&) { out.push_back(entity); });
    return out;
}

struct BakeTarget
{
    Entity entity;
    std::string path;
};

class NavBakeTask final : public CommandTask<NavBakeResult>
{
public:
    NavBakeTask(std::vector<BakeTarget> targets, bool force)
        : m_targets(std::move(targets))
        , m_force(force)
    {
    }

    Expected<std::optional<NavBakeResult>, CommandError> update(CommandContext& context,
                                                                CommandProgress& progress) override
    {
        ILL_PROFILE_SCOPE();
        EditorNavigation& navigation = context.editor().navigation();
        if (!m_started)
        {
            m_started = true;
            navigation.forgetMeshes();
        }
        navigation.pump();
        progress.total = static_cast<f64>(m_targets.size());
        while (m_next < m_targets.size())
        {
            progress.done = static_cast<f64>(m_next);
            const BakeTarget& target = m_targets[m_next];
            progress.message = target.path;
            Expected<bool, CommandError> baked = bakeSurface(context, navigation, target);
            if (!baked)
            {
                return unexpected(std::move(baked).error());
            }
            if (!*baked)
            {
                return std::optional<NavBakeResult>{}; // a model of a Collider is still loading
            }
            ++m_next;
        }
        progress.done = progress.total;
        return std::optional<NavBakeResult>(std::move(m_result));
    }

private:
    // Bakes one surface; false while the meshes of its Colliders load.
    Expected<bool, CommandError> bakeSurface(CommandContext& context, EditorNavigation& navigation,
                                             const BakeTarget& target)
    {
        World& world = context.world();
        const std::string label = context.summary(target.entity).path;
        if (!world.isAlive(target.entity) || !world.has<NavMeshSurface>(target.entity))
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "'" + label + "' is no longer a NavMeshSurface"));
        }
        bool waits = false;
        Expected<NavGeometry, std::string> geometry =
            collectNavGeometry(world, target.entity, [&](const AssetRef& mesh, u32 meshIndex) {
                return navigation.findMesh(mesh, meshIndex, waits);
            });
        if (waits)
        {
            return false;
        }
        if (!geometry)
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "cannot bake '" + label + "': " + geometry.error()));
        }
        VirtualFileSystem& files = context.editor().files();
        std::vector<std::byte> before;
        const FileError readError = files.readFile(target.path, before);
        if (readError != FileError::None && readError != FileError::NotFound)
        {
            return unexpected(
                CommandError::make(ErrorKind::IoError, "cannot read '" + target.path + "' (" +
                                                           std::string(fileErrorName(readError)) + ")"));
        }
        std::optional<NavMeshData> data;
        std::vector<NavTileCoord> rebuilt;
        if (readError == FileError::None && !m_force)
        {
            Expected<NavMeshData, NavDataError> existing = decodeNavMeshData(before);
            if (existing)
            {
                Expected<void, std::string> updated =
                    rebakeStaleTiles(*geometry, &navigation.jobs(), *existing, rebuilt);
                if (!updated)
                {
                    return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                         "cannot bake '" + label + "': " + updated.error()));
                }
                data = std::move(*existing);
            }
        }
        if (!data)
        {
            Expected<NavMeshData, std::string> built = bakeNavMesh(*geometry, &navigation.jobs());
            if (!built)
            {
                return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                     "cannot bake '" + label + "': " + built.error()));
            }
            data = std::move(*built);
            rebuilt.clear();
            for (const NavTileData& tile : data->tiles)
            {
                rebuilt.push_back(NavTileCoord{.x = tile.x, .z = tile.z});
            }
        }
        const std::vector<std::byte> bytes = encodeNavMeshData(*data);
        NavBakeSurface done;
        done.entity = context.summary(target.entity);
        done.path = target.path;
        done.tiles = static_cast<u32>(data->tiles.size());
        done.rebuiltTiles = static_cast<u32>(rebuilt.size());
        done.bytes = static_cast<u32>(bytes.size());
        if (readError != FileError::None || before != bytes)
        {
            Expected<bool, CommandError> written = context.edit().writeFile(target.path, bytes);
            if (!written)
            {
                return unexpected(std::move(written).error());
            }
            done.written = true;
            navigation.reload(target.path);
        }
        const NavMeshSurface& surface = world.get<NavMeshSurface>(target.entity);
        if (surface.data.path() != target.path)
        {
            if (Expected<void, CommandError> set = setData(context, done.entity.id, target.path); !set)
            {
                return unexpected(std::move(set).error());
            }
            done.dataChanged = true;
        }
        m_result.rebuiltTiles += done.rebuiltTiles;
        m_result.written += done.written ? 1u : 0u;
        m_result.surfaces.push_back(std::move(done));
        return true;
    }

    // Sets NavMeshSurface::data through the undoable component.set_field.
    static Expected<void, CommandError> setData(CommandContext& context, const std::string& entityId,
                                                const std::string& path)
    {
        JsonMutDocument doc;
        JsonMutValue params = doc.makeObject();
        params.add("entity", doc.makeString(entityId));
        params.add("type", doc.makeString("illusion.NavMeshSurface"));
        params.add("path", doc.makeString("data"));
        params.add("value", doc.makeString(path));
        doc.setRoot(params);
        std::string text;
        if (!doc.write(text, JsonWriteOptions{.pretty = false}))
        {
            return unexpected(CommandError::make(ErrorKind::Internal, "cannot write the params of a step"));
        }
        const auto parsed = JsonDocument::parse(text);
        CommandOptions options;
        options.origin = context.options().origin;
        options.caller = context.options().caller;
        options.gate = context.options().gate;
        Expected<std::string, CommandError> out =
            context.editor().executeStep("component.set_field", parsed->root(), options);
        if (!out)
        {
            return unexpected(std::move(out).error());
        }
        return {};
    }

    std::vector<BakeTarget> m_targets;
    bool m_force = false;
    bool m_started = false;
    usize m_next = 0;
    NavBakeResult m_result;
};

Expected<std::unique_ptr<CommandTask<NavBakeResult>>, CommandError> navBake(CommandContext& context,
                                                                            const NavBakeParams& params)
{
    if (Expected<void, CommandError> open = requireProject(context); !open)
    {
        return unexpected(std::move(open).error());
    }
    World& world = context.world();
    std::vector<Entity> surfaces;
    if (params.surface)
    {
        Expected<Entity, CommandError> entity = context.entity(*params.surface, "/surface");
        if (!entity)
        {
            return unexpected(std::move(entity).error());
        }
        if (!world.has<NavMeshSurface>(*entity))
        {
            return unexpected(CommandError::invalid("/surface", "the entity has no illusion.NavMeshSurface"));
        }
        surfaces.push_back(*entity);
    }
    else
    {
        if (params.path)
        {
            return unexpected(CommandError::invalid("/path", "needs a surface"));
        }
        surfaces = allSurfaces(world);
    }
    if (params.path)
    {
        if (Expected<void, CommandError> valid = checkNavMeshPath(context, *params.path, "/path"); !valid)
        {
            return unexpected(std::move(valid).error());
        }
    }
    std::vector<BakeTarget> targets;
    for (const Entity entity : surfaces)
    {
        std::string path =
            params.path ? *params.path : std::string(world.get<NavMeshSurface>(entity).data.path());
        if (path.empty())
        {
            path = defaultNavMeshPath(context.summary(entity).name);
        }
        if (Expected<void, CommandError> valid = checkNavMeshPath(context, path, "/surface"); !valid)
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "the data of '" + context.summary(entity).path +
                                                     "' is not a .navmesh file in the assets: mount"));
        }
        if (std::ranges::any_of(targets, [&](const BakeTarget& other) { return other.path == path; }))
        {
            return unexpected(
                CommandError::make(ErrorKind::PreconditionFailed,
                                   "two surfaces would write '" + path + "'; give each its own data file"));
        }
        targets.push_back(BakeTarget{.entity = entity, .path = std::move(path)});
    }
    return std::unique_ptr<CommandTask<NavBakeResult>>(
        std::make_unique<NavBakeTask>(std::move(targets), params.force.value_or(false)));
}

Expected<NavStatusResult, CommandError> navStatus(CommandContext& context, const NavStatusParams& params)
{
    EditorNavigation& navigation = context.editor().navigation();
    if (params.wait.value_or(true))
    {
        navigation.waitIdle();
    }
    else
    {
        navigation.update();
    }
    NavStatusResult result;
    const NavWorld& nav = navigation.world();
    for (const Entity entity : allSurfaces(context.world()))
    {
        const NavMeshSurface& surface = context.world().get<NavMeshSurface>(entity);
        result.surfaces.push_back(NavStatusSurface{.entity = context.summary(entity),
                                                   .agentType = surface.agentType,
                                                   .data = std::string(surface.data.path()),
                                                   .ready = nav.isReady(surface.agentType)});
    }
    const NavStatistics& statistics = nav.statistics();
    result.tiles = statistics.tiles;
    result.polygons = statistics.polygons;
    result.pendingTiles = statistics.pendingTiles;
    result.staleTiles = statistics.staleTiles;
    result.failures = statistics.failures;
    return result;
}

bool finite(const Vec3& point) noexcept
{
    return std::abs(point.x) <= kMaxCoordinate && std::abs(point.y) <= kMaxCoordinate &&
           std::abs(point.z) <= kMaxCoordinate;
}

Expected<NavRouteResult, CommandError> navPath(CommandContext& context, const NavPathParams& params)
{
    if (!finite(params.from))
    {
        return unexpected(CommandError::invalid("/from", "must be finite coordinates within 1,000,000 m"));
    }
    if (!finite(params.to))
    {
        return unexpected(CommandError::invalid("/to", "must be finite coordinates within 1,000,000 m"));
    }
    const u32 agentType = params.agentType.value_or(0);
    if (agentType >= kMaxNavAgentTypes)
    {
        return unexpected(CommandError::invalid("/agentType", "must be between 0 and 7"));
    }
    const u32 maxCorners = params.maxCorners.value_or(kDefaultCorners);
    if (maxCorners < 2 || maxCorners > 1024)
    {
        return unexpected(CommandError::invalid("/maxCorners", "must be between 2 and 1024"));
    }
    EditorNavigation& navigation = context.editor().navigation();
    navigation.update();
    if (!navigation.world().isReady(agentType))
    {
        navigation.waitIdle();
    }
    NavWorld& nav = navigation.world();
    const NavQueryFilter filter{.agentType = agentType,
                                .areaMask = params.areaMask.value_or(0xFFFFFFFFu),
                                .searchExtents = Vec3(2.0f, 4.0f, 2.0f)};
    const std::array<std::pair<const Vec3*, const char*>, 2> ends{
        {{&params.from, "/from"}, {&params.to, "/to"}}};
    for (const auto& [point, pointer] : ends)
    {
        const Expected<Vec3, NavError> nearest = nav.nearestPoint(*point, filter);
        if (nearest)
        {
            continue;
        }
        if (nearest.error() == NavError::NoNavMesh)
        {
            return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                                 "no navmesh of agent type " + std::to_string(agentType) +
                                                     " (add a NavMeshSurface and bake it)"));
        }
        return unexpected(
            CommandError::invalid(pointer, "is not within 2 m (4 m up and down) of the navmesh"));
    }
    std::vector<Vec3> corners(1024);
    const Expected<NavPathResult, NavError> path = nav.findPath(params.from, params.to, corners, filter);
    if (!path)
    {
        return unexpected(CommandError::make(ErrorKind::PreconditionFailed,
                                             "no path: " + std::string(navErrorName(path.error()))));
    }
    NavRouteResult result;
    result.partial = path->partial;
    result.length = path->length;
    result.total = path->total;
    corners.resize(std::min<usize>(path->count, maxCorners));
    result.corners = std::move(corners);
    return result;
}

NavDebugResult debugState(const EditorNavigation& navigation)
{
    const NavDebugOptions& options = navigation.debugOptions;
    return NavDebugResult{.enabled = navigation.debugEnabled,
                          .navMesh = options.navMesh,
                          .tiles = options.tiles,
                          .links = options.links,
                          .agents = options.agents,
                          .paths = options.paths,
                          .obstacles = options.obstacles};
}

Expected<NavDebugResult, CommandError> navDebug(CommandContext& context, const NavDebugParams& params)
{
    EditorNavigation& navigation = context.editor().navigation();
    if (context.dryRun())
    {
        return debugState(navigation);
    }
    const auto set = [](bool& target, const std::optional<bool>& value) {
        if (value)
        {
            target = *value;
        }
    };
    set(navigation.debugEnabled, params.enabled);
    set(navigation.debugOptions.navMesh, params.navMesh);
    set(navigation.debugOptions.tiles, params.tiles);
    set(navigation.debugOptions.links, params.links);
    set(navigation.debugOptions.agents, params.agents);
    set(navigation.debugOptions.paths, params.paths);
    set(navigation.debugOptions.obstacles, params.obstacles);
    return debugState(navigation);
}

} // namespace

void registerNavCommands(CommandRegistry& registry)
{
    registry.addAsync<&navBake>({
        .name = "nav.bake",
        .title = strings::kCommandNavBakeTitle,
        .description = strings::kCommandNavBakeDescription,
        .agentSummary =
            "Asynchronous: bakes the navmesh of one NavMeshSurface (surface) or of every surface of the "
            "scene into its .navmesh file and sets NavMeshSurface.data to it. Only the tiles whose "
            "input changed since the file was baked are built again (force builds them all); a file whose "
            "bytes do not change is not written, so baking twice changes nothing the second time. A "
            "surface without data gets assets:/navigation/<entity name>.navmesh, or path. The file is "
            "written first and its data field set through an undoable step; undoing it keeps the file. "
            "Colliders with mesh assets load first, which can take several frames. Result: per surface the "
            "file, its tiles, the rebuilt tiles and whether it was written. Example: "
            "{\"surface\": \"/Nav Surface\"} or {}.",
        .category = "Navigation",
        .icon = "route",
        .permission = PermissionClass::Files,
        .palette = true,
    });
    registry.add<&navStatus>({
        .name = "nav.status",
        .title = strings::kCommandNavStatusTitle,
        .description = strings::kCommandNavStatusDescription,
        .agentSummary =
            "Lists the NavMeshSurfaces of the scene with their agent type, data file and whether their "
            "navmesh is complete, and counts tiles, polygons, tiles waiting to be baked, stale tiles (the "
            "input changed since the file was baked) and failed surfaces. The editor loads each surface's "
            "file or bakes it in memory first; wait false skips waiting for that. Example: {}.",
        .category = "Navigation",
        .icon = "monitoring",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
    registry.add<&navPath>({
        .name = "nav.path",
        .title = strings::kCommandNavPathTitle,
        .description = strings::kCommandNavPathDescription,
        .agentSummary =
            "Finds the shortest path over the navmesh of the edited scene between two world-space points "
            "(metres) for an agent type and returns its corner points, its length and whether it is partial "
            "(it ends at the reachable point nearest the destination). A point must be within 2 m (4 m up "
            "and down) of the navmesh; the error names /from or /to. Use it to check that a level is "
            "connected after nav.bake. Example: {\"from\": [24, 0, -6], \"to\": [35, 1, 5], "
            "\"agentType\": 1}.",
        .category = "Navigation",
        .icon = "alt_route",
        .flags = {.readOnly = true, .idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Read,
    });
    registry.add<&navDebug>({
        .name = "nav.debug",
        .title = strings::kCommandNavDebugTitle,
        .description = strings::kCommandNavDebugDescription,
        .agentSummary =
            "Shows or hides the navigation in the Scene views: with enabled true they draw the navmesh "
            "polygons (navMesh, coloured by area), tile borders (tiles), NavLinks (links), agents, "
            "corridors (paths) and NavObstacles (obstacles); the other flags choose what. Omitted values "
            "keep their state; the result says what is drawn. Not undoable: it is editor state. Example: "
            "{\"enabled\": true, \"tiles\": true}.",
        .category = "Navigation",
        .icon = "visibility",
        .flags = {.idempotent = true},
        .mode = CommandMode::Edit,
        .permission = PermissionClass::Edit,
        .palette = true,
    });
}

} // namespace illusion::editor::detail

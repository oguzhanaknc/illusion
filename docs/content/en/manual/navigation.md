# Navigation

Navigation lets a character find a path over the ground. illusion bakes a navigation mesh (navmesh) from
the colliders of a level with Recast Navigation ([ADR 0062](../architecture/decisions.md)) and stores it
in a `.navmesh` file.

!!! note "What exists today"
    This page describes the saved Components, the bake, and `NavWorld` with its run-time navmeshes, queries
    and crowd. `NavService` for game modules (NAV-1d) and the editor commands with their documentation
    (NAV-1e) arrive in the next packages.

## Surfaces

A `NavMeshSurface` is one navmesh: the walkable ground for one agent type, built from the colliders inside
a box around the Entity's position (the rotation is ignored).

| Field | Meaning |
| --- | --- |
| `size` | Full size of the box in m |
| `agentType` | The agents that use it, in [0, 8) |
| `agentRadius` | Clearance from walls in m |
| `agentHeight` | Headroom needed in m |
| `agentMaxClimb` | Highest step walked up in m |
| `agentMaxSlope` | Steepest walkable slope in radians (shown in degrees, default 50°) |
| `build` | The bake settings, below |
| `layerMask` | Physics layers of the colliders it reads (bit i = layer i) |
| `areaCosts` | Cost multiplier of walking one metre on each of the 16 areas, at least 1 |
| `data` | The baked `.navmesh` file; none bakes it when the World loads |
| `rebuildAtRuntime` | Rebuild the tiles of sources that change while the game runs |

The bake collects the triangles of every Collider inside the box whose layer is in `layerMask` and whose
body is Static or Kinematic (or that has no RigidBody). Dynamic bodies are not ground. Entities with the
`Inactive` tag are skipped. Convex hull and Mesh colliders need their Mesh to be loaded; the bake counts
the ones it had to skip.

## Settings

`NavMeshSurface::build` holds the Recast settings. Cells are the voxels the bake works in.

| Field | Default | Meaning |
| --- | --- | --- |
| `cellSize` | 0.15 | Horizontal voxel size in m |
| `cellHeight` | 0.1 | Vertical voxel size in m |
| `tileSize` | 64 | Tile width in cells, in [16, 1024] |
| `minRegionArea` | 1 | Smaller islands are dropped, in m² |
| `mergeRegionArea` | 4 | Smaller regions merge into their neighbours, in m² |
| `maxEdgeLength` | 12 | Longest polygon edge along the border in m; 0 for no limit |
| `maxSimplificationError` | 1.3 | Largest distance of a simplified border from the voxels, in cells |
| `detailSampleDistance` | 6 | Spacing of height samples of the detail mesh, in cells; 0 for none |
| `detailSampleMaxError` | 1 | Largest height error of the detail mesh, in cells |

Smaller cells follow the geometry more closely and cost more time and memory. A value out of range makes
the bake fail and name the setting.

## Areas

A `NavArea` marks the triangles of the Entity's Collider, or a box around the Entity, with an area type in
[0, 16). Area 0 is the default walkable ground. Agents prefer cheap areas (`areaCosts`) and can be limited
to some of them (`NavAgent::areaMask`).

| Field | Meaning |
| --- | --- |
| `area` | Area type |
| `walkable` | False cuts the triangles or the box out of the navmesh |
| `volume` | Use the box `size` (full size in m, rotation about +y only) instead of the Collider |

## Links

A `NavLink` is an off-mesh connection from the Entity's position to `end` (in the Entity's local space),
baked into the navmesh for the surfaces whose agent type is in `agentTypeMask`.

| Field | Meaning |
| --- | --- |
| `kind` | `Jump` (a parabola), `Ladder`, `Drop`, `Door` or `Custom` |
| `bidirectional` | Can be crossed from the end back to the start |
| `radius` | Distance in m within which the ends snap to the navmesh |
| `height` | Jump: height of the parabola above the higher end in m |
| `seconds` | Time to cross; 0 derives it from the agent's speed |
| `area` | Area type of the link |
| `agentTypeMask` | Agent types whose navmesh gets it (bit i = type i) |

## Obstacles

A `NavObstacle` is something agents avoid. It is a box (`size` in m) or an upright cylinder (`radius` and
`height` in m). Setting `carve` also cuts it out of the tiles it covers on surfaces with
`rebuildAtRuntime`, once it has moved more than `moveThreshold` (m) and stood still for `settleSeconds`.
The bake treats a carving obstacle like a non-walkable area volume.

## Agents

A `NavAgent` walks the navmesh of its `agentType` to `destination`. `NavWorld` gives it a crowd slot and
adds the runtime state `NavAgentState` at its first update.

| Field | Meaning |
| --- | --- |
| `radius`, `height` | Avoidance size in m |
| `maxSpeed` | In m/s |
| `maxAcceleration` | In m/s² |
| `turnSpeed` | Turning of the facing in rad/s |
| `stoppingDistance` | Distance from the destination that counts as arrived, in m |
| `avoidance` | `None`, `Low`, `Medium` or `High` |
| `priority` | In [0, 99]; lower values are avoided by higher ones |
| `separationWeight` | Strength of keeping apart from neighbours |
| `areaMask` | Areas it may walk (bit i = area i) |
| `drive` | Who moves it: `Character`, `Transform`, `RootMotion` or `None` |
| `faceMovement` | Turn about +y towards the direction of movement |
| `speedParameter` | Float parameter of the animation graph set to its speed; empty for none |
| `destination` | Where it goes in world space; none stands still |

### Drives

`drive` says who moves the agent:

| Drive | Who moves it |
| --- | --- |
| `Character` | Its `CharacterController`. Navigation writes only `NavAgentState::velocity`; the game session gives its horizontal part to physics and keeps the vertical velocity. The next update reads where physics put the character, so it keeps collisions, steps and slopes. |
| `Transform` | Navigation: it moves the Transform on the navmesh surface. For crowds without physics. |
| `RootMotion` | The Animator's root motion. Navigation turns the Entity towards its path and sets its speed. |
| `None` | Game code. Navigation keeps the path, the status and the desired velocity. |

Root motion translation is not applied to `Character` and `Transform` agents. `faceMovement` turns the
Entity's +z axis towards the movement at `turnSpeed`; `RootMotion` agents always turn.

### The crowd

Each update with `advance` runs these phases:

1. Slots and positions are synchronised on the World's thread; changed destinations request a path.
2. At most `maxPathRequestsPerUpdate` path searches start, in slot order. Each runs as a Job with
   `maxPathIterations` A* iterations per update until it finishes.
3. A proximity grid of the agents' positions at the start of the update is built.
4. Jobs per agent: the path corridor follows the agent, then corners, walls nearby, up to `maxNeighbours`
   neighbours, the desired velocity, separation and obstacle avoidance.
5. Jobs per agent: acceleration, movement and separation (`Transform`), link traversal.
6. `NavAgentState`, Transforms and events are written on the World's thread in Entity order.

Each Job phase reads the state at the start of the update and writes only its own agent, so a run with
fixed steps gives the same result for any number of workers. An agent avoids the neighbours whose
`priority` is not higher than its own, and every `NavObstacle` near it. Entities with the `Inactive` tag
keep their slot and do not move. Without `advance` (the editor outside Play mode) agents get slots and
states but do not move.

A path that cannot reach the destination is partial: the agent walks to its end and its status is
`Partial`. An agent farther than its radius plus 1 m from the navmesh is `OffMesh` and searches its path
again when it comes back. An agent pushed away after it arrived walks back.

### Crossing links

When an agent's path reaches a `NavLink`, navigation moves its Transform along the link (physics teleports
a character along): a parabola whose top is `height` above the higher end for `Jump`, vertical then
horizontal for `Ladder` (the climb happens at the lower end), straight for the others. Crossing takes
`seconds`, or the link's length over `maxSpeed` when it is 0. Meanwhile `NavAgentState::link` is the link's
Entity.

### Status and events

| `NavAgentStatus` | Meaning |
| --- | --- |
| `Idle` | No destination |
| `Pending` | Waiting for a path, or for its navmesh to load |
| `Moving` | Following a path |
| `Partial` | Following a path to the reachable point nearest the destination |
| `Arrived` | Within `stoppingDistance` of the destination |
| `Failed` | No path: the destination is not on the navmesh or nothing leads nearer to it |
| `OffMesh` | Too far from the navmesh to steer; it searches again when it returns |

`events()` returns the events of the last update in Entity order: `Arrived`, `PathFailed`, `LinkEntered`
and `LinkExited` (with the link's Entity and kind). At most `maxEvents` are kept; the others are counted in
`NavStatistics::droppedEvents`.

```cpp
navigation.setDestination(guard, door); // saved in NavAgent::destination
navigation.update(step, &jobs, true);
for (const illusion::NavEvent& event : navigation.events())
{
    if (event.type == illusion::NavEventType::LinkEntered && event.kind == illusion::NavLinkKind::Door)
    {
        openDoor(event.link);
    }
}
navigation.stop(guard);             // slows to a stop
navigation.warp(guard, spawnPoint); // moves at once and searches the path again
```

`findSlot()` returns an agent's slot and `pathCorners(slot)` the corners ahead of it. A saved game restores
`destination` only: the agent searches its path again after the load.

### In a game session

`GameSession` owns a `NavWorld` (`navigation()`). Its Navigation step runs after Saves and before
Animation: `NavWorld::update()` (with fixed steps it first waits for bakes and loads), then the session
gives `Character` agents their velocities and sets the `speedParameter`s. The crowd advances only while the
session is started.

### From a module

A game module reaches navigation through [`NavService`](../reference/scripting-api.md#navservice)
(`USES nav`): the same queries as `NavWorld`, `setDestination()`, `stop()` and `warp()` for an agent's
Entity, its `status()` and `remainingDistance()`, and `pollEvents()` for the events of the last update.
Every call returns an `Expected` with a `NavError` instead of asserting.

```cpp
#include <illusion/core/Module.hpp>
#include <illusion/nav/NavService.hpp>

#include <array>

void update(illusion::ModuleContext& context, illusion::f64)
{
    illusion::NavService* nav = context.service<illusion::NavService>();
    if (nav == nullptr)
    {
        return; // this World was attached without navigation
    }
    if (needsOrders)
    {
        const illusion::Expected<void, illusion::NavError> sent = nav->setDestination(guard, door);
        if (!sent)
        {
            ILL_LOG_INFO("Game", "guard: {}", illusion::navErrorName(sent.error()));
        }
        needsOrders = false;
    }
    if (nav->status(guard).valueOr(illusion::NavAgentStatus::Idle) == illusion::NavAgentStatus::Arrived)
    {
        openDoor();
    }
    std::array<illusion::NavEvent, 8> events{};
    const illusion::u32 count = nav->pollEvents(events);
    for (illusion::u32 i = 0; i < count && i < events.size(); ++i)
    {
        if (events[i].type == illusion::NavEventType::PathFailed)
        {
            giveUp(events[i].agent);
        }
    }
}
```

## Baking

The bake is two pure functions of `illusion/nav/NavBuild.hpp`:

```cpp
auto geometry = illusion::collectNavGeometry(world, surface, findMesh);
if (geometry)
{
    auto data = illusion::bakeNavMesh(*geometry, &jobs);
    if (data)
    {
        writeFile("assets:/navigation/forest.navmesh", illusion::encodeNavMeshData(*data));
    }
}
```

`collectNavGeometry()` gathers triangles, areas, area volumes, links and carving obstacles in world space.
`findMesh` returns the Mesh of a convex hull or Mesh Collider, or null when it has not loaded.

`bakeNavMesh()` builds the surface as a grid of tiles, `tileSize` cells wide, one Job per tile. The bytes
are the same for any number of workers, and for the same input they are the same on every run.

Every tile stores a hash of the input that overlaps it and its border, and of the settings. After a change,
`rebakeStaleTiles()` compares the hashes and builds again only the tiles whose hash differs (all of them
when the grid or the settings changed). Moving one box changes only the tiles it touches.

## The `.navmesh` file

`encodeNavMeshData()` writes the file and `decodeNavMeshData()` reads it. It holds a magic, the container
version (`kNavMeshFormatVersion`, now 1) and the Detour tile version, the agent profile and settings, the
tile grid with each tile's data and hash, the baked links, and an xxhash64 checksum. Reading fails with one
of these errors:

| Error | Meaning |
| --- | --- |
| `NotNavMesh` | The magic is missing |
| `Version` | Another container version |
| `DetourVersion` | Baked with another Detour tile version: bake it again |
| `Truncated` | The file ends early or a size field is out of range |
| `Checksum` | The checksum does not match |
| `Invalid` | A value is out of range (tile coordinates, settings, a duplicate tile) |

## In the editor

The editor bakes and inspects navmeshes with four [Editor commands](../reference/editor-commands.md),
which humans (the command palette) and AI agents share. The editor keeps a `NavWorld` over the edited scene
that loads or bakes each surface's navmesh and is never advanced: its agents do not move.

`nav.bake` bakes one `NavMeshSurface` (`surface`) or every surface of the scene, and sets
`NavMeshSurface::data` to the `.navmesh` file it wrote. A surface without `data` gets
`assets:/navigation/<entity name>.navmesh`, or the `path` of the call. The command is asynchronous, because
the models of Mesh Colliders load first.

```json
{"method":"nav.bake","params":{"surface":"/Nav Surface"}}
```

- **Changed tiles only.** If the file exists, only the tiles whose input hash differs are built again
  (`rebakeStaleTiles()`); `force` builds them all. The result lists the tiles and the rebuilt tiles of each
  surface.
- **Baking twice changes nothing.** The bytes are the same for the same input. A file whose bytes do not
  change is not written, and the second bake rebuilds no tile.
- **Undo.** The file is written first, then `data` is set by an undoable step. Undoing it restores `data`
  and keeps the file.
- **Errors.** A target that is not a `NavMeshSurface`, a `path` that is not a `.navmesh` file in `assets:/`,
  or two surfaces that would write the same file, fail before anything is baked.

`nav.status` lists the surfaces with their agent type, data file and whether their navmesh is complete, and
counts tiles, polygons, pending and stale tiles and failed surfaces. `nav.path` finds a path between two
points for an agent type, and returns its corner points, its length and whether it is partial. A point more
than 2 m from the navmesh (4 m up and down) fails with an error that names `/from` or `/to`. Use it to check
that a level is connected.

```json
{"method":"nav.path","params":{"from":[24,0,-6],"to":[35,1,5],"agentType":1}}
```

`nav.debug` shows the navigation in the Scene views through `drawDebug()`: the navmesh polygons coloured by
area (`navMesh`), tile borders (`tiles`), links, agents, paths and obstacles. It is editor state, not
undoable, and the flags you leave out keep their value.

```json
{"method":"nav.debug","params":{"enabled":true,"tiles":true}}
```

The `showcase` project bakes its navmesh with a `nav.bake` step of `build.jsonl`, so replaying the file
regenerates `assets/navigation/yard.navmesh`.

## Run time

`NavWorld` (`illusion/nav/NavWorld.hpp`) keeps one tiled navmesh per `NavMeshSurface`. Create it with the
World and the `AssetStreamer`, and call `update()` once per step:

```cpp
illusion::NavWorld navigation(world, &streamer);
while (running)
{
    streamer.update();
    navigation.update(step, &jobs, true);
}
navigation.waitIdle(&jobs);
```

- **Loading.** `update()` loads each surface's `data` through the streamer (decoder `illusion.navmesh`).
- **Baking.** A surface without `data`, or whose file is missing or rejected, is baked instead. A missing
  or rejected file logs one line and counts in `NavStatistics::failures`. Without a streamer, surfaces are
  always baked.
- **Stale tiles.** When the input of a loaded tile no longer matches its hash, the tile is rebuilt if
  `rebuildAtRuntime` is on. Otherwise it is counted in `NavStatistics::staleTiles` and logged once.
- **Changes while running.** With `rebuildAtRuntime`, the tiles touched by a changed Transform, Collider,
  RigidBody, NavArea or NavLink, and by a carving NavObstacle once it has settled, are rebuilt. A change to
  a model file used by a Mesh Collider does not trigger a rebake.
- **`invalidate(bounds)`** marks the tiles that overlap an `Aabb` stale. Use it when the input changed in a
  way the World cannot see.
- **Live reload.** Saving a `.navmesh` file replaces the tiles of its surface.
- **One surface per agent type.** When several surfaces use the same agent type, the one on the lowest Entity
  index wins; the others are logged once and ignored.
- **Builds.** At most `NavWorldDesc::maxTileBuildsInFlight` tiles are built in the background at once.
  Finished tiles are swapped in at the start of an update, in the order the builds started.
- **`waitIdle(&jobs)`** calls `update(0, jobs, false)` until no surface waits for its data or tiles.
  `isReady(agentType)` tells whether a navmesh has every tile.

`statistics()` reports `surfaces`, `tiles`, `polygons`, `pendingTiles`, `staleTiles`, `tileBuilds`,
`failures`, and for the crowd `agents`, `moving`, `pathSearches` and `droppedEvents`.

## Queries

The queries are `const`, run on the World's thread and never allocate. Every query takes a
`NavQueryFilter`:

| Field | Default | Meaning |
| --- | --- | --- |
| `agentType` | 0 | The navmesh to ask |
| `areaMask` | all areas | Areas the query may cross (bit i = area i) |
| `searchExtents` | (2, 4, 2) | Half size in m of the box searched for the polygon of a point |

```cpp
illusion::NavQueryFilter filter;
filter.agentType = 1;
filter.areaMask = ~(1u << 2); // keep out of area 2

std::array<illusion::Vec3, 32> corners;
auto path = navigation.findPath(from, to, corners, filter);
if (path)
{
    // corners[0 .. path->count) is the route; path->total, path->length and path->partial describe all of it
}
else if (path.error() == illusion::NavError::NoPath)
{
    // the points are on the navmesh but nothing reachable is nearer to `to` than `from`
}
```

`findPath()` writes the corner points of the shortest path; the first is the start on the navmesh. When the
destination cannot be reached, the path is partial: it ends at the reachable point nearest the destination
and `partial` is true. `NoPath` is returned only when nothing reachable is nearer the destination than the
start. An output span smaller than the path gives `count` points, while `total` and `length` still describe
the whole path.

```cpp
auto ground = navigation.nearestPoint(position, filter);
auto sight = navigation.raycast(from, to, filter);
if (sight && sight->hit)
{
    // sight->point, sight->normal and sight->fraction describe the wall
}
auto spot = navigation.randomPoint(center, 5.0f, 1234, filter);
```

- `nearestPoint()` returns the navmesh point nearest the position within `searchExtents`.
- `raycast()` walks the navmesh surface and reports the first wall it meets: `hit`, the `point` where the
  walk stopped, the horizontal `normal` of the wall and the `fraction` of the way walked.
- `randomPoint()` returns a navmesh point within the radius that is reachable from the centre. The same seed
  gives the same point.

`areaCosts` and `areaMask` apply at query time: changing `areaCosts` needs no rebuild.

### Errors

| `NavError` | Meaning |
| --- | --- |
| `NoAgent` | The Entity has no `NavAgent` or is not alive |
| `NoNavMesh` | No navmesh of that agent type is loaded yet |
| `OffMesh` | No navmesh polygon within `searchExtents` of a point |
| `NoPath` | The points are on the navmesh but nothing connects them |
| `InvalidValue` | A value is not finite or is out of range (an agent type, a negative radius, an empty span) |

### Debug drawing

`drawDebug()` sends lines to a `NavDebugSink`; the editor's viewport adapts it to its debug draw.

```cpp
struct LineSink : illusion::NavDebugSink
{
    void line(const illusion::Vec3& a, const illusion::Vec3& b, const illusion::Vec4& color) override;
};

illusion::NavDebugOptions options;
options.tiles = true;
options.agentTypeMask = 1u << 1;
LineSink sink;
navigation.drawDebug(options, sink);
```

`agents` draws each agent's circle, height and velocity, and `paths` the corners ahead of it.

## Performance

Measured on the development desktop:

- Baking one 64 x 64-cell tile of a 10,000-triangle scene takes 2.8 ms (Benchmark `nav/bake-tile`, budget
  20 ms).
- Baking 256 m x 256 m, 729 tiles in parallel on the Job System, takes 99 ms (Benchmark `nav/bake-256m`,
  budget 600 ms).
- Finding 1,000 paths takes 13 ms (Benchmark `nav/find-path-1000`: mean length 96 m on 729 tiles with 225
  walls, budget 80 ms).
- Updating a crowd of 500 walking agents takes 0.3 ms with the Job System (Benchmark `nav/crowd-500`,
  budget 2 ms).

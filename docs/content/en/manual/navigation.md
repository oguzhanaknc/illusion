# Navigation

Navigation lets a character find a path over the ground. illusion bakes a navigation mesh (navmesh) from
the colliders of a level with Recast Navigation ([ADR 0062](../architecture/decisions.md)) and stores it
in a `.navmesh` file.

!!! note "What exists today"
    This page describes the saved Components and the bake. Agents that walk the navmesh, path queries, the
    crowd, the scripting API and the editor commands arrive in the next packages: `NavWorld` with queries
    (NAV-1b), the crowd and `NavAgent` movement (NAV-1c), `NavService` for game modules (NAV-1d) and the
    editor commands with their documentation (NAV-1e). Until then you can bake with the C++ functions
    of `illusion/nav/NavBuild.hpp`.

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

A `NavAgent` walks the navmesh of its `agentType` to `destination`. The Component and its runtime state
`NavAgentState` are registered now, but nothing moves agents yet (NAV-1c).

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

## Performance

Measured on the development desktop:

- Baking one 64 x 64-cell tile of a 10,000-triangle scene takes 2.8 ms (Benchmark `nav/bake-tile`, budget
  20 ms).
- Baking 256 m x 256 m, 729 tiles in parallel on the Job System, takes 99 ms (Benchmark `nav/bake-256m`,
  budget 600 ms).

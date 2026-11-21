# 0062. Navigation and AI: tiled Recast navmeshes baked from colliders, a deterministic crowd on the Job System, behaviour trees as `.behavior` assets

- Status: Proposed
- Date: 2026-11-21

## Context

Games need characters that find their way: a creature that wanders a forest, guards that patrol and chase,
crowds that cross a square without walking into each other. The engine has none of it. The brief asks for
navigation meshes baked from colliders and walkable surfaces with Recast, in the editor and at run time for
changing levels, tiled for large worlds; paths and crowds with Detour on the Job System; off-mesh links; a
`NavAgent` that steers CharacterControllers; behaviour trees as saved assets with blackboards; perception;
an `AiService`; `nav.*` and `ai.*` commands; a creature in the tech demo.

What exists and shapes the design:

- **Physics.** `engine/physics` keeps Jolt private and exposes plain Components: `RigidBody` (motion type,
  layer), `Collider` (Box, Sphere, Capsule, Cylinder, ConvexHull, Mesh from `Collider::mesh`),
  `CharacterController` (radius 0.3, height 1.8, step 0.35, slope 50°, moved by
  `PhysicsWorld::setCharacterVelocity()`; a character whose Transform is changed by game code is teleported).
  `PhysicsWorld` is not thread-safe, raycasts included.
- **Session order.** `GameSession::update()`: Input, Modules, Saves, Animation (and root motion, which sets
  the horizontal velocity of a CharacterController), Physics, Hierarchy, UI, Particles and Audio, Resolvers.
  With `fixedStep` every update is one physics step and runs repeat exactly (ADR 0042).
- **Libraries outside the ECS.** `AnimationWorld` and `ParticleWorld` keep large runtime state in their own
  slots, add a small runtime Component, run on the Job System with per-worker scratch and allocate only when
  slots are added or assets load (ADR 0047, ADR 0059).
- **Assets.** Authored data is a JSON file with a format tag, a version and a parser returning `Expected` with
  the JSON path of an error (`.animgraph`, ADR 0060), loaded with `AssetStreamer::loadFile()` and a named
  decoder; binary data has a versioned container with a checksum (slot files, ADR 0058). Exports ship such
  files as they are.
- **Scripting API.** Modules reach engine systems through abstract `*Service` classes in header-only `_api`
  targets (`USES`), poll results and are never called back; errors are `Expected` values, never asserts (ADR
  0041). Each new service raises the module API version.
- **Editor.** Commands are the one API of humans and agents (ADR 0036); work that spans frames is an async
  command; viewport visualisers record `DebugDraw` lines.

### Options considered

**Navmesh generation: Recast/Detour or our own.** Writing voxelization, region partitioning, contour and
polygon building, detail meshes, A* over polygons, path corridors and velocity-obstacle avoidance is years of
work that Recast Navigation (zlib) has shipped in a great many games. Its parts are separable: Recast builds,
Detour queries, DetourCrowd offers both a whole crowd (`dtCrowd`) and its building blocks. We use Recast and
Detour, and build our own crowd from Detour's blocks (below).

**Baked offline, at run time, or both.** Offline bakes cost nothing at load and give reviewable files, but a
level that changes (a door, a collapsed bridge, a placed crate) needs run-time rebuilds; run-time-only bakes
cost seconds at load for a large world. We do both through one function: the editor bakes a `.navmesh` file;
the runtime loads it, bakes what is missing and rebuilds changed tiles when the surface asks for it.

**Tiled or single.** A single navmesh is rebuilt whole for any change and caps the world's size by polygon
reference bits. Tiles (a grid of independently built pieces) make rebuilds local, bakes parallel and worlds
large. DetourTileCache adds compressed layers and fast cylinder/box obstacles, but a second data path; we
rebuild whole tiles from cached input instead and revisit TileCache if carving cost matters.

**Where crowds run.** `dtCrowd::update()` is single-threaded and updates agents in place in an order that
depends on its internal slots. Our crowd runs its phases as Jobs over agents, each phase reading the previous
phase's buffers and writing only its own agent's slot, so the result does not depend on the worker count.

**How a NavAgent moves a CharacterController without fighting physics.** Either navigation integrates
positions and writes Transforms (physics then teleports the character every step and loses collisions,
steps and slopes), or navigation only *steers*: it reads where physics put the character, plans from there,
and hands physics a desired horizontal velocity, like root motion already does. We steer, and write
Transforms only where no physics body moves the agent (plain agents, link traversal, `warp()`).

**Behaviour trees as data or as code.** Code-only trees are flexible but invisible to the editor and agents
and rebuilt per game. Data trees are files an agent can write, validate and diff, with a fixed set of node
types; game-specific leaves become named actions that modules poll and finish, which matches the polled
scripting API. We choose data, with named actions.

**Blackboard types.** Untyped variants or strings cost allocations and validation at run time. A small fixed
set of types (Bool, Int, Float, Vector3, Entity) covers targets, positions, flags and counters, fits a fixed
value slot and is checked when the tree loads.

**Perception.** Engines either run a full sense framework (sight, hearing, damage, team, stimuli ageing) or
leave it to games. Sight (radius, cone, line of sight through physics) and hearing (noises with a radius) with
a short memory cover most games; the rest is data a module writes to the blackboard.

**Memory.** Detour allocates tiles when they are added and Recast allocates during builds; both accept a
custom allocator. Crowd state per agent (corridor, boundary, neighbours) is fixed-size.

## Decision

- **Libraries.** `engine/nav` (`illusion::nav`) holds the navigation Components, the `.navmesh` format, the
  bake, `NavWorld` (surfaces, tiles, queries, crowd) and `NavService` with its adapter `NavWorldService`.
  `engine/ai` (`illusion::ai`) holds blackboards, behaviour trees, perception, the AI Components, `AiWorld`
  and `AiService` with `AiWorldService`; it links `illusion::nav` and `illusion::physics`. `illusion::nav`
  links `illusion::core`, `illusion::assets` and `illusion::physics_api` (Collider, RigidBody,
  CharacterController types only), never `PhysicsWorld`, the renderer or animation. Header-only
  `illusion::nav_api` and `illusion::ai_api` serve modules (`USES nav`, `USES ai`), exported in the SDK.
- **Dependency.** Recast Navigation **v1.6.0** (zlib), pinned in `cmake/Dependencies.cmake` with its SHA-256,
  built as our own minimal static targets of `Recast`, `Detour`, `DetourCrowd` and `DebugUtils` (no demo, no
  tests, no `DetourTileCache`), private to `engine/nav` like Jolt to `engine/physics`. Compiled with
  `DT_POLYREF64` (a polygon reference is 64 bits: room for large tiled worlds); no Recast or Detour header
  appears in a public header. `rcAllocSetCustom()`/`dtAllocSetCustom()` route through the engine heap.
- **Surfaces.** A saved `NavMeshSurface` on an Entity defines one navmesh: an axis-aligned box (`size` around
  the Entity's position; rotation ignored), one agent profile (`agentType` in [0, `kMaxNavAgentTypes` = 8),
  radius, height, climb, slope; defaults equal the CharacterController's), the Recast settings
  (`NavBuildSettings`: cell size and height, tile size in cells, region and simplification values, detail
  sampling, all in metres or cells as named), the physics `layerMask` of the colliders it reads, area costs,
  `data` (an optional `.navmesh` asset) and `rebuildAtRuntime`. One surface per agent type per World; further
  ones are reported once and ignored.
- **Bake input.** The geometry of a surface is the Colliders inside its box whose layer is in `layerMask` and
  whose body is Static or Kinematic (or that have no RigidBody), shapes as triangles in world space (boxes
  exactly, spheres, capsules and cylinders with 16 segments, ConvexHull and Mesh from the triangles of
  `Collider::mesh`); Colliders with code-made geometry only are skipped with one warning. Inactive Entities
  are skipped. A saved `NavArea` changes the area of its Entity's collider triangles or of a box volume
  (`area` in [0, `kMaxNavAreas` = 16), or not walkable); a saved `NavLink` adds an off-mesh link (start at the
  Entity, end at a local offset, both ways or one, kind Jump, Ladder, Drop, Door or Custom); a saved
  `NavObstacle` with `carve` cuts its box or cylinder out of the tiles it covers. `collectNavGeometry()`
  gathers this as plain data, so the bake is a pure function of it.
- **Bake.** `bakeNavMesh()` builds every tile of a surface independently (Recast with one tile of border
  cells), as Jobs with per-worker Recast contexts; the result is byte-for-byte the same for any worker count.
  Each tile records a hash of its input (the triangles, volumes, links and obstacles overlapping the tile and
  its border, and the settings), so the editor rebakes only changed tiles and the runtime knows a stale tile.
- **The `.navmesh` file** (`AssetKind::NavMesh`) is binary, little-endian: magic `ILNAVMSH`, format version
  (`kNavMeshFormatVersion` = 1), Detour's `DT_NAVMESH_VERSION`, the agent profile and build settings, the tile
  grid origin and size, then per tile its coordinates, input hash and Detour tile data, and an `xxhash64` of
  everything before it. `decodeNavMeshData()` rejects a wrong magic, version, Detour version, size or checksum
  with a `NavDataError`; a rejected file is treated as missing (baked at load) with one log line.
- **NavWorld** (one per GameSession, and one for the editor's edit World that never advances) loads each
  surface's `data` through the `AssetStreamer` (decoder `illusion.navmesh`), bakes surfaces without data and
  tiles whose hash is stale (with `rebuildAtRuntime`; otherwise stale tiles are reported once and kept) in the
  background, rebuilds the tiles touched by changed, added or removed sources while `rebuildAtRuntime` (source
  Entities are watched through change tracking of Transform, Collider, NavArea, NavLink and NavObstacle; a
  carving obstacle counts once it has moved more than `moveThreshold` and stood still for `settleSeconds`),
  and swaps finished tiles in at the start of an update, on the World's thread. Live reload of a `.navmesh`
  file replaces the surface's tiles. Queries (`findPath`, `nearestPoint`, `raycast`, `randomPoint`) run on the
  World's thread with an area mask and the surface's area costs, never allocate, and write into caller spans;
  `randomPoint` takes a seed.
- **Agents.** A saved `NavAgent` (agent type, radius, height, max speed, acceleration, turn speed, stopping
  distance, avoidance quality and priority, area mask, `drive`, `faceMovement`, `speedParameter`, and the
  saved `destination`) gets a crowd slot and a runtime `NavAgentState` (status, desired velocity, speed,
  remaining distance, link). Its `drive` says who moves it:
  - `Character` (needs a CharacterController): navigation writes only `NavAgentState::velocity`; the session
    gives physics its horizontal part (keeping the controller's vertical velocity). The next update reads the
    position physics produced and moves the path corridor there; an agent farther than its radius plus one
    metre from the navmesh is `OffMesh` and replans when it returns.
  - `Transform`: navigation integrates the position on the navmesh surface and writes the Transform (for
    crowds without physics).
  - `RootMotion`: navigation steers only (turns the Entity and writes the speed); the Animator's root motion
    moves it.
  - `None`: paths and status only; game code moves it.
  Root motion translation is not applied to agents whose drive is `Character` or `Transform`. While an agent
  traverses an off-mesh link, navigation moves its Transform along the link (a parabola for Jump, vertical
  then horizontal for Ladder, straight otherwise; physics teleports a character along) and publishes
  `LinkEntered`/`LinkExited` events. `speedParameter` names a Float parameter of the Entity's animation graph
  that the session sets to the agent's speed each update.
- **Crowd step.** `NavWorld::update()` runs, per update: (1) on the World's thread, sync slots and positions,
  read changed destinations, swap finished tiles; (2) Jobs: path requests, at most `maxPathRequestsPerUpdate`
  started in slot order, each searched with a fixed iteration budget per update (sliced A* with a per-worker
  `dtNavMeshQuery`); (3) on the World's thread, a proximity grid of the previous positions; (4) Jobs per
  agent: corridor update and optimization, corners, local boundary, neighbours from the grid, desired
  velocity, obstacle avoidance (`dtObstacleAvoidanceQuery` per worker); (5) Jobs per agent: integration and
  separation from the previous positions (`Transform` drive) or the steering output; (6) on the World's
  thread, write `NavAgentState`, Transforms and events in Entity order. Each Job phase writes only its agent's
  slot, so with `fixedStep` the result is the same for any number of workers. Inactive agents keep their slot
  and do not move.
- **Behaviour trees.** A `.behavior` file (`"format": "illusion.behavior"`, version 1,
  `AssetKind::BehaviorTree`) declares blackboard keys (name, type Bool, Int, Float, Vector3 or Entity,
  optional default) and one root node. Node types: composites `sequence`, `selector`, `parallel` (successes
  needed); decorators `inverter`, `succeeder`, `failer`, `repeat` (count, 0 forever), `retry`, `cooldown`,
  `timeLimit`, `guard` (a blackboard condition with abort mode `None`, `Self` or `LowerPriority`); leaves
  `wait` (seconds, random deviation), `moveTo` (a Vector3 or Entity key, acceptance radius), `wander` (radius
  around a key or the start point), `stop`, `setValue`, `condition` and `action` (a name handled by game
  code). `parseBehaviorTree()` validates the whole file (unknown keys are errors, names resolved, types
  checked, values finite) with messages naming the JSON path, and flattens the nodes in pre-order;
  `writeBehaviorTree()` round-trips it. A tree has at most `kMaxBlackboardKeys` = 32 keys and 1024 nodes.
- **Tree evaluation.** `tickBehaviorTree()` is a pure function of the tree, its state (node states, the
  blackboard, a `Random` seeded from the Entity's guid and the session seed) and a `BehaviorContext`, through
  which leaves read positions and movement status and request moves and actions. Guards are evaluated every
  tick: `Self` aborts its running child when the condition turns false, `LowerPriority` aborts a running later
  sibling of its selector when the condition turns true. An aborted action is reported to the module. It does
  not allocate.
- **Actions.** An `action` leaf that starts becomes Running and appears in `AiService::pollActions()` with an
  id (phase `Started`; `Aborted` when a guard or a parent ends it); the module calls `finishAction(entity, id,
  success)`. An action never finished stays Running.
- **Perception.** A saved `AiPerception` (sight radius, cone angle, eye height, occluder layer mask, hearing
  multiplier, memory seconds, detected teams, the blackboard keys to write the chosen target and its last
  known position) senses Entities with a saved `AiStimulus` (team, aim offset, visible). Per update:
  candidates from a grid within the radius and cone, in parallel; line-of-sight raycasts through
  `PhysicsWorld` on the World's thread, at most `maxSightRaysPerUpdate`, served round-robin in slot order (a
  candidate not served keeps its last result); noises from `AiService::reportNoise()` heard within their
  radius times the hearing multiplier. Each perceiver remembers up to `kMaxPerceived` = 8 Entities for
  `memorySeconds`; the chosen target is the nearest currently sensed one, else the most recent memory.
- **AiWorld** keeps one slot per Entity with a saved `BehaviorAgent` (`tree`, `enabled`, `tickInterval`,
  blackboard overrides) and/or `AiPerception`: perception, then trees ticked as Jobs (each tree's moves and
  actions buffered in its slot), then the buffered requests applied in slot order (destinations through
  `NavWorld`). Trees with a `tickInterval` are staggered by slot. Tree and blackboard state are runtime only:
  a loaded scene or saved game restarts trees with defaults and overrides (`NavAgent::destination` is saved).
- **Session order.** `GameSession::update()` becomes: Input, Modules, Saves, **AI** (`AiWorld::update()`),
  **Navigation** (`NavWorld::update()`, then the session applies `Character` velocities and
  `speedParameter`s), Animation (root motion skipped for navigation-driven agents), Physics, and the rest as
  before. Both run with the session's step (one physics step with `fixedStep`) and advance only while started.
- **Scripting API.** `NavService` (`USES nav`): `findPath`, `nearestPoint`, `raycast`, `randomPoint`,
  `setDestination`, `stop`, `warp`, `status`, `remainingDistance`, `pollEvents`. `AiService` (`USES ai`):
  blackboard writes and reads by key, `pollActions`, `finishAction`, `reportNoise`, `perceived`,
  `runningNode`, `restart`. Errors are `NavError`/`AiError`; each service raises the module API version by one
  when it lands.
- **Editor.** `nav.bake` (async; bakes changed tiles of one or every surface, writes the `.navmesh` and sets
  `NavMeshSurface::data`), `nav.status`, `nav.path`, `nav.debug` (navmesh, tiles, links, agents, paths in the
  viewport through `NavWorld::drawDebug()`); `ai.status`, `ai.setValue`, `ai.noise`, `ai.validate`. The asset
  browser lists `.navmesh` and `.behavior` files; exports ship both as they are.
- **Budgets** (desktop CPU, Job System): one 64 x 64-cell tile of a 10,000-triangle scene bakes in under 20
  ms; 1,000 paths of about 100 m on a 256 m tiled navmesh in under 50 ms on one thread; the crowd update of
  500 agents in under 2 ms; 1,000 behaviour trees ticked in under 1 ms; 200 perceivers among 50 stimuli with
  256 rays in under 1 ms.

## Consequences

- One bake function serves the editor, the load and the run-time rebuild; tiles keep rebuilds local and bakes
  parallel, and per-tile hashes make rebakes incremental and stale data visible.
- Navigation never fights physics: characters keep collisions, steps and slopes, and the crowd follows where
  they really are; the cost is one update of lag between steering and the resulting position.
- Our crowd costs more code than `dtCrowd` but is parallel and repeats with fixed steps for any worker count.
- Tile rebuilds (not TileCache obstacles) make carving cost a tile build (milliseconds, off the World's
  thread); many moving carvers would be slow, so moving things usually avoid by steering, not carving.
- Sight raycasts are serial and budgeted because `PhysicsWorld` is not thread-safe; large crowds of perceivers
  see with a few updates of delay.
- Behaviour trees are data with a fixed node set; anything else is an `action` in game code. Subtrees, shared
  blackboards, utility scoring, saved tree state, crossing between surfaces of one agent type, moving surfaces
  and a visual tree editor are open.
- Recast stays private: replacing or patching it touches only `engine/nav/src`. Every Recast upgrade changes
  `DT_NAVMESH_VERSION` or the bake's bytes and invalidates baked files, which are then rebaked at load.
- Two new services raise the module API version twice, after ANIM-3's version 10: `NavService` to 11 and
  `AiService` to 12; modules built against older versions must be rebuilt.
- The small pure helpers that modules may call (`compareBlackboard()`, `inSightCone()`, `hearsNoise()`) are
  `inline` in their headers, so shared modules, which link only `illusion::core`, can use them.
- One surface per agent type per World, surfaces do not move, and only `NavAgent::destination` is saved (tree,
  blackboard and crowd state start fresh after a load); each is a candidate for a later addendum.

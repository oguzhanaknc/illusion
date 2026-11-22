# AI

illusion drives game characters with behaviour trees. A tree is a `.behavior` file: a blackboard of typed
keys and a tree of nodes that decide, tick by tick, what an agent does ([ADR 0062](../architecture/decisions.md)).
Movement goes through [Navigation](navigation.md).

!!! note "What exists today"
    This page describes the `.behavior` format, the blackboard, the tick, the AI Components, `AiWorld` and
    `AiService` for game modules. The `ai.*` Editor commands (NAV-2d) arrive in the next package.

## The `.behavior` file

A file is a JSON object. Unknown keys are errors, and `AssetKind::BehaviorTree` is the Asset kind of the
`.behavior` extension.

| Key | Meaning |
| --- | --- |
| `format` | Must be `"illusion.behavior"` |
| `version` | Must be 1 (`kBehaviorTreeVersion`) |
| `blackboard` | Optional array of keys, at most 32 (`kMaxBlackboardKeys`) |
| `root` | The root node, required; the tree has at most 1,024 nodes (`kMaxBehaviorNodes`) |

```json
{
  "format": "illusion.behavior",
  "version": 1,
  "blackboard": [
    {"name": "target", "type": "Entity"},
    {"name": "home", "type": "Vector3", "default": [0, 0, 0]},
    {"name": "ammo", "type": "Int", "default": 3}
  ],
  "root": {"name": "guard brain", "selector": [
    {"guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
      "child": {"sequence": [
        {"moveTo": {"key": "target", "acceptance": 1.5}},
        {"timeLimit": {"seconds": 3, "child": {"action": "bite"}}},
        {"setValue": {"key": "target", "value": null}}
      ]}}},
    {"guard": {"key": "ammo", "compare": "Greater", "value": 0,
      "child": {"cooldown": {"seconds": 5, "child": {"action": "reload"}}}}},
    {"sequence": [
      {"wander": {"key": "home", "radius": 8}},
      {"wait": {"seconds": 2, "deviation": 1}}
    ]}
  ]}
}
```

## The blackboard

Each entry of `blackboard` has a unique `name`, a `type` and an optional `default`.

| Type | `default` in a file | Without a `default` |
| --- | --- | --- |
| `Bool` | `true` or `false` | `false` |
| `Int` | A whole number from -2147483648 to 2147483647 | 0 |
| `Float` | A finite number | 0 |
| `Vector3` | An array of 3 finite numbers | Not set |
| `Entity` | None: a file cannot name an Entity | Not set |

`Bool`, `Int` and `Float` keys are always set. A `Vector3` or `Entity` key can be unset, and an Entity that
is no longer alive counts as unset. The game fills the keys of a running agent.

### Comparisons

`guard` and `condition` compare a key with the `compare` they name. The comparison must apply to the key's
type, or the parse fails.

| `compare` | Applies to | `value` |
| --- | --- | --- |
| `IsSet`, `IsNotSet` | `Vector3`, `Entity` | Not allowed |
| `Equal`, `NotEqual` | `Bool`, `Int`, `Float` | Required |
| `Greater`, `Less` | `Int`, `Float` | Required |
| `GreaterEqual`, `LessEqual` | `Int` only | Required |

## Nodes

A node is an object with exactly one node type as its key, and an optional `name` (a non-empty label for
status and debugging; it defaults to the type). The value of the type key holds the fields below. A number
must be finite.

```json
{"name": "chase", "moveTo": {"key": "target"}}
```

### Composites

| Node | Value | Meaning |
| --- | --- | --- |
| `sequence` | An array of at least one node | Runs the children in order until one fails |
| `selector` | An array of at least one node | Runs the children in order until one succeeds |
| `parallel` | `{"successes", "children"}` | Runs every child each tick. `children` is an array of at least one node; `successes` (default 0: all of them, at most the number of children) is how many must succeed. It fails as soon as that can no longer happen, and aborts the children still running when it ends |

### Decorators

A decorator has one `child` node.

| Node | Fields | Meaning |
| --- | --- | --- |
| `inverter` | `child` | Swaps its child's success and failure |
| `succeeder` | `child` | Succeeds when its child ends |
| `failer` | `child` | Fails when its child ends |
| `repeat` | `count`, `child` | Runs its child `count` times (default 0: forever) and fails if it fails |
| `retry` | `count`, `child` | Runs its child again after a failure, up to `count` times (default 0: forever): `count` 2 allows 3 attempts |
| `cooldown` | `seconds`, `child` | Fails for `seconds` after its child ended; `seconds` is above 0 |
| `timeLimit` | `seconds`, `child` | Fails and aborts its child after `seconds`; `seconds` is above 0 |
| `guard` | `key`, `compare`, `value`, `abort`, `child` | Runs its child while the condition holds; fails otherwise |

`count` is a whole number from 0 to 4294967295. For `guard`, `key`, `compare` and `value` are as in
[Comparisons](#comparisons), and `abort` is one of the modes below (default `None`).

### Leaves

| Node | Fields | Meaning |
| --- | --- | --- |
| `wait` | `seconds`, `deviation` | Runs for `seconds` (at least 0) plus or minus up to `deviation` (default 0, at most `seconds`) |
| `moveTo` | `key`, `acceptance` | Moves to a `Vector3` key or to the position of an `Entity` key, arriving within `acceptance` m (above 0, default 1) |
| `wander` | `key`, `radius` | Moves to a random navmesh point within `radius` m (required, above 0) of the `Vector3` key; without a key, of where the agent was at its first tick |
| `stop` | none (`{}`) | Stops the agent and succeeds |
| `setValue` | `key`, `value` | Writes `value` to the key and succeeds. A `Vector3` takes an array of 3 numbers, `Bool`, `Int` and `Float` their own type, and `null` clears a `Vector3` or `Entity` key |
| `condition` | `key`, `compare`, `value` | Succeeds when the condition holds, otherwise fails |
| `action` | A name string | Game code: runs until a module finishes the action with that name, then succeeds or fails |

### Abort modes

| `abort` | Meaning |
| --- | --- |
| `None` | The condition is checked only when the guard starts |
| `Self` | The guard checks it every tick and aborts its running child when it turns false |
| `LowerPriority` | Like `Self`, and when the condition goes from false to true it also aborts the running later sibling of its selector and takes over |

`LowerPriority` is allowed only on a guard whose parent is a `selector`. It takes over only on the change
from false to true: a condition that is already true does not interrupt anything.

### Errors

`parseBehaviorTree()` returns the first problem as a message that starts with the JSON path of the
offending value:

```text
root.selector[0].guard.key: unknown key 'targt'
root.sequence[1].wait.deviation: must be at most seconds
```

Invalid JSON is reported with its line and column, and a file without `format` or with another `version`
is rejected before any node is read. `writeBehaviorTree()` writes a tree back, and `parseBehaviorTree()`
reads that unchanged.

## Ticking a tree

The tick is two pure functions over plain data, so it runs without a World. Each agent has a
`BehaviorTreeState`; a `BehaviorContext` lets the leaves act (where the agent is, move requests, actions).

```cpp
auto tree = illusion::parseBehaviorTree(text);
if (tree)
{
    illusion::BehaviorTreeState state = illusion::makeBehaviorTreeState(*tree, 1234);
    state.blackboard[tree->findKey("home")].vector = spawnPoint;
    state.blackboard[tree->findKey("home")].set = true;
    illusion::BehaviorStatus status = illusion::tickBehaviorTree(*tree, state, 1.0f / 60.0f, context);
}
```

`makeBehaviorTreeState()` returns a state with every node idle, the keys at their defaults and a `Random`
seeded with the seed. `resetBehaviorTree()` sets the nodes idle, the keys back to their defaults and the
ticks and time to 0, and keeps the `Random`; it does not stop requests that are still pending, so the caller
does that. `tickBehaviorTree()` returns the status of the root. A root that finished starts again at the
next tick.

How the nodes behave in a tick:

- **Status.** `Success` and `Failure` stay on a node until it starts again.
- **Repeat and Retry.** They run their child at most once per tick, so a child that succeeds at once
  cannot loop inside one tick.
- **TimeLimit.** It counts only the ticks in which its child still runs.
- **Cooldown.** It counts tree time (the seconds ticked since the tree started) from the end of its child.
- **MoveTo.** It succeeds at once when the agent is already within `acceptance` of the target. While it
  runs, it asks for a new move when the target has moved more than `acceptance` since the last request. It
  fails when the key is not set or the Entity is gone.
- **Wander.** Without a key, it wanders around the agent's position at the first tick of the tree. Each
  move uses `kWanderAcceptance` (0.5 m). It fails when the context finds no point.
- **Action.** It starts the action at its first tick and reports the result at the next tick at the
  earliest.
- **Wait.** Its deviation is drawn from the state's `Random`, so the same seed repeats the same waits.
- **Aborts.** A node that is aborted stops its move or its action through the context.

## Agents

A `BehaviorAgent` Component runs a tree for its Entity, usually together with a `NavAgent` that moves it.

| Field | Meaning |
| --- | --- |
| `tree` | The `.behavior` file |
| `enabled` | Tick it while the game runs |
| `tickInterval` | Seconds between ticks; 0 ticks every update |
| `blackboard` | Starting values that replace the tree's defaults (`BlackboardOverride`: `key`, and `number` for Bool, Int and Float, `vector` for Vector3, `entity` for Entity) |

`AiWorld` keeps the AI of a World. Each update it loads every tree file once through the `AssetStreamer`
(decoder `illusion.behavior`), gives each agent a slot and a runtime `AiAgentState` (its slot, the status
after the last tick, the running node and the perception's target), and, while the game runs, perceives and
ticks the enabled trees as Jobs. A tree starts with its defaults and the agent's overrides; its `Random` is
seeded from the agent's `EntityGuid`, so a scene repeats the same waits and wanders. Trees with a
`tickInterval` tick once per interval with the time since their last tick, spread over the interval so that
many agents do not tick in the same update. Entities with the `Inactive` tag keep their state and do not
tick.

Moves go through the [`NavWorld`](navigation.md): a `moveTo` sets the `NavAgent`'s destination and succeeds
when the agent arrives within `acceptance`; a `wander` draws a reachable navmesh point within its radius. An
agent without a `NavAgent` fails its moves.

```cpp
illusion::AiWorld ai(world, navigation, &streamer);
ai.update(step, &jobs, &physics, true); // after the modules, before navigation
for (const illusion::AiActionRequest& request : ai.actions())
{
    if (request.phase == illusion::AiActionPhase::Started && request.name == "greet")
    {
        ai.finishAction(request.entity, request.action, true);
    }
}
```

### Actions

An `action` node asks game code to do something. `actions()` lists the actions the last update started or
aborted, in slot order; `finishAction()` reports the result, which the node sees at its next tick.
`setValue()` and `value()` write and read a key of an agent's blackboard, `restart()` starts its tree again
from the root with the starting blackboard.

### Live reload

When a `.behavior` file changes, its agents restart with the new tree; their running actions are reported
as aborted. A file that no longer parses keeps the old tree and logs the error.

## Perception

`AiPerception` lets an Entity see and hear. It looks along its +z axis, the way a `NavAgent` with
`faceMovement` faces.

| Field | Meaning |
| --- | --- |
| `sightRadius` | In m; 0 is blind |
| `sightAngle` | Full cone angle in radians (120°) |
| `eyeHeight` | Height of the eye above the Entity's position in m |
| `occluderMask` | Physics layers that block sight |
| `hearing` | Multiplier of the radius of noises; 0 is deaf |
| `memorySeconds` | How long it remembers what it no longer senses |
| `detectTeams` | Teams it notices (bit i = team i) |
| `targetKey` | Entity key of its tree set to the chosen target, cleared when it is forgotten |
| `positionKey` | Vector3 key of its tree set to the target's last known position |

Each update a perceiver gathers the stimuli in its cone (as Jobs; at most `AiWorldDesc::maxSightCandidates`,
nearest first), then tests the line of sight from the eye with a `PhysicsWorld` raycast. The raycasts are
limited to `AiWorldDesc::maxSightRaysPerUpdate` per update and served round-robin, so every perceiver gets
its turn; one not served this update keeps its last result. Noises reported with `reportNoise()` are heard
in the next update within their `radius` times `hearing`. What is sensed is remembered for `memorySeconds`;
`perceived()` lists it nearest first. The target is the nearest one sensed now, else the most recent
memory. With `targetKey`, a guard such as `{"key": "target", "compare": "IsSet", "abort": "LowerPriority"}`
interrupts wandering as soon as a target is seen.

### Stimuli

`AiStimulus` makes an Entity something perceivers see: `team` (below `kMaxAiTeams`, 32), `offset` (the
point looked at, relative to the Entity's position) and `visible` (false while it hides).

The geometry is in `illusion/ai/Perception.hpp`, as pure functions:

- `inSightCone(eye, forward, radius, angle, point)` is true when `point` is within `radius` of `eye` and
  within the cone of full angle `angle` (radians; 2 pi or more sees all around) around the unit `forward`.
  Both bounds are inclusive, and the eye itself is inside. It does not test the line of sight.
- `hearsNoise(listener, hearing, noise)` is true when the noise is within its `radius` times `hearing`
  (inclusive). Nothing is heard at `hearing` 0.

### In a game session

`GameSession` owns an `AiWorld` (`ai()`). Its AI step runs after Saves and before Navigation, so the moves
of a tick become destinations in the same update; with fixed steps it first waits for navmeshes and tree
loads. Trees tick only while the session is started; `drawDebug()` draws sight cones, what is seen,
memories and noises.

## From a module

A game module finds the `AiService` with `context.service<illusion::AiService>()` (`USES ai` in
`illusion_add_module`). It writes blackboard keys that the tree reads, carries out the actions the tree asks
for and reports noises. The [scripting API reference](../reference/scripting-api.md#aiservice) lists every
function and its errors.

## Performance

Measured on the development desktop: ticking 1,000 trees once takes 33 µs (Benchmark `ai/tick-1000`,
budget 250 µs). One `AiWorld` update of 200 perceivers among 50 moving stimuli with 256 sight
raycasts takes about 0.13 ms (`ai/perception-200`, budget 0.75 ms; ADR 0062 aims at 1 ms), and one of
1,000 agents running a 30-node tree about 0.19 ms (`ai/world-1000`, budget 1.3 ms), both on the Job System.

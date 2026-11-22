# A creature that wanders and chases

This tutorial puts a creature into a scene that wanders on its own, notices the player when it sees or hears
them, chases and bites, and searches the last place it saw the player before it wanders again. You bake a
navmesh, write a behaviour tree, give the creature a `NavAgent`, an `AiPerception` and a `BehaviorAgent`, and
write a small C++ game module that moves the player, makes a noise when you shout and answers the creature's
`bite` action.

You need a Release build of illusion and the C++ compiler the editor was built with. The
[AI manual](../manual/ai.md) explains the tree, the blackboard and the perception, the
[Navigation manual](../manual/navigation.md) the navmesh, and the
[Scripting API](../reference/scripting-api.md#aiservice) the module's calls.

## 1. What you build

- A floor with a navmesh, a **Creature** that wanders within 10 m of where it stands and waits a little between
  walks, and a **Player** you move with the keyboard.
- The creature sees the player within 14 m in front of it (a cone of 120 degrees, blocked by walls) and hears the
  noise of your shout. It then runs to the player and bites.
- When it loses the player it walks to the place where it saw them last, waits there, and goes back to
  wandering.
- A tree file `assets/ai/creature.behavior` with three behaviours in order of priority: chase, search, wander.

## 2. Create the project

Start the editor and create a project named **Creature** in the folder `creature` from the **3D Starter**
template:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

## 3. The behaviour tree

Create `assets/ai/creature.behavior` in your code editor:

```json
{
  "format": "illusion.behavior",
  "version": 1,
  "blackboard": [
    {"name": "target", "type": "Entity"},
    {"name": "lastSeen", "type": "Vector3"}
  ],
  "root": {"name": "creature", "selector": [
    {"name": "chase", "guard": {"key": "target", "compare": "IsSet", "abort": "LowerPriority",
      "child": {"sequence": [
        {"moveTo": {"key": "target", "acceptance": 1.2}},
        {"timeLimit": {"seconds": 2, "child": {"action": "bite"}}},
        {"wait": {"seconds": 1}}
      ]}}},
    {"name": "search", "guard": {"key": "lastSeen", "compare": "IsSet", "abort": "Self",
      "child": {"sequence": [
        {"moveTo": {"key": "lastSeen", "acceptance": 1}},
        {"wait": {"seconds": 3}},
        {"setValue": {"key": "lastSeen", "value": null}}
      ]}}},
    {"name": "wander", "sequence": [
      {"wander": {"radius": 10}},
      {"wait": {"seconds": 2, "deviation": 1}}
    ]}
  ]}
}
```

Read it from the top:

- The blackboard has two keys. `target` is an Entity key and `lastSeen` a Vector3 key; neither has a value
  until the perception sets them.
- The root is a `selector`: it tries its children in order until one succeeds or keeps running, so the order is
  the priority.
- **chase** is a `guard` on `target`. While `target` is set, it runs `moveTo` the player until the creature is
  within 1.2 m, then the `action` named `bite` for at most 2 seconds (`timeLimit`), then waits a second.
  `abort: LowerPriority` makes the guard interrupt whatever lower child is running (the wandering) the moment
  `target` becomes set.
- **search** is a `guard` on `lastSeen`. It walks to that point, waits 3 seconds, then clears the key with
  `setValue` and `null`. `abort: Self` ends the search at once if `lastSeen` is cleared meanwhile.
- **wander** is the fallback: a random point on the navmesh within 10 m of the creature's first position, a
  pause of 2 seconds plus or minus 1, and again, forever.

An agent writes the same file with its file tools and checks it with the `ai.validate` command, which names the
JSON path of the first mistake (`root.selector[0].guard.key: ...`):

```json
{"call": "ai.validate", "params": {"path": "assets:/ai/creature.behavior"}}
```

## 4. The scene

Add a floor, the player and the creature. The floor and a **Navigation** Entity with a `NavMeshSurface` come
first; the surface collects the floor's `Collider` into a navmesh:

```json
{"call": "entity.create", "params": {"name": "Floor", "components": {"illusion.Transform": {"position": [0, -0.5, 0], "scale": [24, 1, 24]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Navigation", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.NavMeshSurface": {"size": [24, 4, 24]}}}}
{"call": "nav.bake", "params": {}}
```

`nav.bake` writes `assets/navigation/navigation.navmesh` and sets the surface's `data` to it. Then the two
characters:

```json
{"call": "entity.create", "params": {"name": "Creature", "components": {"illusion.Transform": {"position": [8, 0.5, 8], "scale": [0.8, 1, 0.8]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.NavAgent": {"maxSpeed": 3.5, "radius": 0.4}, "illusion.AiPerception": {"sightRadius": 14, "eyeHeight": 0.8, "memorySeconds": 4, "targetKey": "target", "positionKey": "lastSeen"}, "illusion.BehaviorAgent": {"tree": "assets:/ai/creature.behavior"}}}}
{"call": "entity.create", "params": {"name": "Player", "components": {"illusion.Transform": {"position": [-8, 1, -8]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.CharacterController": {"radius": 0.3, "height": 1.8}, "illusion.AiStimulus": {"offset": [0, 0.9, 0]}}}}
```

- The `NavAgent` walks the navmesh and steers the creature's own position; the `BehaviorAgent` runs the tree and
  gives it destinations.
- The `AiPerception` sees along the creature's +z axis. `targetKey` and `positionKey` name the blackboard keys the
  perception writes: the chosen target, and its last known position. It forgets what it no longer senses after
  `memorySeconds`; the target key is then cleared and `lastSeen` keeps the position.
- The `AiStimulus` makes the Player something perceivers notice; `offset` is the point they look at.

## 5. Input actions

The module reads two actions. Create `assets/input/creature.input`:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "creature.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "creature.shout", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]}
  ]
}
```

and set the actions asset of the Input settings (**Window → Project Settings**) to
`assets:/input/creature.input`, as the [Tetris by Hand](tetris-by-hand.md#4-input-actions) tutorial does.

## 6. The module

Run **New module** from the command palette with the name `creature`, and give it the services it uses:

```cmake
illusion_add_module(creature SHARED SOURCES creature.cpp USES ai input physics runtime)
```

The state of the module is one runtime Component, so that a hot reload keeps it:

```cpp
#include <illusion/ai/AiService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace creature
{

using namespace illusion;

struct State
{
    ActionId move = 0;
    ActionId shout = 0;
    u32 bites = 0;
};

} // namespace creature
```

`start` finds the two actions and creates the state:

```cpp
namespace
{

using namespace illusion;
using creature::State;

constexpr f32 kPlayerSpeed = 5.0f; // m/s
constexpr f32 kShoutRadius = 15.0f; // m at which a listener with hearing 1 hears the shout

bool loadCreature(ModuleContext&)
{
    reflectRuntime<State>("creature.State", {.displayName = "Creature state"});
    return true;
}

void startCreature(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    if (input == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    const std::optional<ActionId> move = input->findAction("creature.move");
    const std::optional<ActionId> shout = input->findAction("creature.shout");
    if (!move || !shout)
    {
        return;
    }
    context.world().create(Name{"Creature State"}, State{*move, *shout, 0});
}
```

Every update moves the player, reports a noise when you shout, and answers the actions the tree started. A
tree's `action` node runs until the game finishes it with `finishAction()`; here the bite succeeds at once and is
counted:

```cpp
void updateCreature(ModuleContext& context, f64)
{
    AiService* ai = context.service<AiService>();
    InputService* input = context.service<InputService>();
    PhysicsService* physics = context.service<PhysicsService>();
    if (ai == nullptr || input == nullptr || physics == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    State* state = nullptr;
    world.query<State>().each([&state](Entity, State& value) { state = &value; });
    Entity player{};
    world.query<const Name, const CharacterController>().each(
        [&player](Entity entity, const Name& name, const CharacterController&) {
            if (name.value == "Player")
            {
                player = entity;
            }
        });
    if (state == nullptr || !world.isAlive(player))
    {
        return;
    }

    const Vec2 move = input->vector(state->move);
    Vec3 velocity = physics->characterVelocity(player);
    velocity.x = move.x * kPlayerSpeed;
    velocity.z = -move.y * kPlayerSpeed;
    physics->setCharacterVelocity(player, velocity);

    if (input->wasPressed(state->shout))
    {
        AiNoise noise;
        noise.position = world.get<Transform>(player).position;
        noise.radius = kShoutRadius;
        noise.instigator = player;
        static_cast<void>(ai->reportNoise(noise));
    }

    std::array<AiActionRequest, 16> actions{};
    const u32 count = std::min<u32>(ai->pollActions(actions), static_cast<u32>(actions.size()));
    for (const AiActionRequest& request : std::span(actions).first(count))
    {
        if (request.phase == AiActionPhase::Started && request.name == "bite")
        {
            ++state->bites;
            static_cast<void>(ai->finishAction(request.entity, request.action, true));
        }
    }
}

constexpr ModuleApi kCreature = makeModuleApi(
    "creature", "1.0.0", {.load = &loadCreature, .start = &startCreature, .update = &updateCreature});

} // namespace

ILL_MODULE(creature, kCreature)
```

Press **Ctrl+B** (**Modules → Build Modules**). When the build succeeds the module is loaded.

## 7. Try it

Press **Play**:

- The creature wanders: it walks to a random point near where it started, pauses, and walks on.
- Walk the player in front of it with **W A S D** within 14 m: it stops wandering, runs up and bites. Hide
  behind a wall or turn your back, and it loses you after four seconds, goes to where it saw you last and
  waits there before it wanders again.
- Stand out of its sight and press **Space**: the shout is heard within 15 m, even behind the creature or
  behind a wall, and the creature comes to the place of the noise.

In the **Scene** view, open the **Visualisers** menu and switch **Perception** on to see the sight cone of the
creature, a line to what it sees, a marker where it remembers something and a circle for each noise. An agent
asks the editor instead:

```json
{"call": "ai.status", "params": {"entity": "/Creature"}}
{"call": "ai.setValue", "params": {"entity": "/Creature", "key": "lastSeen", "value": [0, 0, 0]}}
{"call": "ai.noise", "params": {"position": [-8, 0, -8], "radius": 30}}
```

`ai.status` lists the running node (`chase`, `search` or `wander`), the two keys and what the creature
perceives; `ai.setValue` writes a key (the creature walks to the origin and searches there); `ai.noise` makes a
noise anywhere. All three need a running play session (`play.start`).

Change `creature.behavior` while the game runs, for example the `radius` of `wander`: the editor reloads the file
and the creature restarts with the new tree.

## 8. The limits

- The tree decides; the module only answers actions. A bite here only counts: damage, an animation or a sound
  belong to the module, which finishes the action when they are done.
- The creature walks only on the baked navmesh. Bake it again after moving the floor (`nav.bake` changes only
  the tiles that need it), or let `rebuildAtRuntime` of the surface do it while the game runs.
- Sight needs a clear line from the creature's eye (`eyeHeight`) to the player's `offset`; only the physics
  layers in the perception's `occluderMask` block it.
- The live state of a tree is not saved: loading a saved game restarts the creature's tree, with the blackboard's
  defaults.

## Next steps

- Read the [AI manual](../manual/ai.md) for every node, the abort modes and the perception's team filter.
- The showcase project's module calls the whole `AiService`; its guard tree uses `LowerPriority` as this one does.
- [Animate a character](animated-character.md) gives the creature legs: a `NavAgent`'s `speedParameter` feeds
  the speed of an animation graph.

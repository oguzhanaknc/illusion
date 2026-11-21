# Animate a character

This tutorial puts a rigged character into a scene and makes it walk, run and jump. You import a glTF model
with clips, describe how the clips blend in an animation graph, give the character a `CharacterController`,
and write a small C++ game module that sets the graph's `speed` from the input, triggers a jump, plays a
footstep sound on an animation event, and lets root motion move the character.

You need a Release build of illusion, the C++ compiler the editor was built with, a rigged glTF model whose
clips are called `idle`, `walk`, `run` and `jump` (a character from any glTF library works; the clip names are
the ones the graph below uses) and a short `footstep.wav`. The [Animation manual](../manual/animation.md)
explains every part used here, and the [Scripting API](../reference/scripting-api.md#animationservice) the
module's calls.

## 1. What you build

- A character that stands in an idle pose, walks when you push the stick half way, runs when you push it
  all the way (or press a movement key), and jumps on the jump button with a short cross-fade.
- A graph file `assets/animations/hero.animgraph` with one layer: a locomotion state that blends idle, walk
  and run on the parameter `speed`, and a jump state that a trigger starts.
- Root motion: the feet do not slide, because the clips, not the code, move the character. The
  `CharacterController` gets the horizontal velocity from the animation and keeps its own vertical one.
- A module that drives the parameters from the input actions and plays a sound on each `footstep` event.

## 2. Create the project and import the model

Start the editor and create a project named **Hero** in the folder `hero` from the **3D Starter** template:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

Copy `hero.glb` into `assets/models/` and `footstep.wav` into `assets/audio/`. The editor imports the model:
the Asset Browser shows its meshes, its skin (`hero.glb#skin/0`) and its clips (`hero.glb#animation/idle`,
`walk`, `run`, `jump`). A clip moves joints **by name**, so it plays on every skeleton with those joint
names. The root motion joint of the graph (`hips` below) must be a joint of the skeleton; use the name of
the pelvis joint of your model.

## 3. The animation graph

Create `assets/animations/hero.animgraph` in your code editor:

```json
{
  "format": "illusion.animgraph",
  "version": 1,
  "parameters": [
    {"name": "speed", "type": "Float"},
    {"name": "jump", "type": "Trigger"}
  ],
  "rootMotion": "hips",
  "layers": [
    {
      "name": "base",
      "entry": "locomotion",
      "states": [
        {
          "name": "locomotion",
          "blend1d": {
            "parameter": "speed",
            "points": [
              {"value": 0, "clip": "assets:/models/hero.glb#animation/idle"},
              {"value": 1.5, "clip": "assets:/models/hero.glb#animation/walk"},
              {"value": 4, "clip": "assets:/models/hero.glb#animation/run"}
            ]
          },
          "events": [
            {"name": "footstep", "time": 0.25},
            {"name": "footstep", "time": 0.75}
          ]
        },
        {
          "name": "jump",
          "clip": "assets:/models/hero.glb#animation/jump",
          "loop": false
        }
      ],
      "transitions": [
        {"from": "locomotion", "to": "jump", "duration": 0.1, "conditions": [{"parameter": "jump"}]},
        {"from": "jump", "to": "locomotion", "duration": 0.2, "exitTime": 0.9}
      ]
    }
  ]
}
```

Read it from the top:

- `speed` is a Float the module will set; `jump` is a Trigger, which stays set until a transition uses it.
- `rootMotion` names the joint whose horizontal motion and yaw leave the pose and move the character instead.
- The layer starts in `locomotion`, a **1D blend space**: at `speed` 0 only `idle` plays, at 1.5 only `walk`,
  at 4 only `run`, and in between the neighbouring clips blend. The three clips play in sync on one
  normalized time, so their steps stay together while the weights change.
- `footstep` events at 0.25 and 0.75 of the cycle are the moments your walk and run clips put a foot down;
  look at the clips and set the times to match.
- The first transition starts `jump` from `locomotion` when the `jump` trigger is set, with a 0.1 s
  cross-fade. The jump state does not loop; the second transition returns to `locomotion` over 0.2 s once
  the jump is 90 % played (`exitTime`).

An agent writes the same file with its file tools; the editor checks it when it loads, and a mistake is
reported with its JSON path (`layers[0].transitions[1].to: ...`).

## 4. The character

Add a floor and the character. In the editor create an Entity **Floor** with a `MeshRenderer`
(`builtin:/mesh/cube`), a `Transform` scaled (20, 1, 20) at (0, -0.5, 0), a `RigidBody` with the motion type
Static and a `Collider` (Box, half extents 0.5). Then create **Hero** with a `MeshRenderer` (mesh
`assets:/models/hero.glb#mesh/0`), a `CharacterController` and an `Animator`:

| Animator field | Value |
| --- | --- |
| `skeleton` | `assets:/models/hero.glb#skin/0` |
| `graph` | `assets:/animations/hero.animgraph` |

As commands, for an agent:

```json
{"call": "entity.create", "params": {"name": "Floor", "components": {"illusion.Transform": {"position": [0, -0.5, 0], "scale": [20, 1, 20]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Hero", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.MeshRenderer": {"mesh": "assets:/models/hero.glb#mesh/0"}, "illusion.CharacterController": {"radius": 0.3, "height": 1.8}, "illusion.Animator": {"skeleton": "assets:/models/hero.glb#skin/0", "graph": "assets:/animations/hero.animgraph"}}}}
```

Outside Play mode the Scene view poses Hero in the entry state (idle at the `time` of the Animator). The
`clip`, `speed`, `time`, `loop` and `playing` fields stay as they are: `clip` and `loop` are not used with a
graph, and `speed` and `playing` still control how fast the graph runs and whether it runs.

## 5. Input actions

The module reads two actions. Create `assets/input/hero.input`:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "hero.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "hero.jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]}
  ]
}
```

and set the actions asset of the Input settings (**Window → Project Settings**) to
`assets:/input/hero.input`, as the [Tetris by Hand](tetris-by-hand.md#4-input-actions) tutorial does.

## 6. The module

Run **New module** from the command palette with the name `hero`, and give it the services it uses:

```cmake
illusion_add_module(hero SHARED SOURCES hero.cpp USES animation audio input physics runtime)
```

The state of the module is one runtime Component, so that a hot reload keeps it:

```cpp
#include <illusion/animation/AnimationComponents.hpp>
#include <illusion/animation/AnimationService.hpp>
#include <illusion/audio/AudioService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace hero
{

using namespace illusion;

struct State
{
    ActionId move = 0;
    ActionId jump = 0;
    AudioClipHandle footstep{};
};

} // namespace hero
```

`start` finds the two actions and loads the sound once. Services belong to the host, so the clip handle stays
valid across hot reloads:

```cpp
namespace
{

using namespace illusion;
using hero::State;

constexpr f32 kRunSpeed = 4.0f;  // the `speed` of the run clip in the graph
constexpr f32 kJumpSpeed = 5.0f; // m/s upwards
constexpr f32 kMinStepSpeed = 0.1f;

bool loadHero(ModuleContext&)
{
    reflectRuntime<State>("hero.State", {.displayName = "Hero state"});
    return true;
}

void startHero(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    AudioService* audio = context.service<AudioService>();
    if (input == nullptr || audio == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    const std::optional<ActionId> move = input->findAction("hero.move");
    const std::optional<ActionId> jump = input->findAction("hero.jump");
    if (!move || !jump)
    {
        return;
    }
    const AudioClipHandle footstep = audio->loadClip("assets:/audio/footstep.wav", AudioClipMode::Decode);
    context.world().create(Name{"Hero State"}, State{*move, *jump, footstep});
}
```

Every update sets `speed` from the length of the move vector (a keyboard gives 1, which is a run; a half
pushed stick walks), starts a jump when the character stands on the ground, and plays the events of the last
animation update. The `setFloat()` and `setTrigger()` calls take effect at the next Animation step:

```cpp
void updateHero(ModuleContext& context, f64)
{
    InputService* input = context.service<InputService>();
    AnimationService* animation = context.service<AnimationService>();
    PhysicsService* physics = context.service<PhysicsService>();
    AudioService* audio = context.service<AudioService>();
    if (input == nullptr || animation == nullptr || physics == nullptr || audio == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    State state;
    bool found = false;
    world.query<const State>().each([&](Entity, const State& value) {
        state = value;
        found = true;
    });
    Entity hero{};
    world.query<const Animator, const CharacterController>().each(
        [&hero](Entity entity, const Animator&, const CharacterController&) { hero = entity; });
    if (!found || !world.isAlive(hero))
    {
        return;
    }

    const f32 speed = glm::length(input->vector(state.move)) * kRunSpeed;
    static_cast<void>(animation->setFloat(hero, "speed", speed));
    if (input->wasPressed(state.jump) && physics->isCharacterGrounded(hero))
    {
        // Root motion owns the horizontal velocity; the jump is physics, so it sets the vertical one.
        Vec3 velocity = physics->characterVelocity(hero);
        velocity.y = kJumpSpeed;
        physics->setCharacterVelocity(hero, velocity);
        static_cast<void>(animation->setTrigger(hero, "jump"));
    }

    std::array<AnimationEvent, 16> events{};
    const u32 count = std::min<u32>(animation->pollEvents(events), static_cast<u32>(events.size()));
    for (const AnimationEvent& event : std::span(events).first(count))
    {
        // The idle pose also crosses footstep events (the state is the same), so stand still silently.
        if (event.entity == hero && event.name == "footstep" && speed > kMinStepSpeed && state.footstep.valid())
        {
            AudioPlayParams params;
            params.volume = 0.6f;
            static_cast<void>(audio->playOneShot(state.footstep, params));
        }
    }
}

constexpr ModuleApi kHero =
    makeModuleApi("hero", "1.0.0", {.load = &loadHero, .start = &startHero, .update = &updateHero});

} // namespace

ILL_MODULE(hero, kHero)
```

Press **Ctrl+B** (**Modules → Build Modules**). When the build succeeds the module is loaded.

## 7. Try it

Press **Play**:

- Hero stands in the idle pose. Hold **W**: it runs forward, driven by the root motion of the `run` clip, and
  a footstep sounds at the two marked moments of each cycle. A stick pushed half way walks, with the slower
  clip and slower steps; between them the clips blend.
- Press **Space**: `jump` plays with a 0.1 s cross-fade, the character leaves the ground, and when the clip is
  nearly over the graph fades back to locomotion.
- Change `hero.animgraph` while the game runs (a different `exitTime`, a new event time): the editor reloads
  the file and Hero restarts with the new graph.

An agent starts the game with `play.start`, reads the state of the graph through the module's own logic or
`play.step`, and sees the pose in `viewport.screenshot`; the graph's parameters are set only by game code.

## 8. The limits

- Root motion moves the character forward along the clips' direction. Turning toward the stick is game
  code: rotate the Entity's `Transform` before the Animation step (a graph with root motion turns the
  Entity by the clip's yaw only).
- A transition that is running is not interrupted, so a jump pressed during the fade into another state is
  lost: the trigger is cleared at the end of the update. Keep the cross-fades short or press again.
- Animations of one skeleton are not shared across different joint names; retarget the clips in a DCC tool.
- The live state of the graph is not saved: loading a saved game restarts Hero in `locomotion`.

## Next steps

- Read the [Animation manual](../manual/animation.md) for blend spaces in two dimensions (strafing),
  masked layers (aiming with the upper body) and additive layers (breathing).
- The showcase project's `Column` and its `assets/animations/column.animgraph` show a graph with a trigger
  and an event; its module calls the whole `AnimationService`.
- [A HUD and a pause menu](hud-and-pause-menu.md) adds a score display and a menu to the game.

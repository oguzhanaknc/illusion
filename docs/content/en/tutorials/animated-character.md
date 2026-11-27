# Animate a character

This tutorial puts a rigged character into a scene and makes it walk, run and jump. You import a glTF model
with clips, describe how the clips blend in an animation graph, give the character a `CharacterController`,
and write a small C++ game module that sets the graph's `speed` from the input, triggers a jump, plays a
footstep sound on an animation event, and lets root motion move the character. Then you hang a lantern on
the hand, turn the head towards a visitor, keep the feet on a step, let the character fall like a ragdoll, and
aim with the upper body.

You need a Release build of illusion, the C++ compiler the editor was built with, a rigged glTF model whose
clips are called `idle`, `walk`, `run` and `jump` (a character from any glTF library works; the clip names are
the ones the graph below uses) and a short `footstep.wav`. The [Animation manual](../manual/animation.md)
explains every part used here, and the [Scripting API](../reference/scripting-api.md#animationservice) the
module's calls.

## 1. What you build

- A character that stands in an idle pose, walks when you push the stick half way, runs when you push it
  all the way (or press a movement key), and jumps on the jump button with a short cross-fade.
- A graph file `assets/animations/hero.animgraph` with two layers: a locomotion state that blends idle, walk
  and run on the parameter `speed`, and a jump state that a trigger starts; and an aim layer over the upper
  body that blends nine aim poses by the parameters `aimYaw` and `aimPitch`.
- Root motion: the feet do not slide, because the clips, not the code, move the character. The
  `CharacterController` gets the horizontal velocity from the animation and keeps its own vertical one.
- A module that drives the parameters from the input actions and plays a sound on each `footstep` event.
- A lantern in the right hand (a socket), a head that follows a visitor (look-at), feet that stand on a step
  (foot placement), a key that makes the character fall and stand up (a ragdoll), and a button that aims the
  upper body and the right hand at the visitor (an `Aim`).

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

The aiming step plays nine **aim poses**: single-frame clips of the upper body, authored in your DCC tool and
exported in `hero.glb` next to the other clips, that show the character aiming straight ahead
(`aim_center`), left and right by 1.2 rad (about 70°) (`aim_left`, `aim_right`), up and down by 0.8 rad
(about 46°) (`aim_up`, `aim_down`) and in the four corners (`aim_left_up`, `aim_left_down`, `aim_right_up`,
`aim_right_down`). Pose the weapon arm and the spine, key one frame of each pose as its own action and
export them with the model; the clips only need the spine and the arms. If your model has no aim poses, delete
the `aim` layer and the two aim parameters from the graph below and leave the `yawParameter` and
`pitchParameter` of step 11 empty: the `Aim` then does all the aiming procedurally.

## 3. The animation graph

Create `assets/animations/hero.animgraph` in your code editor:

```json
{
  "format": "illusion.animgraph",
  "version": 2,
  "parameters": [
    {"name": "speed", "type": "Float"},
    {"name": "jump", "type": "Trigger"},
    {"name": "aimYaw", "type": "Float"},
    {"name": "aimPitch", "type": "Float"}
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
    },
    {
      "name": "aim",
      "blend": "Additive",
      "space": "Model",
      "reference": "assets:/models/hero.glb#animation/aim_center",
      "referenceTime": 0,
      "mask": [{"joint": "spine", "weight": 1}],
      "states": [
        {
          "name": "aim",
          "blend2d": {
            "x": "aimYaw",
            "y": "aimPitch",
            "points": [
              {"x": 0, "y": 0, "clip": "assets:/models/hero.glb#animation/aim_center"},
              {"x": -1.2, "y": 0, "clip": "assets:/models/hero.glb#animation/aim_left"},
              {"x": 1.2, "y": 0, "clip": "assets:/models/hero.glb#animation/aim_right"},
              {"x": 0, "y": 0.8, "clip": "assets:/models/hero.glb#animation/aim_up"},
              {"x": 0, "y": -0.8, "clip": "assets:/models/hero.glb#animation/aim_down"},
              {"x": -1.2, "y": 0.8, "clip": "assets:/models/hero.glb#animation/aim_left_up"},
              {"x": -1.2, "y": -0.8, "clip": "assets:/models/hero.glb#animation/aim_left_down"},
              {"x": 1.2, "y": 0.8, "clip": "assets:/models/hero.glb#animation/aim_right_up"},
              {"x": 1.2, "y": -0.8, "clip": "assets:/models/hero.glb#animation/aim_right_down"}
            ]
          }
        }
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
- The file is version 2 because of the `aim` layer. It is an `Additive` layer in `Model` space: each aim pose
  is compared with the `reference` pose `aim_center` (not with its own first frame, which would give nothing
  for a one-frame clip) and the difference turns the model-space rotations of `spine` and everything below
  it, so the aim is the same over a run as over an idle. Its one state blends the nine poses in two
  dimensions by `aimYaw` and `aimPitch`, in radians: the corners are at 1.2 and 0.8, the largest turn of the
  `Aim` in step 11. The layer's weight stays 1.

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

The module reads four actions; `hero.fall` is used in step 10 and `hero.aim` in step 11. Create `assets/input/hero.input`:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "hero.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "hero.jump", "type": "Button", "bindings": ["Key.Space", "Gamepad.South"]},
    {"name": "hero.fall", "type": "Button", "bindings": ["Key.R", "Gamepad.East"]},
    {"name": "hero.aim", "type": "Button", "bindings": ["Mouse.Right", "Gamepad.LeftShoulder"]}
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
#include <illusion/ecs/Transform.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>
#include <illusion/physics/Ragdoll.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
    ActionId fall = 0;
    bool fallen = false;
    ActionId aim = 0;
    bool onTarget = false;
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
    const std::optional<ActionId> fall = input->findAction("hero.fall");
    const std::optional<ActionId> aim = input->findAction("hero.aim");
    if (!move || !jump || !fall || !aim)
    {
        return;
    }
    const AudioClipHandle footstep = audio->loadClip("assets:/audio/footstep.wav", AudioClipMode::Decode);
    context.world().create(Name{"Hero State"}, State{*move, *jump, footstep, *fall, false, *aim, false});
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

} // namespace
```

The module is not finished yet: steps 7 to 11 add a lantern, a gaze, foot placement, a ragdoll and an aim, and
step 12 registers and builds it.

## 7. A lantern in the hand

A **socket** hangs an Entity on a joint. Make the lantern a child of Hero and give it a `BoneAttachment`; the
Animation step then writes its transform from the joint's pose and the offset after every update:

```json
{"call": "entity.create", "params": {"name": "Lantern", "parent": "/Hero", "components": {"illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere", "material": "builtin:/material/default"}, "illusion.BoneAttachment": {"joint": "hand_r", "translation": [0, 0.05, 0.1], "scale": [0.15, 0.15, 0.15]}}}}
```

Use the name of the right hand joint of your model for `joint`. The `translation` is 10 cm along the joint's
own z axis and 5 cm along y, so the lantern hangs in the palm, and `scale` makes the sphere 15 cm wide. The
`BoneAttachment` owns the lantern's `Transform`, so the size is set here and not on the Transform. You should
see the lantern in the Scene view in the idle pose; during Play it swings with the hand.

## 8. A head that follows

A `LookAt` turns the head joint and a few joints below it towards a target, after the animation, up to a
largest angle and at a limited speed. The commands above add a **Visitor** sphere at (3, 1.6, 4) and a `LookAt`
on Hero that turns the head and the neck by at most 1.2 rad (about 70°) at 5 rad/s. Setting the `target`
field to the Visitor's Entity is enough in the Inspector. A module can aim the head at any point instead,
with `AnimationService::setLookAtTarget()`, which clears `target`. This one follows the Visitor's position,
so a game that moves the Visitor moves the gaze:

```json
{"call": "entity.create", "params": {"name": "Visitor", "components": {"illusion.Transform": {"position": [3, 1.6, 4]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/sphere", "material": "builtin:/material/default"}}}}
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.LookAt", "value": {"joint": "head", "chainLength": 2, "maxAngle": 1.2, "speed": 5}}}
```

```cpp
namespace
{

void updateGaze(ModuleContext& context)
{
    AnimationService* animation = context.service<AnimationService>();
    if (animation == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    Entity hero{};
    world.query<const Animator, const LookAt>().each(
        [&hero](Entity entity, const Animator&, const LookAt&) { hero = entity; });
    std::optional<Vec3> visitor;
    world.query<const Name, const Transform>().each([&visitor](Entity, const Name& name, const Transform& transform) {
        if (name.value == "Visitor")
        {
            visitor = transform.position;
        }
    });
    if (visitor && world.isAlive(hero))
    {
        static_cast<void>(animation->setLookAtTarget(hero, *visitor));
    }
}

} // namespace
```

`updateGaze` is called from the module's update in step 12. Move the Visitor in the Scene view while the game
runs: Hero's head follows it, only as far as `maxAngle`, and returns to the animated direction when the
Visitor is behind the character.

## 9. Feet on uneven ground

`FootPlacement` casts a ray below each foot before every Animation step, lifts or lowers the foot to the
ground and moves the pelvis down to the lowest one. The feet are `TwoBoneIk` limbs of the Animator; the
first one can sit on Hero, the others on its children. Add the two legs, the placement and a low step to walk
over:

```json
{"call": "entity.create", "params": {"name": "Foot Target Left", "parent": "/Hero", "components": {"illusion.TwoBoneIk": {"joint": "foot_l", "polePoint": [0, 0.5, 1]}}}}
{"call": "entity.create", "params": {"name": "Foot Target Right", "parent": "/Hero", "components": {"illusion.TwoBoneIk": {"joint": "foot_r", "polePoint": [0, 0.5, 1]}}}}
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.FootPlacement", "value": {"pelvis": "hips", "rayLength": 1.6, "footHeight": 0.08, "maxStep": 0.4, "speed": 10, "layerMask": 1}}}
{"call": "entity.create", "params": {"name": "Step", "components": {"illusion.Transform": {"position": [2, 0.15, 0], "scale": [2, 0.3, 2]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {"halfExtents": [0.5, 0.5, 0.5]}}}}
```

`polePoint` is where the knee bends towards, in Hero's own space: forward and a little up. The ground rays
reach 1.6 m below the hip and correct at most 0.4 m (the step is 0.3 m high). `layerMask` 1 is the physics
layer of the ground, and the **Step** body on it is a static `RigidBody`. Run Hero over the step: the feet
stay on its top and the hips drop, instead of the legs clipping into the ground. In the editor outside Play
mode the feet keep the animated pose, because the rays run in the game session.

## 10. Fall down

A `Ragdoll` on the Animator's Entity turns the animated character into a physics ragdoll while it is
`active`. Without a `profile` the session builds capsules from the skeleton:

```json
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.Ragdoll", "value": {"mass": 70, "blendSeconds": 0.3}}}
```

Add the last function of the module, which toggles the ragdoll on the `hero.fall` action and remembers
which way it went in the module's `State`:

```cpp
namespace
{

void updateFall(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    AnimationService* animation = context.service<AnimationService>();
    if (input == nullptr || animation == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    Entity hero{};
    world.query<const Animator, const Ragdoll>().each(
        [&hero](Entity entity, const Animator&, const Ragdoll&) { hero = entity; });
    if (!world.isAlive(hero))
    {
        return;
    }
    world.query<State>().each([&](Entity, State& state) {
        if (input->wasPressed(state.fall))
        {
            const bool fall = !state.fallen;
            if (animation->setRagdoll(hero, fall).hasValue())
            {
                state.fallen = fall;
            }
        }
    });
}

} // namespace
```

`setRagdoll()` writes `Ragdoll::active`. The character falls from its current pose and velocity, with the
**blendSeconds** blend, and a second press blends the pose back to the animation over the same time. The
lantern, as a socket, falls with the hand.

## 11. Aim with the upper body

An `Aim` points the right hand at a target. It has two halves that work together. Before the graph steps it
measures the yaw and pitch from the spine to the target and writes them, in radians, to the graph parameters
`aimYaw` and `aimPitch`: the `aim` layer of step 3 blends its nine poses by them. After the graph it turns the
spine chain by the little that is left, so the hand points exactly at the target. The command adds it to
Hero (the angles are in radians; the Inspector shows degrees):

```json
{"call": "component.add", "params": {"entity": "/Hero", "type": "illusion.Aim", "value": {"joint": "hand_r", "spine": "upperChest", "chainLength": 3, "maxYaw": 1.2, "maxPitch": 0.8, "speed": 8, "yawParameter": "aimYaw", "pitchParameter": "aimPitch"}}}
```

`joint` is the hand that holds the weapon, `spine` the top joint that turns and `chainLength` 3 shares the
turn over `upperChest` and the two joints below it. `maxYaw` 1.2 and `maxPitch` 0.8 are the largest turns, the
corners of the blend space, and `speed` 8 follows the target at 8 rad/s. Without a `target`, the `point` field
is the aim point; the module sets it every update.

The module holds **hero.aim** (the right mouse button or the left shoulder button) to aim at the Visitor of
step 8. It calls `setAimTarget()` with the Visitor's position, `setAimWeight()` with 1 while the button is
held and 0 otherwise, and reads `aimAngles()` (the yaw and pitch of the last update, smoothed and clamped) to
find out whether the target is inside the cone of `maxYaw` and `maxPitch`, which a game would use to allow a
shot. The result is kept in the module's `State`:

```cpp
namespace
{

constexpr f32 kClampMargin = 0.05f; // radians from the largest turn: the target is inside the cone

void updateAim(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    AnimationService* animation = context.service<AnimationService>();
    if (input == nullptr || animation == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    Entity hero{};
    f32 maxYaw = 0.0f;
    f32 maxPitch = 0.0f;
    world.query<const Animator, const Aim>().each([&](Entity entity, const Animator&, const Aim& aim) {
        hero = entity;
        maxYaw = aim.maxYaw;
        maxPitch = aim.maxPitch;
    });
    std::optional<Vec3> visitor;
    world.query<const Name, const Transform>().each([&visitor](Entity, const Name& name, const Transform& transform) {
        if (name.value == "Visitor")
        {
            visitor = transform.position;
        }
    });
    if (!visitor || !world.isAlive(hero))
    {
        return;
    }
    world.query<State>().each([&](Entity, State& state) {
        const bool holding = input->isDown(state.aim);
        static_cast<void>(animation->setAimTarget(hero, *visitor));
        static_cast<void>(animation->setAimWeight(hero, holding ? 1.0f : 0.0f));
        const Expected<AimAngles, AnimationError> angles = animation->aimAngles(hero);
        state.onTarget = holding && angles.hasValue() && maxYaw - std::abs(angles->yaw) > kClampMargin &&
                         maxPitch - std::abs(angles->pitch) > kClampMargin;
    });
}

} // namespace
```

`setAimWeight()` fades only the procedural turn. The authored `aim` layer keeps following the angles the
`Aim` writes, so the upper body leans towards the Visitor in either case; to keep it straight while the
button is up, aim at a point in front of the character instead. A character that plays another rig's clips
(see the [retargeting](../manual/animation.md#retargeting) section of the manual) keeps its `Aim`: the joint
names `hand_r` and `upperChest` can also be bone names that every mapped rig answers to.

An off hand that holds the weapon is a `TwoBoneIk` on a child Entity. Its `targetJoint` is the right hand, so
its `point` is a point in the right hand's space, after the aim, and the left hand follows the weapon without
lag. The `point` is in the units of the skeleton (centimetres for a Mixamo-style rig, so 30 is a hand length
ahead of the right hand):

```json
{"call": "entity.create", "params": {"name": "Off Hand", "parent": "/Hero", "components": {"illusion.TwoBoneIk": {"joint": "hand_l", "targetJoint": "hand_r", "point": [5, -2, 30], "polePoint": [-0.5, -0.5, 0], "alignToTarget": true}}}}
```

`alignToTarget` keeps the left hand's animated rotation relative to the right hand, so its fingers stay on
the weapon as the character turns. The limbs of step 9 are the feet; a limb with a `targetJoint` is never one
of them.

Two parts of the editor show how the rig is mapped. In the **Skeletons** visualiser of the Scene view, joints
that have no bone in the rig map are drawn dimmed, so a joint you meant to use but that no bone maps shows up
at once. In the Inspector, the Animator card has a **Source skin** row, one for each skin that a retargeted
clip of Hero was authored on; the row is absent while all of Hero's clips come from its own model.

## 12. Build and try it

The module calls the new functions from its update. Register the module with it:

```cpp
namespace
{

void updateAll(ModuleContext& context, f64 deltaSeconds)
{
    updateHero(context, deltaSeconds);
    updateGaze(context);
    updateFall(context);
    updateAim(context);
}

constexpr ModuleApi kHero =
    makeModuleApi("hero", "1.0.0", {.load = &loadHero, .start = &startHero, .update = &updateAll});

} // namespace

ILL_MODULE(hero, kHero)
```

Press **Ctrl+B** (**Modules → Build Modules**). When the build succeeds the module is loaded.

Press **Play**:

- Hero stands in the idle pose with the lantern in its hand. Hold **W**: it runs forward, driven by the root
  motion of the `run` clip, and a footstep sounds at the two marked moments of each cycle. A stick pushed half
  way walks, with the slower clip and slower steps; between them the clips blend.
- Press **Space**: `jump` plays with a 0.1 s cross-fade, the character leaves the ground, and when the clip is
  nearly over the graph fades back to locomotion.
- The head turns towards the Visitor while it is in front of Hero, and the feet follow the step.
- Hold the right mouse button: the spine and the right hand turn towards the Visitor, the left hand stays on
  the weapon, and `onTarget` in the module's `State` is true while the Visitor is inside the aim cone. Move the
  Visitor around the character: the aim stops at `maxYaw` and `maxPitch`.
- Press **R**: Hero collapses with the lantern in its hand. Press **R** again: it blends back to the animation
  where its Entity stands (the ragdoll does not move the Entity; the
  [manual](../manual/animation.md#what-is-suspended-while-a-ragdoll-runs) shows how to stand up where the
  character lies).
- Change `hero.animgraph` while the game runs (a different `exitTime`, a new event time): the editor reloads
  the file and Hero restarts with the new graph.

An agent starts the game with `play.start`, reads the state of the graph through the module's own logic or
`play.step`, and sees the pose in `viewport.screenshot`; the graph's parameters are set only by game code.

## 13. The limits

- Root motion moves the character forward along the clips' direction. Turning toward the stick is game
  code: rotate the Entity's `Transform` before the Animation step (a graph with root motion turns the
  Entity by the clip's yaw only).
- A transition that is running is not interrupted, so a jump pressed during the fade into another state is
  lost: the trigger is cleared at the end of the update. Keep the cross-fades short or press again.
- A clip plays on another skeleton through the rig maps of both (see
  [retargeting](../manual/animation.md#retargeting)); without a rig map it plays by joint name only.
- The live state of the graph is not saved: loading a saved game restarts Hero in `locomotion`.
- A ragdoll only falls: it has no motors, and it suspends root motion and the `CharacterController`.
  Foot placement and look-at do not show on a simulated character.

## Next steps

- Read the [Animation manual](../manual/animation.md) for blend spaces in two dimensions (strafing),
  masked layers and additive layers (breathing); its [Aim](../manual/animation.md#aim) section has every field.
- The showcase project's `Column` and its `assets/animations/column.animgraph` show a graph with a trigger
  and an event; its module calls the whole `AnimationService`.
- [A HUD and a pause menu](hud-and-pause-menu.md) adds a score display and a menu to the game.

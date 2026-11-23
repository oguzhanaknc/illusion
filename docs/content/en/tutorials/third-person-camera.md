# A third-person camera

This tutorial gives a player a third-person camera: a spring arm behind the shoulder that the stick or the
keyboard turns, that walls shorten, and that keeps the player a little off the centre of the screen. A button swaps the
shoulder, another aims through a second, closer virtual camera that the brain blends to, and a hit shakes the
screen. You describe the cameras in the scene, bind the input actions, and write a small C++ game module that
swaps, aims and shakes through the `CameraService`.

You need a Release build of illusion and the C++ compiler the editor was built with. The
[Cameras manual](../manual/cameras.md) explains the stages, the brain and the blends, and the
[Scripting API](../reference/scripting-api.md#cameraservice) the module's calls.

## 1. What you build

- A floor with a **Player** you move with **W A S D** (or the left stick) and a wall to hide behind.
- An **Explore** virtual camera: a spring arm over the right shoulder, turned by **I K J L** (or the right
  stick), with a composer that keeps the player left of the centre.
- An **Aim** virtual camera on the same player with a shorter arm and a narrower lens. Holding **E** raises it
  above Explore; the brain blends to it in a quarter of a second and back when you let go.
- **Q** swaps the shoulder of both arms. **F** is a hit: a `CameraImpulse` shakes every camera near it.

## 2. Create the project

Start the editor and create a project named **Shoulder** in the folder `shoulder` from the **3D Starter**
template:

```bash
build/bin/Release/illusion_editor --workspace ~/games
```

## 3. The scene

The floor, a wall, the player and the brain on the real `Camera` come first. The `CameraBrain` blends over
0.5 s by default:

```json
{"call": "entity.create", "params": {"name": "Floor", "components": {"illusion.Transform": {"position": [0, -0.5, 0], "scale": [40, 1, 40]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Wall", "components": {"illusion.Transform": {"position": [4, 1.5, 0], "scale": [0.5, 3, 8]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.RigidBody": {"motionType": "Static"}, "illusion.Collider": {}}}}
{"call": "entity.create", "params": {"name": "Player", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.MeshRenderer": {"mesh": "builtin:/mesh/cube", "material": "builtin:/material/default"}, "illusion.CharacterController": {"radius": 0.3, "height": 1.8}}}}
{"call": "entity.create", "params": {"name": "Camera", "components": {"illusion.Transform": {"position": [0, 3, 6]}, "illusion.Camera": {}, "illusion.CameraBrain": {"defaultBlend": {"style": "EaseInOut", "duration": 0.5}}}}}
```

The two virtual cameras follow and look at the player, so they need the player's `id` from the result of the
`entity.create` above (the value `1f3a9c52e0b7d468` stands for it here):

```json
{"call": "entity.create", "params": {"name": "Explore", "components": {"illusion.VirtualCamera": {"priority": 10, "follow": "1f3a9c52e0b7d468", "lookAt": "1f3a9c52e0b7d468", "fovY": 1.0472}, "illusion.CameraSpringArm": {"shoulderOffset": [0.6, 1.6, 0], "armLength": 3.5, "collisionRadius": 0.25, "pullInSpeed": 20, "recoverySpeed": 4, "alignToTarget": false, "lookAction": "camera.look", "lookSpeed": [2.5, 1.5]}, "illusion.CameraComposer": {"targetOffset": [0, 1.4, 0], "screenPosition": [-0.2, 0]}}}}
{"call": "entity.create", "params": {"name": "Aim", "components": {"illusion.VirtualCamera": {"priority": 5, "follow": "1f3a9c52e0b7d468", "lookAt": "1f3a9c52e0b7d468", "fovY": 0.7, "customBlend": true, "blendIn": {"style": "EaseInOut", "duration": 0.25}}, "illusion.CameraSpringArm": {"shoulderOffset": [0.5, 1.6, 0], "armLength": 1.8, "collisionRadius": 0.2, "pullInSpeed": 20, "recoverySpeed": 4, "alignToTarget": false, "lookAction": "camera.look", "lookSpeed": [1.5, 1]}, "illusion.CameraComposer": {"targetOffset": [0, 1.4, 0], "screenPosition": [-0.3, 0]}}}}
{"call": "entity.create", "params": {"name": "Hit", "components": {"illusion.Transform": {"position": [0, 1, 0]}, "illusion.CameraImpulse": {"strength": 0.8, "radius": 20, "duration": 0.5, "playOnStart": false}}}}
```

- Explore has the higher `priority`, so the brain shows it. Aim waits at 5 until the module raises it.
- A spring arm casts a sphere of `collisionRadius` from the shoulder against the physics layers and shortens
  when a wall is in the way (`pullInSpeed` metres per second, `20` is nearly at once) and grows back
  (`recoverySpeed`). The cast passes through the follow target's own bodies.
- With `alignToTarget` off the arm's yaw is the world's, so the player can walk across the screen without the
  camera swinging round.
- `customBlend` and `blendIn` make the blend *to* Aim quicker than the brain's default. Going back to Explore
  uses the brain's 0.5 s.
- The `Hit` Entity does not fire at the start (`playOnStart`); the module plays it.

## 4. Input actions

The module reads four actions and the cameras read one, `camera.look`, by name. Create
`assets/input/shoulder.input`:

```json
{
  "format": "illusion.input",
  "version": 1,
  "actions": [
    {"name": "shoulder.move", "type": "Vector2", "bindings": ["Keys.W.S.A.D", "Stick.Left"]},
    {"name": "camera.look", "type": "Vector2", "bindings": ["Keys.I.K.J.L", "Stick.Right"]},
    {"name": "camera.swap", "type": "Button", "bindings": ["Key.Q", "Gamepad.West"]},
    {"name": "camera.aim", "type": "Button", "bindings": ["Key.E", "Gamepad.North"]},
    {"name": "camera.hit", "type": "Button", "bindings": ["Key.F", "Gamepad.East"]}
  ]
}
```

and set the actions asset of the Input settings (**Window → Project Settings**) to
`assets:/input/shoulder.input`, as the [Tetris by Hand](tetris-by-hand.md#4-input-actions) tutorial does. The
spring arm turns at `lookSpeed` radians per second at full deflection, so a stick feels right and the keys turn it
at that full speed. To turn it with the mouse, read a `Delta` action in the module and call
`CameraService::setAngles()`.

## 5. The module

Run **New module** from the command palette with the name `shoulder`, and give it the services it uses:

```cmake
illusion_add_module(shoulder SHARED SOURCES shoulder.cpp USES camera input physics runtime)
```

The state of the module is one runtime Component, so that a hot reload keeps it:

```cpp
#include <illusion/camera/CameraComponents.hpp>
#include <illusion/camera/CameraService.hpp>
#include <illusion/core/Module.hpp>
#include <illusion/ecs/Name.hpp>
#include <illusion/ecs/Reflection.hpp>
#include <illusion/ecs/World.hpp>
#include <illusion/input/InputService.hpp>
#include <illusion/physics/PhysicsComponents.hpp>
#include <illusion/physics/PhysicsService.hpp>

#include <optional>
#include <string_view>

namespace shoulder
{

using namespace illusion;

struct State
{
    ActionId move = 0;
    ActionId swap = 0;
    ActionId aim = 0;
    ActionId hit = 0;
    Entity player;
    Entity explore;
    Entity aimCamera;
    Entity impulse;
    bool aiming = false;
};

} // namespace shoulder
```

`start` finds the actions and the four Entities by name, and creates the state:

```cpp
namespace
{

using namespace illusion;
using shoulder::State;

constexpr f32 kPlayerSpeed = 5.0f; // m/s

bool loadShoulder(ModuleContext&)
{
    reflectRuntime<State>("shoulder.State", {.displayName = "Shoulder state"});
    return true;
}

Entity findNamed(World& world, std::string_view wanted)
{
    Entity found{};
    world.query<const Name>().each([&](Entity entity, const Name& name) {
        if (name.value == wanted)
        {
            found = entity;
        }
    });
    return found;
}

void startShoulder(ModuleContext& context)
{
    InputService* input = context.service<InputService>();
    if (input == nullptr || !context.hasWorld() || context.worldRole() != WorldRole::Simulation)
    {
        return;
    }
    const std::optional<ActionId> move = input->findAction("shoulder.move");
    const std::optional<ActionId> swap = input->findAction("camera.swap");
    const std::optional<ActionId> aim = input->findAction("camera.aim");
    const std::optional<ActionId> hit = input->findAction("camera.hit");
    if (!move || !swap || !aim || !hit)
    {
        return;
    }
    World& world = context.world();
    State state;
    state.move = *move;
    state.swap = *swap;
    state.aim = *aim;
    state.hit = *hit;
    state.player = findNamed(world, "Player");
    state.explore = findNamed(world, "Explore");
    state.aimCamera = findNamed(world, "Aim");
    state.impulse = findNamed(world, "Hit");
    world.create(Name{"Shoulder State"}, state);
}
```

Every update walks the player, swaps the shoulder, raises or lowers the aim camera and plays the impulse. The
shoulder of both arms swaps together, so Aim stays on the side Explore is on. `activate()` lifts Aim above every
other camera until `deactivate()` gives its priority back, and the brain blends each time; the calls only change
the edge of the button, so they are made when `aiming` changes:

```cpp
void updateShoulder(ModuleContext& context, f64)
{
    CameraService* cameras = context.service<CameraService>();
    InputService* input = context.service<InputService>();
    PhysicsService* physics = context.service<PhysicsService>();
    if (cameras == nullptr || input == nullptr || physics == nullptr || !context.hasWorld())
    {
        return;
    }
    World& world = context.world();
    State* state = nullptr;
    world.query<State>().each([&state](Entity, State& value) { state = &value; });
    if (state == nullptr || !world.isAlive(state->player))
    {
        return;
    }

    const Vec2 move = input->vector(state->move);
    Vec3 velocity = physics->characterVelocity(state->player);
    velocity.x = move.x * kPlayerSpeed;
    velocity.z = -move.y * kPlayerSpeed;
    physics->setCharacterVelocity(state->player, velocity);

    if (input->wasPressed(state->swap))
    {
        static_cast<void>(cameras->swapShoulder(state->explore));
        static_cast<void>(cameras->swapShoulder(state->aimCamera));
    }

    const bool aiming = input->isDown(state->aim);
    if (aiming != state->aiming)
    {
        state->aiming = aiming;
        static_cast<void>(aiming ? cameras->activate(state->aimCamera)
                                 : cameras->deactivate(state->aimCamera));
    }

    if (input->wasPressed(state->hit))
    {
        static_cast<void>(cameras->playImpulse(state->impulse));
    }
}

constexpr ModuleApi kShoulder = makeModuleApi(
    "shoulder", "1.0.0", {.load = &loadShoulder, .start = &startShoulder, .update = &updateShoulder});

} // namespace

ILL_MODULE(shoulder, kShoulder)
```

Press **Ctrl+B** (**Modules → Build Modules**). When the build succeeds the module is loaded.

## 6. Try it

Press **Play**:

- The camera stands behind the player's right shoulder, and the player is left of the centre. Turn it with
  **I K J L**; walk with **W A S D**.
- Walk behind the wall and turn the camera towards it: the arm shortens instead of going through it and grows
  back when you step away.
- Press **Q**: the arm swings over the left shoulder. Press again to go back.
- Hold **E**: the view eases in to the closer, narrower Aim camera; let go and it eases back.
- Press **F**: every camera within 20 m shakes for half a second.

An agent can watch and drive the same things. `camera.status` shows what the brain shows while the game steps,
including a blend in progress:

```json
{"call": "camera.list", "params": {}}
{"call": "component.set_field", "params": {"entity": "/Aim", "type": "illusion.VirtualCamera", "path": "priority", "value": 20}}
{"call": "play.step", "params": {"steps": 15}}
{"call": "camera.status", "params": {}}
```

`camera.status` says `live` is `/Aim`, `from` is `/Explore`, and `progress` is between 0 and 1. In the
**Scene** view the live camera's frustum is yellow and the one the blend comes from is orange.

## 7. The limits

- The player here moves in world axes, not relative to the camera. For a camera-relative walk, keep the look
  angles in the module, turn the movement by the yaw and give the angles to the arms with `setAngles()`.
- `setFieldOfView()` jumps. Aim zooms with a second camera because a blend eases between two lenses.
- A swap eases the arm across over `sideDamping` seconds; make it smaller for a quicker swap.
- Cameras that were not evaluated in the previous update start their damping afresh: Aim snaps to its goal
  the first time it is shown, then follows smoothly.

## Next steps

- Read the [Cameras manual](../manual/cameras.md) for orbit and dolly cameras, pair overrides of the brain and
  handheld noise.
- The showcase project's module calls the whole `CameraService`; its rig has all four body stages.
- [A creature that wanders and chases](creature-ai.md) adds something to turn the camera towards.

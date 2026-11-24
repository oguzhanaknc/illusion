# Cameras

A game shows its World through a `Camera` Component. Instead of moving that Camera in game code, put
**virtual cameras** in the scene, each a shot described by data: what it follows, how it moves, what it looks
at. A **camera brain** on the real Camera shows the best virtual camera and blends when another one takes
over ([ADR 0064](../architecture/decisions.md)). `CameraWorld` (`illusion/camera/CameraWorld.hpp`) runs them
in the [Cameras step](runtime.md#step-order) of a game session.

## Virtual cameras

A `VirtualCamera` Component makes an Entity a shot. The brain shows the enabled one with the highest
`priority` whose Entity is not `Inactive`; on equal priorities, the one that became enabled last wins.

| Field | Meaning |
| --- | --- |
| `priority`, `enabled` | Which camera is live |
| `follow` | The body stage's target; empty: the camera's own Entity |
| `lookAt` | The aim stage's target; empty: the `follow` target |
| `fovY`, `nearPlane`, `dutch` | The lens the brain gives the Camera; `dutch` rolls the view |
| `customBlend`, `blendIn` | The blend used when this camera becomes live, unless the brain has a pair override |

The other camera Components on the same Entity are its stages: at most one **body** (where the camera is),
at most one **aim** (where it looks) and optional **noise**. Without a body the camera stands at its
Entity's `Transform`; without an aim it keeps the body's rotation. A second body or aim is reported once
in the log and ignored. To follow a joint, follow an Entity attached to it with a `BoneAttachment`
([sockets](animation.md#sockets)).

Only the live camera and the camera a blend comes from are evaluated in an update. A camera that was not
evaluated in the previous update starts its damping afresh, so it snaps to its goal when it becomes live.

### Follow

`CameraFollow` keeps the camera at `offset` from the target, in the target's space (it turns with the
target and takes its rotation) or in world space, with a smooth time per axis (`damping`).

### Orbit

`CameraOrbit` turns the camera around a pivot `pivotOffset` above the target by yaw and pitch at a
distance, within limits. The vector action `lookAction` turns it (`lookSpeed` radians per second at full
deflection) and the axis action `zoomAction` changes the distance.

### Spring arm

`CameraSpringArm` puts the camera at the end of an arm from the target's shoulder (`shoulderOffset`,
`side`, `armLength`). Each update one sphere of `collisionRadius` is cast along the arm against the physics
layers `collisionLayers`, passing through the target's own bodies, and the arm shortens to stay out of
walls (`pullInSpeed`, `recoverySpeed`). With `alignToTarget` its yaw is relative to where the target
faces; `lookAction` turns it.

### Dolly

`CameraDolly` moves the camera along `path`, Hermite points with tangents in the space of the camera's
Entity, to `position` (point index plus fraction) or, with `NearestToTarget`, to the point nearest the
target.

### Look-at

`CameraLookAt` turns the camera to face the look-at target plus `targetOffset`, damped.

### Composer

`CameraComposer` keeps the target at `screenPosition` (from -1 to 1 on each axis): inside the dead zone
the camera holds still, inside the soft zone it turns back, damped, and the target never leaves the soft
zone.

## Rigs

A rig is a virtual camera with the stages that make a style. Three that most games need. Their `follow`
and `lookAt` take the `id` that `entity.create` returned for the target.

**Chase camera.** A follow body at a fixed offset behind the target and a look-at aim at its chest:

```json
{"method": "entity.create", "params": {"name": "Chase", "components": {
  "illusion.VirtualCamera": {"priority": 10, "follow": "1f3a9c52e0b7d468", "lookAt": "1f3a9c52e0b7d468"},
  "illusion.CameraFollow": {"offset": [0, 2, 6], "damping": [0.2, 0.2, 0.2]},
  "illusion.CameraLookAt": {"targetOffset": [0, 1.5, 0]}}}}
```

**Over-the-shoulder camera.** A spring arm that the mouse or the right stick turns (`lookAction`) and walls
shorten; give it a higher `priority` while the player aims:

```json
{"method": "entity.create", "params": {"name": "Shoulder", "components": {
  "illusion.VirtualCamera": {"priority": 5, "follow": "1f3a9c52e0b7d468", "fovY": 0.9},
  "illusion.CameraSpringArm": {"shoulderOffset": [0.6, 1.6, 0], "armLength": 2.5, "lookAction": "camera.look"}}}}
```

**Rail camera.** A dolly along a track that stays near the target, for a side-scroller or a cutscene shot,
with the composer keeping the target left of the centre:

```json
{"method": "entity.create", "params": {"name": "Rail", "components": {
  "illusion.VirtualCamera": {"priority": 1, "follow": "1f3a9c52e0b7d468"},
  "illusion.CameraDolly": {"mode": "NearestToTarget", "path": [
    {"position": [-8, 2, 6], "tangent": [8, 0, 0]}, {"position": [8, 2, 6], "tangent": [8, 0, 0]}]},
  "illusion.CameraComposer": {"screenPosition": [-0.2, 0]}}}}
```

Put one `CameraBrain` on the Entity with the `Camera`. The camera with the highest `priority` is live, so
the chase camera (10) is shown and the other two wait; `camera.activate` or `CameraService::activate()`
raises another one above it. A pair override of the brain beats the new camera's own `blendIn`: this one
blends from the chase to the shoulder camera over 0.35 s on a curve that starts fast, and everything else
over 0.8 s.

```json
{"method": "component.set", "params": {"entity": "/Camera", "type": "illusion.CameraBrain", "value": {
  "defaultBlend": {"style": "EaseInOut", "duration": 0.8},
  "blends": [{"from": "7c20d4e1a95b3f68", "to": "b6094a7d12ce8f35", "blend": {"style": "Curve", "duration": 0.35,
    "control1": [0.2, 0.0], "control2": [0.0, 1.0]}}]}}}
```

A module drives the same rigs while the game runs: aiming zooms the shoulder camera and a button swaps the
shoulder, which mirrors the arm. `swapShoulder()` fails with `CameraError::NoStage` on a camera without a
spring arm.

```cpp
if (illusion::CameraService* cameras = context.service<illusion::CameraService>())
{
    static_cast<void>(cameras->setFieldOfView(shoulder, aiming ? 0.7f : 0.9f));
    if (swapPressed)
    {
        static_cast<void>(cameras->swapShoulder(shoulder));
    }
}
```

## Noise and shakes

`CameraNoise` adds a handheld sway: two sines per axis, phased by `seed`. A `CameraImpulse` Entity shakes
every live camera within `radius`, strongest at its position and fading over `duration`; it fires when the
game starts (`playOnStart`) and then every `interval` seconds. Code starts a shake with
`CameraWorld::shake()`:

```cpp
illusion::CameraShake shake;
shake.origin = explosion;
shake.strength = 0.8f;
shake.radius = 15.0f;
shake.duration = 0.6f;
static_cast<void>(session.cameras().shake(shake));
```

## The camera brain

A `CameraBrain` on the Entity of the real `Camera` writes the Camera's world `Transform` (and its
`LocalTransform` when it has a parent) and its `fovY` and `nearPlane` every update from the live virtual
camera. When another camera becomes live, it blends over the blend of the pair: the first entry of
`blends` whose `from` and `to` match (an empty one matches any camera), else the new camera's `blendIn`,
else `defaultBlend`. A blend is a style (`Cut`, `Linear`, `EaseInOut` or a cubic Bezier `Curve`) and a
duration. A blend that is broken off by a third camera blends on from where the Camera is. Game code that
moved the Camera itself must stop when it has a brain.

## From a module

A game module finds the `CameraService` with `context.service<illusion::CameraService>()` (`USES camera` in
`illusion_add_module`). It raises a camera above the others with `activate()` and gives the priority back
with `deactivate()`, points cameras at targets, turns an orbit or spring arm, swaps the shoulder, zooms by
the field of view, starts shakes and impulses, and reads the live camera and the blend progress. The
[scripting API reference](../reference/scripting-api.md#cameraservice) lists every function and its errors.

## In the editor

Four `camera.*` [Editor commands](../reference/editor-commands.md) look into cameras and drive them, for
people (the command palette) and AI agents alike:

| Command | What it does |
| --- | --- |
| `camera.list` | The virtual cameras of the scene (up to 200) with `priority`, `enabled`, whether they are `inactive`, the lens in degrees and metres, the body and aim stage (`Follow`, `Orbit`, `SpringArm`, `Dolly` or `None`; `LookAt`, `Composer` or `None`), `noise`, the `follow` and `lookAt` targets and whether a brain shows them now (`live`); the brains with their default blend and number of pair overrides; the number of `CameraImpulse` sources |
| `camera.status` | What each brain shows in a play session: its `live` camera, the camera it blends `from`, the `progress` of the blend from 0 to 1, the `weight` of the live camera and the `position`, `rotation` (pitch, yaw, roll in degrees) and `fieldOfView` it wrote to the Camera, and how many cameras the last update evaluated |
| `camera.activate` | Gives a camera a `priority` one above every other enabled camera, so that the brain shows it. It edits the saved `VirtualCamera`, so it is undoable and the game starts with that camera live. The camera must be enabled and not `Inactive` |
| `camera.preview` | Makes the Game views look through a virtual camera (`entity`) or, with `clear`, through the main Camera again |

```json
{"call": "camera.list", "params": {}}
{"call": "camera.activate", "params": {"entity": "/Rigs/Shoulder"}}
{"call": "camera.preview", "params": {"entity": "/Rigs/Shoulder"}}
{"call": "camera.preview", "params": {"clear": true}}
{"call": "play.step", "params": {"steps": 30}}
{"call": "camera.status", "params": {}}
```

`camera.list` and `camera.status` act on the play World while a game runs, on the edit World otherwise.
Only a play session runs the stages, so `camera.status` in the edit World has `live` false and says so in
`note`; `play.step` followed by `camera.status` shows a blend or a spring arm deterministically.
`camera.activate` and `camera.preview` always act on the edit World, also while the game plays, and the
game starts from what they left. To switch the camera of the running game from the outside, set `priority` of
a `VirtualCamera` with `component.set_field`, which acts on the play World, or call `CameraService::activate()`
from a module. Invalid params are reported with their JSON Pointer: `/entity` for an Entity that is not a
`VirtualCamera`, a disabled one or an inactive one, `/clear` together with `entity`.

### In the viewports

The **Cameras** visualiser of the Scene view (`viewport.set_overlays` with `{"visualisers": {"cameras": true}}`,
on by default) also draws the frustum of every `VirtualCamera`: of the selected ones, or of all with **All**,
in blue at the camera's `Transform` and lens. In a play session the camera a brain shows is always drawn, in
yellow, at the pose the brain evaluated, with its damping and shake; the camera a blend comes from is drawn in
orange, so a blend shows as two frusta that trade places. The previewed camera is yellow too.

The **Game** view shows the brain's output without further setting: in a play session the brain writes its
`Camera`, so the Game view is exactly what the player sees, blends included. Outside a play session the
cameras stand still, because their stages run only in a game; `camera.preview` makes the Game view look
through one virtual camera at where its Entity stands and with its lens, which is enough to place a fixed
shot or a rail's starting point without a main `Camera` in the scene. The preview is editor state: it is not
saved, not undoable, and a play session shows what its brain writes instead.

## Performance

Measured on the development desktop (Release):

- One update of 100 virtual cameras that all run a body stage (follow, orbit, spring arm with a cast, dolly),
  an aim stage, noise and a blend takes 53 µs (Benchmark `camera/stages-100`, budget 400 µs).
- `CameraWorld::update()` on a World with 100 virtual cameras and a brain that changes its live camera every
  ten updates takes 2.1 µs (Benchmark `camera/world-100`, budget 20 µs): only the live camera and the one a
  blend comes from are evaluated, so the cost follows the number of brains and not the number of cameras.
- Nothing is allocated in an update once the World has seen its cameras; the spring arm costs one sphere cast
  per live arm.

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

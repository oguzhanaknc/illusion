# Physics

illusion simulates rigid bodies with [Jolt Physics](https://github.com/jrouwe/JoltPhysics). Bodies are
ordinary Entities: a `Transform`, a `RigidBody` and a `Collider` are all it takes.

Headers: `<illusion/physics/PhysicsWorld.hpp>`, `<illusion/physics/PhysicsComponents.hpp>`

Library: `illusion::physics`

## Getting started

```cpp
illusion::JobSystem jobs;
illusion::World world;
illusion::PhysicsWorld physics(world, jobs);

// Ground: a static box whose top is at y = 0.
illusion::Transform ground;
ground.position = {0.0f, -0.5f, 0.0f};
illusion::RigidBody groundBody;
groundBody.motionType = illusion::MotionType::Static;
illusion::Collider groundShape;
groundShape.halfExtents = {50.0f, 0.5f, 50.0f};
world.create(ground, groundBody, groundShape);

// A falling crate (dynamic is the default; the default Collider is a 1 m box).
illusion::Transform crate;
crate.position = {0.0f, 5.0f, 0.0f};
const illusion::Entity box = world.create(crate, illusion::RigidBody{}, illusion::Collider{});

while (running)
{
    physics.update(frameSeconds); // fixed steps; writes the crate's Transform
}
```

`PhysicsWorld::update()` does everything in one call:

1. Entities with `Transform`, `RigidBody` and `Collider` get a body and a `PhysicsBody` Component
   (the runtime link; do not add it yourself). Bodies of destroyed Entities, or Entities that lost one
   of these Components, are removed.
2. The simulation advances in fixed steps (60 Hz by default, at most 4 steps per update), so results do
   not depend on the frame rate. Simulation work runs on the Job System.
3. Dynamic bodies write their `Transform`, interpolated between the last two steps for smooth motion.
   Rendering therefore shows the simulation up to one step late.

## Bodies

| `MotionType` | Moves by | Use for |
| --- | --- | --- |
| `Static` | Its `Transform` (rarely) | Level geometry |
| `Kinematic` | Its `Transform`, every update; pushes dynamic bodies | Moving platforms, doors |
| `Dynamic` | The simulation; writes its `Transform` | Crates, ragdoll parts, projectiles |

- Setting the `Transform` of a dynamic body teleports it; static and kinematic bodies follow their
  `Transform` automatically.
- `RigidBody::mass` 0 computes the mass from the collider volume (1000 kg/m³). Friction, restitution,
  damping, the gravity factor, sleeping and continuous collision detection (for fast bodies) are
  `RigidBody` fields too.
- `RigidBody::sensor` makes a trigger: it reports contacts but does not collide.
- Changes to `RigidBody`, `Collider` or the `Transform`'s scale apply when the body is created; call
  `physics.rebuild(entity)` to apply them later.

## Colliders

| `ColliderShape` | Size fields |
| --- | --- |
| `Box` | `halfExtents` |
| `Sphere` | `radius` |
| `Capsule` | `halfHeight` (straight part along local Y), `radius` |
| `Cylinder` | `halfHeight` (along local Y), `radius` |
| `ConvexHull` | `mesh` (an asset) or `geometry` from `createConvexHull()` |
| `Mesh` | `mesh` (an asset) or `geometry` from `createMesh()`; static and kinematic bodies only |

`offset` and `rotation` place the shape relative to the Entity. The `Transform`'s scale scales the
shape; spheres and capsules use a uniform scale. An invalid collider (for example a negative radius)
logs a warning and gets a `PhysicsBody` without a body; fix it and call `rebuild()`.

### Mesh and convex hull colliders

Triangle meshes make level geometry; convex hulls fit props of any shape and can be dynamic. Both are
cooked once and shared by every body that uses them. Name a mesh asset in `Collider::mesh` (see
[asset references](assets.md#asset-references)), or create the geometry in code and set
`Collider::geometry`:

```cpp
// From mesh assets: saved in scenes, cooked by a PhysicsAssetResolver.
illusion::PhysicsAssetResolver colliders(physics, streamer);
illusion::Collider levelShape;
levelShape.shape = illusion::ColliderShape::Mesh;
levelShape.mesh = illusion::AssetRef("game:/levels/arena.glb#node/Collision");
world.create(illusion::Transform{}, groundBody, levelShape); // a static RigidBody

illusion::Collider crateShape;
crateShape.shape = illusion::ColliderShape::ConvexHull;
crateShape.mesh = illusion::AssetRef("builtin:/mesh/cube");
world.create(crateTransform, illusion::RigidBody{}, crateShape);

// Every frame, before physics.update():
colliders.update();

// From code: vertices and three indices per triangle (counter-clockwise seen from the front).
illusion::Collider rockShape;
rockShape.shape = illusion::ColliderShape::ConvexHull;
rockShape.geometry = physics.createConvexHull(rockPositions);
world.create(rockTransform, illusion::RigidBody{}, rockShape);
```

- Mesh colliders on dynamic bodies are rejected (Jolt cannot compute their mass); use a convex hull.
- `destroyMesh()` releases a handle; bodies that already use the geometry keep it.
- The `PhysicsAssetResolver` cooks a triangle mesh of all submeshes, or the convex hull of all
  vertices, once per asset, `meshIndex` (the mesh of a model path without a fragment) and shape kind.
  A Collider waiting for its asset gets its body once the geometry is cooked; a Collider whose asset
  changes is rebuilt. An asset that cannot be loaded logs a warning and counts in
  `statistics().failures`.
- `geometry` is runtime data and is not saved in scenes; `mesh` and `meshIndex` are. Geometry created in
  code must be set again after loading a scene.

## Velocities, forces, queries and contacts

```cpp
physics.update(0.0); // creates bodies for new Entities right away
physics.addImpulse(box, {0.0f, 500.0f, 0.0f}); // N·s; changes velocity by impulse / mass
physics.setLinearVelocity(box, {2.0f, 0.0f, 0.0f});
physics.addForce(box, {0.0f, 100.0f, 0.0f}); // N, applied during the next step

if (const auto hit = physics.raycast(cameraPosition, cameraForward, 100.0f))
{
    ILL_LOG_INFO("Game", "looking at entity {} at {} m", hit->entity.index, hit->distance);
}

for (const illusion::ContactEvent& contact : physics.contactEvents())
{
    if (contact.type == illusion::ContactEventType::Begin && contact.sensor)
    {
        // contact.a and contact.b entered a trigger zone
    }
}
```

- Body functions do nothing for Entities that have no body yet; call `update(0.0)` after creating
  Entities to create their bodies immediately.
- `raycast()` returns the closest non-sensor hit with its Entity, point, normal and distance.
- `contactEvents()` lists the contacts that began or ended during the last `update()`, sorted so the
  order does not depend on thread timing. In each event `a` has the lower Entity id and the normal
  points from `a` to `b`. `speed` is how fast the bodies approached along the normal when they touched,
  measured before the solver separates them: use it to scale impact sounds and effects (the
  `physics_playground` sample does).

### Shape casts and overlaps

```cpp
illusion::Collider ball;
ball.shape = illusion::ColliderShape::Sphere;
ball.radius = 0.3f;
const illusion::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

// Sweep a sphere forward: where would a thrown grenade first touch something?
if (const auto hit = physics.shapeCast(ball, start, identity, forward, 20.0f, allLayers, player))
{
    // hit->entity, hit->point, hit->normal, hit->distance
}

// Every body within 3 m of an explosion, sorted, each once.
std::vector<illusion::Entity> inRange; // reuse it between calls
ball.radius = 3.0f;
physics.overlap(ball, explosionCenter, identity, inRange);
```

- Queries take any `Collider` except `Mesh`; its `offset` and `rotation` apply.
- Sensors are skipped; `ignore` (the last argument of `raycast()`, `shapeCast()` and `overlap()`) skips
  one Entity's body, for example the player's own character.
- `shapeCast()` reports the first contact; a shape that starts inside a body hits it at distance 0.

## Characters

A `CharacterController` makes an Entity a player or NPC character: an upright capsule moved by game code
instead of by forces. It walks up steps and slopes, slides along walls, rides moving platforms and pushes
dynamic bodies. The `Transform` position is at the character's feet; its rotation is left to game code.

```cpp
illusion::CharacterController controller; // 1.8 m tall, 0.3 m radius, 0.35 m steps, 50° slopes
const illusion::Entity player = world.create(spawnTransform, controller);

// Every frame, before physics.update():
illusion::Vec3 velocity = physics.characterVelocity(player);
velocity.x = input.x * 5.0f; // walk
velocity.z = input.y * 5.0f;
if (jumpPressed && physics.isCharacterGrounded(player))
{
    velocity.y = 5.0f; // jump
}
physics.setCharacterVelocity(player, velocity);
```

- Gravity (times `gravityFactor`) accelerates the character in the air; landing stops the downward
  velocity and a ceiling stops the upward velocity. The horizontal velocity stays until you change it.
- Slopes steeper than `maxSlope` are not walkable; the character slides down them.
- `CharacterBody` (added by `PhysicsWorld`) holds the velocity, whether the character is `grounded`, the
  ground normal and the `ground` Entity after the last update.
- Characters collide with each other and have an inner body, so raycasts, sensors and contact events see
  them. Setting the `Transform` teleports the character.

## Joints

A `Joint` connects the body of its Entity to the body of `connected` (or to the world when `connected` is
null). Both Entities need a body, and at least one of them must be dynamic. The joint is created in the
bodies' poses at that moment.

| `JointType` | Allows | Uses |
| --- | --- | --- |
| `Fixed` | Nothing: the bodies move as one | `anchor` |
| `Point` | Rotation around the anchor (ball and socket) | `anchor` |
| `Hinge` | Rotation around `axis` (doors, wheels) | `anchor`, `axis`, limits, motor |
| `Slider` | Movement along `axis` (pistons, drawers) | `anchor`, `axis`, limits, motor |
| `Distance` | Anchors between `minLimit` and `maxLimit` apart (ropes, rods) | `anchor`, `connectedAnchor`, limits |

```cpp
// A door hinged at its left edge, opening with a motor.
illusion::Joint hinge;
hinge.type = illusion::JointType::Hinge;
hinge.anchor = {-0.5f, 0.0f, 0.0f};   // in the door's local space
hinge.axis = {0.0f, 1.0f, 0.0f};
hinge.limited = true;
hinge.minLimit = 0.0f;                // radians
hinge.maxLimit = 1.6f;
hinge.motorSpeed = 1.0f;              // rad/s; change it any frame
hinge.motorMaxForce = 500.0f;         // N·m; 0 turns the motor off
world.create(doorTransform, illusion::RigidBody{}, doorCollider, hinge);

// A lamp on a 2 m chain, which breaks when pulled with more than 2000 N.
illusion::Joint chain;
chain.type = illusion::JointType::Distance;
chain.connectedAnchor = ceilingHook; // world space: no connected body
chain.maxLimit = 2.0f;
chain.breakForce = 2000.0f;
world.create(lampTransform, illusion::RigidBody{}, lampCollider, chain);
```

- `jointPosition(entity)` returns the hinge angle, the slider position (both relative to the start) or
  the distance between the anchors. Positive values and motor speeds mean this Entity moves along or
  around +`axis`.
- A joint whose force exceeds `breakForce` is disabled: its `PhysicsJoint::broken` is set and the Entity
  is listed in `brokenJoints()` for that update.
- Joints are removed with either body and recreated when a body is rebuilt. A joint that cannot be
  created (no body, two static bodies) logs a warning; fix it and call `rebuild()`.

## Collision layers

Every body has a layer in [0, 16). All layers collide by default:

```cpp
illusion::RigidBody ghost;
ghost.layer = 1;
physics.setLayersCollide(1, 0, false); // layer 1 passes through layer 0
```

## Scenes and threads

- `reflectPhysicsComponents()` reflects `RigidBody` (`illusion.RigidBody`), `Collider`
  (`illusion.Collider`), `CharacterController` (`illusion.CharacterController`) and `Joint`
  (`illusion.Joint`, with `connected` remapped on load) for scene files; `PhysicsBody`, `CharacterBody`
  and `PhysicsJoint` are runtime state and are not saved.
- `PhysicsWorld` is not thread-safe: call it from one thread, outside of World queries. The World must
  outlive it; destroying it removes the runtime Components.
- Results are deterministic for the same inputs, independent of the number of Job System Workers.
- Bodies, characters and joints of `Inactive` entities (see [Disabled and Inactive](ecs.md#disabled-and-inactive))
  are removed from the simulation and created again from the current `Transform` when the Entity becomes
  active; velocities start from zero. A joint whose connected Entity is Inactive waits for it.
- Dynamic bodies and characters get the runtime tag `WorldSpaceAuthority`: physics writes their
  `Transform` in world space, and `updateTransformHierarchy()` writes their `LocalTransform` from it, so
  their [children](ecs.md#transform-hierarchy) follow them. Run the hierarchy update after
  `PhysicsWorld::update()`.

## Sample

`illusion_physics_playground` stacks a tower of boxes; Space (or the gamepad's south button) shoots a
heavy ball at it and R rebuilds the tower. Every impact plays a knock from where it happened, louder for
faster impacts (`ContactEvent::speed`), through the [audio](audio.md) mixer with a little reverb.

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

`offset` and `rotation` place the shape relative to the Entity. The `Transform`'s scale scales the
shape; spheres and capsules use a uniform scale. An invalid collider (for example a negative radius)
logs a warning and gets a `PhysicsBody` without a body; fix it and call `rebuild()`.

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
  points from `a` to `b`.

## Collision layers

Every body has a layer in [0, 16). All layers collide by default:

```cpp
illusion::RigidBody ghost;
ghost.layer = 1;
physics.setLayersCollide(1, 0, false); // layer 1 passes through layer 0
```

## Scenes and threads

- `reflectPhysicsComponents()` reflects `RigidBody` (`illusion.RigidBody`) and `Collider`
  (`illusion.Collider`) for scene files; `PhysicsBody` is runtime state and is not saved.
- `PhysicsWorld` is not thread-safe: call it from one thread, outside of World queries. The World must
  outlive it; destroying it removes the `PhysicsBody` Components.
- Results are deterministic for the same inputs, independent of the number of Job System Workers.

## Sample

`illusion_physics_playground` stacks a tower of boxes; Space (or the gamepad's south button) shoots a
heavy ball at it and R rebuilds the tower.

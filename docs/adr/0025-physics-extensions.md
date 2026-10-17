# 0025. Mesh and hull geometry, characters, joints and shape queries

- Status: Accepted
- Date: 2026-10-17

## Context

ADR 0023 integrated Jolt Physics with primitive colliders, raycasts and contact events. Games also need
level geometry made of triangles, convex shapes from art, player characters that walk up stairs instead
of tumbling like rigid bodies, joints (doors, ropes, chains, pistons) and volume queries (shape casts and
overlaps for weapons, AI and triggers). These must follow the same ECS-driven model as bodies.

## Decision

- **Geometry handles.** `PhysicsWorld::createMesh()` and `createConvexHull()` cook Jolt shapes once and
  return a `PhysicsMeshHandle` (index + generation) that `Collider::mesh` refers to with the new
  `ColliderShape::Mesh` and `ConvexHull`. Shapes are shared between bodies and scaled per body; destroying
  a handle leaves existing bodies untouched. Mesh colliders are rejected on dynamic bodies (Jolt cannot
  compute their mass or collide two meshes). The handle is runtime data: it is not reflected, like
  `MeshRenderer` handles, until asset references in scenes arrive with the editor (ADR 0022).
- **Characters** use Jolt's `CharacterVirtual` driven by a `CharacterController` Component (capsule size,
  slope, step height, mass, push force, layer) and a runtime `CharacterBody` (velocity, ground state,
  ground Entity). The Transform position is at the feet. Characters move in the fixed step before the
  physics step: gravity accelerates them in the air, the ground stops downward motion, a ceiling stops
  upward motion, and they walk up steps and stick to the floor up to `stepHeight`. Game code sets the
  velocity (`setCharacterVelocity()`). Each character has an inner kinematic body, so raycasts, sensors
  and contact events see it, and characters collide with each other.
- **Joints** are a `Joint` Component on one Entity that names the other body (`connected`, or the world)
  with type Fixed, Point, Hinge, Slider or Distance, an anchor in local space, an axis, optional limits, a
  velocity motor and a break force. They are created in the bodies' current poses once both bodies exist,
  and removed before either body is destroyed (Jolt constraints must not outlive their bodies), so
  rebuilding a body recreates its joints. The connected body is Jolt's body 1 and the Entity's body 2, so
  a positive angle, position or motor speed means this Entity moves along or around +axis. Breaking
  compares the positional constraint impulse of each step, divided by the step time, with `breakForce`;
  broken joints are disabled and reported by `brokenJoints()`.
- **Queries.** `shapeCast()` (closest hit) and `overlap()` (all bodies, sorted, deduplicated) take a
  `Collider` (any convex shape) and a pose; the collider's offset and rotation fold into the query
  transform. Primitive query shapes are built in place without allocation; sensors are skipped and one
  Entity (rigid body or character) can be ignored, which `raycast()` now supports too.

## Consequences

- Scenes save `CharacterController` and `Joint` (including the `connected` Entity, remapped on load); mesh
  and hull colliders need their handles set again after loading until scenes can reference assets.
- Joints between two non-dynamic bodies or to Entities without bodies are reported once and stay inactive
  until `rebuild()`; they are not retried every frame.
- A character's velocity persists between frames; games set its horizontal part every frame.
- Ragdolls, vehicles, soft bodies and cross-platform determinism remain future work.

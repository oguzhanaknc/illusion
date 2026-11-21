# 0061. Sockets, IK, spring bones and ragdolls as post-process stages of the pose

- Status: Proposed
- Date: 2026-11-21

## Context

ADR 0059 keeps poses outside the ECS (one slot per `Animator` in `AnimationWorld`, joints are not entities)
and ADR 0060 drives them with graphs. A game also needs to: hang a lantern in a hand or a sword on a back,
turn a head towards what the player looks at, keep feet on uneven ground, let hair and tails swing, and turn
a character into a ragdoll when it falls. Each of these reads or changes the pose after the graph and before
skinning, and some need the physics world.

What exists and shapes the design:

- **Order of a session update:** Modules, Saves, Animation (graphs, root motion), Physics, then rendering.
  `AnimationWorld` runs on the Job System; `engine/animation` does not link `engine/physics`; `engine/runtime`
  links both.
- **Jolt** (private to `engine/physics`) has a ragdoll type built from a skeleton with per-part shapes and
  constraints, and raycasts.
- **Saved Components** may hold `Entity` fields (remapped by `EntityGuid`, ADR 0033) and refer to assets by
  `AssetRef`.

Options considered for **where these features live**: nodes inside the animation graph (one place, but the
graph would need physics and entity references, and code-only Animators would not get them), or **Components
next to the Animator, evaluated as fixed stages after the graph**. The second keeps graphs pure data, works
with or without a graph, and gives each feature a clear owner.

## Decision

- **Stages.** After graph evaluation and root motion, `AnimationWorld` runs per pose, in this order:
  spring bones, look-at, two-bone IK, ragdoll blend, then model-space matrices, skin matrices and bounds
  (ADR 0059). Each stage is a pure function in `engine/animation` over model-space joint transforms, tested
  without a World, allocation-free.
- **Sockets.** A saved `BoneAttachment` (`joint` name, offset translation/rotation/scale) on a child of the
  Animator's Entity makes that child follow the joint: after the stages, `AnimationWorld` writes its local
  transform (parent = the Animator's Entity, through `Hierarchy.hpp` rules). A missing joint is logged once
  and leaves the child where it is. Physics bodies on attached children are kinematic followers.
- **Look-at.** A saved `LookAt` (`joint` (head), optional `chain` of up to four parent joints that share the
  rotation, `target` Entity or world point, forward axis, `maxAngle`, `weight`, `speed` in rad/s) rotates
  the chain towards the target, clamped and smoothed.
- **Two-bone IK.** A saved `TwoBoneIk` (`joint` = the end, e.g. a foot; its parent and grandparent form the
  chain; `target` Entity or point; `pole` Entity or point; `weight`; `alignToTarget` for the end rotation)
  solves analytically. **Foot placement** is a saved `FootPlacement` on the Animator (two or more
  `TwoBoneIk` feet, ray length, foot height, pelvis joint, max step), handled by `GameSession` before the
  Animation step: it raycasts below each foot's last animated position through `PhysicsWorld`, writes the
  hits as the IK targets and the pelvis offset, so `engine/animation` stays free of physics.
- **Spring bones.** A saved `SpringBone` (`root` joint, applies to the chain below it; `stiffness`,
  `damping`, `gravity`, `radius`) simulates the chain's tips with Verlet integration per Animation step,
  deterministic with fixed steps, colliding only with spheres listed on the same Component; world-collision is
  open.
- **Ragdolls.** A saved `Ragdoll` on the Animator (`profile`: an optional `.ragdoll` JSON asset with per-joint
  shapes, masses and limits; without one a profile is generated from the skeleton's `jointBounds` as capsules
  with conservative cone/twist limits; `active`; `blendSeconds`; `layer`). `PhysicsWorld` builds a Jolt
  ragdoll from the bind pose when it becomes `active`, starts it from the current animated pose and its
  velocities, and every physics step publishes the parts' world transforms; the next Animation step blends
  from the animated pose to the ragdoll pose over `blendSeconds` (and back when it turns off). While active,
  root motion and the CharacterController are suspended. Powered ragdolls (motors following the animation)
  are open.
- **Scripting API.** `AnimationService` gains `jointTransform(entity, joint)` (world space, from the last
  update), `setLookAtTarget`, `setIkTarget`, `setIkWeight` and `setRagdoll(entity, active)`; module API
  version 8 for sockets (`jointTransform`) and 9 for IK (`setLookAtTarget`, `setIkTarget`, `setIkWeight`).
  Everything else is Component data that modules may write directly.
- **Budget.** 300 Animators with look-at, two feet of IK and one spring chain of eight joints add under 1 ms
  to `animation/world-graph-300` on a desktop CPU with the Job System; 20 active ragdolls step in under 1 ms of
  physics time.

## Consequences

- Every stage works with or without a graph and is unit-testable on its own; the graph stays pure data.
- Foot placement lags the animation by one update (it raycasts from the last pose); at game frame rates this
  is invisible and keeps animation free of physics.
- Spring bones do not collide with the world, and ragdolls are passive; both are recorded as open.
- `.ragdoll` profiles are optional: generated profiles get every rigged model a ragdoll, authored ones give
  good limits.
- Adding functions to `AnimationService` raises the module API version: 8 for sockets, 9 for IK.

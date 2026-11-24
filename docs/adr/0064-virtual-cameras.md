# 0064. Virtual cameras: saved rigs with body and aim stages, a camera brain that blends between them

- Status: Proposed
- Date: 2026-11-22

## Context

Games have only the `Camera` Component (projection, exposure, a priority used by `findMainCamera()`); every
sample moves it by hand from module code. A third-person shooter (SHOOTER) needs an over-the-shoulder camera
that follows a character, keeps out of walls, swaps shoulders and zooms to aim; cutscenes need rails and
cuts; impacts need shake. Writing these per game is the largest piece of camera code most games have, and
agents cannot author it as data.

What exists and shapes the design:

- **Update order** of `GameSession`: Modules, Saves, AI, Navigation, Animation, Physics, Hierarchy, UI,
  particles and audio, resolvers; the renderer reads world `Transform`s after the update.
- **Input** through `InputActions` (actions, bindings, rebinding, glyphs) and `InputService`.
- **Physics** raycasts and sphere casts through `PhysicsWorld`; sockets give joint transforms (ADR 0061).
- **Data, not callbacks**: services are polled (ADR 0041); saved Components carry authored data; fixed steps
  repeat exactly (ADR 0042).

Options considered: **a camera controller per style** in code (simple, but every new style is engine code
and styles cannot blend), or **virtual cameras** (each a saved rig of independent stages producing a pose; a
brain on the real camera picks the live one by priority and blends poses). The second is the structure
shipped engines converged on; it separates "where the camera is" from "which one is shown".

## Decision

- **Components.** A saved `VirtualCamera` (priority, lens: field of view, near plane, Dutch angle; blend hint;
  an optional follow and look-at Entity, optionally a socket joint) carries its stages as saved Components on
  the same Entity:
  - **Body** (one of): `CameraFollow` (offset in the target's space or world space, per-axis damping),
    `CameraOrbit` (yaw/pitch with limits, distance, input actions for look), `CameraSpringArm` (shoulder
    offset and side, arm length, collision radius and physics layers, pull-in and recovery speeds),
    `CameraDolly` (a path of points with tangents, position along it from a parameter or nearest to the
    target).
  - **Aim** (one of): `CameraLookAt` (damping), `CameraComposer` (screen-space target position with dead and
    soft zones and damping), or none (the body's rotation).
  - **Noise**: `CameraNoise` (a handheld profile: frequencies and amplitudes on position and rotation).
- **Brain.** A saved `CameraBrain` on the Entity with the real `Camera` picks the highest-priority live
  `VirtualCamera` (enabled, not `Inactive`; ties: the most recently activated), and blends from the previous
  one over the blend of the pair (`cut`, ease in/out, or a curve; a default and per-pair overrides on the
  brain). It writes the real camera's world `Transform` and lens. Without a brain, nothing changes.
- **Evaluation.** A `CameraWorld` (in a new library `engine/camera`, GPU-free) runs in `GameSession` after
  Hierarchy and before UI, so it sees this update's character and joint poses; stages are pure functions
  over plain data (tested without a World); state that damping needs (last pose, arm length, dolly position)
  lives in runtime Components. Spring-arm collision uses `PhysicsWorld` sphere casts from `engine/runtime`
  through a small query interface, so `engine/camera` stays free of physics. Deterministic with fixed steps;
  no per-frame allocation.
- **Impulses.** `CameraService::shake(origin, strength, radius, duration)` and saved `CameraImpulse` sources
  add decaying shake to every live camera within range, on top of `CameraNoise`.
- **Scripting API.** `CameraService` (`USES camera`, module API version raised by one): activate or
  deactivate and reprioritise virtual cameras, set follow/look-at targets, set orbit angles and arm side
  (shoulder swap), trigger shakes, read the live camera and the blend progress.
- **Editor.** `camera.*` commands (list, status, activate, preview), the Scene view shows the frusta of
  virtual cameras and the live/blending one, and the Game view can preview a chosen virtual camera.

## Consequences

- Camera styles are data an agent can write; a new style is a new stage Component, not new game code.
- The brain owns the real camera's Transform: game code that moved the camera directly must stop when a
  brain is present (documented; the samples move to virtual cameras).
- Spring-arm collision costs one sphere cast per live arm per update; inactive cameras are not evaluated
  except the two in a blend.
- Cutscene sequencing (a timeline of camera shots) is left to a later milestone; dolly tracks and priorities
  cover simple rails.

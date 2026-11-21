# 0060. Animation graphs: layered state machines as `.animgraph` assets, evaluated per Animator, driven by parameters

- Status: Proposed
- Date: 2026-11-18

## Context

ADR 0059 gave the engine skeletons, clips, one clip per `Animator` and GPU skinning. A game character needs
more: walking that blends into running with speed, a smooth change from idle to jump, an upper body that
aims while the legs run, footstep sounds at the right frame, and feet that do not slide because the
animation, not code, moves the character.

What exists and shapes the design:

- **Poses.** `engine/animation` has pure pose functions over spans (`sampleClip`, `localToModel`,
  `skinMatrices`) and `AnimationWorld`, which evaluates every `Animator` on the Job System into pose slots
  outside the ECS, before Physics.
- **Assets.** Data that is authored, versioned and shared is a JSON asset file with a format tag and a
  parser returning `Expected` (`.material`, `.input`, ADR 0035); saved Components refer to it with `AssetRef`.
- **Scripting API.** Modules talk to engine systems through abstract `*Service` classes, poll results and are
  never called back (ADR 0041, ADR 0056, ADR 0057, ADR 0058).
- **Characters.** `PhysicsWorld::setCharacterVelocity()` moves a `CharacterController`; Physics runs after
  Animation in `GameSession::update()`.
- **Agents.** The editor must be fully controllable by agents; data an agent edits should be text it can
  write and validate (the ANIM-4 commands and graph editor build on this).

Options considered for **how control is authored**:

- **Code only.** Modules call `play(clip, fade)` and blend weights themselves. Small engine surface, but every
  game rebuilds state machines, the editor cannot preview them and agents cannot inspect them.
- **A node graph of arbitrary blend nodes.** Most flexible, but large to evaluate, validate and edit, and most
  of its power is unused by typical characters.
- **Layered state machines whose states are motions.** The structure nearly every shipped character uses:
  layers (full body, upper body) each run a state machine; a state plays a clip or a blend space; transitions
  fire on parameters. Compact JSON, easy to validate and to edit by an agent.

## Decision

- **Asset.** An animation graph is a `.animgraph` JSON file (`"format": "illusion.animgraph"`, a version, and
  migrations like other assets), parsed by `parseAnimationGraph()` into `AnimationGraph` and validated as a
  whole (names unique, references resolved, values finite) with a message naming the offending path. It holds:
  - **Parameters** — named `Float`, `Int`, `Bool` or `Trigger` with defaults. A trigger stays set until a
    transition consumes it or the update ends.
  - **Layers**, evaluated in order — a name, a weight, a blend mode (`Override` replaces the layers below,
    `Additive` adds its difference from the clip's first frame), an optional **mask** (joint names, each with
    its descendants and a weight), and one state machine.
  - **States** — a name, a **motion**, a speed (optionally multiplied by a Float parameter), loop, and
    **events** (a name at a normalized time). A motion is a clip (`#animation/<name>`), a **1D blend space**
    (a Float parameter and clips at values, linear between neighbours), or a **2D blend space** (two Float
    parameters and clips at points, barycentric in a Delaunay triangulation computed at load, the nearest edge
    outside the hull). Clips of a blend space play in sync on normalized time.
  - **Transitions** — from a state or from any state, to a state, with all-of **conditions** (`Float`/`Int`
    compared with a value, `Bool` equal, a `Trigger` set), an optional exit time (normalized), a duration in
    seconds (a cross-fade of the two poses), and a priority order. A transition does not interrupt a running
    transition in ANIM-2.
  - **Root motion** — an optional joint whose motion is taken out of the pose: per update, its horizontal
    translation and yaw delta (in the Animator's model space) are removed from the joint and handed to the
    session.
- **Components.** `Animator` gains `graph` (`.animgraph`). With a graph, `clip` is ignored and `time` is the
  entry state's time when the graph starts; without one, the ANIM-1 behaviour is unchanged. The graph's live
  state (current states, times, transition progress, parameter values) is runtime state in `AnimationWorld`;
  a scene or saved game restarts a graph in its entry states with default parameters (saving it is open).
- **Evaluation.** Per Animator, in the existing `parallelFor`: advance transitions and times, sample every clip
  that has weight (at most `kMaxActiveClips` per Animator, 16; the lightest are dropped), blend with `nlerp`
  (shortest path) for rotations and linear for translation and scale, apply layers with their masks, extract
  root motion, then continue as in ADR 0059. Scratch poses come from a per-worker `LinearAllocator`, so a
  steady update still allocates nothing. Pure functions of the graph, the parameters and the time step, so
  fixed-step runs repeat.
- **Events.** States' events that time crosses this update (including the loop wrap and both sides of a
  cross-fade, only for states with weight above 0) are collected per Animator in update order and published
  for one update, like `SaveEvent`.
- **Root motion.** `AnimationPose` gains `rootDelta` (translation in metres, yaw in radians, world space). The
  `GameSession` applies it after Animation: an Entity with a `CharacterController` gets it as this step's
  velocity (`setCharacterVelocity(delta / step)`, gravity kept by the controller); otherwise it is added to the
  Entity's `Transform`. A game that wants to steer itself turns root motion off in the graph.
- **Scripting API.** `AnimationService` (`USES animation`, header-only `illusion::animation_api`, module API
  version 7): set and read parameters by name (`setFloat`, `setInt`, `setBool`, `setTrigger`), cross-fade an
  Animator without a graph to another clip (`play(entity, clip, fadeSeconds)`), the current state of a layer,
  the normalized time, and `pollEvents()` (`AnimationEvent{entity, name, layer}`). Calls on an Entity without
  an Animator or with an unknown name return an error value, never assert. Parameter writes are applied at the
  next Animation step.
- **Budget.** 300 Animators, each with two layers and a three-clip blend space cross-fading into another
  state, update in under 3 ms on a desktop CPU with the Job System (`animation/graph-300`).

## Consequences

- Characters are data: a graph file an agent can write, validate and diff, previewed and edited by the
  ANIM-4 tools. Code keeps full control through parameters and `play()`.
- One evaluation path: a single-clip Animator is a graph of one state, so ANIM-1 behaviour stays covered.
- Blend spaces and cross-fades multiply sampling: cost grows with active clips, capped by `kMaxActiveClips`.
- Running transitions are not interruptible and the graph state is not saved; both are recorded as open, as
  are sub-state machines, inertialization, IK layers (ANIM-3) and a visual graph editor (ANIM-4).
- Root motion takes horizontal translation and yaw only; vertical motion (jumps) stays with physics.
- Adding `AnimationService` raises the module API version to 7; modules built against 6 must be rebuilt.

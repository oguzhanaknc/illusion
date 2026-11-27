# 0065. Retargeting and aim layers: rig maps cooked with the skeleton, corrections bound once per clip and rig, aim offsets in model space

- Status: Accepted
- Date: 2026-11-23

## Context

ADR 0059 binds a clip to a skeleton by joint name, so a clip plays only on rigs with the same names, and
on a rig of other proportions or another rest pose it plays wrong (arms through the body, feet in the
ground). The SHOOTER demo is built from CC0 packs: characters from one pack, animations from another, rigs
whose joints are named `mixamorig:Hips`, `pelvis`, `Hips` or `hips_JNT`, in T-pose or A-pose, 1.2 m to
2 m tall. Its player and soldiers must aim a weapon while they walk, run or crouch.

What exists and shapes the design:

- **Assets.** `Skeleton` (joints parents first, rest TRS, inverse binds, `rootTransform`) and `AnimationClip`
  (channels by joint name) are cooked into a `ModelData` (model format 6). Import settings sidecars
  (`<model>.import.json`, version 2) change how a model imports and invalidate its cache.
- **Binding and sampling.** `bindClip()` is called once per (skeleton, clip) and cached by `AnimationWorld`;
  `sampleClip()` starts from the rest pose and overwrites bound channels; graphs bind all their clips once
  per skeleton (`bindGraph()`, `GraphBinding::clipBindings`). Clips may come from any model file.
- **Layers.** `.animgraph` layers have per-joint masks and an `Additive` mode, whose difference is taken
  from each clip's own first frame and added to local rotations (ADR 0060). 2D blend spaces exist.
- **Stages.** After the graph: spring bones, foot placement, look-at, two-bone IK, ragdoll blend (ADR
  0061), per Animator in the parallel evaluation, targets read at the start of the update.
- **Joint names** appear in graphs (masks, root motion), Components (`BoneAttachment`, `LookAt`,
  `TwoBoneIk`, `FootPlacement`, `SpringBone`), `.ragdoll` profiles and `AnimationService::jointTransform()`.
- **Module API** version 13 (CAM-3).

Options considered for **when retargeting happens**:

- **At import or cook time** (bake a copy of each clip per target rig). No run-time cost, but the cook must
  know every pairing in advance, storage grows with clips times rigs, and editing a map recooks clips.
- **At bind time** (compute a correction per channel once per clip and target skeleton; sampling applies it).
  One extra quaternion product per rotation channel; corrections cached with the existing bindings; any clip
  plays on any mapped rig without new assets.
- **Per frame in model space** (sample on the source skeleton, compose, map model-space rotations onto the
  target). Handles any hierarchy, but costs a second skeleton's pose and composition per clip and update.

Options for **the map**: a **pairwise joint map** (source joint to target joint) is general but needs a map
per pair of rigs (n²) and says nothing about what a joint is; a **humanoid-only map** (each rig to a fixed
bone set) needs one map per rig and names the bones aim and IK need, but drops tails, weapons and props. A
**per-rig map to shared bone names** (the humanoid set plus free custom names) keeps one map per rig and
still pairs non-humanoid joints.

Options for **rest-pose correction**: a **local rotation delta** (`targetRest * inverse(sourceRest) *
animated`) is cheap but wrong when the rigs' joint axes differ; a **model-space delta** is right for any
axes but transfers "difference from rest", so a T-pose clip on an A-pose rig keeps the arms 45° low. Aligning
the target's bone directions to the source's rest first (a "retarget pose") fixes that.

Options for **aim**: the existing additive layer cannot hold aim poses (single-frame poses minus their own
first frame are zero, and local-space additives bend with a leaning base pose); a look-at on the spine
(`solveLookAt()`) rolls the horizon and aims the head, not a weapon. Authored aim offsets are the quality
standard, but CC0 packs rarely ship them, so a procedural aim is needed too.

## Decision

- **Rig maps.** `Skeleton` gains `RigMap rig` (`illusion/assets/Rig.hpp`): per joint a bone name (one of
  the 56 `kHumanoidBones`, a custom name shared with other rigs, or empty) and a `RetargetTranslation`
  mode. `generateRigMap()` runs at import, deterministic: names first (case, separators and known prefixes
  ignored, sides from `left`/`l`/`.L`/`_l` tokens), then topology for required bones names missed (hips
  where two downward chains and one upward chain meet, sides by model-space x, spine joints between hips
  and neck become spine, chest, upperChest in order). Modes default to `Scaled` for `root`, `hips` and
  parentless joints and `Skeleton` otherwise. The model sidecar's `"model": {"skins": [{"generate",
  "bones": {joint: bone}, "translation": {joint: mode}}]}` (`SkinImportSettings`, one per `#skin/N`)
  corrects it through `applyRigOverrides()`; an error fails the import naming the sidecar path. A rig is
  **humanoid** when the 15 `kRequiredHumanoidBones` are mapped.
- **Source skeletons.** `AnimationClip` gains `i32 skeleton`: the skin of its model whose joint names its
  channels hit most (lowest index on ties, -1 when none). Model format 7 stores `Skeleton::rig` and
  `AnimationClip::skeleton`; import settings version 3 adds `skins` (versions 1 and 2 still read).
- **Retargeting at bind time.** When an Animator's skeleton is not the skeleton a clip was authored on (a
  different `#skin/N` asset), `AnimationWorld` binds the clip with `bindRetargetedClip(clip, source,
  target)` (`illusion/animation/Retarget.hpp`) instead of `bindClip()`, cached per (target skeleton, clip)
  like today; a clip without a source skeleton, or a pair without rigs (`RetargetError`), binds by name as in
  ANIM-1 (logged once). Joints pair by rig bone, else by equal name (`matchJoints()`).
- **Rest-pose correction.** The target's **reference pose** is its rest pose with each mapped bone that has a
  mapped child turned (shortest arc, parents first, in model space) so the bone points where the source's
  rest bone points. With `Ms`, `Mt` the model-space rest rotations of the source joint and the target joint
  in the reference pose (`rootTransform` included) and `MsP`, `MtP` those of their parents, a rotation
  channel becomes `target local = inverse(MtP) * MsP * source local * inverse(Ms) * Mt`, two constants per
  channel (`ChannelRetarget::pre`, `post`). Sampling starts from the reference pose instead of the rest pose,
  so unmapped joints hold it. Scale channels scale the target's reference by `source / source rest`.
- **Proportions and root motion.** Translation channels follow their target joint's mode: `Skeleton` keeps the
  target's reference translation, `Scaled` adds the source's motion from its rest, turned by `pre` and scaled
  by the **hips height ratio** (target over source hips height in model space, `rigHeightRatio()`) and the
  unit change between the parents' model scales, `Animation` takes the source translation turned and
  converted. Root motion is extracted from the retargeted pose, so its distance scales with the hips ratio
  and a shorter character takes shorter strides at the same cadence, without foot sliding for uniform
  proportions.
- **Names through the rig.** Every joint name in graphs, Components, `.ragdoll` profiles and
  `jointTransform()` resolves through `findRigJoint()`: a joint of that name, else the joint with that bone.
  A graph or a socket written with bone names (`"rootMotion": "hips"`, `"joint": "rightHand"`) works on every
  mapped rig.
- **Feet and hands.** `FootPlacement` works on the retargeted pose unchanged. `TwoBoneIk` gains `targetJoint`:
  when set, the target is `point` in that joint's space of the same pose after the aim stage (an off-hand on a
  rifle held by the other hand, without the one-update lag of a socket Entity); `alignToTarget` then keeps the
  end joint's animated rotation relative to that joint. Full-body IK and stride warping are left for later.
- **Aim offsets (authored).** `.animgraph` version 2 adds to a layer `"reference"` (a clip) and
  `"referenceTime"` (normalized, default 0): an `Additive` layer takes its difference from that pose instead of
  each clip's first frame. `"space": "Model"` (`AnimationAdditiveSpace`) adds it to model-space rotations
  (`addPoseInModelSpace()`), so the offset aims the same way over a leaning run or a crouch. An aim offset is an
  Additive, Model-space layer with an upper-body mask whose state is a 2D blend space over two Float parameters
  in radians (yaw, pitch) of single-frame poses; its weight is the layer's. Version 1 files read unchanged.
- **Aim (procedural).** A saved `Aim` Component on the Animator's Entity (`illusion.Aim`: aim `joint` and its
  `forward` axis, the top `spine` joint and `chainLength` 1-4 with per-joint `weights` (the spine
  distribution), `target` Entity or `point`, `maxYaw`, `maxPitch`, `weight`, `speed`, and the graph's
  `yawParameter`/`pitchParameter`). At the start of the update, the yaw and pitch from the spine joint (last
  update's pose) to the target, smoothed at `speed` and clamped, are written to those parameters in radians,
  so the authored offset reacts in the same update. After the graph and the spring bones, `solveAim()` turns
  the chain by the remaining yaw about model +y and pitch about the yawed right axis (no roll), shared by the
  weights, in two passes (the second corrects the aim joint's offset from the spine), scaled by `weight`.
  With authored offsets the stage only removes the residual error; without them it does all the aiming.
- **Stage order** (ADR 0061 extended): graph (with aim layers) → spring bones → foot placement → **aim** →
  look-at (the head on top of the aimed body) → two-bone IK (off-hand grips through `targetJoint`) → ragdoll
  blend.
- **Determinism, memory, threading.** Rig maps are generated at import from the skeleton alone; corrections
  are f32 constants computed on the World's thread when a binding is made; sampling and the aim stage are pure
  and run per Animator in the existing `parallelFor`, writing only their slot (aim smoothing state lives in
  the pose slot, like look-at). Fixed-step runs repeat bit for bit. Bindings and reference poses allocate once;
  model-space additive scratch (three rotations per joint) comes from the per-worker `LinearAllocator`
  (`graphScratchBytes()` includes it), so a steady update allocates nothing.
- **Scripting API.** `AnimationService` gains `setAimTarget(entity, point)` (world space; clears the target
  Entity), `setAimWeight(entity, weight)` (clamped to [0, 1]) and `aimAngles(entity)` (the yaw and pitch of
  the last update, radians, as `AimAngles`); `UnknownName` without an `Aim`. Module API version: the next free version when ANIM-5c lands (14 today).
- **Editor.** `animation.rig` (a skin's map: each joint's bone and mode, generated or overridden, `humanoid`,
  missing required bones, warnings), `animation.map_joint` (writes one override into the model's sidecar
  through the `asset.set_import_settings` path, undoable, reimports), `animation.retarget` (for a clip and a
  skeleton: retargeted or by name, paired and unpaired channels, height ratio); `animation.list` adds each
  clip's source skin and each skin's `humanoid`. The Animator's Inspector card names the source skin of a
  retargeted clip; the Skeletons visualiser draws unmapped joints dimmed.
- **Headers.** New: `illusion/assets/Rig.hpp`, `illusion/animation/Retarget.hpp`, `illusion/animation/Aim.hpp`,
  the `Aim` Component and `AnimationAdditiveSpace`. Changed by the work packages: `Skeleton::rig`,
  `AnimationClip::skeleton`, `ModelImportSettings::skins` (`std::vector<SkinImportSettings>`),
  `kModelFormatVersion` 7, `kImportSettingsVersion` 3; `AnimationLayer::reference` (`AssetRef`),
  `referenceTime`, `space`, `kAnimationGraphVersion` 2; `GraphBinding::clipRetargets` (a `ClipRetarget` per
  entry of `clips`) and a `bindGraph()` overload whose lookup returns `ClipSource`; `TwoBoneIk::targetJoint`;
  the three `AnimationService` functions; `kModuleApiVersion` raised to the next free version.
- **Budget.** 300 Animators of 60 joints playing a retargeted three-clip blend take at most 15 % more than the
  same clips bound by name (`animation/retarget-300x60` beside `animation/world-update-300x60`); a model-space
  aim layer of nine poses plus the aim stage on 300 Animators adds under 1 ms with the Job System
  (`animation/aim-300`).

## Consequences

- CI tests retargeting on two generated rigs that follow the joint naming and proportions of common CC0
  packs; real packs are first exercised by SHOOTER's fetched assets, outside CI.
- Rig corrections live in the model's import sidecar; a shareable `.rig` asset for many models with the
  same skeleton is a later addendum if projects need it.
- One animation library plays on every mapped rig; agents fix a map with one command and a sidecar line,
  and graphs, sockets and ragdoll profiles written with bone names are shared too.
- Model format 7 recooks every model once; `.animgraph` version 2 and import settings version 3 are read by
  their migrations, version 1 graphs behave as before.
- Retargeting pairs bones one to one: motion of source joints without a pair (an extra spine joint) is lost,
  and joints of the target without a pair hold the reference pose. Rigs with very different hierarchies
  (quadruped to biped) are not supported.
- Non-uniform proportions (long arms) keep the target's lengths, so contacts between hands and between hands
  and props drift; `TwoBoneIk::targetJoint` fixes the off-hand, a general contact fix-up is open.
- Aim parameters are in radians like every angle the engine stores (ADR 0032 field units); editors and
  the manual show degrees. One unit avoids a conversion at every boundary.
- `TwoBoneIk::targetJoint` and `Aim` are new saved data (showcase `build.jsonl`); `AnimationService` grows, so
  modules built against version 13 must be rebuilt.
- Revisit for per-frame model-space retargeting if rigs with different topology must share clips, or for
  baked retargeting if mobile CPU budgets demand it.

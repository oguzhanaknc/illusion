# Handoff: cloud session (2026-10-17)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md.

## State
- main 6d80bcb: M0..M5c merged, CI green.
- PR #16 `feat/m5d-physics-extensions` (M5d): CI 11/11 green, mergeable, no reviews. Code read in full: OK.
- Local verify of PR #16 (Linux clang 18, ci-linux-clang Debug): build 0 warnings; ctest 282/283; samples
  physics_playground + gltf_viewer render on lavapipe with 0 validation errors.
- Not run locally: Release + benchmarks, check_format (needs clang-format 22, box has 18).

## Bug found (on main since M5b, not caused by PR #16)
- Symptom: `ctest -j4` -> random physics tests SIGTRAP (~50% of runs). Serial ctest (CI) passes.
- Cause: engine queue full -> `JobSystem::submit()` runs job inline (engine/core/src/core/JobSystem.cpp).
  `JoltJobSystem::QueueJob` (engine/physics/src/JoltJobSystem.cpp) is called from inside Jolt jobs
  (RemoveDependencyAndQueue) -> nested Jolt job on same thread -> `JPH_ASSERT` in BodyAccess::Grant.
- Why queue fills: barrier waiter executes Jolt jobs itself; their engine queue entries stay until a
  worker pops them. Starved workers -> stale entries accumulate.
- gdb trace: PhysicsWorld::update -> Barrier::Wait -> JobFindCollisions -> QueueJob -> submit(inline)
  -> JobFinalizeIslands -> Grant assert.

## Fix plan (branch claude/upbeat-cray-5vm0mp, from main)
1. `JobSystem::trySubmit(Job, JobCounter*) -> bool`: queue or return false, never inline. `submit()`
   shares the push path.
2. `JoltJobSystem::QueueJob`: use trySubmit; on false release the ref and leave the job to the barrier
   waiter (JobSystemWithBarrier::Wait runs every executable job; each finished job releases the
   semaphore, so no lost wake-up). All Jolt jobs we create belong to PhysicsSystem::Update's barrier.
3. Tests: core trySubmit (full queue -> false, counter untouched, nothing runs); physics regression:
   1 worker busy on a spin job + queue capacity 2 -> update() must not trap and must match a normal run.
4. Docs EN/TR job system page (trySubmit). Then pre-commit-review skill, push, PR.

## After the fix
- Merge fix PR, rebase/merge PR #16 onto it, merge PR #16.
- Next: M5e (audio extensions), then M6 (module system; ADR 0003 hooks already exist:
  Logger/ComponentRegistry::setInstance, setAssertHandler; PIC on; Version.hpp semver).
- Follow-up idea: run unit tests in parallel in CI (benchmarks/GPU serial) to catch races like this.

## Cloud box notes (if another cloud session repeats this)
- Proxy blocks github archive URLs (FetchContent URL deps) -> 403. git clone + release assets work.
  Clone pinned tags/commits from cmake/Dependencies.cmake to /home/user/deps/<name>, configure with
  `-DFETCHCONTENT_SOURCE_DIR_<NAME>=/home/user/deps/<name>` (13 deps; slang downloads fine).
- apt: libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libxext-dev mesa-vulkan-drivers vulkan-validationlayers xvfb xauth.
- Samples: `xvfb-run -a <sample> --frames N --screenshot out.png`.

# Handoff: cloud session (2026-10-17)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md.

## State
- main: M0..M5e merged (#16 M5d, #17 job-queue fix, #18 M5e). ROADMAP has M9 (FSR/DLSS/XeSS upscaling,
  DLSS as optional user-supplied SDK module) and M10 (hybrid ray tracing) at the end.
- Branch claude/upbeat-cray-5vm0mp: M6 (game modules + hot reload), ADR 0027.

## M6 (on the branch)
- Module side: illusion/core/Module.hpp (ModuleApi, makeModuleApi, ILL_MODULE, ModuleContext,
  ModuleServices). Host: engine/modules ModuleHost (shadow copy, validation: API/engine version/build
  key/name/semver/deps, service install, load/update/unload, reload, reloadChanged).
- Services injected: Logger, ComponentRegistry, assert dispatcher (follows host's current handler),
  thread index + worker index forwarding (Thread.cpp, JobSystem.cpp).
- ComponentInfo::module + registry retire/restore/restoreType/typesOfModule/orphan/takeLayoutConflicts.
  Reload: snapshot old types, old unload, retire, new load takes over; leftovers stripped from World and
  orphaned; failures (incl. layout change) roll back to old version.
- cmake/IllusionModules.cmake illusion_add_module(); ILLUSION_SHARED_MODULES (OFF with profiling).
- Tests: tests/modules (TestModule.cpp variants 1-4, NotAModule.cpp), registry tests in
  tests/ecs/TestComponent.cpp; benchmark modules/update (~2.5 ns/module, budget 25).

## Next
- Merge M6 PR, then M7 (editor + AI command API). Open follow-up: reflection-based migration for module
  Components whose layout changed (listed under M6 in ROADMAP).

## Cloud box notes (if another cloud session repeats this)
- Proxy blocks github archive URLs (FetchContent URL deps) -> 403. git clone + release assets work.
  Clone pinned tags/commits from cmake/Dependencies.cmake to /home/user/deps/<name>, configure with
  `-DFETCHCONTENT_SOURCE_DIR_<NAME>=/home/user/deps/<name>` (13 deps; slang downloads fine).
- apt: libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libxext-dev mesa-vulkan-drivers vulkan-validationlayers xvfb xauth
  libclang-rt-18-dev (TSan) doxygen. pip (venv): tools/requirements-dev.txt, docs/requirements.txt.
- Samples: `xvfb-run -a <sample> --frames N --screenshot out.png`.

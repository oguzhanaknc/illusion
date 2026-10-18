# Handoff: cloud session (2026-10-18)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md.

## State
- main: M0..M6 merged (#19 M6). ROADMAP M9 (FSR/DLSS/XeSS) and M10 (hybrid RT) are the last milestones.
- M7 plan: docs/plans/m7-editor.md (24 sub-milestones M7a..M7x, ADRs 0028..0046, command/RPC/MCP catalogue,
  localization, theme tokens, Tetris acceptance test). Owner wants a professional, state-of-the-art editor
  covering every engine feature, English UI, languages added as data files.
- Branch claude/upbeat-cray-5vm0mp: M7a (JSON API + Expected, Random, LogRingSink, ConsoleStreams,
  World::structuralVersion, registry contains/tryInfo, Doxyfile modules + XML), ADR 0028.

## Working method
- One PR per sub-milestone, merged when CI is green. Each one: implement, review from several lenses,
  verify findings, fix, then full local checks (clang + gcc Debug/Release, ASan, TSan, format, docs, doxygen).
- Dependency hashes: GitHub archive URLs are blocked here, but `git archive --format=tar --prefix=<repo>-<ver>/
  <tag> | gzip -n -6 | sha256sum` reproduces GitHub's archive hash (checked against the doctest pin).
- Planned pins (M7d): imgui v1.93.0-docking b916186e (sha 746c2655...; fallback v1.92.9b-docking 90ded916...),
  ImPlot 851cdbd, Roboto v2.138 + Roboto Mono last Apache-2.0 commit + Material Symbols Rounded (Apache-2.0)
  from raw.githubusercontent.com; see the plan's dependency table. No OFL fonts, no ImGuizmo (own gizmos).

## Cloud box notes (if another cloud session repeats this)
- Proxy blocks github archive URLs (FetchContent URL deps) -> 403. git clone + release assets work.
  Clone pinned tags/commits from cmake/Dependencies.cmake to /home/user/deps/<name>, configure with
  `$(cat /home/user/deps/args.txt)` (13 `-DFETCHCONTENT_SOURCE_DIR_<NAME>=...` flags; slang downloads fine).
- apt: libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libxext-dev mesa-vulkan-drivers vulkan-validationlayers xvfb xauth
  libclang-rt-18-dev (TSan) doxygen. pip venv /home/user/venv: tools/requirements-dev.txt, docs/requirements.txt.
- TSan needs `sysctl -w vm.mmap_rnd_bits=28`. Samples: `xvfb-run -a <sample> --frames N --screenshot out.png`.

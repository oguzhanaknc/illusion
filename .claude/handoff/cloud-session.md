# Handoff: cloud session (2026-10-17)

Terse agent notes. Rules: CLAUDE.md, AGENTS.md, .claude/skills/*. Plan: ROADMAP.md.

## State
- main: M0..M5d merged (#16 M5d, #17 job-queue fix). CI green.
- Branch claude/upbeat-cray-5vm0mp: M5e (audio extensions), ADR 0026. PR opened from this branch.

## M5e (done on the branch)
- AssetStreamer::loadFile<T>(path, decoderName, decoder): I/O-thread read + Job System decode, no cache.
- AudioWorldDesc::streamer; requestClip()/clipState(); AudioSource clips load in background,
  AudioPlayback::pendingStart until ready. Clip plays from the AssetHandle data (no copy).
- Mixer graph per voice: sound -> [lpf] -> splitter -> bus group -> [bus lpf] -> [bus hpf] -> endpoint;
  splitter out1 -> shared verblib reverb (miniaudio extras, MIT-0/PD). Nodes created once; routing on the
  audio thread; reverb connected only while fed + decay; delay lines cleared on disconnect
  (illusion_clear_reverb in MiniaudioImpl.c).
- AudioSource: lowPassCutoff, reverbSend, coneInnerAngle/coneOuterAngle/coneOuterVolume (reflected).
  AudioPlayParams: lowPassCutoff, reverbSend. AudioWorld: setBusFilter/busFilter, setReverb/reverb.
- ContactEvent::speed (approach speed before solve). physics_playground plays impact knocks.
- Tests: tests/audio/TestAudioEffects.cpp (+ AudioTestSupport.hpp), streamer loadFile, impact speed.
- Benchmark audio/mix-effects: ~28 ns/voice-frame on the 4-vCPU box (plain mix ~6), budget 120.

## Next
- Merge M5e PR. Then M6 (module system; ADR 0003 hooks exist: Logger/ComponentRegistry::setInstance,
  setAssertHandler; PIC on; Version.hpp semver). Re-registration of module-owned component types on reload.
- Follow-up ideas: parallel unit tests in CI (the #17 race only showed under ctest -j); per-zone reverbs.

## Cloud box notes (if another cloud session repeats this)
- Proxy blocks github archive URLs (FetchContent URL deps) -> 403. git clone + release assets work.
  Clone pinned tags/commits from cmake/Dependencies.cmake to /home/user/deps/<name>, configure with
  `-DFETCHCONTENT_SOURCE_DIR_<NAME>=/home/user/deps/<name>` (13 deps; slang downloads fine).
- apt: libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libxext-dev mesa-vulkan-drivers vulkan-validationlayers xvfb xauth
  libclang-rt-18-dev (TSan) doxygen. pip (venv): tools/requirements-dev.txt, docs/requirements.txt.
- Samples: `xvfb-run -a <sample> --frames N --screenshot out.png`.

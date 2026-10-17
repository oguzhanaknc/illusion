# 0024. Audio with miniaudio, mixed on an audio thread that owns its state

- Status: Accepted
- Date: 2026-10-17

## Context

The engine needs 3D sound, music and a mixer with per-category volumes, on Windows, macOS and Linux,
without per-frame allocations and without audio glitches when the game thread stalls. Sounds should be
ordinary ECS data, clips should load through the virtual file system (loose files and packed archives),
and the mixer must be testable on CI machines that have no sound card. miniaudio (public domain or
MIT-0) was chosen in the project brief: one C file with device I/O for every desktop platform, decoders
for WAV, FLAC and MP3, resampling, a node graph and 3D spatialization.

## Decision

- **Private dependency:** miniaudio is linked privately by `illusion::audio`, compiled as C in its own
  target with the resource manager, encoders and generators disabled. Ogg Vorbis decoding uses the
  `stb_vorbis` copy shipped in miniaudio's `extras/`. On macOS, Core Audio is linked at build time
  (`MA_NO_RUNTIME_LINKING`) so applications pass notarization; on Linux, ALSA, PulseAudio and JACK stay
  loaded at run time, so builds need no audio development packages.
- **The audio thread owns miniaudio.** After construction the game thread never calls miniaudio's graph
  API. `AudioWorld::update()` writes what should play (per-voice clip, parameters, position and velocity;
  the listener; bus volumes and pauses) into a snapshot and publishes it through a lock-free triple
  buffer. At the start of each device callback the mixer applies the newest snapshot (starting,
  stopping and updating `ma_sound`s), mixes with `ma_engine_read_pcm_frames()`, and reports through
  atomics which voices reached their end and which snapshot it applied. Neither side ever waits for the
  other; there are no races on miniaudio objects (TSan-clean) and stalls of the game thread cannot cause
  dropouts.
- **The engine owns the device.** `ma_engine` runs without a device; the `AudioWorld` opens the default
  device (falling back to miniaudio's null backend) and calls the mixer from its callback.
  `AudioOutput::Offline` runs the same mixer from `AudioWorld::render()`, so tests assert on rendered
  PCM deterministically with exactly the code path a device uses.
- **No allocation while mixing.** A fixed pool of voices (`maxVoices`) is created up front. Every
  `ma_sound` starts fresh (no state leaks between sounds) and allocates its node through the engine's
  allocation callbacks, which recycle blocks by exact size; the pools are warmed for every voice in mono
  and stereo at start-up. Stealing takes the lowest-priority, then oldest, voice; lower-priority sounds
  are dropped and counted.
- **Clip lifetime:** decoded clips are converted once to the mixing rate (mono or stereo); streamed
  clips keep the compressed file and get a decoder per voice (created on the game thread). Unloading and
  stopping defer freeing data until the mixer has acknowledged a snapshot that no longer references it.
- **ECS:** `AudioSource` (clip path, bus, volume, pitch, pan, loop, 3D settings, priority) and the
  `AudioListener` tag are reflected for scenes; the runtime `AudioPlayback` component links an Entity to
  its voice and is not saved. Velocities for the doppler effect come from Transform movement. Clips are
  referenced by virtual path, the identity asset ids are derived from (ADR 0019).
- **Mixer:** five fixed buses (Sfx, Music, Ambience, Dialogue, Ui) under a master volume; a bus can be
  paused, freezing its sounds in place.

## Consequences

- Changes made on the game thread are heard at the next `update()`, adding up to one frame of latency on
  top of the device period (10 ms by default).
- `AudioAttenuation::None` is applied as inverse attenuation without rolloff, because miniaudio's "none"
  model also disables panning and the doppler effect.
- Starting a streamed clip opens a decoder (an allocation on the game thread); streams are meant for
  music and long ambience, not for frequent effects.
- Clips load synchronously on first use; games preload with `loadClip()`. Integration with the
  asynchronous asset streamer, effects (reverb, filters), sound cones and custom buses are future work.

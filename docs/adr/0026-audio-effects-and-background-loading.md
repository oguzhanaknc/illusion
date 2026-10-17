# 0026. Audio effects, sound cones and clips loaded through the asset streamer

- Status: Accepted
- Date: 2026-10-17

## Context

ADR 0024 left effects, sound cones and asynchronous clip loading for later. Games need sounds muffled
behind walls, bus-wide filters (a pause menu, a radio voice), a room reverb, directional sources
(loudspeakers, voices) and clips that load without stalling a frame. Impact sounds need to know how hard
two bodies hit, which the contact events of ADR 0023 did not report.

Constraints from ADR 0024 stay: the audio thread owns miniaudio, mixing never allocates, and miniaudio is
private to `illusion::audio`, so the asset streamer (ADR 0020) cannot decode audio itself. miniaudio's core
has low-pass, high-pass and splitter nodes and cones on `ma_sound`; its `extras/` (already in the pinned
archive) has a Freeverb-style reverb node, `verblib`, under MIT No Attribution or public domain, with
fixed-size delay lines. Options for reverb sends were a send per bus (cheap, but every sound on a bus gets
the same amount) or per sound (a splitter node per voice).

## Decision

- **Graph.** Each voice owns, besides its `ma_sound`, a 2nd-order low-pass node and a two-output splitter,
  all created once when the mixer starts. A sound goes to its low-pass node only while
  `lowPassCutoff > 0`, otherwise straight to the splitter; splitter output 0 feeds the bus group and
  output 1 feeds the reverb at `reverbSend × bus volume` (detached while the send or the bus volume is 0
  or the bus is paused). Each bus group goes through its own low-pass and high-pass nodes only while the
  `AudioBusFilter` enables them. Idle voices are disconnected from the graph. Routing changes happen on
  the audio thread while it applies a snapshot, before it mixes; filter re-initialization keeps the
  filter order, so neither allocates.
- **Reverb.** One `ma_reverb_node` (compiled from miniaudio's `extras/` in the C backend target) mixes
  into the endpoint. It is connected only while voices send to it and until its decay time has passed;
  when it disconnects, its delay lines are cleared so a later send starts from silence. It needs a mixing
  rate of 22050 Hz or more; below that, sends are ignored. `AudioReverbSettings` (room size, damping,
  width, volume) are part of the snapshot.
- **Cones** use `ma_sound_set_cone()` with the Entity's forward axis (-z of its `Transform`);
  `kAudioFullCone` angles (the default) make a source omnidirectional.
- **Background loading.** `AssetStreamer::loadFile<T>(path, decoderName, decoder)` reads a file on the
  I/O thread and runs a type-erased decoder on the Job System, without the import cache; requests share
  a handle by path and decoder name. With `AudioWorldDesc::streamer`, `requestClip()` and `AudioSource`
  clips load this way: the decoder (owned by the audio module) decodes to the mixing rate or probes a
  stream. A clip keeps its `AssetHandle` and plays from the handle's data, so nothing is copied.
  `AudioWorld::update()` publishes finished clips; sources that should play wait in
  `AudioPlayback::pendingStart` and start when their clip is ready. A failed load is logged, unloaded and
  forgotten, so a later request tries again.
- **Impact speed.** `ContactEvent::speed` is the relative velocity of the two bodies at the contact point
  along the normal, read in Jolt's contact-added callback, before the solver separates them.

## Consequences

- Each voice costs two extra nodes in memory, and active voices pay for the splitter (and the filter
  when it is on). Mixing 64 voices with filters and the reverb measures about 4–5 times the cost of plain
  mixing; the `audio/mix-effects` benchmark tracks it.
- Sends are per sound, so one sound can be dry and the next wet; per-zone or multiple reverbs, and effects
  other than the low-pass, are future work.
- Bus pauses and volumes apply to what a bus sends to the reverb, but the tail of the shared reverb keeps
  ringing when a bus is paused.
- `AudioWorld` now depends on `illusion::assets` for `AssetStreamer` and `AssetState`.
- `loadFile()` is generic: other modules (fonts, localization tables, scripts) can stream their files the
  same way.

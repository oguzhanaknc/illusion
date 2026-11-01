# Audio

illusion plays sound with [miniaudio](https://github.com/mackron/miniaudio): 3D sound with distance
attenuation, panning, sound cones and the doppler effect, music streaming, a mixer with buses, filters and
a reverb, and clips that load in the background. Sounds are ordinary
Entities with an `AudioSource`; the camera Entity carries the `AudioListener`.

Headers: `<illusion/audio/AudioWorld.hpp>`, `<illusion/audio/AudioComponents.hpp>`

Library: `illusion::audio`

## Getting started

```cpp
illusion::VirtualFileSystem files;
files.mount("game", gameDataDirectory);
illusion::World world;
illusion::AudioWorld audio(world, files);

// The listener: usually the camera Entity.
world.create(cameraTransform, illusion::AudioListener{});

// A crackling fire, positioned at its Transform.
illusion::AudioSource fire;
fire.clip = illusion::AssetRef("game:/audio/fire.ogg");
fire.loop = true;
world.create(fireTransform, fire);

// Background music: not spatial, streamed, on the music bus.
illusion::AudioSource music;
music.clip = illusion::AssetRef("game:/audio/theme.ogg");
music.bus = illusion::AudioBus::Music;
music.spatial = false;
music.stream = true;
music.loop = true;
world.create(music);

while (running)
{
    audio.update(frameSeconds); // starts new sources, moves 3D sounds, hands the mix to the audio thread
}
```

`AudioWorld::update()` does everything in one call:

1. `AudioSource` Entities seen for the first time load their clip and get an `AudioPlayback` Component
   (the runtime link; do not add it yourself). Those with `playOnStart` (the default) start playing, or
   start once their clip has loaded in the background (see below).
2. Sounds of destroyed Entities, or of Entities that lost their `AudioSource`, stop; finished sounds
   release their voices.
3. Volumes, pitches, buses, and the positions and velocities of spatial sounds and of the listener are
   handed to the audio thread. Velocities, for the doppler effect, are the `Transform` movement divided
   by the frame time.

Mixing runs on the audio device's thread and never allocates memory or waits for the game thread.
Changes you make are heard at the next `update()`.

## Clips

| Format | Extensions |
| --- | --- |
| WAV | `.wav` |
| FLAC | `.flac` |
| MP3 | `.mp3` |
| Ogg Vorbis | `.ogg` |

Clips are read through the [virtual file system](filesystem.md), so they work from loose files and from
packed archives. They are converted to the mixing rate; more than two channels are downmixed to stereo.

| `AudioClipMode` | Memory | Use for |
| --- | --- | --- |
| `Decode` (default) | Decoded PCM (4 bytes per sample) | Effects; any number of voices play it cheaply |
| `Stream` (`AudioSource::stream`) | The compressed file; each voice decodes while playing | Music, long ambience |

```cpp
// Preload during a loading screen, so the first play() does not read and decode the file.
const illusion::AudioClipHandle explosion = audio.loadClip("game:/audio/explosion.wav");

// Generated sound: interleaved samples in [-1, 1].
const illusion::AudioClipHandle beep = audio.createClip(samples, 1, 44100);

if (const auto info = audio.clipInfo(explosion))
{
    ILL_LOG_INFO("Game", "{} frames, {} channels, {} bytes", info->frames, info->channels, info->memoryBytes);
}
audio.unloadClip(explosion); // the handle is invalid at once; memory is freed when no voice plays it
```

`loadClip()` returns the cached clip when the path was loaded before. A clip that cannot be read or
decoded logs a warning and yields an invalid handle; an `AudioSource` with such a clip stays silent.

### Loading in the background

Give the `AudioWorld` an [AssetStreamer](assets.md) and clips load without stalling the game: the file
is read on the streamer's I/O thread and decoded on the Job System.

```cpp
illusion::JobSystem jobs;
illusion::AssetStreamer streamer(files, jobs);
illusion::AudioWorldDesc desc;
desc.streamer = &streamer; // AudioSource clips now load in the background
illusion::AudioWorld audio(world, files, desc);

const illusion::AudioClipHandle thunder = audio.requestClip("game:/audio/thunder.ogg");
while (running)
{
    streamer.update(); // publishes finished loads first
    audio.update(frameSeconds);
    if (audio.clipState(thunder) == illusion::AssetState::Ready)
    {
        audio.playOneShot(thunder);
    }
}
```

- `requestClip()` returns a handle at once; `clipState()` is `Loading` until a later `update()` sees the
  finished load, then `Ready` (or `Failed`, with a warning in the log). Without a streamer it is
  `loadClip()`.
- `AudioSource` clips are requested this way too. A source that should play waits for its clip
  (`AudioPlayback::pendingStart`) and starts as soon as it is ready; `stop()` cancels the wait.
- `playOneShot()` and `clipInfo()` need a ready clip. `loadClip()` on a clip that is loading returns its
  handle without waiting.
- Live reload: when `AssetStreamer::reload()` reports the file of a clip loaded through the streamer (see
  [Reloading changed files](assets.md#reloading-changed-files)), the clip loads again in the background
  and keeps its handle. The old data plays until the new data is ready; then the voices playing it stop,
  the `AudioSource`s among them start again on the new data, and the old data is freed once the mixer no
  longer uses it. A reload that fails keeps the old data. `stats().clipReloads` counts the reloads.

## Sources

| `AudioSource` field | Meaning |
| --- | --- |
| `clip` | The clip, an `AssetRef` (saved as its virtual path) |
| `bus` | Mixer bus (`Sfx` by default) |
| `volume`, `pitch`, `pan` | Linear gain; playback speed (clamped to [1/16, 16]); stereo balance in [-1, 1] |
| `loop` | Restart at the end instead of stopping |
| `playOnStart` | Start when the `AudioWorld` first sees the Entity |
| `spatial` | 3D sound at the `Transform` (default); `false` plays it as is (music, interface) |
| `stream` | Load the clip in `Stream` mode |
| `attenuation`, `minDistance`, `maxDistance`, `rolloff` | Distance attenuation (below) |
| `dopplerFactor` | Strength of the doppler effect; 0 disables it |
| `priority` | Who keeps a voice when all are in use |
| `lowPassCutoff` | Low-pass filter cutoff in Hz that muffles the sound; 0 (default) is off ([Effects](#effects)) |
| `reverbSend` | Share of the sound sent to the reverb, in [0, 1] ([Effects](#effects)) |
| `coneInnerAngle`, `coneOuterAngle`, `coneOuterVolume` | Directional sound ([Sound cones](#sound-cones)) |

```cpp
audio.play(door);           // starts or restarts; a changed clip path is loaded now
audio.stop(door);
if (audio.isPlaying(door))  // as of the last update()
{
}
```

Sources without a `Transform` play at the origin, which only matters if they are spatial.

## One-shots

Short sounds that need no Entity, such as impacts and interface clicks, are played directly:

```cpp
illusion::AudioPlayParams params;
params.spatial = true;
params.position = contact.point;
params.volume = 0.8f;
const illusion::AudioVoiceHandle voice = audio.playOneShot(explosion, params);
audio.stop(voice); // optional: one-shots stop by themselves at the end
```

## 3D sound

The listener is the first Entity with `AudioListener` and a `Transform`: it looks along the Transform's
-z axis with +y up (like cameras). Without one, the listener is at the origin looking along -z.

| `AudioAttenuation` | Gain at distance d (clamped to [minDistance, maxDistance]) |
| --- | --- |
| `None` | 1 (the sound is still panned) |
| `Inverse` (default) | minDistance / (minDistance + rolloff × (d − minDistance)) |
| `Linear` | 1 − rolloff × (d − minDistance) / (maxDistance − minDistance) |
| `Exponential` | (d / minDistance)^−rolloff |

Sounds are panned by their direction from the listener, and their pitch follows the doppler effect for
the relative speed of source and listener (speed of sound: `AudioWorldDesc::speedOfSound`, 343.3 m/s).
Moving a source by setting its `Transform` far away in one frame looks like a very fast movement; set
`dopplerFactor` to 0 for sounds that teleport.

### Sound cones

A source with a cone is loud in front and quiet behind, like a loudspeaker or a voice. Its forward axis is
the -z axis of its `Transform`.

```cpp
illusion::AudioSource speaker;
speaker.clip = illusion::AssetRef("game:/audio/announcement.ogg");
speaker.coneInnerAngle = 1.5f;  // full volume within ~86 degrees around the speaker's forward axis
speaker.coneOuterAngle = 3.0f;  // quiet behind it
speaker.coneOuterVolume = 0.2f;
```

Within `coneInnerAngle` (radians, the full opening angle) the volume is unchanged; beyond
`coneOuterAngle` it is `coneOuterVolume`; in between it fades. The default angles, `kAudioFullCone`,
make a source omnidirectional.

## Mixer

Every sound plays through one of five buses, all under a master volume:

| `AudioBus` | Typical content |
| --- | --- |
| `Sfx` | Sound effects |
| `Music` | Music |
| `Ambience` | Ambient loops |
| `Dialogue` | Voices |
| `Ui` | Menu sounds (keep them playing while the game is paused) |

```cpp
audio.setMasterVolume(settings.masterVolume);
audio.setBusVolume(illusion::AudioBus::Music, settings.musicVolume);
audio.setBusPaused(illusion::AudioBus::Sfx, true); // pause menu: effects freeze in place
```

## Effects

```cpp
// A sound behind a wall: muffled, and a little of it in the room's reverb.
illusion::AudioSource& radio = world.get<illusion::AudioSource>(radioEntity);
radio.lowPassCutoff = 800.0f;
radio.reverbSend = 0.3f;

// Pause menu: muffle the game and keep the menu sounds clear.
audio.setBusFilter(illusion::AudioBus::Sfx, illusion::AudioBusFilter{600.0f, 0.0f});
audio.setBusFilter(illusion::AudioBus::Ambience, illusion::AudioBusFilter{600.0f, 0.0f});

// A radio voice: only the middle of the spectrum.
audio.setBusFilter(illusion::AudioBus::Dialogue, illusion::AudioBusFilter{3500.0f, 400.0f});

// A large hall.
illusion::AudioReverbSettings hall;
hall.roomSize = 0.85f;
hall.damping = 0.4f;
audio.setReverb(hall);
```

| Effect | Set with | Notes |
| --- | --- | --- |
| Low-pass per sound | `AudioSource::lowPassCutoff`, `AudioPlayParams::lowPassCutoff` | 12 dB per octave; can change every frame |
| Bus filters | `setBusFilter(bus, AudioBusFilter{lowPass, highPass})` | Apply to everything on the bus; 0 turns a filter off |
| Reverb | `reverbSend` on sounds, `setReverb()` for the room | One room reverb (Freeverb) shared by all sounds |

Cutoffs are clamped to [10 Hz, 45% of the mixing rate]. The reverb hears each sound at its bus volume, so
muting or pausing a bus also silences what it sends; the reverb's tail rings on. It runs only while sounds
feed it and until its tail has died away, and needs a mixing rate of at least 22050 Hz.

## Voices and output

`AudioWorldDesc::maxVoices` (128 by default) limits how many sounds play at once. When all voices are in
use, a new sound takes the voice of the lowest-priority sound (the oldest among equals) if that priority
is not higher than its own; otherwise it is dropped. `stats()` counts active voices, stolen voices,
dropped sounds and clip memory.

| `AudioOutput` | Use for |
| --- | --- |
| `Device` (default) | The default audio device; falls back to `Null` when there is none |
| `Null` | No sound card: a thread consumes the mix in real time (servers, CI) |
| `Offline` | No device: you call `render()` to pull mixed frames (tests, offline rendering) |

```cpp
illusion::AudioWorldDesc desc;
desc.output = illusion::AudioOutput::Offline;
desc.sampleRate = 48000;
illusion::AudioWorld offline(world, files, desc);
std::vector<float> frames(2 * 480); // 10 ms of interleaved stereo
offline.update(0.0);
offline.render(frames);
```

## Scenes and threads

- `reflectAudioComponents()` reflects `AudioSource` (`illusion.AudioSource`) and `AudioListener`
  (`illusion.AudioListener`) for scene files; `AudioPlayback` is runtime state and is not saved.
- `AudioWorld` is not thread-safe: call it from one thread, outside of World queries. The World and the
  virtual file system must outlive it; destroying it removes the `AudioPlayback` Components.
- Starting a streamed clip opens a decoder (an allocation); everything else that runs every frame is
  allocation-free once clips are loaded, including filters and the reverb.
- Sources of `Inactive` entities (see [Disabled and Inactive](ecs.md#disabled-and-inactive)) stop, and an
  Inactive listener is ignored. When the Entity becomes active again, a source with `playOnStart` starts
  from the beginning.

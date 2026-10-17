# 0020. Asset streaming: an I/O thread, the Job System and main-thread publication

- Status: Accepted
- Date: 2026-10-17

## Context

The AssetDatabase (ADR 0019) loads synchronously, which stalls the frame. Streaming must respect the
Job System rule that jobs never block on files, give game code a simple, race-free view of loading
assets, and avoid uploading many large models to the GPU in one frame.

## Decision

- **One dedicated I/O thread** performs every file read (cached file, source files for dependency
  checks, image sources) and every cache write. Writes are served before loads.
- **The Job System** verifies dependency hashes, deserializes cooked data and imports images (decode and
  mips). glTF imports run on the I/O thread because cgltf reads buffers and images while parsing; they
  happen only until a model is cached. A stale model entry found by a job goes back to the I/O thread
  for the import.
- **Handles** (`AssetHandle<T>`) share one slot per (importer, settings, path) request; the data is freed
  when the last handle goes away and the request is forgotten, so a later request reloads. Loaders write
  the slot's value or error before handing it to a mutex-protected completion list.
- **Publication on the calling thread:** `AssetStreamer::update()` moves finished slots to Ready or
  Failed. Game code therefore never sees a state change in the middle of a frame and never races with
  the loaders. `waitIdle()` serves loading screens, tools and tests.
- **Shutdown** skips queued loads, waits for the load in hand and for running jobs, writes queued cache
  entries and stops; handles stay valid objects.
- **GPU uploads** go through an `UploadQueue` that uploads loaded models in request order within a
  per-call byte budget (at least one model per call), so uploads are spread over frames.

## Consequences

- One I/O thread serializes file access; that matches storage devices well but a slow first-time glTF
  import delays loads queued behind it. Splitting imports onto their own thread is possible later.
- GPU uploads are still synchronous inside RenderResources; the budget bounds the work per frame rather
  than overlapping it with rendering.
- Handles must be read on the thread that calls update(); other threads need their own synchronization.

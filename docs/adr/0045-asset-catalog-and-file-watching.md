# 0045. Asset catalog and file watching

- Status: Accepted
- Date: 2026-10-31

## Context

M7u turns the editor's asset commands (M7h: `asset.list`, `asset.move`, `asset.delete`, `asset.references`)
into an asset pipeline with an asset browser, thumbnails, import settings and live reload. All of that needs
two things the editor did not have:

- **Knowing what is in the project without reading it all again.** `ReferenceIndex` reads every scene and
  material on each use and compares hashes; that is fine for a few hundred files but not for a browser that
  shows sizes, kinds and hashes of every file, or for rename detection, which needs the content hash of a
  file that is already gone.
- **Noticing changes made outside the editor**: an artist saving a texture, `git checkout`, a file manager
  rename. Without that, the catalog is only as fresh as the last command that rescanned it.

Options considered for change detection:

- **Rescan on every use.** Simple and always correct, but a directory walk plus reads per command and per
  browser frame.
- **A third-party watcher library** (efsw, watchman's client). Adds a dependency for about 600 lines of
  platform code, and none offers delivery at an explicit `poll()` with our coalescing rules.
- **Native notifications behind a small platform class, with a polling backend** for file systems that
  have none (network drives, Linux once the inotify watch limit is exhausted) and for deterministic tests.

## Decision

- **`FileWatcher`** (`engine/platform`): watches a directory tree with ReadDirectoryChangesW (overlapped,
  completed without blocking inside `poll()`), inotify (one watch per directory, non-blocking reads inside
  `poll()`, watches added for directories that appear and removed for directories that leave) or FSEvents
  (file events on a private dispatch queue, queued under a mutex). `FileWatchBackend::Polling` lists the tree
  and compares sizes and stamps. A Native watcher the system cannot provide falls back to Polling and says
  so (`backend()`), so callers never need two code paths.
- **Delivery and coalescing at `poll()`.** Events are kept per path between polls: created then modified
  is created, created then removed is nothing, removed then created is modified (created when a folder took
  the place of a file). Renames are a removal plus a creation, because the platforms disagree on rename
  pairing (inotify cookies, Windows old/new pairs, FSEvents none); content-hash matching in the catalog
  recovers them. Directory modifications are dropped (they only mean "a child changed"). A lost queue is an
  `Overflow` change: rescan everything. FSEvents flags accumulate over an item's history, so the watcher asks
  the file system whether the item exists instead of trusting the flags.
- **`AssetCatalog`** (editor core): a sorted map of every file and folder under `assets:/` with kind, size,
  stamp, xxHash64 content hash and dependencies (references of scenes and materials found through
  reflection, external buffers and images of `.gltf` files), and a reverse index rebuilt on demand.
  `refresh()` reads only files whose size or stamp changed; `apply()` reads exactly the files a watcher
  reported, even when their stamp did not change, so an edit inside the file system's stamp resolution is not
  lost. Each update reports added, modified, removed and renamed paths; a rename is a removed and an added
  non-empty file with the same size and hash in one update.
- **Editor integration.** `EditorContext` watches the project's `assets/` directory (Native by default,
  `EditorContextOptions::assetWatching`). `assets()` applies the watcher's changes *and* refreshes, because
  native notifications arrive after the write and a command must see its own writes; `endFrame()` applies
  only the watcher's changes (every `assetPollInterval` seconds, once the catalog has been used) and posts
  everything found since the last post as the `asset.changed` event. `asset.find_missing` is the first
  command built on the catalog.
- **What comes later in M7u, under this ADR:** import settings sidecars (`<file>.import.json`, written only
  for non-default settings), live reload of resolved assets (`AssetStreamer::invalidate`,
  `AssetReloadedEvent`), generation-checked `RenderResources` handles with deferred release, the environment
  bake cache, thumbnails and the asset browser. Each will extend this record with an addendum.

## Consequences

- A second index of references exists beside `ReferenceIndex`, which keeps the detailed reference records
  (entity, Component, field) that `asset.move` rewrites; the catalog only keeps dependency paths. Both parse
  through the same reflection functions.
- Catalog updates that report a rename need the removal and the addition in one update; a removal seen by
  the watcher and an addition seen by a later refresh are reported as a plain removal and addition.
- Native backends are tested on all three CI platforms with a failure ceiling instead of exact event lists
  (the systems batch and reorder); the coalescing and the catalog are tested deterministically with the
  polling backend and explicit stamps.
- The Polling backend walks the whole tree per poll; the editor rate-limits it through
  `assetPollInterval`.

## Addendum: live reload and GPU resource lifetime (M7u-2, 2026-11-01)

### Context

Reloading an asset while the editor runs means freeing the GPU resources of the old version, but
`RenderResources` could only create: handles were plain indices, geometry was appended to the pools, and
the material table and bindless texture table only grew. A reimport therefore leaked a mesh, its textures
and its materials every time, and nothing told the `RenderAssetCache` that a file had changed.

### Decision

- **Generation-checked handles.** `MeshHandle`, `TextureHandle`, `MaterialHandle` and `EnvironmentHandle`
  carry the generation of their slot. Releasing bumps it, so a released handle is stale at once:
  renderers skip it (`mesh()` returns null, `material()` the default material, `environment()` the black
  environment), `isValid()` is false, and releasing or updating it again asserts. The first use of a slot
  has generation 0, so `MeshHandle{3}` written by older code still names the first resource in slot 3.
- **Release deferred until the GPU is done**, not by a fixed number of frames: `release*()` moves the
  resource to a batch, `collect()` closes the batch with an NVRHI event query and frees the batches the GPU
  has reached. `RenderResources` has no frame counter and is used with and without swapchains (tools,
  tests, offscreen editor views), and an event query is exact where "frames in flight" is a guess. The
  create functions and `RenderAssetCache::update()` call `collect()`.
- **Free ranges in the geometry pools** through `RangeAllocator` (core): first fit, merging neighbours,
  shrinking the end. Freed texture slots point at the default white texture before reuse, so the bindless
  table never holds a destroyed view. An environment slot that changes owner bumps `generation()`, because
  renderers cache binding sets per environment slot.
- **`updateMaterial()`** rewrites the constants and the material table record under the same handle and
  makes a new binding set only when a texture or the sampler changed. Material files reload in place
  through it, so resolvers do not need to touch their renderers.
- **Live reload through the streamer.** `AssetStreamer::invalidate(path)` forgets the requests for a file
  (any import settings or decoder); `reload(path)` also publishes `AssetReloadedEvent` on the streamer's
  `EventBus`. The `RenderAssetCache` queues the event and, in its next `update()`, loads again every model,
  texture, material file and environment made from the file. A Ready asset keeps its GPU data until the
  new data is uploaded and keeps it when the reload fails (the error is recorded); a Failed asset loads
  again. A change that arrives while a load runs reloads once more when it ends. When handles that
  resolvers wrote change (new model, environment, a failed asset now loading, an eviction), the cache's
  epoch increases and every `RenderAssetResolver` resolves all its renderers again on its next update; the
  steady-state check stays the per-renderer key comparison.
- **Eviction.** Resolvers count the assets they use; `RenderAssetResolver::releaseUnused(world)` drops the
  ones no renderer refers to any more and `RenderAssetCache::evictUnused()` frees assets with no resolver,
  then models and textures no remaining asset uses. Both walk everything and run on events, not per frame.
- **Editor.** `ViewportService` listens to `asset.changed`, reloads each file and its catalog referrers
  (`AssetCatalog::referrers()`: the glTF file of a buffer or image) on the next frame, then releases and
  evicts unused assets; it also evicts when a play session stops.
- **Split.** Import settings sidecars, `asset.reimport` and `asset.set_import_settings` follow in M7u-3,
  then the environment bake cache, thumbnails and the asset browser.

### Consequences

- Handles are 8 bytes instead of 4; `RenderMesh` grows by 8 bytes (runtime state, never saved).
- Code that keeps handles outside the ECS must expect them to go stale after a reload and re-read them from
  the cache's resolvers; `isValid()` makes the check cheap.
- `PhysicsAssetResolver` and `AudioWorld` do not listen to `AssetReloadedEvent` yet; reloading collision
  meshes and audio clips is left to a later milestone (tracked in ROADMAP).
- Reload of a glTF model depends on the caller knowing its dependencies (the editor's catalog); a game that
  reloads at run time must reload the model file itself.

## Addendum: import settings sidecars and reimport (M7u-3, 2026-11-01)

### Context

Textures and models were imported with fixed settings: the colour space came from the material slot that
used an image, mips were always built, and models kept their file's units. Users need to override that per
asset (a UI texture without mips, a mask stored in a colour slot, a model authored in centimetres), the
override must reach the cooked build, and a forced reimport is needed when the cache is suspected wrong.

### Decision

- **Sidecars next to the asset.** `<file>.import.json` (`"format": "illusion.import"`, `"version": 1`)
  with a `"texture"` section (`colorSpace` auto/srgb/linear, `generateMips`, `maxSize`) or a `"model"`
  section (`scale`). A sidecar holds only non-default settings and does not exist when every setting is a
  default, so most assets have none and diffs stay small. Parsing is hand-written (no reflection registry
  needed at run time) and strict: unknown members, the other kind's section, wrong types and out-of-range
  values are errors with a JSON pointer. Changes are JSON merge patches (`mergeImportSettings()`).
  Alternatives rejected: settings inside the asset catalog or a project-wide database (merge conflicts in
  one file, lost when files move outside the editor), and settings in the referring material (an image
  shared by several materials would be imported several ways).
- **Honoured in the import layer.** `AssetDatabase`, `AssetStreamer` and so `cookAssets()` read the
  sidecar of every image and model they import and record it as a dependency of the cooked file; a missing
  sidecar hashes as empty, so adding one makes the entry stale. The cache key stays the caller's requested
  settings, not the effective ones, so a shipped game (sources and sidecars absent, `verifySources` off)
  finds the entries it was cooked with. The importer versions became `image:2` and `gltf:2`, which
  ignores caches written before the sidecar dependency existed. `cookAssets()` reports sidecars without an
  asset or next to a file that has no import settings.
- **Editor.** The catalog does not keep sidecars as records: it stores their hash on the asset
  (`importSettingsHash`) and reports the asset as modified when its sidecar appears, changes or
  disappears, so the existing live-reload path reloads it and the stale cache entry imports again.
  `asset.list` hides sidecars, `asset.move` and `asset.delete` carry them along in the same undo step and
  refuse sidecar paths. `asset.set_import_settings` (undoable, idempotent) merges a patch and writes or
  deletes the sidecar; `asset.info` returns the effective settings.
- **Forced reimport.** `AssetStreamer::reload(path, AssetReloadMode::Reimport)` deletes the cache entries
  of the file's current requests (queued on the I/O thread before any later load) and then reloads.
  `asset.reimport` (a file or a folder) asks for that through `EditorContext::requestReimport()`; the next
  `endFrame()` posts the paths in the `reimported` list of `asset.changed`, and the viewports reload them
  and the glTF files that refer to them in Reimport mode.
- **Unrelated fix found on the way.** `AssetCatalog::removeTree()` stopped at a sibling that sorts between
  a folder and its contents (`m.png` between `m` and `m/x`) and left records behind; it now searches from
  `"<folder>/"`.

### Consequences

- Every image import reads one more (usually missing) file, and every verified cache hit checks it; both
  happen at load time, not per frame.
- Cache entries of import settings that are not requested at the time of a forced reimport keep their old
  data until their sources change; the editor requests every setting it uses, so it sees the reimport.
- The asset browser's import settings inspector (Apply/Revert) builds on `asset.set_import_settings` and
  the `importSettings` of `asset.info`.

## Addendum: environment bake cache and live reload of physics and audio (M7u-4a, 2026-11-01)

### Context

Every run baked every environment again: a 2K HDRI costs tens of milliseconds of GPU time plus the image
import, and the editor reopens projects all day. The bake runs on the GPU, so the import cache could not
make it (the streamer has no GPU and the renderer has no cache). Live reload (M7u-2) also left two
consumers behind: collision meshes cooked by the `PhysicsAssetResolver` and audio clips loaded by the
`AudioWorld` kept their first version until restart.

### Decision

- **Derived assets in the import cache.** `AssetStreamer::loadCooked(path, importKey, type, version)`
  looks up an entry that the caller makes itself and verifies it like an import (the file and its import
  settings sidecar, by content hash). A hit returns the entry; a miss returns the current source hashes,
  which the caller records in the entry it gives to `storeCooked()` (written on the I/O thread). Hashing
  the sources at lookup time instead of at store time means a file that changes between the two leaves an
  entry with the old hash and new data, which the next lookup rejects (wasted work, never wrong data).
  `AssetReloadMode::Reimport` skips the entry. `AssetReloadedEvent` carries the reload mode so that caches
  of derived assets can honour a forced reimport.
- **Baked environments as an asset type.** `AssetType::Environment` holds a `BakedEnvironment`: both
  RGBA16F cube maps with all mips (mip, then face, then rows) and the SH irradiance, validated on load.
  `RenderResources` reads an environment back through staging textures and an event query
  (`requestEnvironmentReadback()`/`readEnvironment()`, never waiting unless asked) and creates one from a
  `BakedEnvironment` without baking.
- **The `RenderAssetCache` uses it** for environments of image files, with the import key
  `envbake:<baker version>:<cube>:<specular>:<samples>` (the `EnvironmentDesc` as the baker rounds it). A
  hit uploads the cube maps and skips the image load; a miss loads the image, bakes, starts a readback and
  stores the bake once the GPU has finished it, polled in `update()` (so a bake never stalls a frame) and
  drained by `waitIdle()`. Procedural skies have no file and are not cached. Alternatives rejected:
  baking on the CPU (minutes for 128 GGX samples per texel at 512²), and a renderer-private cache folder
  (a second invalidation scheme beside the import cache, invisible to `asset.reimport`).
- **Live reload of collision meshes.** The `PhysicsAssetResolver` subscribes to `AssetReloadedEvent`,
  loads the models of its entries again, cooks new geometry, hands it to the Colliders that use it (their
  bodies are rebuilt) and then destroys the old geometry; Jolt shapes are reference counted, so bodies not
  yet rebuilt keep theirs. A failed reload keeps the previous geometry.
- **Live reload of audio clips.** The `AudioWorld` reloads clips loaded through its streamer under the same
  handle. The mixer reads clip samples (and stream decoders the encoded bytes) without locks, so the data
  cannot change under a playing voice: when the new data is ready, voices on the clip are released, the old
  data is retired until the mixer acknowledges a snapshot without them (like unloaded clips), and the
  `AudioSource`s that were playing start again on the new data. One-shots on the old data stop.
- **Split.** M7u-4 was split: thumbnails (`ThumbnailRenderer`, `asset.thumbnail`, the PNG cache in
  `.illusion/thumbnails`) follow in M7u-4b, then the asset browser and `#` search in M7u-5.

### Consequences

- A cached environment at the default `EnvironmentDesc` is about 22 MB on disk (uncompressed half floats,
  so a hit is a read and a `memcpy`); `assets/environment-load` measures the verify and deserialize path.
- The first run with a new image or `EnvironmentDesc` pays for a readback of the bake once.
- Shipped games bake at run time on first start unless the cache is kept between runs; cooking bakes ahead
  of time (`cookAssets`) needs a GPU in the cook and is not done yet.
- Restarting looping sounds from the start on a clip reload is audible, which is acceptable for an editing
  feature; games that reload clips at run time should do it between sounds.

## Addendum: thumbnails (M7u-4b, 2026-11-01)

### Context

The asset browser (M7u-5), the asset fields of the Inspector and agents need small previews of models,
materials, textures and environments. Making one needs the GPU and the asset pipeline of the editor window,
takes longer than a frame for a model that still loads, and is wasted if it is made again on every request or
after every restart.

### Decision

- **The core asks, the window renders.** `ThumbnailHost` (editor core) is the interface: queue a request,
  collect its result by ticket, cancel it. `ThumbnailRenderer` in `illusion::editor_viewport` implements it,
  so the core stays GPU-free (ADR 0030, ADR 0036) and the core tests use a stand-in. The renderer is owned
  by `ViewportService` and shares its `AssetStreamer` and `RenderAssetCache`: live reload, the import cache
  and the environment bake cache reach thumbnails without a second invalidation path. Alternative rejected:
  a renderer in `engine/render`; nothing outside the editor needs thumbnails yet, and the editor-side
  placement keeps the request queue, the error kinds and the command's lifetime rules together.
- **One step per frame.** Requests are served in order, one at a time: start loading, wait for the preview
  World's assets, render it once (256 px RGBA8, no shadows, bloom, TAA or occlusion culling, a procedural sky
  and a key light; environments as the background behind a sphere), then read it back through a
  one-slot `ReadbackRing` without waiting. At most one render, or one CPU scale of a texture
  (`textureThumbnail()`: the smallest mip at least 256 px, box filtered over a checkerboard; float images
  tonemapped), happens per frame, which keeps a step near the planned 2 ms. A request waits at most 3,600
  frames for its assets and fails with `timeout` rather than block the queue.
- **PNG cache keyed by content.** `ThumbnailCache` writes `<key>.png` to the new `thumbnails:` mount
  (`.illusion/thumbnails`, per machine and git-ignored like `cache:`). The key is xxHash64 of the thumbnail
  version, size, extension, and the path, content hash and import settings hash of the asset and,
  recursively, of its catalog dependencies, so a texture edit changes the key of the materials that use it.
  A stale file is never served; `asset.changed` additionally deletes the files the cache made or found for
  changed, removed or renamed assets and their referrers. `asset.thumbnail` checks the key again when the
  image arrives and renders again (at most four times) if the asset changed meanwhile.
- **The command.** `asset.thumbnail` is async (never undoable, read-only for permissions): a cache hit
  finishes on its first update, a miss without a renderer fails with `unsupported`, a file that does not
  load fails with `io_error`. It returns the virtual and the absolute path of the PNG; the image itself is
  not embedded, because a browser shows hundreds of them from disk.

### Consequences

- Thumbnails from earlier sessions that nobody asked for again stay in `.illusion/thumbnails` until the
  folder is cleaned; each is 5 to 30 KB.
- A thumbnail shows what the editor's renderer draws on the machine that made it; golden tests compare
  them with a tolerance like the other editor images.
- Headless editors without a window serve only cached thumbnails.

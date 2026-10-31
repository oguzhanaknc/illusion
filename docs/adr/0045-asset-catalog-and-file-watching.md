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

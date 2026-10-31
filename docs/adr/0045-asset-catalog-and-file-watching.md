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

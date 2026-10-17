# 0021. Packed archives, archive mounts and cooked shipping

- Status: Accepted
- Date: 2026-10-17

## Context

Exported games should ship a few large files instead of thousands of loose sources, load cooked data
without the import pipeline, and detect damaged downloads. Decisions: the archive format, how archives
reach game code, how they are read, and how cooked data replaces sources.

## Decision

- **Format `.illpak`:** a 48-byte header (magic, version, entry count, table offset, size and xxHash64
  checksum), file data at 16-byte aligned offsets, and a table of contents of (path, offset, size,
  xxHash64 of the data) sorted by path. Writing is deterministic (sorted, zero padding) and atomic.
  Uncompressed for now; per-file compression (LZ4 or zstd) can be added with a format version bump.
- **Validation:** opening checks the header, the table checksum, every path (canonical relative paths
  only), every entry's bounds, ordering and uniqueness; every read checks the file's hash, so a damaged
  file is reported as `FileError::IoError` instead of returning corrupt data.
- **Memory-mapped reads:** the archive is mapped read-only (`MapViewOfFile` / `mmap`, isolated in
  `Archive.cpp`); lookups are binary searches and reads are copies, safe from any thread without locks
  (find + read of a small file: 175 ns, against 3 µs with stream reads).
- **VFS integration:** `VirtualFileSystem::mountArchive()` adds a read-only mount backed by an archive;
  reads, `exists()` and `list()` behave like a directory mount, writes return `ReadOnly` and `resolve()`
  returns the new `FileError::Unsupported`.
- **Cooked shipping:** `cookAssets()` imports every image and glTF model of a directory into the import
  cache; `illusion_pack --cook` packs that cache. A shipped game mounts the cooked archive as its cache
  mount and creates the AssetDatabase or AssetStreamer with source verification off, so the sources are
  not needed. `illusion_pack` without `--cook` packs any directory (for data that is not imported).

## Consequences

- Archives are read-only and memory mapped: on Windows a mounted archive cannot be replaced until it is
  unmounted. Patching works by adding archives (later mounts can shadow earlier ones in the future).
- Standalone images are cooked with default import settings only; other settings still need the source
  or a cook step that knows them (part of the export pipeline, M8).

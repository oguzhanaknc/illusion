# 0019. Asset ids, cooked asset files and a content-hashed import cache

- Status: Accepted
- Date: 2026-10-17

## Context

Every load decoded source files (PNG, JPEG, HDR, glTF) and rebuilt mip chains, which is slow and will
not scale to real projects. M4 needs an asset database, an import cache and later streaming and packed
archives. Decisions: how assets are identified, how cooked data is stored, and how stale cache entries
are detected.

## Decision

- **Asset ids** are the FNV-1a hash of the normalized virtual path (`game:/textures/rock.png`). Ids are
  stable across runs and machines and need no sidecar files; renaming a file changes its id. A GUID
  scheme with metadata files can supersede this when the editor needs rename tracking.
- **Cooked files** use one container: magic `ILLA`, container version, asset type, payload format
  version, the list of source files with content hashes, payload size, payload checksum, payload. All
  values are little-endian (big-endian hosts are rejected at compile time) and written field by field,
  so equal inputs give byte-identical files. Readers validate every count and index before use, so a
  corrupted or hostile file is rejected without crashing or over-allocating.
- **Import cache:** a cooked file's name is the hash of importer name and version, import settings and
  source path. An entry is valid only while every recorded source file (for glTF: the model, its buffers
  and its images) still has the same content hash; otherwise the source is imported again and the entry
  rewritten (atomically, through the VFS). Bumping an importer or payload version invalidates old
  entries.
- **Content hashes and checksums use xxHash64** (implemented from the specification and checked against
  reference vectors), at ~14 GB/s instead of FNV-1a's ~1.3 GB/s; a cached 512×512 image with mips loads
  in 1.9 ns per texel against 22 ns to import it.
- Content hashing is preferred over file times: checkouts, copies and archive extraction change times
  without changing content.

## Consequences

- Validating an entry reads and hashes its source files; for very large projects a time-and-size fast
  path can be added in front of the hash without changing the format.
- Textures are cooked uncompressed (RGBA8/RGBA32F); GPU block compression is future work.
- The database is synchronous and single-threaded; asynchronous streaming (M4b) builds on it.

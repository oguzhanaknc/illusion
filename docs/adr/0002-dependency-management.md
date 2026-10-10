# 0002. Pin dependencies with CMake FetchContent and SHA-256 hashes

- Status: Accepted
- Date: 2026-10-10

## Context

The engine depends on GLFW, SDL3, NVRHI, glm, Jolt Physics, miniaudio, Tracy, doctest and nanobench.
Builds must be reproducible on Windows, macOS and Linux, in CI and on developer machines, without
requiring a system package manager. Supply-chain integrity matters for a commercial product.

Options considered: git submodules, vcpkg/Conan, vendoring sources, CMake FetchContent.

## Decision

Use CMake `FetchContent` with release archives pinned by exact version and verified with
`URL_HASH SHA256`. Repositories that need git submodules (NVRHI) are pinned to an exact commit.
All declarations live in `cmake/Dependencies.cmake`. Dependencies are declared up front but only
downloaded when a module calls `illusion_require_dependency(<name>)`. Dependencies are added with
`SYSTEM` so our warning flags do not apply to them. Every dependency is listed with its licence in
`THIRD_PARTY_NOTICES.md`; only licences compatible with closed-source commercial distribution are
allowed (MIT, BSD, Zlib, Apache-2.0, public domain).

The Steamworks SDK is the exception: it is supplied by the user and never committed or downloaded.

## Consequences

- A fresh configure needs network access; CI downloads archives on every run (small today).
- Bumping a dependency requires a new hash, which makes upgrades deliberate and reviewable.
- If download times grow, add a shared download cache or CI caching rather than switching to a package
  manager.
